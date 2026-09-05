// 负责人：成员1：服务端架构/组长
#include "cloud/common/JsonLite.h"
#include "cloud/common/ProtocolCodec.h"
#include "cloud/common/Sha256.h"

#include <cassert>
#include <iostream>

int main() {
    using namespace cloud::common;

    // 准备两个有不同 requestId 的报文，后面用 ID 判断解码顺序是否正确。
    const auto first = makeJsonPacket(MessageType::LoginReq, 42, "{\"x\":1}");
    const auto second = makeJsonPacket(MessageType::ListReq, 43, "{}");
    const auto a = encodePacket(first);
    const auto b = encodePacket(second);

    // 模拟极端“半包”：网络每次只到 1 字节，最后一个字节到达前都不应产出报文。
    PacketStreamDecoder decoder;
    Packet out;
    std::string error;
    for (std::size_t i = 0; i < a.size(); ++i) {
        decoder.append(&a[i], 1);
        if (i + 1 < a.size()) assert(!decoder.tryPop(out, error));
    }
    assert(decoder.tryPop(out, error));
    assert(out.header.requestId == 42 && bodyAsString(out) == "{\"x\":1}");

    // 模拟“粘包”：两个完整报文一次到达，decoder 应按顺序弹出两个包。
    std::vector<std::uint8_t> joined;
    joined.insert(joined.end(), a.begin(), a.end());
    joined.insert(joined.end(), b.begin(), b.end());
    decoder.append(joined.data(), joined.size());
    assert(decoder.tryPop(out, error) && out.header.requestId == 42);
    assert(decoder.tryPop(out, error) && out.header.requestId == 43);
    assert(!decoder.tryPop(out, error));

    // 使用公开的 SHA-256 标准测试向量，验证算法实现没有偏差。
    assert(sha256Hex("abc") ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    // 验证简化 JSON 解析器能处理 UTF-8 文本、整数和布尔值。
    const auto obj = json::parseObject("{\"name\":\"中 文\",\"n\":12,\"ok\":true}");
    assert(json::requireString(obj, "name") == "中 文");
    assert(json::requireInt(obj, "n") == 12 && json::requireBool(obj, "ok"));

    std::cout << "cloud_common_tests passed\n";
}
