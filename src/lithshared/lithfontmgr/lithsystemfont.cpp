//*********************************************************************************
//*********************************************************************************
// Project:		Lith Tech Font Manager
// Purpose:		A general font method for bitmap and system fonts
//*********************************************************************************
// File:		LithSystemFont.cpp
//*********************************************************************************
//*********************************************************************************

#include "engine/portable/text.h"
#include "lithsystemfont.h"

//*********************************************************************************

LithSystemFont::LithSystemFont()
{
	m_pFont = LTNULL;
	m_pStringSurf = LTNULL;

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

LithSystemFont::~LithSystemFont()
{
	Free();
	LithFont::Free();
}

//*********************************************************************************

LTBOOL LithSystemFont::Init(ILTClient *pClient, LITHFONTCREATESTRUCT *lfCS)
{
	if (!LithFont::Init(pClient, lfCS))
		return LTFALSE;

	// Create the engine font
	m_pFont = m_pClient->CreateFont(lfCS->szFontName, lfCS->nWidth, lfCS->nHeight,
									lfCS->bItalic, lfCS->bUnderline, lfCS->bBold);
	if (!m_pFont)
		return LTFALSE;

	m_nHeight = lfCS->nHeight;

	return LTTRUE;
}

//*********************************************************************************

void LithSystemFont::Free()
{
	if (m_pFont)
	{
		m_pClient->DeleteFont(m_pFont);
		m_pFont = LTNULL;
	}

	if (m_pStringSurf)
	{
		m_pClient->DeleteSurface(m_pStringSurf);
		m_pStringSurf = LTNULL;
	}

	m_temp_lfSD.szPrevString = LTNULL;
}

//*********************************************************************************

void LithSystemFont::Draw(HSURFACE pDest, char *szString, LITHFONTDRAWDATA *lfDD, int nX, int nY, LITHFONTSAVEDATA *lfSD)
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

void LithSystemFont::GetTextExtents(char *szString, LITHFONTDRAWDATA *lfDD, uint32 &x, uint32 &y, int index)
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

void LithSystemFont::GetPositionFromIndex(char *szString, LITHFONTDRAWDATA *lfDD, int index, uint32 &x, uint32 &y)
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

uint8 LithSystemFont::GetState(LITHFONTSAVEDATA *lfSD)
{
	if (lfSD)
		return lfSD->byLastState;

	return m_temp_lfSD.byLastState;
}

//*********************************************************************************

void LithSystemFont::SetDrawData(HSURFACE pDest, char *szString, LITHFONTDRAWDATA *lfDD, int nX, int nY, LITHFONTSAVEDATA *lfSD, int index)
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
		m_pClient->SetFontExtraSpace(m_pFont, m_nExtraLetterSpace);

		if (m_dwFlags & LTF_DRAW_FORMATTED)
			SetFormatData(m_lfDD->dwFormatWidth);
		else
			SetFormatData(0);
	}

	if (m_pStringSurf)
		m_pClient->SetSurfaceAlpha(m_pStringSurf, m_lfDD->fAlpha);

	SetupCompositeSurface();
}

//*********************************************************************************

void LithSystemFont::ClearDrawData()
{
	m_szString = "";

	m_pDest = LTNULL;
	m_lfDD = LTNULL;
	m_lfSD = LTNULL;
	m_lfFD = LTNULL;
}

//*********************************************************************************

