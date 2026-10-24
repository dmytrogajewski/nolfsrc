#include "engine/portable/text.h"
#include "engine/portable/string.h"
//*********************************************************************************
//*********************************************************************************
// Project:		Lith Tech Font Manager
// Purpose:		A general font method for bitmap and system fonts
//*********************************************************************************
// File:		LithBitmapFont.cpp
//*********************************************************************************
//*********************************************************************************

#include "lithbitmapfont.h"

// NOTE: 18/25 functions are byte-identical to the shipped object. The other
// seven (Init's file helpers, the string measurers and the two _Draw workers)
// are near-matches within a few bytes: same instructions, calls, constants
// and control flow, but SP3-vs-SP6 differences in callee-saved register
// tie-breaking (ebx/ebp swapped), deferred-block emission order, and one
// spilled-vs-registered value (verified instruction-by-instruction).

//*********************************************************************************

LithBitmapFont::LithBitmapFont()
{
	m_pFont = LTNULL;
	m_nSurfWidth = 0;
	m_nSurfHeight = 0;
	m_nGroupFlags = 0;
	m_nAvgWidth = 0;
	m_hTransColor = LTNULL;
	m_bChromaKey = LTFALSE;

	m_pDest = LTNULL;
	m_nDestSurfWidth = 0;
	m_nDestSurfHeight = 0;
	m_szString = "";
	m_lfDD = LTNULL;
	m_lfSD = LTNULL;
	m_lfFD = LTNULL;
	m_dwFlags = 0;
	m_byJustify = 0;
	m_nX = 0;
	m_nY = 0;
	m_nExtraLetterSpace = 0;
	m_nWaitLines = 1;
}

//*********************************************************************************

LithBitmapFont::~LithBitmapFont()
{
	Free();
	LithFont::Free();
}

//*********************************************************************************

LTBOOL LithBitmapFont::Init(ILTClient *pClient, LITHFONTCREATESTRUCT *lfCS)
{
	if (!lfCS || !lfCS->szFontBitmap || std::strlen(lfCS->szFontBitmap) >= LTF_OFFSETS_NAME_SIZE) return LTFALSE;
	if (LithFont::Init(pClient, lfCS))
	{
		// Create the font surface
		m_pFont = m_pClient->CreateSurfaceFromBitmap(lfCS->szFontBitmap);
		if (m_pFont)
		{
			if (lfCS->nGroupFlags)
			{
				m_pClient->GetSurfaceDims(m_pFont, &m_nSurfWidth, &m_nSurfHeight);
				m_nHeight = m_nSurfHeight;

				m_rRect.Init(0, 0, 0, m_nSurfHeight);

				strcpy(m_lfOD.szRefName, lfCS->szFontBitmap);
				nolf::ascii_uppercase(m_lfOD.szRefName);

				m_nGroupFlags = lfCS->nGroupFlags;

				if (lfCS->bChromaKey)
				{
					m_bChromaKey = LTTRUE;
					m_hTransColor = lfCS->hTransColor;
				}

				m_OffsetCacheKey = nolf::font_offset_key(*m_pClient,lfCS->szFontBitmap,m_nSurfWidth,m_nSurfHeight,m_nGroupFlags,lfCS->nFixedPitch);
				if (!LoadOffsets())
				{
					if (!CalcOffsets(lfCS->nFixedPitch))
					{
						Free();
						return LTFALSE;
					}

					SaveOffsets();
				}

				CalcAvgWidth();

				return LTTRUE;
			}
		}
	}

	return LTFALSE;
}

//*********************************************************************************

void LithBitmapFont::Free()
{
	m_OffsetCacheKey.reset();
	if (m_pClient)
	{
		if (m_pFont)
		{
			m_pClient->DeleteSurface(m_pFont);
			m_pFont = LTNULL;
		}

		m_temp_lfSD.szPrevString = LTNULL;

		m_pClient = LTNULL;
	}
}

//*********************************************************************************

void LithBitmapFont::Draw(HSURFACE pDest, char *szString, LITHFONTDRAWDATA *lfDD, int nX, int nY, LITHFONTSAVEDATA *lfSD)
{
	if (!m_pClient)
		return;

	if (!pDest)
		return;

	if (!szString)
		return;

	if (!lfDD)
		return;

	SetDrawData(pDest, szString, lfDD, nX, nY, lfSD, -1);

	if (m_dwFlags & LTF_DRAW_TIMED)
	{
		Draw_Method_Timed();
	}
	else
	{
		Draw_Method_Normal();
	}

	ClearDrawData();
}

