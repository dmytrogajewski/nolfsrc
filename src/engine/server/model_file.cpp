#include "model_file.h"
#include "commonmodel.h"
#include "iltstream.h"
#include "ltcodes.h"
#include <bit>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
constexpr std::size_t file_limit = 128u * 1024 * 1024;
constexpr std::size_t memory_limit = 512u * 1024 * 1024;
struct Reader {
    std::span<const std::byte> data;
    std::size_t pos = 0;
    std::size_t remaining() const { return data.size() - pos; }
    void need(std::size_t n) const { if (n > remaining()) throw std::runtime_error("Truncated model record"); }
    void skip(std::size_t n) { need(n); pos += n; }
    std::uint32_t integer(unsigned bytes) {
        need(bytes); std::uint32_t result = 0;
        for (unsigned i = 0; i < bytes; ++i) result |= std::uint32_t(std::to_integer<unsigned char>(data[pos++])) << (i*8);
        return result;
    }
    std::uint32_t word() { return integer(4); }
    float scalar() {
        auto value = std::bit_cast<float>(word());
        if (!std::isfinite(value)) throw std::runtime_error("Non-finite model transform");
        return value;
    }
    void floats(std::size_t n) { need(n*4); for (std::size_t i = 0; i < n; ++i) scalar(); }
    std::string_view string() {
        auto length = integer(2); need(length);
        auto result = std::string_view(reinterpret_cast<const char*>(data.data()+pos),length);
        pos += length;
        if (result.find('\0') != result.npos) throw std::runtime_error("Embedded null in model string");
        return result;
    }
    std::uint32_t count(std::size_t minimum, std::uint32_t limit = 65536) {
        auto value = word();
        if (value > limit || (minimum && value > remaining()/minimum)) throw std::runtime_error("Invalid model record count");
        return value;
    }
};
struct Validator {
    std::map<std::string_view,Reader> chunks;
    std::size_t memory = 0;
    std::uint32_t version = 0, lods = 0, pool = 0, used_vertices = 0, nodes = 0;
    std::uint32_t required_nodes = 0;
    void allocate(std::size_t count,std::size_t size) {
        if (count > (memory_limit-memory)/size) throw std::runtime_error("Model exceeds native memory limit");
        memory += count*size;
    }
    Reader chunk(std::string_view name) {
        const auto found = chunks.find(name);
        if (found == chunks.end()) throw std::runtime_error("Missing model chunk " + std::string(name));
        return found->second;
    }
    void directory(std::span<const std::byte> data) {
        if (data.empty() || data.size() > file_limit) throw std::runtime_error("Invalid model file size");
        std::size_t position = 0;
        for (unsigned i = 0; i < 512; ++i) {
            Reader reader{data,position}; auto name = reader.string(); const auto next = reader.word();
            const auto end = next == UINT32_MAX ? data.size() : next;
            if (name.empty() || end < reader.pos || end > data.size() || end <= position)
                throw std::runtime_error("Invalid model chunk directory");
            if (!chunks.emplace(name,Reader{data.subspan(reader.pos,end-reader.pos)}).second)
                throw std::runtime_error("Duplicate model chunk");
            if (next == UINT32_MAX) return;
            position = next;
        }
        throw std::runtime_error("Too many model chunks");
    }
    void vertex_data(Reader& reader) {
        const auto triangles = reader.count(30);
        allocate(triangles,sizeof(ModelTriangle));
        std::uint32_t required_vertices=0;
        for (std::uint32_t i = 0; i < triangles; ++i) for (unsigned corner = 0; corner < 3; ++corner) {
            reader.floats(2); required_vertices=std::max(required_vertices,reader.integer(2)+1);
        }
        const auto records = reader.count(28);
        if (required_vertices>records) throw std::runtime_error("Model triangle vertex index out of range");
        allocate(records,sizeof(ModelVertex));
        for (std::uint32_t i = 0; i < records; ++i) {
            const auto vertices = reader.integer(2); reader.integer(2);
            if (vertices > pool-used_vertices) throw std::runtime_error("Model vertex pool overflow");
            used_vertices += vertices;
            for (std::uint32_t j=0;j<vertices;++j) {
                const auto node=reader.word();
                if (node>=65535) throw std::runtime_error("Model vertex bone index out of range");
                required_nodes=std::max(required_nodes,node+1); reader.floats(4);
            }
            reader.floats(6);
        }
    }
    void validate(std::span<const std::byte> data) {
        directory(data); memory = data.size();
        auto header = chunk("Header"); version = header.word();
        if (version < 9 || version > 12) throw std::runtime_error("Unsupported model version");
        header.skip(13*4); header.string(); header.scalar(); lods = header.integer(2); header.skip(62); header.floats(lods);
        auto pieces = chunk("Pieces"); pool = pieces.count(0,4*1024*1024); allocate(pool,sizeof(CModelVertRec));
        const auto piece_count = pieces.count(18); allocate(piece_count,sizeof(ModelPiece)+sizeof(ModelPiece*));
        for (std::uint32_t i = 0; i < piece_count; ++i) {
            pieces.integer(2); pieces.floats(2); if (version >= 10) pieces.word();
            const auto skipped = pieces.integer(2); pieces.skip(std::size_t(skipped)*4); pieces.string();
            allocate(lods,sizeof(ModelPieceData));
            for (std::uint32_t j = 0; j <= lods; ++j) vertex_data(pieces);
        }
        auto tree = chunk("Nodes");
        std::vector<std::uint32_t> pending{1};
        while (!pending.empty()) {
            if (!pending.back()) { pending.pop_back(); continue; }
            --pending.back();
            if (++nodes > 65535 || pending.size() > 512) throw std::runtime_error("Model skeleton exceeds limits");
            allocate(1,sizeof(ModelNode)+sizeof(LTMatrix)+sizeof(ModelNode*)); tree.string(); tree.integer(2); tree.integer(1); tree.floats(16);
            const auto children = tree.count(73,65535);
            if (children) pending.push_back(children);
        }
        if (required_nodes>nodes) throw std::runtime_error("Model vertex bone does not exist in skeleton");
        const auto weights = tree.count(6); allocate(weights,sizeof(ModelWeightSet)+sizeof(ModelWeightSet*));
        for (std::uint32_t i = 0; i < weights; ++i) {
            tree.string(); const auto count = tree.count(4); allocate(count,4); tree.floats(count);
            if (count != nodes) throw std::runtime_error("Weight set does not match model skeleton");
        }
        auto children = chunk("ChildModels"); const auto child_count = children.count(4,16);
        if (!child_count) throw std::runtime_error("Missing model self binding");
        allocate(std::size_t(nodes)*child_count,sizeof(CChildNodeXform));
        allocate(child_count,sizeof(Model::CModelSub1D4));
        for (std::uint32_t i = 0; i < child_count; ++i) {
            if (i) children.string();
            children.word();
            children.floats(std::size_t(nodes)*7);
        }
        auto animations = chunk("Animation"); const auto animation_count = animations.count(18); allocate(animation_count,sizeof(ModelAnim)+sizeof(AnimInfo));
        for (std::uint32_t i = 0; i < animation_count; ++i) {
            animations.floats(3); animations.string();
            if (version >= 11) animations.word();
            if (version >= 12) animations.word();
            const auto keys = animations.count(6); allocate(keys,sizeof(AnimKeyFrame)); allocate(std::size_t(keys)*nodes,sizeof(CModelXform)); allocate(nodes,sizeof(ModelAnimationNode)+sizeof(ModelAnimationNode*)*2);
            std::uint32_t previous = 0;
            for (std::uint32_t j = 0; j < keys; ++j) {
                auto time = animations.word(); animations.string();
                if (time < previous) throw std::runtime_error("Unsorted animation keyframes");
                previous = time;
            }
            animations.floats(std::size_t(keys)*nodes*7);
        }
        auto bindings = chunk("AnimBindings");
        for (std::uint32_t i = 0; i < child_count; ++i) {
            const auto count = bindings.count(26);
            for (std::uint32_t j = 0; j < count; ++j) { bindings.string(); bindings.floats(6); }
        }
        auto sockets = chunk("Sockets"); const auto socket_count = sockets.count(34); allocate(socket_count,sizeof(ModelSocket)+sizeof(ModelSocket*));
        for (std::uint32_t i = 0; i < socket_count; ++i) {
            if (sockets.word() >= nodes) throw std::runtime_error("Invalid socket node");
            sockets.string(); sockets.floats(7);
        }
    }
};
}
bool model_ValidateFile(std::span<const std::byte> data,std::string* reason) {
    try { Validator{}.validate(data); if (reason) reason->clear(); return true; }
    catch (const std::exception& error) { if (reason) *reason = error.what(); return false; }
}
bool model_ValidateStream(ILTStream* stream,std::string* reason) {
    if (!stream) return false;
    uint32 length = 0, position = 0;
    if (stream->GetLen(&length) != LT_OK || stream->GetPos(&position) != LT_OK || length > file_limit) return false;
    try {
        std::vector<std::byte> data(length);
        const bool read = stream->SeekTo(0) == LT_OK && stream->Read(data.data(),length) == LT_OK;
        const bool restored = stream->SeekTo(position) == LT_OK;
        return read && restored && model_ValidateFile(data,reason);
    } catch (const std::exception& error) { if (reason) *reason = error.what(); return false; }
}
