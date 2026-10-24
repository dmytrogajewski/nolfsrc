#include "engine/portable/string.h"
#include "engine/portable/text.h"
#include <atomic>
//*********************************************************************************
//*********************************************************************************
// Project:		Lith Tech Font Manager
// Purpose:		A general font method for bitmap and system fonts
//*********************************************************************************
// File:		LithFont.cpp
//*********************************************************************************
//*********************************************************************************

#include "lithfont.h"

//*********************************************************************************

LithFont::LithFont()
{
	static std::atomic<uint64> next_generation{1};
	m_FontGeneration = next_generation.fetch_add(1,std::memory_order_relaxed);
	m_pClient = LTNULL;
	memset(m_szRefName, 0, LTF_REF_NAME_SIZE);
	m_nHeight = 0;
}

//*********************************************************************************

LithFont::~LithFont()
{
	Free();
}

//*********************************************************************************

LTBOOL LithFont::Init(ILTClient *pClient, LITHFONTCREATESTRUCT *lfCS)
{
	if (!pClient || !lfCS)
		return LTFALSE;

    const char* source = lfCS->szRefName ? lfCS->szRefName : lfCS->szFontBitmap ? lfCS->szFontBitmap : lfCS->szFontName;
    if (!source) return LTFALSE;
    std::string_view name(source);
    if (!lfCS->szRefName && lfCS->szFontBitmap) {
        if (const auto slash = name.find_last_of("/\\"); slash != name.npos) name.remove_prefix(slash+1);
        name = name.substr(0,name.find('.'));
    }
    if (name.empty() || name.size() >= sizeof(m_szRefName)) return LTFALSE;
    std::memcpy(m_szRefName,name.data(),name.size()); m_szRefName[name.size()] = 0;

	nolf::ascii_uppercase(m_szRefName);

	m_pClient = pClient;

	return LTTRUE;
}

//*********************************************************************************

void LithFont::Free()
{
	m_pClient = LTNULL;
}

char* LithFont::PrepareText(const char* source, int index)
{
    if (!source) { m_StringStorage.clear(); return m_StringStorage.data(); }
    const auto* end = index >= 0 ? nolf::text::advance(source,std::size_t(index)) : source+std::strlen(source);
    m_StringStorage = std::string(source,static_cast<std::size_t>(end-source));
    return m_StringStorage.data();
}

bool LithFont::UpdateLayoutIdentity(LITHFONTSAVEDATA& saved, const LITHFONTDRAWDATA& draw, uint32 flags)
{
    if (!draw.bForceFormat && saved.szPrevString && saved.previousText == m_StringStorage &&
        saved.fontGeneration == m_FontGeneration && saved.formatWidth == draw.dwFormatWidth &&
        saved.formatFlags == flags && saved.nLastLetterSpace == draw.nLetterSpace &&
        saved.letterDelay == draw.fLetterDelay && saved.lineDelay == draw.fLineDelay &&
        saved.scrollTime == draw.fLineScrollTime && saved.waitLines == draw.nNumWaitLines) return false;
    saved.szPrevString = m_StringStorage.data();
    saved.previousText = m_StringStorage;
    saved.fontGeneration = m_FontGeneration;
    saved.formatWidth = draw.dwFormatWidth; saved.formatFlags = flags;
    saved.nLastLetterSpace = draw.nLetterSpace;
    saved.letterDelay = draw.fLetterDelay; saved.lineDelay = draw.fLineDelay;
    saved.scrollTime = draw.fLineScrollTime; saved.waitLines = draw.nNumWaitLines;
    return true;
}

//*********************************************************************************

void LithFont::Draw(HSURFACE pDest, HSTRING szString, LITHFONTDRAWDATA *lfDD, int nX, int nY, LITHFONTSAVEDATA *lfSD)
{
	if (!szString)
		return;

	Draw(pDest, m_pClient->GetStringData(szString), lfDD, nX, nY, lfSD);
}

//*********************************************************************************

void LithFont::GetTextExtents(HSTRING szString, LITHFONTDRAWDATA *lfDD, uint32 &x, uint32 &y, int index)
{
	if (!szString)
		return;

	GetTextExtents(m_pClient->GetStringData(szString), lfDD, x, y, index);
}

//*********************************************************************************

void LithFont::GetPositionFromIndex(HSTRING szString, LITHFONTDRAWDATA *lfDD, int index, uint32 &x, uint32 &y)
{
	if (!szString)
		return;

	GetPositionFromIndex(m_pClient->GetStringData(szString), lfDD, index, x, y);
}

//*********************************************************************************