//*********************************************************************************

void LithBitmapFont::GetTextExtents(char *szString, LITHFONTDRAWDATA *lfDD, uint32 &x, uint32 &y, int index)
{
	if (!m_pClient)
		return;

	if (!szString)
		return;

	if (!lfDD)
		return;

	SetDrawData(LTNULL, szString, lfDD, 0, 0, LTNULL, index);

	y = (m_lfDD->nLineSpace + m_nHeight) * m_lfFD->nNumLines;

	x = 0;
	for (uint32 i = 0; i < m_lfFD->nNumLines; i++)
	{
		if (x < m_lfFD->pWidths[i])
			x = m_lfFD->pWidths[i];
	}

	m_lfSD->byLastState = LTF_STATE_EXTENTS;

	ClearDrawData();
}

//*********************************************************************************

void LithBitmapFont::GetPositionFromIndex(char *szString, LITHFONTDRAWDATA *lfDD, int index, uint32 &x, uint32 &y)
{
	if (!m_pClient)
		return;

	if (!szString)
		return;

	if (!lfDD)
		return;

	GetTextExtents(szString, lfDD, x, y, index);

	if (y > 0)
	{
		y -= lfDD->nLineSpace;
		y -= m_nHeight;
	}
}

//*********************************************************************************

uint8 LithBitmapFont::GetState(LITHFONTSAVEDATA *lfSD)
{
	if (lfSD)
		return lfSD->byLastState;

	return m_temp_lfSD.byLastState;
}

//*********************************************************************************

void LithBitmapFont::SetDrawData(HSURFACE pDest, char *szString, LITHFONTDRAWDATA *lfDD, int nX, int nY, LITHFONTSAVEDATA *lfSD, int index)
{
	if (!m_pClient)
		return;

	if (!szString)
		return;

	if (!lfDD)
		return;

    m_pDest = pDest;
    m_szString = PrepareText(szString,index);

	m_lfDD = lfDD;
	m_dwFlags = lfDD->dwFlags;
	m_byJustify = lfDD->byJustify;
	m_nX = nX;
	m_nY = nY;

	m_lfSD = lfSD ? lfSD : &m_temp_lfSD;

	m_nWaitLines = (lfDD->nNumWaitLines > 1) ? lfDD->nNumWaitLines : 1;

	m_nDestSurfWidth = 0;
	m_nDestSurfHeight = 0;
	if (m_pDest)
		m_pClient->GetSurfaceDims(m_pDest, &m_nDestSurfWidth, &m_nDestSurfHeight);

	m_lfFD = &m_lfSD->lfFD;

	if (m_dwFlags & LTF_USE_OFFSETS)
	{
		m_nX += lfDD->nXOffset;
		m_nY += lfDD->nYOffset;
	}

    if (UpdateLayoutIdentity(*m_lfSD,*m_lfDD,m_dwFlags))
    {
		m_nExtraLetterSpace = m_lfDD->nLetterSpace;

		if (m_dwFlags & LTF_DRAW_FORMATTED)
			SetFormatData(m_lfDD->dwFormatWidth);
		else
			SetFormatData(0);
	}

	if (m_pFont)
		m_pClient->SetSurfaceAlpha(m_pFont, m_lfDD->fAlpha);
}

//*********************************************************************************

void LithBitmapFont::ClearDrawData()
{
	m_szString = "";

	m_pDest = LTNULL;
	m_lfDD = LTNULL;
	m_lfSD = LTNULL;
	m_lfFD = LTNULL;
}

//*********************************************************************************

// Byte-identical: bFound is set before fopen and returned by the failed-open path
// too (shipped zeroes its slot before the call), so the body sits in if (pFile).
LTBOOL LithBitmapFont::LoadOffsets()
{
    if (!m_pClient || !m_OffsetCacheKey) return LTFALSE;
    try { return nolf::FontOffsetCache().load(*m_OffsetCacheKey,m_lfOD); }
    catch (const std::exception&) { return LTFALSE; }
}

//*********************************************************************************

