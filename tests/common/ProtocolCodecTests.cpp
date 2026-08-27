#include "cloud/common/JsonLite.h"
#include "cloud/common/ProtocolCodec.h"
#include "cloud/common/Sha256.h"

#include <cassert>
#include <iostream>

int main() {
    using namespace cloud::common;
    const auto first=makeJsonPacket(MessageType::LoginReq,42,"{\"x\":1}");
    const auto second=makeJsonPacket(MessageType::ListReq,43,"{}");
    const auto a=encodePacket(first), b=encodePacket(second);
    PacketStreamDecoder decoder;
    Packet out; std::string error;
    for(std::size_t i=0;i<a.size();++i) {
        decoder.append(&a[i],1);
        if(i+1<a.size()) assert(!decoder.tryPop(out,error));
    }
    assert(decoder.tryPop(out,error));
    assert(out.header.requestId==42&&bodyAsString(out)=="{\"x\":1}");
    std::vector<std::uint8_t> joined; joined.insert(joined.end(),a.begin(),a.end()); joined.insert(joined.end(),b.begin(),b.end());
    decoder.append(joined.data(),joined.size());
    assert(decoder.tryPop(out,error)&&out.header.requestId==42);
    assert(decoder.tryPop(out,error)&&out.header.requestId==43);
    assert(!decoder.tryPop(out,error));
    assert(sha256Hex("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const auto obj=json::parseObject("{\"name\":\"中 文\",\"n\":12,\"ok\":true}");
    assert(json::requireString(obj,"name")=="中 文");
    assert(json::requireInt(obj,"n")==12&&json::requireBool(obj,"ok"));
    std::cout<<"cloud_common_tests passed\n";
}
