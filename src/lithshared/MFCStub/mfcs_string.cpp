// Portable implementation of the game's MFC substitute string.
#include "mfcs_string.h"
#include "engine/portable/string.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
DWORD string_size(std::size_t size) {
    // Capacity includes a terminating byte in the fixed-width public API.
    if (size >= std::numeric_limits<DWORD>::max())
        throw std::length_error("CString exceeds its capacity limit");
    return static_cast<DWORD>(size);
}
}

CString::CString() : m_pBuffer(nullptr) {}
CString::~CString() { Empty(); }
CString::CString(LPCTSTR text) : m_pBuffer(nullptr) { CopyString(text); }
CString::CString(const CString& text) : m_pBuffer(nullptr) { CopyString(text); }

void CString::FormatV(LPCTSTR format, va_list args) {
    if (!format) return;
    // Both the format and string arguments may point into this CString.
    // Finish formatting before replacing its storage. Each pass owns its list.
    va_list measure;
    va_copy(measure, args);
    const int length = std::vsnprintf(nullptr, 0, format, measure);
    va_end(measure);
    if (length < 0) throw std::runtime_error("Invalid CString format");
    std::vector<char> output(static_cast<std::size_t>(length) + 1);
    va_list render;
    va_copy(render, args);
    const int written = std::vsnprintf(output.data(), output.size(), format, render);
    va_end(render);
    if (written != length) throw std::runtime_error("CString formatting failed");
    CopyString(output.data());
}

void CString::Format(LPCTSTR format, ...) {
    va_list args;
    va_start(args, format);
    try { FormatV(format, args); }
    catch (...) { va_end(args); throw; }
    va_end(args);
}

int CString::Replace(LPCTSTR oldText, LPCTSTR newText) {
    if (!oldText || !*oldText) return 0;
    const std::string needle(oldText), replacement(newText ? newText : "");
    std::string result(static_cast<LPCTSTR>(*this));
    std::size_t position = 0;
    int count = 0;
    while ((position = result.find(needle, position)) != std::string::npos) {
        result.replace(position, needle.size(), replacement);
        position += replacement.size();
        if (count == std::numeric_limits<int>::max())
            throw std::length_error("Too many CString replacements");
        ++count;
    }
    if (count) CopyString(result.c_str());
    return count;
}

int CString::Find(char ch, DWORD start) const {
    if (start > GetLength()) return -1;
    const char* found = std::strchr(GetBuffer() + start, ch);
    return found ? static_cast<int>(found - GetBuffer()) : -1;
}

int CString::Find(LPCTSTR substring, DWORD start) const {
    if (!substring || start > GetLength()) return -1;
    const char* found = std::strstr(GetBuffer() + start, substring);
    return found ? static_cast<int>(found - GetBuffer()) : -1;
}

void CString::ReleaseBuffer(SDWORD length) {
    if (!m_pBuffer) return;
    const auto capacity = GetBufferSize();
    if (length < 0) {
        // A caller may have filled every byte. Always leave a terminator.
        m_pBuffer[capacity - 1] = '\0';
        SetLength(static_cast<DWORD>(std::strlen(m_pBuffer)));
    } else {
        const auto size = std::min(static_cast<DWORD>(length), capacity - 1);
        m_pBuffer[size] = '\0';
        SetLength(size);
    }
}

void CString::CopyString(LPCTSTR text) {
    if (!text) { Empty(); return; }
    // Assignment from our own buffer or a suffix survives reallocation.
    const std::string copy(text);
    const auto size = string_size(copy.size());
    ExpandBuffer(size + 1);
    std::memcpy(m_pBuffer, copy.c_str(), static_cast<std::size_t>(size) + 1);
    SetLength(size);
}

BOOL CString::ExpandBuffer(DWORD capacity) {
    if (GetBufferSize() >= capacity) return TRUE;
    auto* storage = new char[sizeof(CStringData) + static_cast<std::size_t>(capacity)];
    auto* data = ::new (storage) CStringData;
    data->m_BufferSize = capacity;
    data->m_Length = GetLength();
    char* buffer = data->data();
    if (m_pBuffer) std::memcpy(buffer, m_pBuffer, static_cast<std::size_t>(GetLength()) + 1);
    else buffer[0] = '\0';
    delete[] reinterpret_cast<char*>(GetData());
    m_pBuffer = buffer;
    return TRUE;
}

BOOL CString::ShrinkBuffer(DWORD capacity) {
    if (capacity >= GetBufferSize()) return TRUE;
    if (!capacity) {
        delete[] reinterpret_cast<char*>(GetData());
        m_pBuffer = nullptr;
        return TRUE;
    }
    const auto length = std::min(GetLength(), capacity - 1);
    auto* storage = new char[sizeof(CStringData) + static_cast<std::size_t>(capacity)];
    auto* data = ::new (storage) CStringData;
    data->m_BufferSize = capacity;
    data->m_Length = length;
    std::memcpy(data->data(), m_pBuffer, length);
    data->data()[length] = '\0';
    delete[] reinterpret_cast<char*>(GetData());
    m_pBuffer = data->data();
    return TRUE;
}

char* CString::GetBuffer(DWORD minLength) {
    if (minLength == std::numeric_limits<DWORD>::max())
        throw std::length_error("CString capacity overflow");
    ExpandBuffer(minLength + 1);
    return m_pBuffer;
}

void CString::Concat(LPCTSTR text) {
    if (!text || !*text) return;
    const std::string copy(text);
    const auto length = GetLength();
    const auto size = string_size(static_cast<std::size_t>(length) + copy.size());
    ExpandBuffer(size + 1);
    std::memcpy(m_pBuffer + length, copy.c_str(), copy.size() + 1);
    SetLength(size);
}

void CString::Concat(char ch) {
    const auto length = GetLength();
    const auto size = string_size(static_cast<std::size_t>(length) + 1);
    ExpandBuffer(size + 1);
    m_pBuffer[length] = ch;
    m_pBuffer[size] = '\0';
    SetLength(size);
}

CString CString::Mid(DWORD first, DWORD count) const {
    if (first >= GetLength()) return CString("");
    count = std::min(count, GetLength() - first);
    return CString(std::string(GetBuffer() + first, count).c_str());
}
CString CString::Mid(DWORD first) const { return Mid(first, GetLength()); }
CString CString::Left(DWORD count) const { return Mid(0, count); }
CString CString::Right(DWORD count) const {
    count = std::min(count, GetLength());
    return Mid(GetLength() - count, count);
}
int CString::Compare(LPCTSTR text) const { return std::strcmp(GetBuffer(), text ? text : ""); }
int CString::CompareNoCase(LPCTSTR text) const { return nolf::ascii_compare(GetBuffer(), text ? text : ""); }