// Byte-identical to the shipped lib. One function-scope nNumChars serves both the
// fixed-pitch total and each group's char count (shipped keeps both in edi, and
// nGroupFlag = 0 is the xor ebx); the per-char store is ONE ternary (both arms
// share the store, and the strength-reduced pointer is based at [nCurChar-1]).
LTBOOL LithBitmapFont::CalcOffsets(uint8 nPitch)
{
	if (!m_pClient || !m_pFont || !m_nSurfWidth || !m_nSurfHeight)
		return LTFALSE;

	int nCurChar = 1;
	int nLastChar = 0;
	LTBOOL bFixed = LTFALSE;
	uint32 nGroupFlag = 0;
	uint32 nNumChars = 0;

	m_lfOD.pOffsets[0] = 0;

	if (nPitch)
	{
		// Verify that the fixed pitch value is correct
		if (m_nGroupFlags & LTF_INCLUDE_SYMBOLS_1)	nNumChars += LTF_NUM_SYMBOLS_1;
		if (m_nGroupFlags & LTF_INCLUDE_NUMBERS)	nNumChars += LTF_NUM_NUMBERS;
		if (m_nGroupFlags & LTF_INCLUDE_SYMBOLS_2)	nNumChars += LTF_NUM_SYMBOLS_2;
		if (m_nGroupFlags & LTF_INCLUDE_UPPERCASE)	nNumChars += LTF_NUM_LETTERS;
		if (m_nGroupFlags & LTF_INCLUDE_SYMBOLS_3)	nNumChars += LTF_NUM_SYMBOLS_3;
		if (m_nGroupFlags & LTF_INCLUDE_LOWERCASE)	nNumChars += LTF_NUM_LETTERS;
		if (m_nGroupFlags & LTF_INCLUDE_SYMBOLS_4)	nNumChars += LTF_NUM_SYMBOLS_4;

		if (!nNumChars) return LTFALSE;
		if (m_nSurfWidth / nNumChars != nPitch)
			m_pClient->CPrint("LithFontMgr! ERROR!! - A fixed pitch calculation was incorrect on font %s", m_szRefName);
		else
			bFixed = LTTRUE;
	}

	for (int i = 0; i < LTF_INCLUDE_TOTAL_GROUPS; i++)
	{
		switch (i)
		{
			case 0:	nGroupFlag = LTF_INCLUDE_SYMBOLS_1;	nNumChars = LTF_NUM_SYMBOLS_1;	break;
			case 1:	nGroupFlag = LTF_INCLUDE_NUMBERS;	nNumChars = LTF_NUM_NUMBERS;		break;
			case 2:	nGroupFlag = LTF_INCLUDE_SYMBOLS_2;	nNumChars = LTF_NUM_SYMBOLS_2;	break;
			case 3:	nGroupFlag = LTF_INCLUDE_UPPERCASE;	nNumChars = LTF_NUM_LETTERS;		break;
			case 4:	nGroupFlag = LTF_INCLUDE_SYMBOLS_3;	nNumChars = LTF_NUM_SYMBOLS_3;	break;
			case 5:	nGroupFlag = LTF_INCLUDE_LOWERCASE;	nNumChars = LTF_NUM_LETTERS;		break;
			case 6:	nGroupFlag = LTF_INCLUDE_SYMBOLS_4;	nNumChars = LTF_NUM_SYMBOLS_4;	break;
		}

		nLastChar += nNumChars;

		if (m_nGroupFlags & nGroupFlag)
		{
			for (; nCurChar <= nLastChar; nCurChar++)
			{
				m_lfOD.pOffsets[nCurChar] = bFixed ? m_lfOD.pOffsets[nCurChar-1] + nPitch : FindOffset(m_lfOD.pOffsets[nCurChar-1]);
			}
		}
		else
		{
			for (; nCurChar <= nLastChar; nCurChar++)
			{
				m_lfOD.pOffsets[nCurChar] = m_lfOD.pOffsets[nCurChar-1];
			}
		}
	}

	// Calculate the widths of each character
	for (int j = 0; j < LTF_NUM_TOTAL_CHARS; j++)
	{
		m_lfOD.pWidths[j] = m_lfOD.pOffsets[j+1] - m_lfOD.pOffsets[j];

		if (m_lfOD.pWidths[j] >= m_nSurfWidth)
			return LTFALSE;
	}

	return LTTRUE;
}

//*********************************************************************************

