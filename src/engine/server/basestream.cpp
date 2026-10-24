// Generic SDK stream operations. Lengths are fixed-width on every host.
#include "basestream.h"
#include "ltcodes.h"
#include <algorithm>
#include <cstring>
#include <limits>

LTRESULT CBaseStream::ReadString(char* destination, uint32 capacity)
{
    if (capacity && !destination) return LT_INVALIDPARAMS;
    uint16 length = 0;
    if (Read(&length, sizeof(length)) != LT_OK) {
        if (capacity) destination[0] = 0;
        return LT_ERROR;
    }
    const uint32 copied = capacity ? std::min<uint32>(length, capacity - 1) : 0;
    if (copied && Read(destination, copied) != LT_OK) {
        destination[0] = 0;
        return LT_ERROR;
    }
    if (capacity) destination[copied] = 0;
    uint32 remaining = length - copied;
    char discard[256];
    while (remaining) {
        const auto count = std::min<uint32>(remaining, sizeof(discard));
        if (Read(discard, count) != LT_OK) return LT_ERROR;
        remaining -= count;
    }
    return ErrorStatus();
}

LTRESULT CBaseStream::WriteString(char* value)
{
    if (!value) return LT_INVALIDPARAMS;
    const auto size = std::strlen(value);
    if (size > std::numeric_limits<uint16>::max()) return LT_ERROR;
    const auto length = static_cast<uint16>(size);
    if (Write(&length, sizeof(length)) != LT_OK) return LT_ERROR;
    return Write(value, length);
}

LTRESULT CBaseStream::WriteStream(ILTStream& source, uint32 first, uint32 last)
{
    uint32 length = 0, position = 0;
    if (source.GetLen(&length) != LT_OK) return LT_ERROR;
    if (first == WRITESTREAM_DEFAULT) {
        if (source.GetPos(&position) != LT_OK) return LT_ERROR;
        first = position;
    } else if (source.SeekTo(first) != LT_OK) return LT_ERROR;
    if (last == WRITESTREAM_DEFAULT) last = length;
    if (last < first || last > length) return LT_ERROR;
    uint32 remaining = last - first;
    char buffer[512];
    while (remaining) {
        const auto count = std::min<uint32>(remaining, sizeof(buffer));
        if (source.Read(buffer, count) != LT_OK || Write(buffer, count) != LT_OK) return LT_ERROR;
        remaining -= count;
    }
    return source.ErrorStatus() == LT_OK ? ErrorStatus() : LT_ERROR;
}
