// 负责人：成员1：服务端架构/组长
#pragma once

#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace cloud::common::json {

using Object = std::map<std::string, std::string>;

std::string quote(const std::string& value);
std::string object(std::initializer_list<std::pair<std::string, std::string>> fields);
std::string array(const std::vector<std::string>& rawValues);
Object parseObject(const std::string& text);
std::vector<Object> parseObjectArray(const std::string& rawArray);

std::string requireString(const Object& object, const std::string& key);
std::int64_t requireInt(const Object& object, const std::string& key);
bool requireBool(const Object& object, const std::string& key);
std::string optionalString(const Object& object, const std::string& key,
                           const std::string& fallback = {});

} // namespace cloud::common::json
