#include "saved_game.h"
#include "wire.h"
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace nolf {
namespace {
constexpr std::size_t file_limit = 256u*1024*1024, memory_limit = 512u*1024*1024;
[[noreturn]] void invalid(const char* text) { throw std::runtime_error(std::string("invalid saved game: ") + text); }
class Reader {
public:
    explicit Reader(std::span<const std::byte> input) : data(input) { if (input.size() > file_limit) invalid("file budget"); }
    std::span<const std::byte> bytes(std::size_t count) {
        if (count > data.size() - pos) invalid("truncated record");
        auto result = data.subspan(pos,count); pos += count; return result;
    }
    template<class T> T scalar() { return wire::load<T>(bytes(sizeof(T)).data()); }
    template<class T> void count(std::size_t count, std::size_t minimum) {
        if (count > (data.size() - pos) / minimum || count > (memory_limit - memory) / sizeof(T)) invalid("record budget");
        memory += count*sizeof(T);
    }
    std::string string() {
        auto source = bytes(scalar<std::uint16_t>());
        const std::string text(reinterpret_cast<const char*>(source.data()),source.size());
        if (text.find('\0') != text.npos) invalid("embedded string terminator");
        return text;
    }
    std::vector<std::byte> blob(std::size_t count) { auto source = bytes(count); return {source.begin(),source.end()}; }
    bool flag() { const auto value = scalar<std::uint8_t>(); if (value > 1) invalid("boolean field"); return value != 0; }
    std::size_t pos = 0;
private:
    std::span<const std::byte> data;
    std::size_t memory = 0;
};
class Writer {
public:
    void bytes(std::span<const std::byte> source) {
        if (source.size() > file_limit - data.size()) invalid("file budget");
        data.insert(data.end(),source.begin(),source.end());
    }
    template<class T> void scalar(T value) {
        std::array<std::byte,sizeof(T)> buffer; wire::store(buffer.data(),value); bytes(buffer);
    }
    void string(const std::string& text) {
        if (text.size() > 0xffff || text.find('\0') != text.npos) invalid("string extent");
        scalar(static_cast<std::uint16_t>(text.size())); bytes(std::as_bytes(std::span(text)));
    }
    std::uint32_t offset(std::uint32_t origin) const {
        if (data.size() >= std::numeric_limits<std::uint32_t>::max() - origin) invalid("absolute stream offset");
        return static_cast<std::uint32_t>(data.size() + origin);
    }
    std::vector<std::byte> data;
};
template<std::size_t N> void read_floats(Reader& input,std::array<float,N>& values) { for (auto& value : values) value = input.scalar<float>(); }
template<std::size_t N> void write_floats(Writer& output,const std::array<float,N>& values) { for (auto value : values) output.scalar(value); }
template<std::size_t N> void finite(const std::array<float,N>& values,const char* field="state",const SavedObject* object=nullptr) {
    for (auto value : values) if (!std::isfinite(value))
        throw std::runtime_error(std::string("invalid saved game: nonfinite ")+field+(object ? " of "+object->class_name+" "+object->name : std::string()));
}
void valid_object(const SavedObject& object) {
    if (object.type > 10 || object.class_name.empty()) invalid("object type/class");
    finite(object.position,"position",&object); finite(object.scale,"scale",&object); finite(object.rotation,"rotation",&object);
    finite(object.velocity,"velocity",&object); finite(object.acceleration,"acceleration",&object); finite(object.dimensions,"dimensions",&object);
    finite(std::array{object.light_radius,object.next_update,object.friction,object.mass,object.force_ignore_limit_squared},"light radius/update/friction/mass/force limit",&object);
    if (bool(object.sky_slot) != bool(object.change_flags & 0x100) || (object.sky_slot && *object.sky_slot >= 30)) invalid("sky slot");
    if (bool(object.animation) != (object.type == 1)) invalid("animation object type");
    if ((object.type != 1 && !object.skins[1].empty()) ||
        (object.type != 4 && object.light_radius != 0) ||
        (object.type != 9 && object.container_code != 0xffff)) invalid("state for another object type");
    if (object.sfx_message && object.sfx_message->size() > 0xffff) invalid("SFX message size");
    if (object.attachments.size() > 0xffff || object.relations.size() > 0xffff) invalid("object link count");
}
void valid_links(const SavedGame& game) {
    for (std::size_t i = 0; i < game.objects.size(); ++i) {
        const auto& object = game.objects[i]; valid_object(object);
        for (const auto& link : object.attachments) {
            finite(link.offset); finite(link.rotation);
            if (link.child >= game.objects.size() || link.child == i || link.node < -1) invalid("attachment reference");
        }
        for (const auto& link : object.relations)
            if (link.other >= game.objects.size() || link.other == i || link.type > 1) invalid("relation reference/type");
    }
    // Attachment creation rejects cycles too. Detect them before the restore
    // creates any actors or calls any game-owned load handlers.
    std::vector<std::uint8_t> colors(game.objects.size());
    struct Frame { std::size_t object, next = 0; };
    for (std::size_t start = 0; start < game.objects.size(); ++start) {
        if (colors[start]) continue;
        std::vector<Frame> pending{{start}}; colors[start] = 1;
        while (!pending.empty()) {
            auto& frame = pending.back(); const auto& links = game.objects[frame.object].attachments;
            if (frame.next == links.size()) { colors[frame.object] = 2; pending.pop_back(); continue; }
            const auto child = links[frame.next++].child;
            if (colors[child] == 1) invalid("attachment cycle");
            if (!colors[child]) { colors[child] = 1; pending.push_back({child}); }
        }
    }
}
void valid_timing(const SavedGame& game) {
    for (auto index : {1,2,4,5,6}) if (!std::isfinite(std::bit_cast<float>(game.timing_words[index]))) invalid("nonfinite timing");
}
}
SavedGame DecodeSavedGame(std::span<const std::byte> bytes,std::uint32_t origin) {
    Reader input(bytes); SavedGame game;
    if (bytes.size() >= std::numeric_limits<std::uint32_t>::max() - origin) invalid("absolute stream extent");
    if (input.scalar<std::uint32_t>() != saved_game_version) invalid("unsupported version");
    const auto count = input.scalar<std::uint32_t>();
    if (count > 0xffff) invalid("object count");
    input.count<SavedObject>(count,8);
    for (auto& value : game.timing_words) value = input.scalar<std::uint32_t>();
    valid_timing(game);
    const auto worlds = input.scalar<std::uint32_t>();
    if (worlds != 0xffffffff) {
        if (worlds > 0xffff) invalid("world model count");
        input.count<std::vector<std::uint16_t>>(worlds,4);
        game.portal_flags.emplace(worlds);
        for (auto& flags : *game.portal_flags) {
            const auto portals = input.scalar<std::uint32_t>(); input.count<std::uint16_t>(portals,2);
            flags.resize(portals); for (auto& flag : flags) flag = input.scalar<std::uint16_t>();
        }
    }
    while (input.flag()) {
        if (game.console_variables.size() >= 65535) invalid("console variable count");
        auto name = input.string(), value = input.string();
        game.console_variables.emplace_back(std::move(name),std::move(value));
    }
    game.objects.resize(count);
    for (auto& object : game.objects) {
        const auto end = input.scalar<std::uint32_t>(), start = input.scalar<std::uint32_t>();
        if (start < origin || end < start || end - origin > bytes.size() || end - start < 4) invalid("object data offsets");
        object.class_name = input.string(); object.type = input.scalar<std::uint8_t>();
        object.flags = input.scalar<std::uint32_t>(); object.flags2 = input.scalar<std::uint32_t>();
        read_floats(input,object.position); read_floats(input,object.scale); read_floats(input,object.rotation);
        object.user_flags = input.scalar<std::uint32_t>();
        object.name = input.string(); object.filename = input.string();
        object.skins[0] = input.string(); if (object.type == 1) object.skins[1] = input.string();
        if (object.type == 4) object.light_radius = input.scalar<float>();
        if (object.type == 9) object.container_code = input.scalar<std::uint16_t>();
        object.next_update = input.scalar<float>(); object.blocking_priority = input.scalar<std::uint8_t>();
        if (input.flag()) object.sfx_message = input.blob(input.scalar<std::uint16_t>());
        for (auto& color : object.color) color = input.scalar<std::uint8_t>();
        read_floats(input,object.velocity); read_floats(input,object.acceleration);
        object.friction = input.scalar<float>(); object.mass = input.scalar<float>(); object.force_ignore_limit_squared = input.scalar<float>();
        read_floats(input,object.dimensions); object.change_flags = input.scalar<std::uint32_t>();
        if (object.change_flags & 0x100) object.sky_slot = input.scalar<std::uint16_t>();
        if (object.type == 1) object.animation = SavedAnimation{input.scalar<std::uint32_t>(),input.scalar<std::uint32_t>(),input.scalar<std::uint16_t>()};
        if (input.flag()) { const auto flags = input.scalar<std::uint32_t>(); object.client = SavedClient{flags,input.string()}; }
        valid_object(object);
        if (input.pos != start - origin) invalid("object state/data boundary");
        object.game_data = input.blob(end - start - 4);
        if (input.scalar<std::uint32_t>() != saved_game_sentinel) invalid("object sentinel");
    }
    if (input.scalar<std::uint32_t>() != 0xffffffff) invalid("object run terminator/count");
    for (auto& object : game.objects) {
        const auto attachments = input.scalar<std::uint16_t>(); input.count<SavedAttachment>(attachments,34);
        object.attachments.resize(attachments);
        for (auto& link : object.attachments) {
            read_floats(input,link.offset); read_floats(input,link.rotation);
            link.child = input.scalar<std::uint16_t>(); link.node = input.scalar<std::int32_t>();
        }
        const auto relations = input.scalar<std::uint16_t>(); input.count<SavedRelation>(relations,3);
        object.relations.resize(relations);
        for (auto& link : object.relations) { link.type = input.scalar<std::uint8_t>(); link.other = input.scalar<std::uint16_t>(); }
    }
    if (input.flag() || input.pos != bytes.size()) invalid("file terminator/extent");
    valid_links(game); return game;
}
std::vector<std::byte> EncodeSavedGame(const SavedGame& game,std::uint32_t origin) {
    if (game.objects.size() > 0xffff || game.console_variables.size() > 0xffff) invalid("table count");
    valid_timing(game); valid_links(game);
    Writer output;
    output.scalar(saved_game_version); output.scalar(static_cast<std::uint32_t>(game.objects.size()));
    for (auto value : game.timing_words) output.scalar(value);
    if (game.portal_flags) {
        if (game.portal_flags->size() > 0xffff) invalid("world model count");
        output.scalar(static_cast<std::uint32_t>(game.portal_flags->size()));
        for (const auto& flags : *game.portal_flags) {
            if (flags.size() > file_limit / 2) invalid("portal count");
            output.scalar(static_cast<std::uint32_t>(flags.size())); for (auto flag : flags) output.scalar(flag);
        }
    } else output.scalar(std::uint32_t{0xffffffff});
    for (const auto& [name,value] : game.console_variables) { output.scalar(std::uint8_t{1}); output.string(name); output.string(value); }
    output.scalar(std::uint8_t{0});
    for (const auto& object : game.objects) {
        const auto offset = output.data.size(); output.scalar(std::uint32_t{0}); output.scalar(std::uint32_t{0});
        output.string(object.class_name); output.scalar(object.type); output.scalar(object.flags); output.scalar(object.flags2);
        write_floats(output,object.position); write_floats(output,object.scale); write_floats(output,object.rotation);
        output.scalar(object.user_flags); output.string(object.name); output.string(object.filename);
        output.string(object.skins[0]); if (object.type == 1) output.string(object.skins[1]);
        if (object.type == 4) output.scalar(object.light_radius);
        if (object.type == 9) output.scalar(object.container_code);
        output.scalar(object.next_update); output.scalar(object.blocking_priority);
        output.scalar(static_cast<std::uint8_t>(bool(object.sfx_message)));
        if (object.sfx_message) { output.scalar(static_cast<std::uint16_t>(object.sfx_message->size())); output.bytes(*object.sfx_message); }
        for (auto color : object.color) output.scalar(color);
        write_floats(output,object.velocity); write_floats(output,object.acceleration);
        output.scalar(object.friction); output.scalar(object.mass); output.scalar(object.force_ignore_limit_squared);
        write_floats(output,object.dimensions); output.scalar(object.change_flags);
        if (object.sky_slot) output.scalar(*object.sky_slot);
        if (object.animation) { output.scalar(object.animation->index); output.scalar(object.animation->time); output.scalar(object.animation->flags); }
        output.scalar(static_cast<std::uint8_t>(bool(object.client)));
        if (object.client) { output.scalar(object.client->flags); output.string(object.client->name); }
        const auto start = output.offset(origin);
        output.bytes(object.game_data); output.scalar(saved_game_sentinel);
        wire::store(output.data.data() + offset,output.offset(origin));
        wire::store(output.data.data() + offset + 4,start);
    }
    output.scalar(std::uint32_t{0xffffffff});
    for (const auto& object : game.objects) {
        output.scalar(static_cast<std::uint16_t>(object.attachments.size()));
        for (const auto& link : object.attachments) {
            write_floats(output,link.offset); write_floats(output,link.rotation); output.scalar(link.child); output.scalar(link.node);
        }
        output.scalar(static_cast<std::uint16_t>(object.relations.size()));
        for (const auto& link : object.relations) { output.scalar(link.type); output.scalar(link.other); }
    }
    output.scalar(std::uint8_t{0}); output.offset(origin); return std::move(output.data);
}
}
