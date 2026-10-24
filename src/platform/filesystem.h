#pragma once
#include <cstddef>
#include <filesystem>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nolf::platform {
// Asset names use ASCII case folding, independently of the host locale.
// Absolute paths, traversal, drive prefixes and NUL bytes are rejected.
std::string asset_key(std::string_view name);
std::vector<std::byte> read_file(const std::filesystem::path& path);
void atomic_write(const std::filesystem::path& path, std::span<const std::byte> bytes);
class AtomicOutputFile {
public:
    explicit AtomicOutputFile(std::filesystem::path destination);
    ~AtomicOutputFile();
    AtomicOutputFile(const AtomicOutputFile&) = delete;
    AtomicOutputFile& operator=(const AtomicOutputFile&) = delete;
    void write(std::span<const std::byte> bytes);
    void commit();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Independent logical cursors can share one open file safely. The platform
// adapter owns the handle and all seek/read synchronization.
class InputFile {
public:
    explicit InputFile(std::filesystem::path path);
    ~InputFile();
    InputFile(const InputFile&) = delete;
    InputFile& operator=(const InputFile&) = delete;
    std::uint64_t size() const;
    const std::filesystem::path& path() const;
    void read_at(std::uint64_t offset, std::span<std::byte> output) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

enum class FileAccess { Read, Write, ReadWrite };
// An owned binary file with explicit offsets. Write creates/truncates;
// ReadWrite opens an existing file. Host handles and positioning stay here.
class SeekableFile {
public:
    SeekableFile(std::filesystem::path, FileAccess);
    ~SeekableFile();
    SeekableFile(const SeekableFile&) = delete;
    SeekableFile& operator=(const SeekableFile&) = delete;
    std::uint64_t size() const;
    void read_at(std::uint64_t, std::span<std::byte>);
    void write_at(std::uint64_t, std::span<const std::byte>);
    void flush();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

namespace detail {
// Flush and replace are supplied by the host adapter, including replacement
// of an existing file on Windows. Temporary and destination share a filesystem.
void commit_file(const std::filesystem::path& temporary, const std::filesystem::path& destination);
}

class DirectoryIndex {
public:
    explicit DirectoryIndex(std::filesystem::path root);
    std::filesystem::path resolve(std::string_view name) const;
    std::vector<std::string> names() const;
    struct Entry { std::filesystem::path path; bool directory; };
    const std::unordered_map<std::string, Entry>& entries() const { return entries_; }
private:
    std::unordered_map<std::string, std::filesystem::path> files_;
    std::unordered_map<std::string, Entry> entries_;
};
}
