// 负责人：成员1：服务端架构/组长
#include "cloud/server/ServerApp.h"
#include "cloud/server/ServiceError.h"
#include "cloud/common/JsonLite.h"
#include "cloud/common/ExecutablePath.h"
#include "cloud/common/MessageType.h"
#include "cloud/common/ProtocolConstants.h"
#include "cloud/common/Sha256.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace cloud::server {
namespace {

using namespace cloud::common;

std::uint64_t read64(const std::uint8_t* bytes) {
    std::uint64_t value=0;
    for(int i=0;i<8;++i) value=(value<<8U)|bytes[i];
    return value;
}

std::string boolean(bool value) { return value ? "true" : "false"; }

std::filesystem::path converterHelperPath() {
    const auto executable=cloud::common::executableDirectory();
    if(const auto configured=std::getenv("LANCLOUD_DOCUMENT_CONVERTER")) {
        const std::filesystem::path path=configured;
        if(std::filesystem::is_regular_file(path)) return path;
    }
    for(const auto& candidate : {executable/"tools"/"document_converter.exe",
                                  executable.parent_path()/"tools"/"document_converter.exe"}) {
        if(std::filesystem::is_regular_file(candidate)) return candidate;
    }
    auto cursor=executable;
    for(int depth=0;depth<6;++depth) {
        const auto candidate=cursor/"out"/"tools"/"document_converter.exe";
        if(std::filesystem::is_regular_file(candidate)) return candidate;
        const auto parent=cursor.parent_path();
        if(parent==cursor) break;
        cursor=parent;
    }
    return executable/"tools"/"document_converter.exe";
}

bool endsWithMarkdownExtension(const std::string& name) {
    if(name.size()<3) return false;
    std::string extension=name.substr(name.size()-3);
    std::transform(extension.begin(),extension.end(),extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension==".md";
}

std::string defaultMarkdownName(const std::string& sourceName) {
    const auto dot=sourceName.find_last_of('.');
    return sourceName.substr(0,dot==std::string::npos?sourceName.size():dot)+".md";
}

std::string lowerExtension(const std::string& name) {
    auto extension=std::filesystem::path(name).extension().string();
    std::transform(extension.begin(),extension.end(),extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

bool isTextPreviewable(const std::string& name) {
    const auto extension=lowerExtension(name);
    return extension==".txt" || extension==".md" || extension==".markdown" ||
           extension==".cpp" || extension==".c" || extension==".h" ||
           extension==".hpp" || extension==".json" || extension==".log" ||
           extension==".csv" || extension==".py" || extension==".cmake" ||
           extension==".yml" || extension==".yaml";
}

bool isImagePreviewable(const std::string& name) {
    const auto extension=lowerExtension(name);
    return extension==".png" || extension==".jpg" || extension==".jpeg" ||
           extension==".bmp" || extension==".gif" || extension==".svg";
}

bool isVideoPreviewable(const std::string& name) {
    const auto extension=lowerExtension(name);
    return extension==".mp4" || extension==".m4v" || extension==".mov" ||
           extension==".mkv" || extension==".avi" || extension==".webm" ||
           extension==".wmv" || extension==".mpeg" || extension==".mpg";
}

std::string readPreviewText(const std::filesystem::path& path,
                            std::size_t maxBytes,bool& truncated) {
    std::ifstream input(path,std::ios::binary);
    if(!input) throw ServiceError(ErrorCode::IoError,"cannot read preview source");
    std::string content(maxBytes,'\0');
    input.read(content.data(),static_cast<std::streamsize>(content.size()));
    content.resize(static_cast<std::size_t>(input.gcount()));
    truncated=input.peek()!=std::char_traits<char>::eof();
    return content;
}

std::vector<std::uint8_t> readPreviewAsset(const std::filesystem::path& path) {
    std::error_code sizeError;
    const auto size=std::filesystem::file_size(path,sizeError);
    if(sizeError) throw ServiceError(ErrorCode::IoError,"cannot read preview asset");
    if(size>kMaxBodySize)
        throw ServiceError(ErrorCode::BadRequest,"preview asset exceeds the 4 MiB protocol limit");
    std::ifstream input(path,std::ios::binary);
    if(!input) throw ServiceError(ErrorCode::IoError,"cannot open preview asset");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if(input.gcount()!=static_cast<std::streamsize>(bytes.size()))
        throw ServiceError(ErrorCode::IoError,"cannot read preview asset");
    return bytes;
}

#ifdef _WIN32
std::wstring quotePreviewArgument(const std::wstring& value) {
    std::wstring result=L"\"";
    for(const auto character:value) {
        if(character==L'\"') result+=L"\\\"";
        else result.push_back(character);
    }
    return result+L"\"";
}

void renderPdfFirstPage(const std::filesystem::path& source,
                        const std::filesystem::path& outputBase,
                        std::int64_t page) {
    const auto executable=cloud::common::executableDirectory();
    auto renderer=executable/"tools"/"pdftoppm.exe";
    bool usePoppler=std::filesystem::is_regular_file(renderer);
    if(!usePoppler) {
        if(const auto configured=std::getenv("LANCLOUD_PDFTOPPM")) {
            renderer=configured;
            usePoppler=true;
        }
    }
    if(usePoppler) {
        auto command=quotePreviewArgument(renderer.wstring())+
            L" -f "+std::to_wstring(page)+L" -l "+std::to_wstring(page)+
            L" -singlefile -png -scale-to-x 1400 -scale-to-y -1 "+
            quotePreviewArgument(source.wstring())+L" "+quotePreviewArgument(outputBase.wstring());
        STARTUPINFOW startup{};
        startup.cb=sizeof(startup);
        PROCESS_INFORMATION process{};
        if(CreateProcessW(renderer.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,
                          nullptr,nullptr,&startup,&process)) {
            CloseHandle(process.hThread);
            const auto wait=WaitForSingleObject(process.hProcess,30000);
            DWORD exitCode=1;
            GetExitCodeProcess(process.hProcess,&exitCode);
            if(wait==WAIT_TIMEOUT) TerminateProcess(process.hProcess,1);
            CloseHandle(process.hProcess);
            if(wait==WAIT_OBJECT_0 && exitCode==0) return;
        }
    }

    const auto helper=converterHelperPath();
    if(!std::filesystem::is_regular_file(helper))
        throw ServiceError(ErrorCode::InternalError,
            "document converter component is missing; PDF preview is unavailable");
    auto output=outputBase;
    output += ".png";
    auto errorPath=outputBase;
    errorPath += ".error.txt";
    auto command=quotePreviewArgument(helper.wstring())+L" --render-pdf-page "+
        quotePreviewArgument(source.wstring())+L" "+quotePreviewArgument(output.wstring())+
        L" "+std::to_wstring(page)+L" "+quotePreviewArgument(errorPath.wstring());
    STARTUPINFOW startup{};
    startup.cb=sizeof(startup);
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(helper.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,
                       nullptr,nullptr,&startup,&process))
        throw ServiceError(ErrorCode::InternalError,"cannot start bundled PDF preview renderer");
    CloseHandle(process.hThread);
    const auto wait=WaitForSingleObject(process.hProcess,30000);
    DWORD exitCode=1;
    GetExitCodeProcess(process.hProcess,&exitCode);
    if(wait==WAIT_TIMEOUT) TerminateProcess(process.hProcess,1);
    CloseHandle(process.hProcess);
    if(wait!=WAIT_OBJECT_0 || exitCode!=0) {
        std::ifstream input(errorPath,std::ios::binary);
        std::string message((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
        if(message.empty()) message="PDF page rendering failed";
        throw ServiceError(ErrorCode::BadRequest,message.substr(0,4096));
    }
}

void renderVideoPreviewGif(const std::filesystem::path& source,
                           const std::filesystem::path& output,
                           const std::filesystem::path& errorPath) {
    const auto helper=converterHelperPath();
    if(!std::filesystem::is_regular_file(helper))
        throw ServiceError(ErrorCode::InternalError,
            "document converter component is missing; video preview is unavailable");
    auto command=quotePreviewArgument(helper.wstring())+L" --render-video-preview "+
        quotePreviewArgument(source.wstring())+L" "+quotePreviewArgument(output.wstring())+
        L" "+quotePreviewArgument(errorPath.wstring());
    STARTUPINFOW startup{};
    startup.cb=sizeof(startup);
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(helper.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,
                       nullptr,nullptr,&startup,&process))
        throw ServiceError(ErrorCode::InternalError,"cannot start bundled video preview renderer");
    CloseHandle(process.hThread);
    const auto wait=WaitForSingleObject(process.hProcess,45000);
    DWORD exitCode=1;
    GetExitCodeProcess(process.hProcess,&exitCode);
    if(wait==WAIT_TIMEOUT) TerminateProcess(process.hProcess,1);
    CloseHandle(process.hProcess);
    if(wait!=WAIT_OBJECT_0 || exitCode!=0) {
        std::ifstream input(errorPath,std::ios::binary);
        std::string message((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
        if(message.empty()) message="video preview rendering failed";
        throw ServiceError(ErrorCode::BadRequest,message.substr(0,4096));
    }
}
#endif

std::vector<std::uint8_t> renderPdfPreview(const std::filesystem::path& source,
                                           std::int64_t page) {
#ifdef _WIN32
    const auto work=std::filesystem::temp_directory_path()/
        ("lancloud-pdf-preview-"+cloud::common::randomHex(16));
    std::filesystem::create_directories(work);
    try {
        const auto outputBase=work/"page";
        renderPdfFirstPage(source,outputBase,page);
        const auto bytes=readPreviewAsset(work/"page.png");
        std::error_code ignored;
        std::filesystem::remove_all(work,ignored);
        return bytes;
    } catch(...) {
        std::error_code ignored;
        std::filesystem::remove_all(work,ignored);
        throw;
    }
#else
    throw ServiceError(ErrorCode::InternalError,"PDF page preview is currently available on Windows only");
#endif
}


std::vector<std::uint8_t> renderVideoPreview(const std::filesystem::path& source) {
#ifdef _WIN32
    const auto cacheRoot=std::filesystem::temp_directory_path()/"lancloud-video-preview-cache";
    std::filesystem::create_directories(cacheRoot);
    auto key=source.filename().string();
    if(key.empty()) key=cloud::common::randomHex(16);
    const auto cacheFile=cacheRoot/(key+".gif");
    if(std::filesystem::is_regular_file(cacheFile)) {
        try { return readPreviewAsset(cacheFile); }
        catch(...) {
            std::error_code ignored;
            std::filesystem::remove(cacheFile,ignored);
        }
    }

    const auto work=std::filesystem::temp_directory_path()/
        ("lancloud-video-preview-"+cloud::common::randomHex(16));
    std::filesystem::create_directories(work);
    const auto output=work/"preview.gif";
    const auto errorPath=work/"error.txt";
    try {
        renderVideoPreviewGif(source,output,errorPath);
        auto bytes=readPreviewAsset(output);
        std::error_code ignored;
        std::filesystem::copy_file(output,cacheFile,
            std::filesystem::copy_options::overwrite_existing,ignored);
        std::filesystem::remove_all(work,ignored);
        return bytes;
    } catch(...) {
        std::error_code ignored;
        std::filesystem::remove_all(work,ignored);
        throw;
    }
#else
    throw ServiceError(ErrorCode::InternalError,"video preview is currently available on Windows only");
#endif
}

} // namespace

ServerApp::ServerApp(std::string address, std::uint16_t port,
                     const std::filesystem::path& runtimeRoot)
    : address_(std::move(address)), port_(port),
      repository_(runtimeRoot / "cloud.db", runtimeRoot / "storage"),
      markdownConverter_(converterHelperPath(),runtimeRoot/"conversion") {}

void ServerApp::run() {
    auto listener = listenTcp(address_, port_);
    std::cout << "LanCloudDrive server listening on " << address_ << ':' << port_ << '\n';
    std::thread(&ServerApp::cleanupLoop, this).detach();
    for (;;) {
        std::string peer;
        auto client = acceptTcp(listener, &peer);
        std::cout << "client connected: " << peer << '\n';
        std::thread(&ServerApp::serveClient, this, std::move(client), std::move(peer)).detach();
    }
}

// 成员2：服务端存储 - 上传会话过期清理的后台回收线程。
void ServerApp::cleanupLoop() {
    // 清理间隔（秒），可用 LANCLOUD_UPLOAD_CLEANUP_SECONDS 覆盖，默认 10 分钟。
    const auto interval = [] {
        if (const char* v = std::getenv("LANCLOUD_UPLOAD_CLEANUP_SECONDS")) {
            try { const auto n = std::stoi(v); return n < 1 ? 1 : n; } catch (...) { return 600; }
        }
        return 600;
    }();
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(interval));
        try {
            repository_.cleanupExpiredUploads();
            std::cout << "cleaned up expired upload sessions\n";
        } catch (const std::exception& e) {
            std::cout << "upload cleanup failed: " << e.what() << '\n';
        }
    }
}

void ServerApp::serveClient(Socket client, std::string peer) {
    try {
        for (;;) {
            const auto request = receivePacket(client);
            sendPacket(client, handle(request));
        }
    } catch (const std::exception& e) {
        std::cout << "client disconnected: " << peer << " (" << e.what() << ")\n";
    }
}

std::int64_t ServerApp::requireUser(const json::Object& object) {
    const auto token=json::requireString(object,"token");
    const auto user=sessions_.find(token);
    if(!user) throw ServiceError(ErrorCode::Unauthorized,"login required or session expired");
    return *user;
}

Packet ServerApp::error(const Packet& request, ErrorCode code, const std::string& message) const {
    return makeJsonPacket(MessageType::ErrorResp,request.header.requestId,
        json::object({{"originalType",std::to_string(static_cast<std::uint16_t>(request.header.type))},
                      {"errorCode",json::quote(std::string(toString(code)))},
                      {"message",json::quote(message)}}),FlagResponse|FlagError);
}

Packet ServerApp::handle(const Packet& request) {
    try {
        if(request.header.requestId==0) throw ServiceError(ErrorCode::BadRequest,"requestId must be non-zero");
        if((request.header.flags&(FlagResponse|FlagPush))!=0) throw ServiceError(ErrorCode::BadRequest,"client packet has invalid flags");
        const auto type=request.header.type;
        if(type==MessageType::Heartbeat)
            return makeJsonPacket(MessageType::Heartbeat,request.header.requestId,"{}",FlagResponse);
        if(type==MessageType::Hello)
            return makeJsonPacket(MessageType::Hello,request.header.requestId,
                json::object({{"version",std::to_string(kProtocolVersion)}}),FlagResponse);

        if(type==MessageType::UploadChunk) {
            constexpr std::size_t prefix=64+32+8;
            if(request.body.size()<prefix) throw ServiceError(ErrorCode::BadRequest,"upload block is too short");
            const std::string token(request.body.begin(),request.body.begin()+64);
            const std::string transfer(request.body.begin()+64,request.body.begin()+96);
            const auto user=sessions_.find(token);
            if(!user) throw ServiceError(ErrorCode::Unauthorized,"login required or session expired");
            const auto offset=static_cast<std::int64_t>(read64(request.body.data()+96));
            const auto received=repository_.appendUpload(*user,transfer,offset,request.body.data()+prefix,request.body.size()-prefix);
            return makeJsonPacket(MessageType::UploadChunkAck,request.header.requestId,
                json::object({{"received",std::to_string(received)}}),FlagResponse);
        }

        if(request.body.size()>kMaxJsonBodySize) throw ServiceError(ErrorCode::BadRequest,"JSON request too large");
        const auto body=json::parseObject(bodyAsString(request));
        switch(type) {
        case MessageType::RegisterReq: {
            repository_.registerUser(json::requireString(body,"username"),json::requireString(body,"password"));
            return makeJsonPacket(MessageType::RegisterResp,request.header.requestId,
                json::object({{"ok","true"}}),FlagResponse);
        }
        case MessageType::LoginReq: {
            const auto user=repository_.authenticate(json::requireString(body,"username"),json::requireString(body,"password"));
            if(!user) throw ServiceError(ErrorCode::Unauthorized,"invalid username or password");
            const auto token=sessions_.create(*user);
            return makeJsonPacket(MessageType::LoginResp,request.header.requestId,
                json::object({{"token",json::quote(token)}}),FlagResponse);
        }
        case MessageType::LogoutReq: {
            const auto token=json::requireString(body,"token");
            if(!sessions_.find(token)) throw ServiceError(ErrorCode::Unauthorized,"session is not active");
            sessions_.remove(token);
            return makeJsonPacket(MessageType::LogoutResp,request.header.requestId,"{}",FlagResponse);
        }
        case MessageType::ListReq: {
            const auto user=requireUser(body); const auto parent=json::requireInt(body,"parentId");
            std::vector<std::string> entries;
            for(const auto& node:repository_.list(user,parent)) {
                entries.push_back(json::object({{"id",std::to_string(node.id)},
                    {"parentId",std::to_string(node.parentId)},{"name",json::quote(node.name)},
                    {"directory",boolean(node.directory)},{"size",std::to_string(node.size)},
                    {"modifiedAt",std::to_string(node.modifiedAt)}}));
            }
            return makeJsonPacket(MessageType::ListResp,request.header.requestId,
                json::object({{"parentId",std::to_string(parent)},{"entries",json::array(entries)}}),FlagResponse);
        }
        case MessageType::MkdirReq: {
            const auto user=requireUser(body);
            const auto id=repository_.mkdir(user,json::requireInt(body,"parentId"),json::requireString(body,"name"));
            return makeJsonPacket(MessageType::MkdirResp,request.header.requestId,
                json::object({{"nodeId",std::to_string(id)}}),FlagResponse);
        }
        case MessageType::RenameReq: {
            const auto user=requireUser(body); repository_.renameNode(user,json::requireInt(body,"nodeId"),json::requireString(body,"name"));
            return makeJsonPacket(MessageType::RenameResp,request.header.requestId,"{}",FlagResponse);
        }
        case MessageType::DeleteReq: {
            const auto user=requireUser(body); repository_.deleteNode(user,json::requireInt(body,"nodeId"));
            return makeJsonPacket(MessageType::DeleteResp,request.header.requestId,"{}",FlagResponse);
        }
        case MessageType::ShareCreateReq: {
            const auto user=requireUser(body);
            const auto code=repository_.createShareCode(user,json::requireInt(body,"nodeId"));
            return makeJsonPacket(MessageType::ShareCreateResp,request.header.requestId,
                json::object({{"code",json::quote(code)}}),FlagResponse);
        }
        case MessageType::ShareClaimReq: {
            const auto user=requireUser(body);
            const auto nodeId=repository_.claimShareCode(user,json::requireString(body,"code"));
            return makeJsonPacket(MessageType::ShareClaimResp,request.header.requestId,
                json::object({{"nodeId",std::to_string(nodeId)}}),FlagResponse);
        }
        case MessageType::UploadInitReq: {
            const auto user=requireUser(body);
            const auto result=repository_.beginUpload(user,json::requireInt(body,"parentId"),json::requireString(body,"name"),json::requireInt(body,"size"),json::requireString(body,"sha256"));
            return makeJsonPacket(MessageType::UploadInitResp,request.header.requestId,
                json::object({{"transferId",json::quote(result.transferId)},{"instant",boolean(result.instant)},
                              {"nodeId",std::to_string(result.nodeId)},{"chunkSize",std::to_string(kDefaultChunkSize)},
                              {"received",std::to_string(result.received)}}),FlagResponse);
        }
        case MessageType::UploadFinishReq: {
            const auto user=requireUser(body); const auto id=repository_.finishUpload(user,json::requireString(body,"transferId"));
            return makeJsonPacket(MessageType::UploadFinishResp,request.header.requestId,
                json::object({{"nodeId",std::to_string(id)}}),FlagResponse);
        }
        case MessageType::DownloadInitReq: {
            const auto user=requireUser(body); const auto info=repository_.beginDownload(user,json::requireInt(body,"nodeId"));
            return makeJsonPacket(MessageType::DownloadInitResp,request.header.requestId,
                json::object({{"transferId",json::quote(info.transferId)},{"name",json::quote(info.name)},
                              {"size",std::to_string(info.size)},{"sha256",json::quote(info.sha256)},
                              {"chunkSize",std::to_string(kDefaultChunkSize)}}),FlagResponse);
        }
        case MessageType::DownloadChunkReq: {
            const auto user=requireUser(body); bool final=false;
            const auto maxBytes=static_cast<std::size_t>(std::clamp<std::int64_t>(json::requireInt(body,"maxBytes"),1,kDefaultChunkSize));
            auto bytes=repository_.readDownload(user,json::requireString(body,"transferId"),json::requireInt(body,"offset"),maxBytes,final);
            return makePacket(MessageType::DownloadChunk,request.header.requestId,std::move(bytes),
                              FlagResponse|FlagBinary|(final?FlagFinal:FlagNone));
        }
        case MessageType::ConvertReq: {
            const auto user=requireUser(body);
            const auto source=repository_.getStoredFile(user,json::requireInt(body,"nodeId"));
            auto outputName=json::requireString(body,"outputName");
            if(outputName.empty()) outputName=defaultMarkdownName(source.name);
            if(!endsWithMarkdownExtension(outputName))
                throw ServiceError(ErrorCode::BadRequest,"output name must end with .md");
            const auto converted=markdownConverter_.convert(source.blobPath,source.name);
            const auto nodeId=repository_.importLocalFile(user,source.parentId,
                                                          outputName,converted.path());
            return makeJsonPacket(MessageType::ConvertResp,request.header.requestId,
                json::object({{"nodeId",std::to_string(nodeId)},
                              {"name",json::quote(outputName)}}),FlagResponse);
        }
        case MessageType::PreviewReq: {
            const auto user=requireUser(body);
            const auto source=repository_.getStoredFile(user,json::requireInt(body,"nodeId"));
            const auto maxBytes=static_cast<std::size_t>(std::clamp<std::int64_t>(
                json::requireInt(body,"maxBytes"),1,512*1024));
            const auto extension=lowerExtension(source.name);
            bool markdown=false;
            bool truncated=false;
            std::string content;
            if(isTextPreviewable(source.name)) {
                content=readPreviewText(source.blobPath,maxBytes,truncated);
                markdown=extension==".md" || extension==".markdown";
            } else if(extension==".docx" || extension==".pdf") {
                const auto converted=markdownConverter_.convert(source.blobPath,source.name);
                content=readPreviewText(converted.path(),maxBytes,truncated);
                markdown=true;
            } else {
                throw ServiceError(ErrorCode::BadRequest,
                    "preview supports text, Markdown, DOCX, and text-based PDF files");
            }
            return makeJsonPacket(MessageType::PreviewResp,request.header.requestId,
                json::object({{"name",json::quote(source.name)},
                              {"content",json::quote(content)},
                              {"markdown",boolean(markdown)},
                              {"truncated",boolean(truncated)}}),FlagResponse);
        }
        case MessageType::PreviewAssetReq: {
            const auto user=requireUser(body);
            const auto source=repository_.getStoredFile(user,json::requireInt(body,"nodeId"));
            const auto extension=lowerExtension(source.name);
            std::vector<std::uint8_t> bytes;
            const auto page=json::requireInt(body,"page");
            if(page<1) throw ServiceError(ErrorCode::BadRequest,"preview page must be positive");
            if(isImagePreviewable(source.name)) {
                if(page!=1) throw ServiceError(ErrorCode::BadRequest,"image preview has only one page");
                bytes=readPreviewAsset(source.blobPath);
            } else if(extension==".pdf") {
                bytes=renderPdfPreview(source.blobPath,page);
            } else if(isVideoPreviewable(source.name)) {
                if(page!=1) throw ServiceError(ErrorCode::BadRequest,"video preview has only one clip");
                bytes=renderVideoPreview(source.blobPath);
            } else throw ServiceError(ErrorCode::BadRequest,
                "visual preview supports images, PDF, and common video files");
            return makePacket(MessageType::PreviewAssetResp,request.header.requestId,
                              std::move(bytes),FlagResponse|FlagBinary);
        }
        default: throw ServiceError(ErrorCode::BadRequest,"unsupported message type");
        }
    } catch(const ServiceError& e) { return error(request,e.code(),e.what()); }
      catch(const std::exception& e) { return error(request,ErrorCode::BadRequest,e.what()); }
}

} // namespace cloud::server