// NEAR (286 vs 288). Prologue and exits match: the zero locals are declared before
// the colour components, and m_nSurfWidth is returned by a single exit at the end
// (the space-found returns sit inside the loop). Left: shipped sinks the solid-pixel
// bodies after the loop latch (tests, then spaceStart/spaceEnd/found bodies) where
// ours keeps them inline; not moved by solid-first order, if/else vs continue,
// per-arm breaks, nested !bInSpace, a while y loop.
// Also measured (f4): the compare/if graph is identical to shipped; only block layout
// differs. Gotos placing the bodies in shipped order, do/while and while y loops,
// flags threaded out of the loop, and every if/else spelling of the solid arm all
// collapse to the same few layouts. Routing the colour test through a bool (a local
// or an inline helper, with or without GetPixel inside it) flips the latch to
// shipped's `jae exit; jmp top`, but the solid bodies stay inline, so size is still 286.
uint32 LithBitmapFont::FindOffset(uint32 nStartPos)
{
	uint32 nSpaceStart = 0;
	uint32 nSpaceEnd = 0;
	LTBOOL bFoundLetter = LTFALSE;
	LTBOOL bInSpace = LTFALSE;

	uint32 nRed = GETR(m_hTransColor);
	uint32 nGreen = GETG(m_hTransColor);
	uint32 nBlue = GETB(m_hTransColor);

	while (1)
	{
		for (uint32 y = 0; y < m_nSurfHeight; y++)
		{
			HLTCOLOR nColor;
			m_pClient->GetPixel(m_pFont, nStartPos, y, &nColor);

			if (GETR(nColor) == nRed && GETG(nColor) == nGreen && GETB(nColor) == nBlue)
			{
				// This pixel is transparent
				if (bFoundLetter && !bInSpace && y == m_nSurfHeight - 1)
				{
					nSpaceStart = nStartPos;
					bInSpace = LTTRUE;
					break;
				}

				continue;
			}

			// This pixel is solid
			if (bInSpace)
			{
				nSpaceEnd = nStartPos - 1;
				bInSpace = LTFALSE;
			}
			else if (!bFoundLetter)
			{
				bFoundLetter = LTTRUE;
			}

			break;
		}

		nStartPos++;

		if (nSpaceStart && nSpaceEnd)
		{
			if (m_nGroupFlags & LTF_INCLUDE_FIXED_END)
				return nSpaceStart;

			return nSpaceStart + (nSpaceEnd - nSpaceStart) / 2;
		}

		if (nStartPos >= m_nSurfWidth)
			break;
	}

	return m_nSurfWidth;
}

//*********************************************************************************

LTBOOL LithBitmapFont::SaveOffsets()
{
    if (!m_pClient || !m_OffsetCacheKey) return LTFALSE;
    try { return nolf::FontOffsetCache().store(*m_OffsetCacheKey,m_lfOD); }
    catch (const std::exception&) { return LTFALSE; }
}

//*********************************************************************************

LTBOOL LithBitmapFont::CalcAvgWidth()
{
	uint32 nTotal = 0;
	uint32 nCount = 0;

	for (int i = 0; i < LTF_NUM_TOTAL_CHARS; i++)
	{
		if (m_lfOD.pWidths[i])
		{
			nTotal += m_lfOD.pWidths[i];
			nCount++;
		}
	}

	m_nAvgWidth = nTotal / nCount;

	return LTTRUE;
}

//*********************************************************************************

// The index is advanced inside each arm (before the add on the space arm,
// straight after nChar on the other): shipped has an `inc` in both arms ahead of
// the shared loop test, and this is also what gives nWidth ebx, m_szString ebp.
uint32 LithBitmapFont::GetStringLength(uint32 nIndex1, uint32 nIndex2, LTBOOL bIgnorLastSpace)
{
	if (!m_szString)
		return 0;

	uint32 nWidth = 0;
	uint32 nLen = strlen(m_szString);

	if (!nLen || nIndex1 > nLen || nIndex2 > nLen)
		return 0;

	if (nIndex1 == 0 && nIndex2 == 0)
		nIndex2 = nLen;

	while (nIndex1 < nIndex2)
	{
		char c = m_szString[nIndex1];

		if (c == ' ' && (!bIgnorLastSpace || nIndex1 != nIndex2 - 1))
		{
			nIndex1++;
			nWidth += m_nExtraLetterSpace + m_nAvgWidth;
		}
		else
		{
			int nChar = c - 33;
			nIndex1++;

			if ((uint32)nChar < LTF_NUM_TOTAL_CHARS)
				nWidth += m_lfOD.pWidths[nChar] + m_nExtraLetterSpace;
		}
	}

	return nWidth;
}

