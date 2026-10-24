#include "platform/filesystem.h"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <thread>

namespace fs = std::filesystem;
namespace nolf::platform {
struct InputFile::Impl {
    explicit Impl(fs::path name) : path(std::move(name)), input(path, std::ios::binary | std::ios::ate) {
        if (!input) throw std::runtime_error("cannot open " + path.string());
        const auto end = input.tellg();
        if (end < 0) throw std::runtime_error("cannot determine file size: " + path.string());
        length = static_cast<std::uint64_t>(end);
    }
    fs::path path;
    mutable std::ifstream input;
    mutable std::mutex mutex;
    std::uint64_t length;
};
InputFile::InputFile(fs::path path) : impl_(std::make_unique<Impl>(std::move(path))) {}
InputFile::~InputFile() = default;
std::uint64_t InputFile::size() const { return impl_->length; }
const fs::path& InputFile::path() const { return impl_->path; }
void InputFile::read_at(std::uint64_t offset, std::span<std::byte> output) const {
    if (offset > size() || output.size() > size() - offset) throw std::runtime_error("file read outside extent: " + path().string());
    if (output.empty()) return;
    if (offset > std::uint64_t(std::numeric_limits<std::streamoff>::max()) ||
        output.size() > std::uint64_t(std::numeric_limits<std::streamsize>::max()))
        throw std::runtime_error("file read exceeds host stream range");
    std::lock_guard lock(impl_->mutex);
    impl_->input.clear();
    impl_->input.seekg(static_cast<std::streamoff>(offset));
    if (!impl_->input.read(reinterpret_cast<char*>(output.data()), static_cast<std::streamsize>(output.size())))
        throw std::runtime_error("short file read: " + path().string());
}
struct SeekableFile::Impl {
    Impl(fs::path path, FileAccess mode) : access(mode) {
        auto flags = std::ios::binary;
        switch (access) {
        case FileAccess::Read: flags |= std::ios::in; break;
        case FileAccess::Write: flags |= std::ios::out | std::ios::trunc; break;
        case FileAccess::ReadWrite: flags |= std::ios::in | std::ios::out; break;
        default: throw std::invalid_argument("invalid file access");
        }
        stream.open(path,flags);
        if (!stream) throw std::runtime_error("cannot open binary file");
        if (access != FileAccess::Write) {
            stream.seekg(0,std::ios::end);
            const auto end = stream.tellg();
            if (end < 0) throw std::runtime_error("cannot determine binary file size");
            length = static_cast<std::uint64_t>(end);
        }
    }
    FileAccess access;
    std::fstream stream;
    mutable std::mutex mutex;
    std::uint64_t length = 0;
};
namespace {
void file_extent(std::uint64_t offset, std::size_t size) {
    const auto maximum = std::uint64_t(std::numeric_limits<std::streamoff>::max());
    if (offset > maximum || size > maximum - offset ||
        size > std::uint64_t(std::numeric_limits<std::streamsize>::max()))
        throw std::length_error("binary file operation exceeds host stream range");
}
}
SeekableFile::SeekableFile(fs::path path, FileAccess access) : impl_(std::make_unique<Impl>(std::move(path),access)) {}
SeekableFile::~SeekableFile() = default;
std::uint64_t SeekableFile::size() const { std::lock_guard lock(impl_->mutex); return impl_->length; }
void SeekableFile::read_at(std::uint64_t offset, std::span<std::byte> output) {
    file_extent(offset,output.size());
    std::lock_guard lock(impl_->mutex);
    if (impl_->access == FileAccess::Write) throw std::logic_error("file is not readable");
    if (offset > impl_->length || output.size() > impl_->length - offset) throw std::out_of_range("binary file read extent");
    if (output.empty()) return;
    impl_->stream.clear(); impl_->stream.seekg(static_cast<std::streamoff>(offset));
    if (!impl_->stream.read(reinterpret_cast<char*>(output.data()),static_cast<std::streamsize>(output.size())))
        throw std::runtime_error("short binary file read");
}
void SeekableFile::write_at(std::uint64_t offset, std::span<const std::byte> input) {
    file_extent(offset,input.size());
    std::lock_guard lock(impl_->mutex);
    if (impl_->access == FileAccess::Read) throw std::logic_error("file is not writable");
    if (input.empty()) return;
    impl_->stream.clear(); impl_->stream.seekp(static_cast<std::streamoff>(offset));
    if (!impl_->stream.write(reinterpret_cast<const char*>(input.data()),static_cast<std::streamsize>(input.size())))
        throw std::runtime_error("short binary file write");
    impl_->length = std::max(impl_->length,offset + input.size());
}
void SeekableFile::flush() {
    std::lock_guard lock(impl_->mutex);
    if (impl_->access != FileAccess::Read && !impl_->stream.flush()) throw std::runtime_error("binary file flush failed");
}
std::string asset_key(std::string_view name) {
    if (name.empty() || name.front() == '/' || name.front() == '\\' ||
        name.find(':') != name.npos || name.find('\0') != name.npos)
        throw std::invalid_argument("invalid asset path");
    std::string key, component;
    auto append = [&] {
        if (component == "..") throw std::invalid_argument("asset path traversal");
        if (!component.empty() && component != ".") {
            if (!key.empty()) key += '/';
            key += component;
        }
        component.clear();
    };
    for (unsigned char c : name) {
        if (c == '/' || c == '\\') append();
        else component += static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    }
    append();
    if (key.empty()) throw std::invalid_argument("empty asset path");
    return key;
}

std::vector<std::byte> read_file(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("cannot open " + path.string());
    const auto length = stream.tellg();
    if (length < 0 || static_cast<std::uintmax_t>(length) > std::numeric_limits<std::size_t>::max())
        throw std::runtime_error("invalid file size: " + path.string());
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("short read: " + path.string());
    return bytes;
}

struct AtomicOutputFile::Impl {
    fs::path destination, reservation, temporary;
    std::ofstream output;
    bool committed = false;
};
AtomicOutputFile::AtomicOutputFile(fs::path path) : impl_(std::make_unique<Impl>()) {
    impl_->destination = std::move(path);
    auto parent = impl_->destination.parent_path();
    if (parent.empty()) parent = ".";
    fs::create_directories(parent);
    static std::atomic<std::uint64_t> sequence{};
    for (;;) {
        impl_->reservation = parent / ("." + impl_->destination.filename().string() + ".tmp-" +
            std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + "-" +
            std::to_string(sequence.fetch_add(1)));
        std::error_code error;
        if (fs::create_directory(impl_->reservation, error)) break;
        if (error && error != std::errc::file_exists) throw fs::filesystem_error("reserve temporary file", impl_->reservation, error);
    }
    try {
        impl_->temporary = impl_->reservation / "data";
        impl_->output.exceptions(std::ios::badbit | std::ios::failbit);
        impl_->output.open(impl_->temporary, std::ios::binary | std::ios::trunc);
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(impl_->reservation, ignored);
        throw;
    }
}
AtomicOutputFile::~AtomicOutputFile() {
    if (!impl_->committed) {
        if (impl_->output.is_open()) {
            impl_->output.exceptions(std::ios::goodbit);
            impl_->output.close();
        }
        std::error_code ignored;
        fs::remove_all(impl_->reservation, ignored);
    }
}
void AtomicOutputFile::write(std::span<const std::byte> bytes) {
    if (impl_->committed) throw std::logic_error("atomic file is already committed");
    if (bytes.size() > std::uint64_t(std::numeric_limits<std::streamsize>::max())) throw std::length_error("file write exceeds host stream range");
    if (!bytes.empty()) impl_->output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}
void AtomicOutputFile::commit() {
    if (impl_->committed) throw std::logic_error("atomic file is already committed");
    impl_->output.close();
    detail::commit_file(impl_->temporary, impl_->destination);
    impl_->committed = true;
    std::error_code ignored;
    fs::remove(impl_->reservation, ignored);
}
void atomic_write(const fs::path& path, std::span<const std::byte> bytes) {
    AtomicOutputFile output(path);
    output.write(bytes);
    output.commit();
}

DirectoryIndex::DirectoryIndex(fs::path root) {
    root = fs::canonical(root);
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_symlink()) throw std::runtime_error("asset symlink is not allowed: " + entry.path().string());
        const bool directory = entry.is_directory();
        if (!directory && !entry.is_regular_file()) throw std::runtime_error("unsupported asset file type: " + entry.path().string());
        auto key = asset_key(entry.path().lexically_relative(root).generic_string());
        auto [previous, inserted] = entries_.emplace(key, Entry{entry.path(), directory});
        if (!inserted) throw std::runtime_error("ambiguous asset paths: " + previous->second.path.string() + " and " + entry.path().string());
        if (!directory) files_.emplace(std::move(key), entry.path());
    }
}
fs::path DirectoryIndex::resolve(std::string_view name) const {
    auto it = files_.find(asset_key(name));
    return it == files_.end() ? fs::path{} : it->second;
}
std::vector<std::string> DirectoryIndex::names() const {
    std::vector<std::string> result;
    result.reserve(files_.size());
    for (const auto& [name, path] : files_) result.push_back(name);
    return result;
}
}
