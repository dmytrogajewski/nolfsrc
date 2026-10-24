#include "engine/portable/string.h"
//*********************************************************************************
//*********************************************************************************
// Project:		Lith Tech Font Manager
// Purpose:		A general font method for bitmap and system fonts
//*********************************************************************************
// File:		LithCursor.cpp
//*********************************************************************************
//*********************************************************************************

#include "lithcursor.h"

//*********************************************************************************

LithCursor::LithCursor()
{
	m_pClient = LTNULL;
	memset(m_szRefName, 0, LTF_REF_NAME_SIZE);
	m_pFont = LTNULL;
	m_pDest = LTNULL;
	m_nX = 0;
	m_nY = 0;
}

//*********************************************************************************

LithCursor::~LithCursor()
{
	Free();
}

//*********************************************************************************

LTBOOL LithCursor::Init(ILTClient *pClient, LITHCURSORCREATESTRUCT *lcCS)
{
	if (!pClient || !lcCS || (lcCS->szRefName && std::strlen(lcCS->szRefName) >= sizeof(m_szRefName)))
		return LTFALSE;

	m_pClient = pClient;

	// Setup the reference name for this cursor
	if (lcCS->szRefName)
	{
		strcpy(m_szRefName, lcCS->szRefName);
		nolf::ascii_uppercase(m_szRefName);
	}

	m_pFont = lcCS->pFont;
	m_pDest = lcCS->pDest;
	m_nX = lcCS->nXLoc;
	m_nY = lcCS->nYLoc;

	memcpy(&m_lfDD, &lcCS->lfDD, sizeof(LITHFONTDRAWDATA));

	return LTTRUE;
}

//*********************************************************************************

void LithCursor::Free()
{
	m_lfSD.szPrevString = LTNULL;

	m_pClient = LTNULL;
	m_pFont = LTNULL;
	m_pDest = LTNULL;
}

//*********************************************************************************

uint8 LithCursor::GetState()
{
	if (m_pFont)
		return m_pFont->GetState(&m_lfSD);

	return LTF_STATE_NONE;
}

//*********************************************************************************

void LithCursor::Draw(char *szString, LITHFONTDRAWDATA *lfDD)
{
	if (lfDD)
		memcpy(&m_lfDD, lfDD, sizeof(LITHFONTDRAWDATA));

	if (m_pFont && szString)
	{
		m_pFont->Draw(m_pDest, szString, &m_lfDD, m_nX, m_nY, &m_lfSD);
	}
}

//*********************************************************************************

void LithCursor::Draw(HSTRING szString, LITHFONTDRAWDATA *lfDD)
{
	if (lfDD)
		memcpy(&m_lfDD, lfDD, sizeof(LITHFONTDRAWDATA));

	if (m_pFont && szString)
	{
		m_pFont->Draw(m_pDest, szString, &m_lfDD, m_nX, m_nY, &m_lfSD);
	}
}

//*********************************************************************************

void LithCursor::GetTextExtents(char *szString, LITHFONTDRAWDATA *lfDD, uint32 &x, uint32 &y)
{
	if (!m_pFont)
		return;

	if (!szString)
		return;

	m_pFont->GetTextExtents(szString, lfDD ? lfDD : &m_lfDD, x, y, -1);
}

//*********************************************************************************

void LithCursor::GetTextExtents(HSTRING szString, LITHFONTDRAWDATA *lfDD, uint32 &x, uint32 &y)
{
	if (!m_pFont)
		return;

	if (!szString)
		return;

	m_pFont->GetTextExtents(szString, lfDD ? lfDD : &m_lfDD, x, y, -1);
}

//*********************************************************************************

void LithCursor::GetPositionFromIndex(char *szString, LITHFONTDRAWDATA *lfDD, int index, uint32 &x, uint32 &y)
{
	if (!m_pFont)
		return;

	if (!szString)
		return;

	m_pFont->GetPositionFromIndex(szString, lfDD ? lfDD : &m_lfDD, index, x, y);
}

//*********************************************************************************

void LithCursor::GetPositionFromIndex(HSTRING szString, LITHFONTDRAWDATA *lfDD, int index, uint32 &x, uint32 &y)
{
	if (!m_pFont)
		return;

	if (!szString)
		return;

	m_pFont->GetPositionFromIndex(szString, lfDD ? lfDD : &m_lfDD, index, x, y);
}
