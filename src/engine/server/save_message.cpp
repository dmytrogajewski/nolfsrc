#include "save_message.h"
#include "engine/portable/saved_game.h"
#include <array>
#include <cstring>

SaveMessage::SaveMessage(ILTStream& stream, SaveMessageDirection direction, CMsgObjectIO* objects)
    : stream_(stream), direction_(direction) { m_pObjectIO = objects; }
LTRESULT SaveMessage::Verify() const { return status_ || stream_.ErrorStatus() != LT_OK ? LT_ERROR : LT_OK; }
LTRESULT SaveMessage::FlagError(uint32 flags, LTRESULT result) { status_ |= flags; return result; }
LTRESULT SaveMessage::GetStatus(uint32& flags) {
    if (stream_.ErrorStatus() != LT_OK) status_ |= direction_ == SaveMessageDirection::Read ? LMSTAT_READOVERFLOW : LMSTAT_WRITEOVERFLOW;
    flags = status_; return LT_OK;
}
LTRESULT SaveMessage::ReadRawFL(void* output, uint32 size) {
    if (direction_ != SaveMessageDirection::Read) return FlagError(LMSTAT_READOVERFLOW,LT_ERROR);
    if (!size) return LT_OK;
    if (!output) return FlagError(LMSTAT_READOVERFLOW,LT_INVALIDPARAMS);
    std::array<std::byte,4> guard{};
    if (!(status_ & LMSTAT_READOVERFLOW) && stream_.Read(output,size) == LT_OK &&
        stream_.Read(guard.data(),guard.size()) == LT_OK && nolf::wire::load<uint32>(guard.data()) == nolf::saved_game_sentinel) return LT_OK;
    std::memset(output,0,size); return FlagError(LMSTAT_READOVERFLOW,LT_ERROR);
}
LTRESULT SaveMessage::WriteRaw(void* input, uint32 size) {
    if (direction_ != SaveMessageDirection::Write) return FlagError(LMSTAT_WRITEOVERFLOW,LT_ERROR);
    if (!size) return LT_OK;
    if (!input) return FlagError(LMSTAT_WRITEOVERFLOW,LT_INVALIDPARAMS);
    std::array<std::byte,4> guard; nolf::wire::store(guard.data(),nolf::saved_game_sentinel);
    if (!(status_ & LMSTAT_WRITEOVERFLOW) && stream_.Write(input,size) == LT_OK && stream_.Write(guard.data(),guard.size()) == LT_OK) return LT_OK;
    return FlagError(LMSTAT_WRITEOVERFLOW,LT_ERROR);
}
LTRESULT SaveMessage::ReadStringFL(char* output, uint32 capacity) {
    if (!output || !capacity) return FlagError(LMSTAT_READOVERFLOW,LT_INVALIDPARAMS);
    output[0] = 0;
    for (uint32 count = 0; count < 0xffff; ++count) {
        uint8 value = 0;
        if (ReadByteFL(value) != LT_OK) { output[0] = 0; return LT_ERROR; }
        if (!value) return LT_OK;
        if (count < capacity - 1) { output[count] = static_cast<char>(value); output[count+1] = 0; }
    }
    output[0] = 0; return FlagError(LMSTAT_READOVERFLOW,LT_ERROR);
}
LTRESULT SaveMessage::WriteString(char* text) {
    if (!text) return WriteByte(0);
    const auto length = std::strlen(text);
    if (length >= 0xffff) return FlagError(LMSTAT_WRITEOVERFLOW,LT_ERROR);
    for (std::size_t i = 0; i <= length; ++i) if (WriteByte(static_cast<uint8>(text[i])) != LT_OK) return LT_ERROR;
    return LT_OK;
}
