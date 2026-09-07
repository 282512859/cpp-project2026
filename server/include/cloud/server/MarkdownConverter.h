#pragma once

#include <filesystem>
#include <string>

namespace cloud::server {

class ConvertedDocument {
public:
    ConvertedDocument(std::filesystem::path workDirectory,
                      std::filesystem::path outputPath);
    ~ConvertedDocument();
    ConvertedDocument(const ConvertedDocument&) = delete;
    ConvertedDocument& operator=(const ConvertedDocument&) = delete;
    ConvertedDocument(ConvertedDocument&& other) noexcept;
    ConvertedDocument& operator=(ConvertedDocument&& other) noexcept;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return outputPath_;
    }

private:
    void cleanup() noexcept;
    std::filesystem::path workDirectory_;
    std::filesystem::path outputPath_;
};

class MarkdownConverter {
public:
    MarkdownConverter(std::filesystem::path helperPath,
                      std::filesystem::path tempRoot);

    ConvertedDocument convert(const std::filesystem::path& sourceBlob,
                              const std::string& sourceName) const;

private:
    std::filesystem::path helperPath_;
    std::filesystem::path tempRoot_;
};

} // namespace cloud::server
