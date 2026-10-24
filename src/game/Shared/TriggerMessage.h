#pragma once
#include "iltcsbase.h"
#include "iltmessage.h"
#include <stdexcept>
#include <string>

// Object-to-object triggers carry text by value. A process address is never
// written to a DWORD, and each receiver owns its text through nested callbacks.
inline LTRESULT WriteTriggerMessage(HMESSAGEWRITE message, const char* text) {
    return message ? message->WriteStringAsHString(const_cast<char*>(text)) : LT_INVALIDPARAMS;
}

inline std::string ReadTriggerMessage(ILTCSBase& api, HMESSAGEREAD message) {
    if (!message) throw std::runtime_error("Missing trigger message");
    HSTRING text = nullptr;
    const auto result = message->ReadHStringFL(text);
    struct Release {
        ILTCSBase& api;
        HSTRING text;
        ~Release() { if (text) api.FreeString(text); }
    } release{api, text};
    if (result != LT_OK) throw std::runtime_error("Invalid trigger message");
    const char* data = text ? api.GetStringData(text) : nullptr;
    return data ? data : "";
}
