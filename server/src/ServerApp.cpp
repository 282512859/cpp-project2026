// 负责人：成员1：服务端架构/组长
#include "cloud/server/ServerApp.h"
#include "cloud/server/ServiceError.h"
#include "cloud/common/JsonLite.h"
#include "cloud/common/MessageType.h"
#include "cloud/common/ProtocolConstants.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace cloud::server {
namespace {

using namespace cloud::common;

std::uint64_t read64(const std::uint8_t* bytes) {
    std::uint64_t value=0;
    for(int i=0;i<8;++i) value=(value<<8U)|bytes[i];
    return value;
}

std::string boolean(bool value) { return value ? "true" : "false"; }

} // namespace

ServerApp::ServerApp(std::string address, std::uint16_t port,
                     const std::filesystem::path& runtimeRoot)
    : address_(std::move(address)), port_(port),
      repository_(runtimeRoot / "cloud.db", runtimeRoot / "storage") {}

void ServerApp::run() {
    auto listener = listenTcp(address_, port_);
    std::cout << "LanCloudDrive server listening on " << address_ << ':' << port_ << '\n';
    for (;;) {
        std::string peer;
        auto client = acceptTcp(listener, &peer);
        std::cout << "client connected: " << peer << '\n';
        std::thread(&ServerApp::serveClient, this, std::move(client), std::move(peer)).detach();
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
                              {"nodeId",std::to_string(result.nodeId)},{"chunkSize",std::to_string(kDefaultChunkSize)}}),FlagResponse);
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
        default: throw ServiceError(ErrorCode::BadRequest,"unsupported message type");
        }
    } catch(const ServiceError& e) { return error(request,e.code(),e.what()); }
      catch(const std::exception& e) { return error(request,ErrorCode::BadRequest,e.what()); }
}

} // namespace cloud::server
