#include "engine/server/message.h"
#include "engine/server/stringbank.h"
#include "servermath.h"
#include <limits>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Packet {
    CAbstractIO* owner = nullptr;
    Packet(uint32 size = 4096) {
        msg_AllocPacket(&owner, size, 4096);
        require(owner != nullptr, "message allocation");
    }
    ~Packet() { owner->Release(); }
    ServerMessage& packet() { return *static_cast<ServerMessage*>(owner); }
    ILTMessage& message() { return packet().m_Write; }
};
void encodings() {
    CPacket raw;
    raw.WriteType(uint8(0xab)); raw.WriteType(uint32(0x12345678));
    raw.m_Pos = 1;
    require(raw.ReadType(static_cast<uint8*>(nullptr)) == 0xab && raw.ReadType(static_cast<uint32*>(nullptr)) == 0x12345678,
            "packet growth preserves prior bytes");
    Packet p;
    auto& msg = p.message();
    msg.WriteByte(0xab); msg.WriteWord(0x1234); msg.WriteDWord(0x89abcdef); msg.WriteFloat(1.5f);
    std::array<uint8, 11> expected{0xab, 0x34, 0x12, 0xef, 0xcd, 0xab, 0x89, 0, 0, 0xc0, 0x3f};
    require(p.packet().m_DataLen == expected.size() + 1, "wire scalar sizes");
    require(std::memcmp(p.packet().m_pData + 1, expected.data(), expected.size()) == 0, "Windows scalar encoding");
    msg.ResetPos();
    require(msg.ReadByte() == 0xab && msg.ReadWord() == 0x1234 && msg.ReadDWord() == 0x89abcdef && msg.ReadFloat() == 1.5f, "SDK scalar dispatch");
    uint32 flags;
    msg.ReadDWord(); msg.GetStatus(flags);
    require(flags == LMSTAT_READOVERFLOW, "unwritten bytes cannot be read");
    Packet exact(5);
    require(exact.message().WriteDWord(0x12345678) == LT_OK, "exact packet capacity");
    require(exact.message().WriteByte(1) != LT_OK, "write bound");
    exact.message().GetStatus(flags);
    require(flags == LMSTAT_WRITEOVERFLOW && exact.packet().m_DataLen == 5, "failed write preserves length");
}
void stringsAndMessages() {
    Packet p, child;
    auto& msg = p.message();
    char text[] = "Cate Archer";
    child.message().WriteDWord(0x12345678);
    msg.WriteString(text); msg.WriteStringAsHString(text); msg.WriteStringAsHString(nullptr);
    require(msg.WriteMessage(child.message()) == LT_OK, "nested message write");
    msg.ResetPos();
    char truncated[5];
    require(msg.ReadStringFL(truncated, sizeof(truncated)) == LT_OK && std::strcmp(truncated, "Cate") == 0, "truncated string drained");
    HSTRING string = msg.ReadHString();
    require(string && std::strcmp(sb_GetStringData(string), text) == 0, "string handle serialized by value");
    auto copy = sb_CopyString(string); sb_FreeString(string);
    require(std::strcmp(sb_GetStringData(copy), text) == 0, "string reference survives release");
    sb_FreeString(copy);
    require(msg.ReadHString() == nullptr, "null HSTRING sentinel");
    ILTMessage* nested = nullptr;
    require(msg.ReadMessageFL(nested) == LT_OK && nested, "SDK nested read dispatch");
    // The nested message owns its own bytes and works after the parent changes.
    msg.ResetPos(); msg.WriteDWord(0);
    require(nested->ReadDWord() == 0x12345678, "nested message independent storage");
    nested->Release();
    Packet self;
    self.message().WriteByte(42);
    require(self.message().WriteMessage(self.message()) == LT_OK, "self-contained nested snapshot");
}
class ObjectIO final : public CMsgObjectIO {
public:
    CPosRef bounds;
    HOBJECT object = reinterpret_cast<HOBJECT>(this);
    ObjectIO() {
        bounds.m_vMin.Init(0, 0, 0); bounds.m_vMax.Init(100, 200, 50);
        bounds.m_vScale.Init(0.01f, 0.005f, 0.02f);
    }
    LTRESULT ReadObject(LMessageImpl* msg, HOBJECT* result) override {
        uint16 id = 0xffff;
        const auto status = msg->ReadWordFL(id);
        *result = id == 42 ? object : nullptr;
        return status;
    }
    LTRESULT WriteObject(LMessageImpl* msg, HOBJECT value) override { return msg->WriteWord(value == object ? 42 : 0xffff); }
    LTRESULT WriteFormattedHString(LMessageImpl* msg, int, va_list*) override { char text[] = "formatted"; return msg->WriteStringAsHString(text); }
    void GetPosRef(CPosRef** result) override { *result = &bounds; }
};
void objectContext() {
    Packet p;
    ObjectIO io;
    p.packet().m_Write.m_pObjectIO = &io;
    auto& msg = p.message();
    LTVector position(50, 100, 25);
    require(msg.WriteCompPos(position) == LT_OK && msg.WriteObject(io.object) == LT_OK, "object and world context dispatch");
    const std::array<uint8, 8> expected{0, 0x80, 0, 0x80, 0, 0x80, 42, 0};
    require(std::memcmp(p.packet().m_pData + 1, expected.data(), expected.size()) == 0, "Windows position and object-id encoding");
    msg.WriteHStringFormatted(1);
    msg.ResetPos();
    const auto decoded = msg.ReadCompPos();
    require((decoded - position).MagSqr() < 0.001f && msg.ReadObject() == io.object, "native object references stay out of wire bytes");
    HSTRING text = msg.ReadHString();
    require(text && std::strcmp(sb_GetStringData(text), "formatted") == 0, "formatted HSTRING delegates to owning context");
    sb_FreeString(text);
}
void codecs() {
    Packet p;
    auto& msg = p.message();
    LTVector vector(4, -2, 1), zero(0, 0, 0);
    msg.WriteCompVector(vector); msg.WriteCompVector(zero);
    const std::array<uint8, 9> expected{0, 0, 0x80, 0x40, 0xff, 0xff, 0xff, 0xff, 0x28};
    require(std::memcmp(p.packet().m_pData + 1, expected.data(), expected.size()) == 0, "Windows compressed vector encoding");
    LTRotation rotation;
    rotation.Init();
    msg.WriteCompRotation(rotation);
    msg.ResetPos();
    LTVector decoded = msg.ReadCompVector();
    require(std::fabs(decoded.x - 4) < 0.0001f && std::fabs(decoded.y + 2) < 0.0001f && std::fabs(decoded.z - 1) < 0.0001f, "vector compression roundtrip");
    decoded = msg.ReadCompVector();
    require(decoded.x == 0 && decoded.y == 0 && decoded.z == 0, "zero vector compression");
    auto rot = msg.ReadCompRotation();
    require(std::isfinite(rot.m_Quat[3]) && std::fabs(rot.m_Quat[3]) > 0.99f, "rotation compression roundtrip");
}
void rotationMatrix() {
    ILTMath math;
    for (unsigned i = 0; i < 200; ++i) {
        Packet p;
        LTRotation original;
        math.SetupEuler(original, float(i % 17) * 0.37f, float(i % 19) * 0.31f, float(i % 13) * 0.47f);
        require(p.message().WriteCompRotation(original) == LT_OK, "rotation matrix encode");
        p.message().ResetPos();
        LTRotation decoded;
        require(p.message().ReadCompRotationFL(decoded) == LT_OK, "rotation matrix decode");
        float dot = 0;
        for (unsigned j = 0; j < 4; ++j) dot += original.m_Quat[j] * decoded.m_Quat[j];
        require(std::fabs(dot) > 0.99f, "compressed rotations preserve orientation");
    }
}
void malformed() {
    Packet p;
    auto& msg = p.message();
    msg.WriteByte('x'); msg.ResetPos(); p.packet().m_Flags |= 1;
    char text[8];
    require(msg.ReadStringFL(text, sizeof(text)) != LT_OK && text[0] == 0, "unterminated string fails without cycling");
    Packet shortRead;
    shortRead.message().WriteByte(42); shortRead.message().ResetPos();
    std::array<uint8, 16> output; output.fill(0xff);
    require(shortRead.message().ReadRawFL(output.data(), output.size()) != LT_OK, "truncated raw message");
    for (auto byte : output) require(byte == 0, "failed raw read clears output");
    Packet badNested;
    badNested.message().WriteWord(1); badNested.message().ResetPos(); badNested.packet().m_Flags |= 1;
    ILTMessage* result = nullptr;
    require(badNested.message().ReadMessageFL(result) != LT_OK && !result, "truncated nested message with automatic rewind");
    Packet badVector;
    badVector.message().WriteFloat(1); badVector.message().ResetPos(); badVector.packet().m_Flags |= 1;
    LTVector v;
    require(badVector.message().ReadVectorFL(v) != LT_OK, "truncated vector cannot wrap into itself");
}
void malformedCodecs() {
    Packet invalid;
    LTVector vector(std::numeric_limits<float>::quiet_NaN(), 0, 0);
    require(invalid.message().WriteCompVector(vector) != LT_OK, "NaN vector cannot reach integer codec");
    std::uint32_t random = 0x12345678;
    for (unsigned i = 0; i < 1000; ++i) {
        Packet p;
        std::array<uint8, 16> bytes;
        for (auto& byte : bytes) { random = random * 1664525u + 1013904223u; byte = uint8(random >> 24); }
        p.message().WriteRaw(bytes.data(), i % bytes.size());
        p.message().ResetPos(); p.packet().m_Flags |= 1;
        LTRotation rotation;
        p.message().ReadCompRotationFL(rotation);
        p.message().ResetPos();
        p.message().ReadCompVectorFL(vector);
        p.message().ResetPos();
        char text[8]; p.message().ReadStringFL(text, sizeof(text));
        p.message().ResetPos();
        ILTMessage* nested = nullptr;
        p.message().ReadMessageFL(nested);
        if (nested) nested->Release();
    }
}
void references() {
    Packet p;
    CMsgCursor cursor;
    cursor.Init(&p.packet()); cursor.Init(&p.packet());
    require(p.packet().m_nRefCount == 2, "cursor self-assignment");
    CMessageRef ref;
    ref.Init(&p.owner); ref.Init(&p.owner);
    require(p.packet().m_nRefCount == 3, "message reference self-assignment");
    p.message().WriteByte(12); p.message().WriteWord(345);
    CMessageRef effect;
    require(effect.CopyFrom(p.packet(), 1) == LT_OK && effect.m_pMsg->m_DataLen == 3 &&
            effect.m_pMsg->m_pData[0] == 12 && effect.m_pMsg->m_pData[1] == 0x59 && effect.m_pMsg->m_pData[2] == 1,
            "late-join effect retains payload without packet ID");
    p.packet().m_pData[1] = 99;
    require(effect.m_pMsg->m_pData[0] == 12, "retained effect owns independent bytes");
    auto* previous = effect.m_pMsg;
    require(effect.CopyFrom(p.packet(), 999) == LT_INVALIDPARAMS && effect.m_pMsg == previous, "invalid effect preserves prior reference");
    require(effect.CopyFrom(*effect.m_pMsg) == LT_OK && effect.m_pMsg->m_pData[0] == 12, "message reference can copy itself");
}
}
int main() {
    try {
        static_assert(!std::is_abstract_v<LMessageImpl>);
        encodings(); stringsAndMessages(); codecs(); objectContext(); rotationMatrix(); malformed(); malformedCodecs(); references();
        std::puts("Native SDK messages, Windows wire encodings, and malformed-input checks passed");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
