#include "message.h"
#include "stringbank.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace {
// Rewind once per compound value. A short value must fail at the packet
// boundary instead of reading its remaining components from the beginning.
class CompoundRead {
public:
    explicit CompoundRead(LMessageImpl& message) : msg(message), repeat(message.m_pOwner ? message.m_pOwner->m_Flags & 1 : 0) {
        if (msg.m_pOwner) msg.m_pOwner->m_Flags &= ~uint32(1);
    }
    ~CompoundRead() {
        if (!msg.m_pOwner) return;
        msg.m_pOwner->m_Flags |= repeat;
        if (!(msg.m_pOwner->m_Flags & PACKETFLAG_READOVERFLOW)) lmsg_AfterRead(&msg);
    }
private:
    LMessageImpl& msg;
    uint32 repeat;
};
bool finite(const LTVector& vector) {
    return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
}
LTRESULT invalidWrite(LMessageImpl& msg) {
    return msg.FlagError(LMSTAT_WRITEOVERFLOW,LT_INVALIDPARAMS);
}
template<class T> LTRESULT readScalar(LMessageImpl& msg, T& value) {
    std::array<uint8, sizeof(T)> bytes{};
    const auto status = msg.ReadRawFL(bytes.data(), bytes.size());
    value = nolf::wire::load<T>(bytes.data());
    return status;
}
template<class T> LTRESULT writeScalar(LMessageImpl& msg, T value) {
    std::array<uint8, sizeof(T)> bytes;
    nolf::wire::store(bytes.data(), value);
    return msg.WriteRaw(bytes.data(), bytes.size());
}
}
LTRESULT LMessageImpl::Release() {
    if (!m_pOwner) return LT_NOTINITIALIZED;
    m_pOwner->Release();
    return LT_OK;
}
LTRESULT LMessageImpl::ReadRawFL(void* data, uint32 size) {
    if (!m_pOwner) return LT_NOTINITIALIZED;
    if (!m_pOwner->ReadData(data, size)) return LT_ERROR;
    lmsg_AfterRead(this);
    return LT_OK;
}
LTRESULT LMessageImpl::WriteRaw(void* data, uint32 size) {
    return m_pOwner ? (m_pOwner->WriteData(data, size) ? LT_OK : LT_ERROR) : LT_NOTINITIALIZED;
}
LTRESULT LMessageImpl::ReadByteFL(uint8& v) { return readScalar(*this, v); }
LTRESULT LMessageImpl::ReadWordFL(uint16& v) { return readScalar(*this, v); }
LTRESULT LMessageImpl::ReadDWordFL(uint32& v) { return readScalar(*this, v); }
LTRESULT LMessageImpl::ReadFloatFL(float& v) { return readScalar(*this, v); }
LTRESULT LMessageImpl::WriteByte(uint8 v) { return writeScalar(*this, v); }
LTRESULT LMessageImpl::WriteWord(uint16 v) { return writeScalar(*this, v); }
LTRESULT LMessageImpl::WriteDWord(uint32 v) { return writeScalar(*this, v); }
LTRESULT LMessageImpl::WriteFloat(float v) { return writeScalar(*this, v); }
LTRESULT LMessageImpl::ResetPos() {
    if (!m_pOwner) return LT_NOTINITIALIZED;
    m_pOwner->m_Pos = 1;
    return LT_OK;
}
LTRESULT LMessageImpl::GetStatus(uint32& flags) {
    flags = 0;
    if (!m_pOwner) return LT_NOTINITIALIZED;
    if (m_pOwner->m_Flags & PACKETFLAG_READOVERFLOW) flags |= LMSTAT_READOVERFLOW;
    if (m_pOwner->m_Flags & PACKETFLAG_WRITEOVERFLOW) flags |= LMSTAT_WRITEOVERFLOW;
    return LT_OK;
}
LTRESULT LMessageImpl::FlagError(uint32 flags, LTRESULT result) {
    if (m_pOwner) {
        if (flags & LMSTAT_READOVERFLOW) m_pOwner->m_Flags |= PACKETFLAG_READOVERFLOW;
        if (flags & LMSTAT_WRITEOVERFLOW) m_pOwner->m_Flags |= PACKETFLAG_WRITEOVERFLOW;
    }
    return result;
}
LTRESULT LMessageImpl::ReadStatus() {
    uint32 flags = 0;
    const auto result = GetStatus(flags);
    return result != LT_OK ? result : (flags & LMSTAT_READOVERFLOW ? LT_ERROR : LT_OK);
}
LTRESULT LMessageImpl::WriteString(char* text) {
    char empty = 0;
    if (!text) text = &empty;
    const auto size = std::strlen(text) + 1;
    if (size > 0xffff) return FlagError(LMSTAT_WRITEOVERFLOW,LT_ERROR);
    return WriteRaw(text, uint32(size));
}
LTRESULT LMessageImpl::ReadStringFL(char* text, uint32 capacity) {
    if (!text || !capacity) return LT_INVALIDPARAMS;
    text[0] = 0;
    if (!m_pOwner) return LT_NOTINITIALIZED;
    // Scan before changing the cursor: automatic rewind must not cycle
    // forever when a malformed message contains no string terminator.
    const char* source = m_pOwner->ReadStringInPlace();
    if (!source) return LT_ERROR;
    const auto count = std::min<std::size_t>(std::strlen(source), capacity - 1);
    std::memmove(text, source, count);
    text[count] = 0;
    lmsg_AfterRead(this);
    return LT_OK;
}
LTRESULT LMessageImpl::WriteVector(LTVector& v) {
    const auto a = WriteFloat(v.x), b = WriteFloat(v.y), c = WriteFloat(v.z);
    return a == LT_OK && b == LT_OK && c == LT_OK ? LT_OK : LT_ERROR;
}
LTRESULT LMessageImpl::ReadVectorFL(LTVector& v) {
    if (!Ready()) return LT_NOTINITIALIZED;
    CompoundRead scope(*this);
    const auto a = ReadFloatFL(v.x), b = ReadFloatFL(v.y), c = ReadFloatFL(v.z);
    return a == LT_OK && b == LT_OK && c == LT_OK ? LT_OK : LT_ERROR;
}
LTRESULT LMessageImpl::WriteRotation(LTRotation& r) {
    for (float value : r.m_Quat) if (WriteFloat(value) != LT_OK) return LT_ERROR;
    return LT_OK;
}
LTRESULT LMessageImpl::ReadRotationFL(LTRotation& r) {
    if (!Ready()) return LT_NOTINITIALIZED;
    CompoundRead scope(*this);
    LTRESULT status = LT_OK;
    for (float& value : r.m_Quat) if (ReadFloatFL(value) != LT_OK) status = LT_ERROR;
    return status;
}
LTRESULT LMessageImpl::WriteCompVector(LTVector& v) {
    if (!finite(v)) return invalidWrite(*this);
    lmsg_WriteCompVector(this, &v);
    return Verify();
}
LTRESULT LMessageImpl::WriteCompRotation(LTRotation& r) {
    float magnitude = 0;
    for (float value : r.m_Quat) {
        if (!std::isfinite(value)) return invalidWrite(*this);
        magnitude += value * value;
    }
    if (std::fabs(magnitude - 1.0f) > 0.01f) return invalidWrite(*this);
    lmsg_WriteCompRotation(this, &r);
    return Verify();
}
LTRESULT LMessageImpl::ReadCompRotationFL(LTRotation& r) {
    if (!Ready()) return LT_NOTINITIALIZED;
    CompoundRead scope(*this);
    lmsg_ReadCompRotation(this, &r);
    for (float value : r.m_Quat) if (!std::isfinite(value)) {
        r.Init();
        return FlagError(LMSTAT_READOVERFLOW,LT_ERROR);
    }
    return ReadStatus();
}
LTRESULT LMessageImpl::WriteCompPos(LTVector& v) {
    CPosRef* reference = nullptr;
    if (m_pObjectIO) m_pObjectIO->GetPosRef(&reference);
    if (!reference) return FlagError(LMSTAT_WRITEOVERFLOW,LT_NOTINITIALIZED);
    if (!finite(v) || !finite(reference->m_vMin) || !finite(reference->m_vScale)) return invalidWrite(*this);
    const auto delta = v - reference->m_vMin;
    for (float component : {delta.x * reference->m_vScale.x, delta.y * reference->m_vScale.y, delta.z * reference->m_vScale.z}) {
        const double fixed = double(component) * 65536.0;
        if (fixed < std::numeric_limits<int32>::min() || fixed > std::numeric_limits<int32>::max() || !std::isfinite(fixed))
            return invalidWrite(*this);
    }
    lmsg_WriteCompPos(this, &v, reference);
    return Verify();
}
LTRESULT LMessageImpl::ReadCompPosFL(LTVector& v) {
    if (!Ready()) return LT_NOTINITIALIZED;
    CompoundRead scope(*this);
    CPosRef* reference = nullptr;
    if (m_pObjectIO) m_pObjectIO->GetPosRef(&reference);
    if (!reference) { v.Init(); return FlagError(LMSTAT_READOVERFLOW,LT_NOTINITIALIZED); }
    lmsg_ReadCompPos(this, &v, reference);
    return ReadStatus();
}
LTRESULT LMessageImpl::ReadObjectFL(HOBJECT& object) {
    object = nullptr;
    return m_pObjectIO ? m_pObjectIO->ReadObject(this, &object) : FlagError(LMSTAT_READOVERFLOW,LT_NOTINITIALIZED);
}
LTRESULT LMessageImpl::WriteObject(HOBJECT object) {
    return m_pObjectIO ? m_pObjectIO->WriteObject(this, object) : FlagError(LMSTAT_WRITEOVERFLOW,LT_NOTINITIALIZED);
}
LTRESULT LMessageImpl::WriteMessage(ILTMessage& message) {
    auto* concrete = dynamic_cast<LMessageImpl*>(&message);
    return concrete ? WriteMessage(concrete) : FlagError(LMSTAT_WRITEOVERFLOW,LT_INVALIDPARAMS);
}
LTRESULT LMessageImpl::WriteMessage(LMessageImpl* message) {
    if (!message) return WriteWord(0xffff);
    if (!message->m_pOwner || message->Verify() != LT_OK) return FlagError(LMSTAT_WRITEOVERFLOW,LT_NOTINITIALIZED);
    auto& packet = *message->m_pOwner;
    if (packet.m_DataLen < 1 || packet.m_DataLen > packet.m_DataSize) return LT_ERROR;
    const uint16 size = packet.m_DataLen - 1;
    // Snapshot first also handles writing a message inside itself.
    std::vector<uint8> bytes(packet.m_pData + 1, packet.m_pData + packet.m_DataLen);
    if (WriteWord(size) != LT_OK) return LT_ERROR;
    for (auto value : bytes) if (WriteByte(value) != LT_OK) return LT_ERROR;
    return LT_OK;
}
LTRESULT LMessageImpl::ReadMessageFL(ILTMessage*& message) {
    message = nullptr;
    if (!Ready()) return LT_NOTINITIALIZED;
    CompoundRead scope(*this);
    uint16 size = 0;
    if (ReadWordFL(size) != LT_OK) return LT_ERROR;
    if (size == 0xffff) return LT_OK;
    if (m_pOwner && !m_pOwner->CanRead(size)) return FlagError(LMSTAT_READOVERFLOW,LT_ERROR);
    CAbstractIO* storage = nullptr;
    msg_AllocPacket(&storage, std::max<uint32>(0x1000, uint32(size) + 1), 0x1000);
    if (!storage) return LT_ERROR;
    auto* nested = static_cast<ServerMessage*>(storage);
    nested->m_Write.SetObjectIO(owned_object_io_);
    nested->m_Write.m_pObjectIO = m_pObjectIO;
    if (!nested->Reserve(uint32(size) + 1)) { storage->Release(); return LT_ERROR; }
    for (uint32 i = 0; i < size; ++i) if (ReadByteFL(nested->m_pData[i+1]) != LT_OK) { storage->Release(); return LT_ERROR; }
    nested->m_DataLen = size + 1;
    nested->m_Pos = 1;
    message = &nested->m_Write;
    return LT_OK;
}
LTRESULT LMessageImpl::ReadMessageFL(LMessageImpl*& message) {
    ILTMessage* result = nullptr;
    const auto status = ReadMessageFL(result);
    message = static_cast<LMessageImpl*>(result);
    return status;
}
LTRESULT LMessageImpl::ReadMessage(void** message) {
    if (!message) return LT_INVALIDPARAMS;
    LMessageImpl* result = nullptr;
    const auto status = ReadMessageFL(result);
    *message = result;
    return status;
}
LTRESULT LMessageImpl::WriteHString(HSTRING string) {
    if (!string) return WriteWord(0xffff);
    uint32 length = 0;
    auto* text = sb_GetStringDataAndLen(string, &length);
    if (length >= 0xffff) return FlagError(LMSTAT_WRITEOVERFLOW,LT_ERROR);
    if (WriteWord(uint16(length)) != LT_OK) return LT_ERROR;
    return WriteRaw(text, length);
}
LTRESULT LMessageImpl::WriteStringAsHString(char* text) {
    if (!text) return WriteWord(0xffff);
    const auto length = std::strlen(text) + 1;
    if (length >= 0xffff) return FlagError(LMSTAT_WRITEOVERFLOW,LT_ERROR);
    if (WriteWord(uint16(length)) != LT_OK) return LT_ERROR;
    return WriteRaw(text, uint32(length));
}
LTRESULT LMessageImpl::ReadHStringFL(HSTRING& string) {
    string = nullptr;
    if (!Ready()) return LT_NOTINITIALIZED;
    CompoundRead scope(*this);
    uint16 length = 0;
    if (ReadWordFL(length) != LT_OK) return LT_ERROR;
    if (length == 0xffff) return LT_OK;
    if (!length) { string = sb_CreateStringRaw(g_EmptyString); return LT_OK; }
    std::vector<char> text(length);
    if (ReadRawFL(text.data(), length) != LT_OK) return LT_ERROR;
    if (text.back() != 0) return FlagError(LMSTAT_READOVERFLOW,LT_ERROR);
    string = sb_CreateStringRaw(text.data());
    return string ? LT_OK : LT_ERROR;
}
LTRESULT LMessageImpl::ReadHStringAsStringFL(char* text, uint32 capacity) {
    if (!text || !capacity) return LT_INVALIDPARAMS;
    text[0] = 0;
    HSTRING string = nullptr;
    const auto status = ReadHStringFL(string);
    if (string) {
        const auto* source = sb_GetStringData(string);
        const auto length = std::min<std::size_t>(capacity - 1, std::strlen(source));
        std::memcpy(text, source, length);
        text[length] = 0;
        sb_FreeString(string);
    }
    return status;
}
LTRESULT LMessageImpl::WriteHStringFormatted(int code, ...) {
    va_list arguments;
    va_start(arguments, code);
    const auto status = WriteHStringArgList(code, &arguments);
    va_end(arguments);
    return status;
}
LTRESULT LMessageImpl::WriteHStringArgList(int code, va_list* arguments) {
    return m_pObjectIO ? m_pObjectIO->WriteFormattedHString(this, code, arguments) : FlagError(LMSTAT_WRITEOVERFLOW,LT_NOTINITIALIZED);
}
LTRESULT LMessageImpl::ReadCompVectorFL(LTVector& vec)
{
    if (!Ready()) return LT_NOTINITIALIZED;
    CompoundRead scope(*this);
	float  fMax;
	uint16 nLowA;
	uint32 nA;
	uint32 nB;
	float  fA;
	float  fB;

    ((IMessageRead*)this)->ReadFloat(&fMax);
    if (!std::isfinite(fMax)) {
        vec.Init();
        return FlagError(LMSTAT_READOVERFLOW,LT_ERROR);
    }
	((IMessageRead*)this)->ReadWord(&nLowA);
	{
		uint16 nLowB;
		((IMessageRead*)this)->ReadWord(&nLowB);
		nB = nLowB;
	}

	{
		uint8 nFlags;
        ((IMessageRead*)this)->ReadByte(&nFlags);
        if ((nFlags >> 6) == 3) {
            vec.Init();
            return FlagError(LMSTAT_READOVERFLOW,LT_ERROR);
        }

		nA = ((uint32)(nFlags & 0x18) << 13) + nLowA;
#ifdef NOLFSRC_MODERN
		fA = ((float)nA * (1.0f / 262143.0f)) * fMax;
#else
		fA = (float)nA * fMax * (1.0f / 262144.0f);
#endif

		if (nFlags & 0x20)
			fA = fA * -1.0f;

		nB += (uint32)(nFlags & 0x03) << 16;
#ifdef NOLFSRC_MODERN
		fB = ((float)nB * (1.0f / 262143.0f)) * fMax;
#else
		fB = (float)nB * fMax * (1.0f / 262144.0f);
#endif

		if (nFlags & 0x04)
			fB = fB * -1.0f;

		switch (nFlags >> 6)
		{
			case 1:
				vec.x = fA;
				vec.y = fMax;
				vec.z = fB;
				break;

			case 2:
				vec.x = fA;
				vec.y = fB;
				vec.z = fMax;
				break;

			default:
				vec.x = fMax;
				vec.y = fA;
				vec.z = fB;
				break;
		}
	}

	return ReadStatus();
}