//*********************************************************************************

void LithBitmapFont::SetFormatData(uint32 nFormatWidth)
{
	if (!m_lfFD)
		return;

	uint32 nIndex = 0;
	uint32 nLastSpace = 0;
	uint32 nWidth = 0;
	uint32 nLastWidth = 0;

	m_lfFD->nNumLines = 0;
	m_lfFD->pIndex[0] = 0;

	while (m_szString[nIndex] != '\0' && m_lfFD->nNumLines+1 < LTF_MAX_FORMAT_LINES)
	{
		char c = m_szString[nIndex];

		if (c == ' ')
		{
			nLastWidth = nWidth;
			nWidth += m_nExtraLetterSpace + m_nAvgWidth;
			nLastSpace = nIndex + 1;
		}
		else if (c == '\n')
		{
			// Newline... break the line here
			nIndex++;
			nLastWidth = 0;

			m_lfFD->pIndex[m_lfFD->nNumLines+1] = nIndex;
			m_lfFD->pWidths[m_lfFD->nNumLines] = nWidth;
			m_lfFD->nNumLines++;

			nWidth = 0;
			nLastSpace = 0;

			continue;
		}
		else
		{
			int nChar = c - 33;

			if ((uint32)nChar < LTF_NUM_TOTAL_CHARS)
				nWidth += m_lfOD.pWidths[nChar] + m_nExtraLetterSpace;
		}

		if (nWidth > nFormatWidth && nLastSpace && nFormatWidth)
		{
			// This line is too wide... break the line at the last space
			m_lfFD->pIndex[m_lfFD->nNumLines+1] = nLastSpace;
			m_lfFD->pWidths[m_lfFD->nNumLines] = nLastWidth;
			m_lfFD->nNumLines++;

			nLastWidth = 0;
			nWidth = 0;
			nIndex = nLastSpace;
			nLastSpace = 0;
		}
		else
		{
			nIndex++;
		}
	}

	if (nWidth && m_lfFD->nNumLines+1 < LTF_MAX_FORMAT_LINES)
	{
		// Add the last line
		m_lfFD->pIndex[m_lfFD->nNumLines+1] = nIndex;
		m_lfFD->pWidths[m_lfFD->nNumLines] = nWidth;
		m_lfFD->nNumLines++;
	}

	m_lfFD->nNumChars = m_lfFD->pIndex[m_lfFD->nNumLines];

	// Setup the timing data
	if (m_dwFlags & LTF_DRAW_TIMED)
	{
		m_lfSD->fStartDrawTime = m_pClient->GetTime();
		m_lfSD->fTotalDrawTime = 0;

		if (m_dwFlags & LTF_TIMED_LETTERS)
			m_lfSD->fTotalDrawTime += m_lfFD->nNumChars * m_lfDD->fLetterDelay;

		if ((m_dwFlags & LTF_TIMED_LINES) && m_lfFD->nNumLines > 0)
			m_lfSD->fTotalDrawTime += (m_lfFD->nNumLines - 1) * m_lfDD->fLineDelay;

		if ((m_dwFlags & LTF_TIMED_SCROLL) && m_lfFD->nNumLines > m_nWaitLines)
			m_lfSD->fTotalDrawTime += (m_lfFD->nNumLines - m_nWaitLines) * m_lfDD->fLineScrollTime;

		for (uint32 i = 0; i < m_lfFD->nNumLines; i++)
		{
			m_lfFD->pTimes[i] = (m_lfFD->pIndex[i+1] - m_lfFD->pIndex[i]) * m_lfDD->fLetterDelay;
		}
	}
}

//*********************************************************************************

