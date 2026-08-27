#include "cloud/common/JsonLite.h"

#include <cctype>
#include <stdexcept>

namespace cloud::common::json {
namespace {

void skipSpace(const std::string& s, std::size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
}

std::string parseString(const std::string& s, std::size_t& i) {
    if (i >= s.size() || s[i] != '"') throw std::runtime_error("expected JSON string");
    ++i;
    std::string out;
    while (i < s.size()) {
        const char c = s[i++];
        if (c == '"') return out;
        if (c != '\\') { out.push_back(c); continue; }
        if (i >= s.size()) throw std::runtime_error("invalid JSON escape");
        switch (s[i++]) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        default: throw std::runtime_error("unsupported JSON escape");
        }
    }
    throw std::runtime_error("unterminated JSON string");
}

std::string parseRawValue(const std::string& s, std::size_t& i) {
    skipSpace(s, i);
    if (i >= s.size()) throw std::runtime_error("missing JSON value");
    if (s[i] == '"') return parseString(s, i);
    if (s[i] == '[' || s[i] == '{') {
        const std::size_t start = i;
        const char open = s[i], close = open == '[' ? ']' : '}';
        int depth = 0;
        bool inString = false, escaped = false;
        for (; i < s.size(); ++i) {
            const char c = s[i];
            if (inString) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') inString = false;
            } else if (c == '"') inString = true;
            else if (c == open) ++depth;
            else if (c == close && --depth == 0) { ++i; return s.substr(start, i - start); }
        }
        throw std::runtime_error("unterminated JSON container");
    }
    const std::size_t start = i;
    while (i < s.size() && s[i] != ',' && s[i] != '}') ++i;
    std::size_t end = i;
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

} // namespace

std::string quote(const std::string& value) {
    std::string out = "\"";
    for (unsigned char c : value) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) throw std::runtime_error("control character in JSON string");
            out.push_back(static_cast<char>(c));
        }
    }
    out.push_back('"');
    return out;
}

std::string object(std::initializer_list<std::pair<std::string, std::string>> fields) {
    std::string out = "{";
    bool first = true;
    for (const auto& [key, raw] : fields) {
        if (!first) out.push_back(',');
        first = false;
        out += quote(key) + ":" + raw;
    }
    out.push_back('}');
    return out;
}

std::string array(const std::vector<std::string>& rawValues) {
    std::string out = "[";
    for (std::size_t i = 0; i < rawValues.size(); ++i) {
        if (i) out.push_back(',');
        out += rawValues[i];
    }
    out.push_back(']');
    return out;
}

Object parseObject(const std::string& text) {
    std::size_t i = 0;
    skipSpace(text, i);
    if (i >= text.size() || text[i++] != '{') throw std::runtime_error("expected JSON object");
    Object out;
    while (true) {
        skipSpace(text, i);
        if (i < text.size() && text[i] == '}') { ++i; break; }
        const auto key = parseString(text, i);
        skipSpace(text, i);
        if (i >= text.size() || text[i++] != ':') throw std::runtime_error("expected ':'");
        out[key] = parseRawValue(text, i);
        skipSpace(text, i);
        if (i < text.size() && text[i] == ',') { ++i; continue; }
        if (i < text.size() && text[i] == '}') { ++i; break; }
        throw std::runtime_error("expected ',' or '}'");
    }
    skipSpace(text, i);
    if (i != text.size()) throw std::runtime_error("trailing JSON content");
    return out;
}

std::vector<Object> parseObjectArray(const std::string& rawArray) {
    std::size_t i = 0;
    skipSpace(rawArray, i);
    if (i >= rawArray.size() || rawArray[i++] != '[') throw std::runtime_error("expected JSON array");
    std::vector<Object> result;
    while (true) {
        skipSpace(rawArray, i);
        if (i < rawArray.size() && rawArray[i] == ']') break;
        const std::size_t start = i;
        const auto raw = parseRawValue(rawArray, i);
        if (raw.empty() || raw.front() != '{') throw std::runtime_error("array item is not object");
        result.push_back(parseObject(raw));
        skipSpace(rawArray, i);
        if (i < rawArray.size() && rawArray[i] == ',') { ++i; continue; }
        if (i < rawArray.size() && rawArray[i] == ']') break;
        (void)start;
        throw std::runtime_error("invalid JSON array");
    }
    return result;
}

std::string requireString(const Object& o, const std::string& key) {
    const auto it = o.find(key);
    if (it == o.end()) throw std::runtime_error("missing field: " + key);
    return it->second;
}
std::int64_t requireInt(const Object& o, const std::string& key) {
    const auto value = requireString(o, key);
    std::size_t used = 0;
    const auto result = std::stoll(value, &used);
    if (used != value.size()) throw std::runtime_error("invalid integer field: " + key);
    return result;
}
bool requireBool(const Object& o, const std::string& key) {
    const auto value = requireString(o, key);
    if (value == "true") return true;
    if (value == "false") return false;
    throw std::runtime_error("invalid boolean field: " + key);
}
std::string optionalString(const Object& o, const std::string& key, const std::string& fallback) {
    const auto it = o.find(key);
    return it == o.end() ? fallback : it->second;
}

} // namespace cloud::common::json
