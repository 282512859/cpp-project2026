#include "cloud/server/MarkdownConverter.h"
#include "cloud/server/ServiceError.h"
#include "cloud/common/ErrorCode.h"
#include "cloud/common/Sha256.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace cloud::server {
namespace {

using cloud::common::ErrorCode;
constexpr std::uintmax_t kMaximumSourceSize=100U*1024U*1024U;
constexpr std::uintmax_t kMaximumMarkdownSize=64U*1024U*1024U;

std::string lowerExtension(const std::string& name) {
    // 文件名来自 UTF-8 JSON。Windows 的 filesystem::path(string) 会尝试按
    // 当前多字节代码页解释中文，可能抛出“无法映射 Unicode 字符”；这里
    // 只需识别扩展名，直接在 UTF-8 字节串上处理即可。
    const auto slash=name.find_last_of("/\\");
    const auto dot=name.find_last_of('.');
    if(dot==std::string::npos || (slash!=std::string::npos && dot<=slash)) return {};
    auto extension=name.substr(dot);
    std::transform(extension.begin(),extension.end(),extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

std::string readError(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    if(!input) return "document converter failed";
    std::string message(4096,'\0');
    input.read(message.data(),static_cast<std::streamsize>(message.size()));
    message.resize(static_cast<std::size_t>(input.gcount()));
    while(!message.empty() && (message.back()=='\r' || message.back()=='\n'))
        message.pop_back();
    return message.empty()?"document converter failed":message;
}

#ifdef _WIN32
std::wstring quoteArgument(const std::wstring& value) {
    std::wstring result=L"\"";
    std::size_t slashes=0;
    for(const auto character:value) {
        if(character==L'\\') {
            ++slashes;
        } else if(character==L'\"') {
            result.append(slashes*2+1,L'\\');
            result.push_back(L'\"');
            slashes=0;
        } else {
            result.append(slashes,L'\\');
            slashes=0;
            result.push_back(character);
        }
    }
    result.append(slashes*2,L'\\');
    result.push_back(L'\"');
    return result;
}

void runHelper(const std::filesystem::path& helper,
               const std::filesystem::path& input,
               const std::filesystem::path& output,
               const std::filesystem::path& errorPath) {
    auto command=quoteArgument(helper.wstring())+L" "+quoteArgument(input.wstring())+
                 L" "+quoteArgument(output.wstring())+L" "+quoteArgument(errorPath.wstring());
    STARTUPINFOW startup{};
    startup.cb=sizeof(startup);
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(helper.c_str(),command.data(),nullptr,nullptr,FALSE,
                       CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)) {
        throw ServiceError(ErrorCode::InternalError,
                           "cannot start document converter (Windows error "+
                           std::to_string(GetLastError())+")");
    }
    CloseHandle(process.hThread);
    const auto wait=WaitForSingleObject(process.hProcess,120000);
    if(wait==WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess,1);
        WaitForSingleObject(process.hProcess,5000);
        CloseHandle(process.hProcess);
        throw ServiceError(ErrorCode::InternalError,
                           "document conversion timed out after 120 seconds");
    }
    DWORD exitCode=1;
    GetExitCodeProcess(process.hProcess,&exitCode);
    CloseHandle(process.hProcess);
    if(wait!=WAIT_OBJECT_0 || exitCode!=0)
        throw ServiceError(ErrorCode::BadRequest,readError(errorPath));
}
#else
void runHelper(const std::filesystem::path&,const std::filesystem::path&,
               const std::filesystem::path&,const std::filesystem::path&) {
    throw ServiceError(ErrorCode::InternalError,
                       "bundled document conversion is currently available on Windows only");
}
#endif

} // namespace

ConvertedDocument::ConvertedDocument(std::filesystem::path workDirectory,
                                     std::filesystem::path outputPath)
    : workDirectory_(std::move(workDirectory)),outputPath_(std::move(outputPath)) {}

ConvertedDocument::~ConvertedDocument() { cleanup(); }

ConvertedDocument::ConvertedDocument(ConvertedDocument&& other) noexcept
    : workDirectory_(std::move(other.workDirectory_)),
      outputPath_(std::move(other.outputPath_)) {
    other.workDirectory_.clear();
    other.outputPath_.clear();
}

ConvertedDocument& ConvertedDocument::operator=(ConvertedDocument&& other) noexcept {
    if(this!=&other) {
        cleanup();
        workDirectory_=std::move(other.workDirectory_);
        outputPath_=std::move(other.outputPath_);
        other.workDirectory_.clear();
        other.outputPath_.clear();
    }
    return *this;
}

void ConvertedDocument::cleanup() noexcept {
    if(workDirectory_.empty()) return;
    std::error_code ignored;
    std::filesystem::remove_all(workDirectory_,ignored);
    workDirectory_.clear();
    outputPath_.clear();
}

MarkdownConverter::MarkdownConverter(std::filesystem::path helperPath,
                                     std::filesystem::path tempRoot)
    : helperPath_(std::move(helperPath)),tempRoot_(std::move(tempRoot)) {}

ConvertedDocument MarkdownConverter::convert(
    const std::filesystem::path& sourceBlob,const std::string& sourceName) const {
    if(!std::filesystem::is_regular_file(helperPath_))
        throw ServiceError(ErrorCode::InternalError,
                           "document converter component is missing from the server package");
    const auto extension=lowerExtension(sourceName);
    if(extension!=".docx" && extension!=".pdf")
        throw ServiceError(ErrorCode::BadRequest,
                           "only .docx and .pdf files can be converted");
    std::error_code sizeError;
    const auto sourceSize=std::filesystem::file_size(sourceBlob,sizeError);
    if(sizeError) throw ServiceError(ErrorCode::IoError,"cannot read source document");
    if(sourceSize>kMaximumSourceSize)
        throw ServiceError(ErrorCode::BadRequest,
                           "source document exceeds the 100 MiB conversion limit");

    std::filesystem::create_directories(tempRoot_);
    const auto work=tempRoot_/cloud::common::randomHex(16);
    std::filesystem::create_directories(work);
    const auto input=work/("source"+extension);
    const auto output=work/"result.md";
    const auto errorPath=work/"error.txt";
    try {
        std::filesystem::copy_file(sourceBlob,input,
                                   std::filesystem::copy_options::overwrite_existing);
        runHelper(helperPath_,input,output,errorPath);
        const auto outputSize=std::filesystem::file_size(output,sizeError);
        if(sizeError) throw ServiceError(ErrorCode::IoError,
                                         "converter did not produce a Markdown file");
        if(outputSize>kMaximumMarkdownSize)
            throw ServiceError(ErrorCode::BadRequest,
                               "converted Markdown exceeds the 64 MiB limit");
        return ConvertedDocument(work,output);
    } catch(...) {
        std::error_code ignored;
        std::filesystem::remove_all(work,ignored);
        throw;
    }
}

} // namespace cloud::server
