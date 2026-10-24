#include "platform/filesystem.h"
#include "platform/module.h"
#include "platform/sockets.h"
#include "platform/sync.h"
#include "platform/system.h"
#include "platform/settings.h"
#include "platform/timing.h"
#include "platform/termination.h"
#include "engine/portable/binary.h"
#include "engine/portable/text.h"
#include "engine/portable/rez_archive.h"
#include "engine/portable/memory_stream.h"
#include "ltcodes.h"
#include "servermath.h"
#include "lthread.h"
#include "engine/portable/resources.h"
#include "engine/portable/message_format.h"
#include "collection.h"
#include <array>
#include <atomic>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;
using namespace std::chrono_literals;
static void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class F> static void rejects(F&& operation) {
    bool rejected = false;
    try { operation(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "expected operation to fail");
}

static std::string format(const char* text, ...) {
    va_list arguments;
    va_start(arguments, text);
    try {
        auto result = nolf::format_message(text, &arguments);
        va_end(arguments);
        return result;
    } catch (...) { va_end(arguments); throw; }
}
static std::string resource_format(const nolf::ResourceModule& module, uint32 id, ...) {
    va_list arguments;
    va_start(arguments, id);
    try {
        auto result = nolf::format_resource(module, id, &arguments);
        va_end(arguments);
        return result;
    } catch (...) { va_end(arguments); throw; }
}
struct ThrowingElement {
    static inline int live = 0;
    static inline int attempts = 0;
    ThrowingElement() { if (++attempts == 3) throw std::runtime_error("construction failure"); ++live; }
    ~ThrowingElement() { --live; }
};
static void container_checks() {
    CMoArray<std::string> source;
    source.Append("Cate"); source.Append("Archer");
    CMoArray<std::string> copy(source);
    copy[1] = "Agent";
    require(source[1] == "Archer", "array copy owns distinct elements");
    source.Remove(BAD_INDEX); source.Remove(99);
    require(source.GetSize() == 2 && source[1] == "Archer", "out-of-range removal cannot underflow a native array");
    copy = copy;
    require(copy.GetSize() == 2 && copy[0] == "Cate", "array self-assignment");
    require(copy.Fast_NiceSetSize(10) && copy[1] == "Agent", "nontrivial elements survive array growth");
    CMoArray<std::string> moved(std::move(copy));
    require(moved[1] == "Agent" && copy.GetSize() == 0, "array move transfers ownership");
    std::array<int, 4> values{1, 2, 3, 4};
    int needle = 3;
    require(MoArray_FindElementMemcmp(&needle, values.data(), values.size(), sizeof(needle)) == 2,
            "array search uses element storage");
    CMoArray<void*> pointers;
    pointers.Append(&source); pointers.Append(&copy);
    require(pointers.FindElement(&copy) == 1, "native pointer array search");
    rejects([&] { BaseNew(&g_DefAlloc, static_cast<ThrowingElement*>(nullptr), 4); });
    require(ThrowingElement::live == 0, "failed array construction releases completed elements");
}

int main(int argc, char** argv) {
    try {
        {
            const std::string unicode = "A\xc3\xa9\xe2\x82\xac\xf0\x90\x90\xb7Z";
            const char* cursor = unicode.c_str();
            for (std::size_t offset : {1u,3u,6u,10u,11u}) {
                cursor = nolf::text::next(cursor);
                require(cursor == unicode.c_str()+offset,"UTF-8 traversal preserves character boundaries");
            }
            require(nolf::text::next(cursor) == cursor && nolf::text::advance(unicode.c_str(),100) == cursor,
                "UTF-8 traversal stops at the terminator");
            for (std::string invalid : {std::string("\xc0\x80"),std::string("\xed\xa0\x80"),std::string("\xf4\x90\x80\x80"),std::string("\xe2\x82")})
                require(nolf::text::next(invalid.c_str()) == invalid.c_str()+1,"invalid UTF-8 advances safely one byte");
            for (std::size_t size=0; size<unicode.size(); ++size) {
                const auto prefix = unicode.substr(0,size);
                require(nolf::text::advance(prefix.c_str(),100) == prefix.c_str()+size,"truncated UTF-8 cannot advance past its owned buffer");
            }
        }
        {
            nolf::platform::TerminationRequest termination;
            require(!termination.requested(), "termination starts without a stop request");
            rejects([] { nolf::platform::TerminationRequest duplicate; });
        }
        {
            nolf::platform::TerminationRequest restarted;
            require(!restarted.requested(), "termination handler can be restored and installed again");
        }
        if (argc == 3 && std::string_view(argv[1]) == "--resources") {
            nolf::Resources resources(argv[2]);
            nolf::ResourceModule module{&resources, "cres", 1033};
            require(resource_format(module, 238, 7) == "Scene 7", "real campaign scene resource");
            require(resource_format(module, 501, 1920, 1080, 32) == "1920x1080x32", "real video mode resource");
            require(resource_format(module, 500, "Cate", "Archer") == "Cate - Archer", "real mixed pointer resource");
            std::cout << "native game resource formatting passed\n";
            return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "--module") {
            nolf::platform::Module module(argv[2]);
            require(module.setup().version == 1, "module setup version");
            rejects([&] { module.symbol("missing_export"); });
            rejects([&] { module.setup(999); });
            return 0;
        }
        container_checks();
        require(format("%2!s!: %1!04d!, %2!s!", 7, "Agent") == "Agent: 0007, Agent", "numbered resource inserts preserve native argument types");
        require(format("%1!s! scored %2!d!%%", "Cate", 100) == "Cate scored 100%", "resource percent escape");
        require(format("a%n%tb%0ignored") == "a\r\n\tb", "resource controls and terminator");
        rejects([&] { format("%1!n!", 0); });
        rejects([&] { format("%2!s!", "unknown", "skipped"); });
        rejects([&] { format("%1!s! %1!d!", "conflict"); });
        rejects([&] { format("%1!9999999d!", 1); });
        using namespace nolf::platform;
        require(asset_key("Worlds\\M01S01.DAT") == "worlds/m01s01.dat", "legacy path normalization");
        for (const char* name : {"../escape", "/absolute", "C:\\game", "a/../../b", "", "."}) rejects([&] { asset_key(name); });
        const auto root = fs::temp_directory_path() / ("nolf-tests-" + std::to_string(monotonic_nanoseconds()));
        fs::create_directory(root);
        struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
        Settings preferences(root / "config/preferences.cfg");
        require(preferences.get("language", "English") == "English" && !fs::exists(root / "config"), "missing preferences are read-only");
        preferences.set("language", "German");
        preferences.set("quoted key", "UTF-8: Привет, \"Cate\"\nnext line\\path");
        preferences.save();
        Settings reloaded(root / "config/preferences.cfg");
        require(reloaded.get("language") == "German" && reloaded.get("quoted key") == preferences.get("quoted key"), "native preferences persist escaped Unicode values");
        const auto valid_preferences = read_file(root / "config/preferences.cfg");
        rejects([&] { preferences.set("", "invalid"); });
        require(read_file(root / "config/preferences.cfg") == valid_preferences, "invalid preference leaves file intact");
        const std::string duplicate = "NOLF SETTINGS 1\n\"key\" \"first\"\n\"key\" \"second\"\n";
        atomic_write(root / "duplicate.cfg", std::as_bytes(std::span(duplicate.data(), duplicate.size())));
        rejects([&] { Settings invalid(root / "duplicate.cfg"); });
        const auto start = Clock::time_point{} + 1s;
        FrameLimiter cadence(10);
        require(cadence.deadline(start) == start && cadence.deadline(start + 10ms) == start + 100ms, "monotonic frame schedule");
        require(cadence.deadline(start + 100ms) == start + 200ms && cadence.deadline(start + 1s) == start + 1s, "late frame resets backlog");
        cadence.reset(); require(cadence.deadline(start + 2s) == start + 2s, "frame limiter reset");
        rejects([] { FrameLimiter invalid(0); });
        const auto log_path = root / "diagnostics/modern.log";
        set_log_file(log_path); log("test", "first line"); set_log_file(log_path); log("test", "second line"); set_log_file({});
        const auto log_bytes = read_file(log_path);
        const std::string log_text(reinterpret_cast<const char*>(log_bytes.data()), log_bytes.size());
        require(log_text == "[test] first line\n[test] second line\n", "log destination appends and flushes complete lines");
        std::array payload{std::byte{1}, std::byte{2}, std::byte{3}};
        atomic_write(root / "Maps/Example.DAT", payload);
        DirectoryIndex index(root);
        require(read_file(index.resolve("maps\\EXAMPLE.dat")) == std::vector(payload.begin(), payload.end()), "indexed file contents");
        require(index.resolve("missing").empty(), "missing asset lookup");
        atomic_write(root / "Maps/Example.DAT", std::span(payload).first(1));
        require(read_file(root / "Maps/Example.DAT").size() == 1, "atomic replacement truncates");
        atomic_write(root / "Maps/example.dat", payload);
        // Case collisions only exist on case-sensitive host filesystems.
        if (fs::directory_iterator(root / "Maps") != fs::directory_iterator{}) {
            std::size_t count = 0;
            for (const auto& entry : fs::directory_iterator(root / "Maps")) { (void)entry; ++count; }
            if (count == 2) rejects([&] { DirectoryIndex collision(root); });
        }

        Event event;
        require(!event.wait_for(1ms), "event starts reset");
        std::atomic<int> ready{};
        std::jthread first([&] { if (event.wait()) ++ready; });
        std::jthread second([&] { if (event.wait()) ++ready; });
        event.set(); first.join(); second.join();
        require(ready == 2 && event.wait_for(0ms), "manual-reset event wakes all and stays signaled");
        event.reset();
        std::jthread cancelled([&](std::stop_token stop) { require(!event.wait(stop), "cancel event wait"); });
        cancelled.request_stop(); cancelled.join();
        RecursiveMutex mutex; { std::lock_guard a(mutex); std::lock_guard b(mutex); }

        LThreadQueue queue;
        LThreadMessage message;
        int pointer_payload = 7;
        message.m_Data[0] = reinterpret_cast<uintptr_t>(&pointer_payload);
        require(queue.PostMessage(&message) == LT_OK, "engine queue post");
        LThreadMessage received;
        require(queue.PeekMessage(&received) == LT_OK && reinterpret_cast<int*>(received.m_Data[0]) == &pointer_payload,
                "engine queue preserves 64-bit payloads");
        require(queue.GetMessage(&received, LTFALSE) == LT_OK && queue.GetMessage(&received, LTFALSE) == LT_NOTFOUND,
                "engine queue dequeue and empty state");
        class Worker final : public LThread {
        public:
            ~Worker() override { Term(LTTRUE); }
            Event completed;
            std::atomic<int> count{};
            void ProcessMessage(LThreadMessage*) override { if (++count == 1000) completed.set(); }
        } worker;
        require(worker.Start(3) == LT_OK, "engine worker start");
        for (int i = 0; i < 1000; ++i) require(worker.m_IncomingQueue.PostMessage(&message) == LT_OK, "engine worker enqueue");
        require(worker.completed.wait_for(2000ms), "engine worker drains its queue");
        require(worker.Term(LTTRUE) == LT_OK && worker.count == 1000, "engine worker cooperative shutdown");
        require(worker.Start(3) == LT_OK && worker.Term(LTTRUE) == LT_OK, "engine worker restart and immediate shutdown");
        GenListPos pointer_position(&pointer_payload);
        BaseGenListPos copied_position;
        copied_position = pointer_position;
        require(copied_position.m_Pos == &pointer_payload, "engine iterator copy preserves pointer width");

        UdpSocket receiver, sender;
        sender.send({"127.0.0.1", receiver.port()}, payload);
        auto packet = receiver.receive(1000ms);
        require(packet && packet->bytes == std::vector(payload.begin(), payload.end()) && packet->peer.port == sender.port(), "UDP loopback and source port");
        sender.send({"127.0.0.1", receiver.port()}, {});
        packet = receiver.receive(1000ms);
        require(packet && packet->bytes.empty(), "zero-length UDP packet");
        require(!receiver.receive(1ms), "UDP timeout");

        nolf::BinaryWriter writer;
        writer.integer<std::uint32_t>(0x78563412);
        writer.real(1.5f);
        require(writer.data()[0] == std::byte{0x12}, "wire byte order");
        nolf::BinaryReader reader(writer.data());
        require(reader.integer<std::uint32_t>() == 0x78563412 && reader.real() == 1.5f, "wire roundtrip");
        rejects([&] { reader.integer<std::uint8_t>(); });

        static_assert(sizeof(uint32) == 4 && sizeof(uint64) == 8);
        nolf::MemoryStream source(std::vector(payload.begin(), payload.end()));
        require(source.SeekTo(1) == LT_OK, "SDK stream seek");
        nolf::MemoryStream output({}, true);
        require(output.WriteStream(source, WRITESTREAM_DEFAULT, WRITESTREAM_DEFAULT) == LT_OK && output.data().size() == 2,
                "default stream copy starts at current position");
        require(output.WriteStream(source, 0, 2) == LT_OK && output.data().size() == 4, "explicit SDK stream range");
        require(output.WriteStream(source, 0, 999) != LT_OK, "SDK range bounds");
        nolf::MemoryStream aliased(std::vector(payload.begin(), payload.end()), true);
        aliased.SeekTo(3);
        require(aliased.Write(aliased.data().data(), 3) == LT_OK && aliased.data().size() == 6 && aliased.data()[5] == payload[2],
                "stream self-copy survives storage growth");
        nolf::MemoryStream strings({}, true);
        char greeting[] = "hello";
        char empty[] = "";
        require(strings.WriteString(greeting) == LT_OK && strings.WriteString(empty) == LT_OK, "SDK string write");
        strings.SeekTo(0);
        char short_buffer[3]{};
        require(strings.ReadString(short_buffer, 3) == LT_OK && std::string(short_buffer) == "he", "SDK truncated destination");
        require(strings.GetPos() == 7, "SDK string drains uncopied bytes");
        require(strings.ReadString(nullptr, 0) == LT_OK, "SDK zero-size empty string destination");
        std::array<std::byte, 10> overflow{};
        overflow.fill(std::byte{255});
        require(source.Read(overflow.data(), 10) != LT_OK && overflow[0] == std::byte{} && overflow[9] == std::byte{},
                "SDK failed read zeros complete destination");
        require(source.ErrorStatus() != LT_OK, "SDK errors remain sticky");

        ILTMath math;
        LTRotation rotation;
        require(math.SetupEuler(rotation, 0, 0, 0) == LT_OK && rotation.IsIdentity(), "engine identity rotation");
        LTMatrix transform;
        LTVector translation(1, 2, 3);
        require(math.SetupTransformationMatrix(transform, translation, rotation) == LT_OK, "engine transform construction");
        require(transform.m[0][3] == 1 && transform.m[1][3] == 2 && transform.m[2][3] == 3 && transform.m[3][3] == 1,
                "engine translation matrix");
        LTVector right, up, forward;
        require(math.GetRotationVectors(rotation, right, up, forward) == LT_OK, "engine orientation vectors");
        require(right == LTVector(1, 0, 0) && up == LTVector(0, 1, 0) && forward == LTVector(0, 0, 1), "identity orientation basis");
        LTRotation halfway;
        math.InterpolateRotation(halfway, rotation, rotation, 0.5f);
        require(halfway.IsIdentity(), "engine quaternion interpolation");

        const std::string label = "No One Lives Forever";
        atomic_write(root / "resources/cres/1033/strings/1.txt", std::as_bytes(std::span(label)));
        nolf::Resources resources(root);
        require(resources.string("CRES", 1, 1049) == label, "portable resource language fallback");
        rejects([&] { resources.string("../cres", 1); });
        const auto now = monotonic_nanoseconds();
        require(monotonic_nanoseconds() >= now, "monotonic time");
        std::cout << "native platform checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