void LithSystemFont::SetFormatData(uint32 nFormatWidth)
{
	if (!m_lfFD)
		return;

	m_lfFD->nNumLines = 0;
	m_lfFD->pIndex[0] = 0;

	int nIndex = 0;
	uint32 nLastSpace = 0;
	uint32 nLastWidth = 0;

	char *pString = m_szString;
	char *pStart = pString;
	char *pCurrent = pString;

	while (m_lfFD->nNumLines+1 < LTF_MAX_FORMAT_LINES)
	{
		int nSizeX, nSizeY;

		if (*pCurrent == ' ' || *pCurrent == '\n' || *pCurrent == '\0')
		{
			std::string szTemp(pStart, pCurrent);

			HSTRING hString = m_pClient->CreateString(szTemp.data());
			m_pClient->GetStringDimensions(m_pFont, hString, &nSizeX, &nSizeY);
			m_pClient->FreeString(hString);

			int nWidth = nSizeX;

			if (nFormatWidth && nLastSpace && (uint32)nWidth > nFormatWidth)
			{
				// This segment is too wide... break the line at the last space
				m_lfFD->pIndex[m_lfFD->nNumLines+1] = nLastSpace;
				m_lfFD->pWidths[m_lfFD->nNumLines] = nLastWidth;
				m_lfFD->nNumLines++;

				pStart = nolf::text::advance(pString, nLastSpace);

				nLastSpace = 0;
				nLastWidth = 0;
				pCurrent = pStart;
				nIndex = nLastSpace;

				continue;
			}
			else if (*pCurrent == ' ')
			{
				nLastWidth = nWidth;
				nLastSpace = nIndex + 1;
			}
			else
			{
				// Newline or end of string... break the line here
				m_lfFD->pIndex[m_lfFD->nNumLines+1] = nIndex + 1;
				m_lfFD->pWidths[m_lfFD->nNumLines] = nWidth;
				m_lfFD->nNumLines++;

				pStart = nolf::text::advance(pString, nIndex + 1);

				nLastSpace = 0;
				nLastWidth = 0;
				pCurrent = pStart;
				nIndex = nIndex + 1;

				if (*pStart == '\0')
					break;

				continue;
			}
		}

		pCurrent = nolf::text::next(pCurrent);
		nIndex++;
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

void LithSystemFont::Draw_Method_Normal()
{
	if (!m_lfFD || !m_lfFD->nNumLines)
		return;

	HLTCOLOR hColor = m_lfDD->hColor;
	HLTCOLOR hTransColor = m_pClient->SetupColor2(1.0f, 0.0f, 1.0f, LTFALSE);

	int nLineHeight = m_lfDD->nLineSpace + m_nHeight;

	if (m_dwFlags & LTF_EXTRA_NEWLINE_UP)
		nLineHeight = -nLineHeight;

	if (m_dwFlags & LTF_EXTRA_LOCKLAST)
		m_nY -= m_lfFD->nNumLines * nLineHeight;

	if (m_dwFlags & LTF_DRAW_SOLID)
		hColor = m_lfDD->hColor;

	for (uint32 i = 0; i < m_lfFD->nNumLines; i++)
	{
		uint32 nWidth = m_lfFD->pWidths[i];

		int x = m_nX;
		switch (m_byJustify)
		{
			case LTF_JUSTIFY_LEFT:		x = m_nX;				break;
			case LTF_JUSTIFY_RIGHT:		x = m_nX - nWidth;		break;
			case LTF_JUSTIFY_CENTER:	x = m_nX - nWidth/2;	break;
		}

		m_rRect.Init(0, i * m_nHeight, nWidth, (i+1) * m_nHeight);

		LTRect rcDest(x, m_nY, x + nWidth, m_nY + m_nHeight);

		if (ClipFont(rcDest))
		{
			m_pClient->DrawSurfaceSolidColor(m_pDest, m_pStringSurf, &m_rRect,
											 rcDest.left, rcDest.top, hTransColor, hColor);
		}

		m_nY += nLineHeight;
	}

	m_lfSD->byLastState = LTF_STATE_DRAW;
}

//*********************************************************************************

void LithSystemFont::Draw_Method_Timed()
{
	if (!m_lfFD || !m_lfFD->nNumLines)
		return;

	if (!m_lfDD)
		return;

	int nCurLine = m_lfFD->nNumLines;
	HLTCOLOR hColor = m_lfDD->hColor;
	HLTCOLOR hTransColor = m_pClient->SetupColor2(1.0f, 0.0f, 1.0f, LTFALSE);

	int nLineHeight = m_lfDD->nLineSpace + m_nHeight;

	LTFLOAT fElapsed = m_pClient->GetTime() - m_lfSD->fStartDrawTime;
	LTFLOAT fLineTime = 0.0f;
	int x;

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
	for (uint32 j = 1; j < (uint32)nCurLine; j++)
	{
		if (m_dwFlags & LTF_TIMED_LETTERS)
			fLineTime += m_lfFD->pTimes[j-1];

		if (m_dwFlags & LTF_TIMED_LINES)
			fLineTime += m_lfDD->fLineDelay;

		if (m_dwFlags & LTF_TIMED_SCROLL)
		{
			if (j - 1 >= m_nWaitLines - 1)
				fLineTime += m_lfDD->fLineScrollTime;
		}
	}

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

	if (m_dwFlags & LTF_DRAW_SOLID)
		hColor = m_lfDD->hColor;

	// Draw each line
	for (uint32 i = 0; i < (uint32)nCurLine; i++)
	{
		uint32 nWidth = m_lfFD->pWidths[i];
		uint32 nStartIndex = m_lfFD->pIndex[i];
		uint32 nEndIndex = m_lfFD->pIndex[i+1];

		if ((m_dwFlags & LTF_TIMED_LETTERS) && m_lfFD->pTimes[i] > 0.0f && i == (uint32)nCurLine - 1 && fElapsed < m_lfSD->fTotalDrawTime)
		{
			// This line is still updating... draw a partial line

			uint32 nIndex = nStartIndex + (uint32)((fElapsed - fLineTime) / m_lfFD->pTimes[i] * (nEndIndex - nStartIndex));
			if (nIndex > nEndIndex)
				nIndex = nEndIndex;

			int nSizeX = 0, nSizeY;
			char *pStart = nolf::text::advance(m_szString, nStartIndex);
			char *pEnd = nolf::text::advance(m_szString, nIndex);

			std::string szTemp(pStart, pEnd);

			char *pTemp = szTemp.data();
			while (*pTemp)
			{
				if (*pTemp == '\r' || *pTemp == '\n')
					*pTemp = ' ';

				pTemp = nolf::text::next(pTemp);
			}

			HSTRING hString = m_pClient->CreateString(szTemp.data());
			m_pClient->GetStringDimensions(m_pFont, hString, &nSizeX, &nSizeY);
			m_pClient->FreeString(hString);

			int nLineWidth = nSizeX;

			switch (m_byJustify)
			{
				case LTF_JUSTIFY_LEFT:		x = m_nX;						break;
				case LTF_JUSTIFY_RIGHT:		x = m_nX - nLineWidth;			break;
				case LTF_JUSTIFY_CENTER:	x = m_nX - (nLineWidth >> 1);	break;
			}

			m_rRect.Init(0, i * m_nHeight, nLineWidth, (i+1) * m_nHeight);

			LTRect rcDest(x, m_nY, x + nLineWidth, m_nY + m_nHeight);

			if (ClipFont(rcDest))
			{
				m_pClient->DrawSurfaceSolidColor(m_pDest, m_pStringSurf, &m_rRect,
												 rcDest.left, rcDest.top, hTransColor, hColor);
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

			m_rRect.Init(0, i * m_nHeight, nWidth, (i+1) * m_nHeight);

			LTRect rcDest(x, m_nY, x + nWidth, m_nY + m_nHeight);

			if (ClipFont(rcDest))
			{
				m_pClient->DrawSurfaceSolidColor(m_pDest, m_pStringSurf, &m_rRect,
												 rcDest.left, rcDest.top, hTransColor, hColor);
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

LTBOOL LithSystemFont::ClipFont(LTRect &rcFont)
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
				m_rRect.top += m_lfDD->rcClip.top - rcFont.top;
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
		else
		{
			if (rcFont.left > m_lfDD->rcClip.right || rcFont.right < m_lfDD->rcClip.left)
				return LTFALSE;

			if (rcFont.left < m_lfDD->rcClip.left)
			{
				m_rRect.left += m_lfDD->rcClip.left - rcFont.left;
				rcFont.left = m_lfDD->rcClip.left;
			}

			if (rcFont.right > m_lfDD->rcClip.right)
			{
				m_rRect.right += m_lfDD->rcClip.right - rcFont.right;
			}
		}
	}
	else
	{
		if (rcFont.right <= 0 || rcFont.left >= (int)m_nDestSurfWidth)
			return LTFALSE;
	}

	return LTTRUE;
}

//*********************************************************************************

LTBOOL LithSystemFont::SetupCompositeSurface()
{
	char *pString = m_szString;

	HLTCOLOR hForeColor = m_pClient->SetupColor2(1.0f, 1.0f, 1.0f, LTFALSE);
	HLTCOLOR hBackColor = m_pClient->SetupColor2(1.0f, 0.0f, 1.0f, LTFALSE);

	// Figure out the size that the composite surface needs to be
	uint32 nTotalHeight = m_lfFD->nNumLines * m_nHeight;

	uint32 nWidth = 0;
	for (uint32 i = 0; i < m_lfFD->nNumLines; i++)
	{
		if (nWidth < m_lfFD->pWidths[i])
			nWidth = m_lfFD->pWidths[i];
	}

	// See if the current surface is already the right size
	uint32 nSurfWidth = 0, nSurfHeight = 0;
	LTBOOL bNewSurface = LTFALSE;

	if (m_pStringSurf)
		m_pClient->GetSurfaceDims(m_pStringSurf, &nSurfWidth, &nSurfHeight);

	if (nSurfWidth != nWidth || nSurfHeight != nTotalHeight)
	{
		if (m_pStringSurf)
		{
			m_pClient->DeleteSurface(m_pStringSurf);
			m_pStringSurf = LTNULL;
		}

		m_pStringSurf = m_pClient->CreateSurface(nWidth, nTotalHeight);
		bNewSurface = LTTRUE;
	}

	if (m_pStringSurf && m_pFont)
	{
		// Clear out the surface
		m_pClient->FillRect(m_pStringSurf, LTNULL, hBackColor);

		// Draw each line onto the composite surface
		for (uint32 i = 0; i < m_lfFD->nNumLines; i++)
		{
			char *pStart = nolf::text::advance(pString, m_lfFD->pIndex[i]);
			char *pEnd = nolf::text::advance(pString, m_lfFD->pIndex[i+1]);

			std::string szTemp(pStart, pEnd);

			char *pTemp = szTemp.data();
			while (*pTemp)
			{
				if (*pTemp == '\r' || *pTemp == '\n')
					*pTemp = ' ';

				pTemp = nolf::text::next(pTemp);
			}

			HSTRING hString = m_pClient->CreateString(szTemp.data());

			m_rRect.Init(0, i * m_nHeight, m_lfFD->pWidths[i], (i+1) * m_nHeight);

			m_pClient->DrawStringToSurface(m_pStringSurf, m_pFont, hString, &m_rRect, hForeColor, hBackColor);

			m_pClient->FreeString(hString);
		}
	}

	return bNewSurface;
}