void LithBitmapFont::Draw_Method_Normal()
{
	if (!m_lfFD || !m_lfFD->nNumLines)
		return;

	int nLineHeight = m_lfDD->nLineSpace + m_nHeight;

	void (LithBitmapFont::*pDrawFn)(int, int, uint32, uint32) = &LithBitmapFont::_Draw;

	if (m_dwFlags & LTF_DRAW_SOLID)
		pDrawFn = &LithBitmapFont::_DrawSolid;

	if (m_dwFlags & LTF_EXTRA_NEWLINE_UP)
		nLineHeight = -nLineHeight;

	if (m_dwFlags & LTF_EXTRA_LOCKLAST)
		m_nY -= m_lfFD->nNumLines * nLineHeight;

	for (uint32 i = 0; i < m_lfFD->nNumLines; i++)
	{
		uint32 nWidth = m_lfFD->pWidths[i];
		uint32 nStartIndex = m_lfFD->pIndex[i];
		uint32 nEndIndex = m_lfFD->pIndex[i+1];

		int x = m_nX;
		switch (m_byJustify)
		{
			case LTF_JUSTIFY_LEFT:		x = m_nX;					break;
			case LTF_JUSTIFY_RIGHT:		x = m_nX - nWidth;			break;
			case LTF_JUSTIFY_CENTER:	x = m_nX - nWidth/2;		break;
		}

		m_rRect.top = 0;
		m_rRect.bottom = m_nSurfHeight;

		LTRect rcDest(x, m_nY, x + nWidth, m_nY + m_nHeight);

		if (ClipFont(rcDest))
		{
			(this->*pDrawFn)(x, rcDest.top, nStartIndex, nEndIndex);
		}

		m_nY += nLineHeight;
	}

	m_lfSD->byLastState = LTF_STATE_DRAW;
}

//*********************************************************************************

