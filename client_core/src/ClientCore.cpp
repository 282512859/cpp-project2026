#include "cloud/client/ClientCore.h"
#include "cloud/common/JsonLite.h"
#include "cloud/common/ProtocolConstants.h"
#include "cloud/common/Sha256.h"

#include <algorithm>
#include <array>
#include <fstream>

namespace cloud::client {
namespace {

using namespace cloud::common;

void put64(std::uint8_t* bytes,std::uint64_t value) {
    for(int i=7;i>=0;--i) bytes[7-i]=static_cast<std::uint8_t>(value>>(i*8));
}

} // namespace

ClientCore::ClientCore(const std::string& host,std::uint16_t port)
    : socket_(connectTcp(host,port)) {
    const auto response=request(MessageType::Hello,"{}");
    if(response.header.type!=MessageType::Hello) throw std::runtime_error("server HELLO failed");
}

Packet ClientCore::request(MessageType type,const std::string& jsonText) {
    return request(makeJsonPacket(type,nextRequestId_++,jsonText));
}

Packet ClientCore::request(Packet packet) {
    const auto id=packet.header.requestId;
    sendPacket(socket_,packet);
    auto response=receivePacket(socket_);
    if(response.header.requestId!=id||(response.header.flags&FlagResponse)==0)
        throw std::runtime_error("server returned an unmatched response");
    if((response.header.flags&FlagError)!=0) {
        const auto object=json::parseObject(bodyAsString(response));
        throw ClientError(json::optionalString(object,"errorCode","INTERNAL_ERROR"),
                          json::optionalString(object,"message","server request failed"));
    }
    return response;
}

json::Object ClientCore::jsonResponse(MessageType type,const std::string& jsonText) {
    return json::parseObject(bodyAsString(request(type,jsonText)));
}

std::string ClientCore::authJson(std::initializer_list<std::pair<std::string,std::string>> fields) const {
    std::string out="{\"token\":"+json::quote(token_);
    for(const auto& [key,value]:fields) out+=","+json::quote(key)+":"+value;
    return out+"}";
}

void ClientCore::registerUser(const std::string& username,const std::string& password) {
    jsonResponse(MessageType::RegisterReq,json::object({{"username",json::quote(username)},{"password",json::quote(password)}}));
}

void ClientCore::login(const std::string& username,const std::string& password) {
    const auto response=jsonResponse(MessageType::LoginReq,json::object({{"username",json::quote(username)},{"password",json::quote(password)}}));
    token_=json::requireString(response,"token");
}

void ClientCore::logout() {
    if(token_.empty()) return;
    jsonResponse(MessageType::LogoutReq,authJson({})); token_.clear();
}

std::vector<RemoteNode> ClientCore::list(std::int64_t parentId) {
    const auto response=jsonResponse(MessageType::ListReq,authJson({{"parentId",std::to_string(parentId)}}));
    std::vector<RemoteNode> nodes;
    for(const auto& item:json::parseObjectArray(json::requireString(response,"entries"))) {
        nodes.push_back({json::requireInt(item,"id"),json::requireInt(item,"parentId"),
                         json::requireString(item,"name"),json::requireBool(item,"directory"),
                         json::requireInt(item,"size"),json::requireInt(item,"modifiedAt")});
    }
    return nodes;
}

std::int64_t ClientCore::mkdir(std::int64_t parentId,const std::string& name) {
    const auto response=jsonResponse(MessageType::MkdirReq,authJson({{"parentId",std::to_string(parentId)},{"name",json::quote(name)}}));
    return json::requireInt(response,"nodeId");
}

void ClientCore::renameNode(std::int64_t nodeId,const std::string& name) {
    jsonResponse(MessageType::RenameReq,authJson({{"nodeId",std::to_string(nodeId)},{"name",json::quote(name)}}));
}

void ClientCore::deleteNode(std::int64_t nodeId) {
    jsonResponse(MessageType::DeleteReq,authJson({{"nodeId",std::to_string(nodeId)}}));
}

std::int64_t ClientCore::upload(const std::filesystem::path& localPath,
                                std::int64_t parentId,const std::string& remoteName,
                                ProgressCallback progress) {
    if(!std::filesystem::is_regular_file(localPath)) throw std::runtime_error("local upload path is not a regular file");
    const auto size=static_cast<std::int64_t>(std::filesystem::file_size(localPath));
    const auto sha=sha256File(localPath);
    const auto name=remoteName.empty()?localPath.filename().string():remoteName;
    const auto init=jsonResponse(MessageType::UploadInitReq,authJson({{"parentId",std::to_string(parentId)},
        {"name",json::quote(name)},{"size",std::to_string(size)},{"sha256",json::quote(sha)}}));
    if(json::requireBool(init,"instant")) { if(progress) progress(size,size); return json::requireInt(init,"nodeId"); }
    const auto transfer=json::requireString(init,"transferId");
    const auto chunkSize=static_cast<std::size_t>(json::requireInt(init,"chunkSize"));
    std::ifstream in(localPath,std::ios::binary);
    std::int64_t offset=0;
    while(offset<size) {
        const auto count=static_cast<std::size_t>(std::min<std::int64_t>(chunkSize,size-offset));
        std::vector<std::uint8_t> body(64+32+8+count);
        std::copy(token_.begin(),token_.end(),body.begin());
        std::copy(transfer.begin(),transfer.end(),body.begin()+64);
        put64(body.data()+96,static_cast<std::uint64_t>(offset));
        in.read(reinterpret_cast<char*>(body.data()+104),static_cast<std::streamsize>(count));
        if(static_cast<std::size_t>(in.gcount())!=count) throw std::runtime_error("cannot read local upload file");
        const auto response=request(makePacket(MessageType::UploadChunk,nextRequestId_++,std::move(body),FlagBinary));
        const auto ack=json::parseObject(bodyAsString(response));
        offset=json::requireInt(ack,"received");
        if(progress) progress(offset,size);
    }
    const auto finish=jsonResponse(MessageType::UploadFinishReq,authJson({{"transferId",json::quote(transfer)}}));
    return json::requireInt(finish,"nodeId");
}

void ClientCore::download(std::int64_t nodeId,const std::filesystem::path& localPath,
                          ProgressCallback progress) {
    if(std::filesystem::exists(localPath)) throw std::runtime_error("download target already exists");
    const auto init=jsonResponse(MessageType::DownloadInitReq,authJson({{"nodeId",std::to_string(nodeId)}}));
    const auto transfer=json::requireString(init,"transferId");
    const auto size=json::requireInt(init,"size");
    const auto expected=json::requireString(init,"sha256");
    const auto chunkSize=json::requireInt(init,"chunkSize");
    auto temporary=localPath; temporary += ".download";
    std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
    if(!out) throw std::runtime_error("cannot create local download file");
    std::int64_t offset=0;
    try {
        while(offset<size || size==0) {
            auto response=request(MessageType::DownloadChunkReq,authJson({{"transferId",json::quote(transfer)},
                {"offset",std::to_string(offset)},{"maxBytes",std::to_string(chunkSize)}}));
            if(response.header.type!=MessageType::DownloadChunk||(response.header.flags&FlagBinary)==0)
                throw std::runtime_error("invalid download block response");
            out.write(reinterpret_cast<const char*>(response.body.data()),static_cast<std::streamsize>(response.body.size()));
            if(!out) throw std::runtime_error("cannot write local download file");
            offset+=static_cast<std::int64_t>(response.body.size());
            if(progress) progress(offset,size);
            if((response.header.flags&FlagFinal)!=0) break;
        }
        out.close();
        if(offset!=size||sha256File(temporary)!=expected) throw std::runtime_error("download size or SHA-256 verification failed");
        std::filesystem::rename(temporary,localPath);
    } catch(...) { out.close(); std::error_code ignored; std::filesystem::remove(temporary,ignored); throw; }
}

} // namespace cloud::client
