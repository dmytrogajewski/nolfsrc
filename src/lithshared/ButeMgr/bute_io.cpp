#include "butemgr.h"
#include "platform/filesystem.h"
#include <exception>
#include <locale>
#include <sstream>

bool CButeMgr::ParseText(std::string text, int decryptCode) {
    Reset();
    m_decryptCode = decryptCode;
    m_sourceText = std::move(text);
    m_tagTab.clear(); m_auxTagTab.clear(); m_newTagTab.clear();
    m_pCurrTabOfItems = nullptr;
    std::istringstream input(m_sourceText);
    input.imbue(std::locale::classic());
    m_pData = &input;
    struct ResetStream { CButeMgr& owner; ~ResetStream() { owner.m_pData = nullptr; } } reset{*this};
    try { m_bErrorFlag = !TagList(); }
    catch (const std::exception& error) { m_bErrorFlag = true; DisplayMessage("%s", error.what()); }
    return !m_bErrorFlag;
}

bool CButeMgr::Parse(void* bytes, unsigned long size, int decryptCode) {
    if (!bytes) return false;
    m_bCrypt = false;
    m_sAttributeFilename.Empty();
    return ParseText(std::string(static_cast<const char*>(bytes), size), decryptCode);
}
bool CButeMgr::Parse(void* bytes, unsigned long size, const char* key) {
    if (!bytes) return false;
    m_bCrypt = true;
    m_sAttributeFilename.Empty();
    m_cryptMgr.SetKey(key);
    try {
        std::istringstream input(std::string(static_cast<const char*>(bytes), size));
        std::ostringstream output;
        m_cryptMgr.Decrypt(input, output);
        return ParseText(output.str(), 0);
    } catch (const std::exception& error) { m_bErrorFlag = true; DisplayMessage("%s", error.what()); return false; }
}
bool CButeMgr::Parse(CString filename, int decryptCode) {
    try {
        m_bCrypt = false;
        m_sAttributeFilename = filename;
        const auto bytes = nolf::platform::read_file(static_cast<const char*>(filename));
        return ParseText(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()), decryptCode);
    } catch (const std::exception& error) { m_bErrorFlag = true; DisplayMessage("%s", error.what()); return false; }
}
bool CButeMgr::Parse(CString filename, const char* key) {
    try {
        m_bCrypt = true;
        m_sAttributeFilename = filename;
        m_cryptMgr.SetKey(key);
        const auto bytes = nolf::platform::read_file(static_cast<const char*>(filename));
        std::istringstream input(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        std::ostringstream output;
        m_cryptMgr.Decrypt(input, output);
        return ParseText(output.str(), 0);
    } catch (const std::exception& error) { m_bErrorFlag = true; DisplayMessage("%s", error.what()); return false; }
}