// NEAR (1309 vs 1301). Measured: the second loop reuses i (shipped keeps i, j and
// the pTimes walker in one slot); one loop-scope x (edi, frame 0x4c not 0x50); a
// GetStringLength call per arm with a constant flag (shipped pushes $1/$1/$0).
// Residual is allocation: shipped gives nStartIndex ebp and spills nEndIndex, we
// do the reverse. Not moved by declaration order (all 6), int types, register,
// a function-scope pair, a named count/percent, the inverted if, a while loop.
// Also measured (f4), none of which moved it: const locals, a pIndex pointer local,
// reading pIndex[i+1] at the clamp, compare or full draw (reloads), a clamp then
// ==/ternary flag, nIndex = Start then +=, a commuted compare, Start or End
// declared per arm, and new loop variables for loops 2/3 (VC6 splits webs anyway).
// An int count local gives 1301 bytes, but only because it drops the uint->float
// high-dword store; the allocation is unchanged.
void LithBitmapFont::Draw_Method_Timed()
{
	if (!m_lfFD || !m_lfFD->nNumLines)
		return;

	if (!m_lfDD)
		return;

	int nCurLine = m_lfFD->nNumLines;
	int nLineHeight = m_lfDD->nLineSpace + m_nHeight;

	LTFLOAT fElapsed = m_pClient->GetTime() - m_lfSD->fStartDrawTime;
	LTFLOAT fLineTime = 0.0f;

	void (LithBitmapFont::*pDrawFn)(int, int, uint32, uint32) = &LithBitmapFont::_Draw;

	// Figure out which line we're currently drawing
	LTFLOAT fTime = 0.0f;
	for (uint32 i = 0; i < m_lfFD->nNumLines; i++)
	{
		if (m_dwFlags & LTF_TIMED_LETTERS)
			fTime += m_lfFD->pTimes[i];

		if (m_dwFlags & LTF_TIMED_LINES)
			fTime += m_lfDD->fLineDelay;

		if (m_dwFlags & LTF_TIMED_SCROLL)
		{
			if (i >= m_nWaitLines - 1)
				fTime += m_lfDD->fLineScrollTime;
		}

		if (fTime > fElapsed)
		{
			nCurLine = i + 1;
			break;
		}
	}

	// Figure out the time at which the current line started
	for (uint32 i = 1; i < (uint32)nCurLine; i++)
	{
		if (m_dwFlags & LTF_TIMED_LETTERS)
			fLineTime += m_lfFD->pTimes[i-1];

		if (m_dwFlags & LTF_TIMED_LINES)
			fLineTime += m_lfDD->fLineDelay;

		if (m_dwFlags & LTF_TIMED_SCROLL)
		{
			if (i - 1 >= m_nWaitLines - 1)
				fLineTime += m_lfDD->fLineScrollTime;
		}
	}

	if (m_dwFlags & LTF_DRAW_SOLID)
		pDrawFn = &LithBitmapFont::_DrawSolid;

	if (m_dwFlags & LTF_EXTRA_NEWLINE_UP)
		nLineHeight = -nLineHeight;

	if (m_dwFlags & LTF_EXTRA_LOCKLAST)
	{
		if (nCurLine >= m_nWaitLines)
		{
			LTFLOAT fScrollOffset = 0.0f;

			if (m_dwFlags & LTF_TIMED_SCROLL)
			{
				if (m_lfDD->fLineScrollTime > 0.0f && nCurLine != (int)m_lfFD->nNumLines)
				{
					LTFLOAT fScrollTime = fElapsed - fLineTime - m_lfFD->pTimes[nCurLine-1] - m_lfDD->fLineDelay;
					if (fScrollTime > 0.0f)
					{
						LTFLOAT fPercent = fScrollTime / m_lfDD->fLineScrollTime;
						if (fPercent > 1.0f)
							fPercent = 1.0f;

						fScrollOffset = fPercent * nLineHeight;
					}
				}
			}

			m_nY -= (nCurLine - m_nWaitLines) * nLineHeight + (int)fScrollOffset;
		}
	}

	// Draw each line
	for (uint32 i = 0; i < (uint32)nCurLine; i++)
	{
		uint32 nEndIndex = m_lfFD->pIndex[i+1];
		uint32 nStartIndex = m_lfFD->pIndex[i];
		uint32 nWidth = m_lfFD->pWidths[i];
		int x = m_nX;

		if ((m_dwFlags & LTF_TIMED_LETTERS) && m_lfFD->pTimes[i] > 0.0f && i == (uint32)nCurLine - 1 && fElapsed < m_lfSD->fTotalDrawTime)
		{
			// This line is still updating... draw a partial line
			uint32 nIndex = nStartIndex + (uint32)((fElapsed - fLineTime) / m_lfFD->pTimes[i] * (nEndIndex - nStartIndex));

			uint32 nLineWidth;

			if (nIndex > nEndIndex)
			{
				nIndex = nEndIndex;
				nLineWidth = GetStringLength(nStartIndex, nIndex, LTTRUE);
			}
			else if (nIndex == nEndIndex)
			{
				nLineWidth = GetStringLength(nStartIndex, nIndex, LTTRUE);
			}
			else
			{
				nLineWidth = GetStringLength(nStartIndex, nIndex, LTFALSE);
			}

			switch (m_byJustify)
			{
				case LTF_JUSTIFY_LEFT:		x = m_nX;					break;
				case LTF_JUSTIFY_RIGHT:		x = m_nX - nLineWidth;		break;
				case LTF_JUSTIFY_CENTER:	x = m_nX - nLineWidth/2;	break;
			}

			m_rRect.top = 0;
			m_rRect.bottom = m_nSurfHeight;

			LTRect rcDest(x, m_nY, x + nWidth, m_nY + m_nHeight);

			if (ClipFont(rcDest))
			{
				(this->*pDrawFn)(x, rcDest.top, nStartIndex, nIndex);
			}
		}
		else
		{
			switch (m_byJustify)
			{
				case LTF_JUSTIFY_LEFT:		x = m_nX;				break;
				case LTF_JUSTIFY_RIGHT:		x = m_nX - nWidth;		break;
				case LTF_JUSTIFY_CENTER:	x = m_nX - nWidth/2;	break;
			}

			m_rRect.top = 0;
			m_rRect.bottom = m_nSurfHeight;

			LTRect rcDest(x, m_nY, x + nWidth, m_nY + m_nHeight);

			if (ClipFont(rcDest))
			{
				(this->*pDrawFn)(x, rcDest.top, nStartIndex, nEndIndex);
			}
		}

		m_nY += nLineHeight;
	}

	// Set the current state
	if (fElapsed >= m_lfSD->fTotalDrawTime)
	{
		m_lfSD->byLastState = LTF_STATE_DRAW_FINISHED;
		return;
	}

	if (m_lfDD->fLineScrollTime != 0.0f)
	{
		if (fElapsed - fLineTime > m_lfFD->pTimes[nCurLine-1] + m_lfDD->fLineDelay)
		{
			m_lfSD->byLastState = LTF_STATE_DRAW_SCROLLING;
			return;
		}
	}

	if (m_lfDD->fLineDelay != 0.0f)
	{
		if (fElapsed - fLineTime > m_lfFD->pTimes[nCurLine-1])
		{
			m_lfSD->byLastState = LTF_STATE_DRAW_LINE_DELAY;
			return;
		}
	}

	m_lfSD->byLastState = LTF_STATE_DRAW_UPDATING;
}

//*********************************************************************************

