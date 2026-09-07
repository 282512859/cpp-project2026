// 负责人：成员4：客户端界面
#include "cloud/client/ClientCore.h"
#include "cloud/common/ProtocolConstants.h"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

void help() {
    std::cout <<
      "commands:\n"
      "  register <username> <password>\n"
      "  login <username> <password>\n"
      "  ls [parentId]\n"
      "  mkdir <parentId> \"name\"\n"
      "  rename <nodeId> \"new name\"\n"
      "  rm <nodeId>\n"
      "  put \"local path\" <parentId> [\"remote name\"]\n"
      "  get <nodeId> \"local path\"\n"
      "  convert <nodeId> [\"output name.md\"]\n"
      "  logout | help | quit\n";
}

void progress(std::int64_t done,std::int64_t total) {
    const auto percent=total?done*100/total:100;
    std::cout << "\r  " << percent << "% (" << done << '/' << total << ")" << std::flush;
    if(done==total) std::cout << '\n';
}

std::filesystem::path utf8Path(const std::string& value) {
#ifdef _WIN32
    const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),
                                         static_cast<int>(value.size()),nullptr,0);
    if(length<=0) throw std::runtime_error("invalid UTF-8 local path");
    std::wstring wide(static_cast<std::size_t>(length),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),
                        static_cast<int>(value.size()),wide.data(),length);
    return std::filesystem::path(wide);
#else
    return std::filesystem::path(value);
#endif
}

} // namespace

int main(int argc,char** argv) {
    try {
#ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
#endif
        const std::string host=argc>1?argv[1]:"127.0.0.1";
        const auto port=argc>2?static_cast<std::uint16_t>(std::stoul(argv[2])):cloud::common::kDefaultPort;
        cloud::client::ClientCore client(host,port);
        std::cout << "connected to " << host << ':' << port << '\n'; help();
        std::string line;
        while(std::cout<<"> "&&std::getline(std::cin,line)) {
            try {
                // PowerShell can prefix redirected UTF-8 input with a BOM.
                if(line.starts_with("\xEF\xBB\xBF")) line.erase(0,3);
                std::istringstream in(line); std::string command; in>>command;
                if(command.empty()) continue;
                if(command=="quit"||command=="exit") break;
                if(command=="help") { help(); continue; }
                if(command=="register") { std::string u,p; in>>u>>p; client.registerUser(u,p); std::cout<<"registered\n"; }
                else if(command=="login") { std::string u,p; in>>u>>p; client.login(u,p); std::cout<<"logged in\n"; }
                else if(command=="logout") { client.logout(); std::cout<<"logged out\n"; }
                else if(command=="ls") {
                    std::int64_t parent=0; in>>parent;
                    for(const auto& n:client.list(parent))
                        std::cout<<std::setw(6)<<n.id<<"  "<<(n.directory?"DIR ":"FILE")<<"  "<<std::setw(10)<<n.size<<"  "<<n.name<<'\n';
                } else if(command=="mkdir") { std::int64_t p; std::string name; in>>p>>std::quoted(name); std::cout<<"created node "<<client.mkdir(p,name)<<'\n'; }
                else if(command=="rename") { std::int64_t id; std::string name; in>>id>>std::quoted(name); client.renameNode(id,name); std::cout<<"renamed\n"; }
                else if(command=="rm") { std::int64_t id; in>>id; client.deleteNode(id); std::cout<<"deleted\n"; }
                else if(command=="put") { std::string path,name; std::int64_t p; in>>std::quoted(path,'"','\0')>>p; if(in>>std::quoted(name)){} std::cout<<"uploaded as node "<<client.upload(utf8Path(path),p,name,progress)<<'\n'; }
                else if(command=="get") { std::int64_t id; std::string path; in>>id>>std::quoted(path,'"','\0'); client.download(id,utf8Path(path),progress); std::cout<<"downloaded\n"; }
                else if(command=="convert") { std::int64_t id; std::string name; in>>id; if(in>>std::quoted(name)){} std::cout<<"converted as node "<<client.convertToMarkdown(id,name)<<'\n'; }
                else std::cout<<"unknown command; type help\n";
            } catch(const cloud::client::ClientError& e) { std::cout<<"server error ["<<e.code()<<"] "<<e.what()<<'\n'; }
              catch(const std::exception& e) { std::cout<<"error: "<<e.what()<<'\n'; }
        }
    } catch(const std::exception& e) { std::cerr<<"fatal client error: "<<e.what()<<'\n'; return 1; }
}