// Each of the three arms advances nIndex1 itself (no for-increment), and nChar
// is unsigned (`ja`, not `jg`, on the range test). Together these give shipped's
// allocation: nIndex1 in ebx, nX in ebp.
void LithBitmapFont::_Draw(int nX, int nY, uint32 nIndex1, uint32 nIndex2)
{
	if (!m_pClient)
		return;

	if (!m_pDest)
		return;

	if (!m_pFont)
		return;

	if (!m_szString)
		return;

	if (nIndex1 >= nIndex2)
		return;

	while (nIndex1 < nIndex2)
	{
		char c = m_szString[nIndex1];
		uint32 nChar = c - 33;

		if (c == ' ')
		{
			nX += m_nExtraLetterSpace + m_nAvgWidth;
			nIndex1++;
		}
		else if (nChar <= LTF_NUM_TOTAL_CHARS - 1)
		{
			m_rRect.left = m_lfOD.pOffsets[nChar];
			m_rRect.right = m_lfOD.pOffsets[nChar+1];

			if (m_bChromaKey)
			{
				m_pClient->DrawSurfaceToSurfaceTransparent(m_pDest, m_pFont, &m_rRect, nX, nY, m_hTransColor);
			}
			else
			{
				m_pClient->DrawSurfaceToSurface(m_pDest, m_pFont, &m_rRect, nX, nY);
			}

			nX += m_lfOD.pWidths[nChar] + m_nExtraLetterSpace;
			nIndex1++;
		}
		else
			nIndex1++;
	}
}

//*********************************************************************************

// Each of the three arms advances nIndex1 itself (no for-increment), and nChar
// is unsigned (`ja`, not `jg`, on the range test). Together these give shipped's
// allocation: nIndex1 in ebx, nX in ebp.
void LithBitmapFont::_DrawSolid(int nX, int nY, uint32 nIndex1, uint32 nIndex2)
{
	if (!m_pClient)
		return;

	if (!m_pDest)
		return;

	if (!m_pFont)
		return;

	if (!m_szString)
		return;

	if (nIndex1 >= nIndex2)
		return;

	while (nIndex1 < nIndex2)
	{
		char c = m_szString[nIndex1];
		uint32 nChar = c - 33;

		if (c == ' ')
		{
			nX += m_nExtraLetterSpace + m_nAvgWidth;
			nIndex1++;
		}
		else if (nChar <= LTF_NUM_TOTAL_CHARS - 1)
		{
			m_rRect.left = m_lfOD.pOffsets[nChar];
			m_rRect.right = m_lfOD.pOffsets[nChar+1];

			m_pClient->DrawSurfaceSolidColor(m_pDest, m_pFont, &m_rRect, nX, nY,
											 m_hTransColor, m_lfDD->hColor);

			nX += m_lfOD.pWidths[nChar] + m_nExtraLetterSpace;
			nIndex1++;
		}
		else
			nIndex1++;
	}
}

//*********************************************************************************

LTBOOL LithBitmapFont::ClipFont(LTRect &rcFont)
{
	if (m_dwFlags & LTF_USE_CLIPRECT_Y)
	{
		if (m_dwFlags & LTF_USE_EXCLUSIVE_CLIP)
		{
			if (rcFont.top < m_lfDD->rcClip.top || rcFont.bottom > m_lfDD->rcClip.bottom)
				return LTFALSE;
		}
		else
		{
			if (rcFont.top > m_lfDD->rcClip.bottom || rcFont.bottom < m_lfDD->rcClip.top)
				return LTFALSE;

			if (rcFont.top < m_lfDD->rcClip.top)
			{
				m_rRect.top = m_lfDD->rcClip.top - rcFont.top;
				rcFont.top = m_lfDD->rcClip.top;
			}

			if (rcFont.bottom > m_lfDD->rcClip.bottom)
			{
				m_rRect.bottom += m_lfDD->rcClip.bottom - rcFont.bottom;
			}
		}
	}
	else
	{
		if (rcFont.bottom <= 0 || rcFont.top >= (int)m_nDestSurfHeight)
			return LTFALSE;
	}

	if (m_dwFlags & LTF_USE_CLIPRECT_X)
	{
		if (m_dwFlags & LTF_USE_EXCLUSIVE_CLIP)
		{
			if (rcFont.left < m_lfDD->rcClip.left || rcFont.right > m_lfDD->rcClip.right)
				return LTFALSE;
		}
	}
	else
	{
		if (rcFont.right <= 0 || rcFont.left >= (int)m_nDestSurfWidth)
			return LTFALSE;
	}

	return LTTRUE;
}
