// LTGUIScrollBarCtrl.cpp: implementation of the CLTGUIScrollBarCtrl class.
//
//////////////////////////////////////////////////////////////////////

#include "ltguimgr.h"
#include "LTGUIScrollBarCtrl.h"

// Time delay before the up/down arrows begin repeating
static LTFLOAT REPEAT_DELAY = 0.75f;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CLTGUIScrollBarCtrl::CLTGUIScrollBarCtrl()
{
	m_dwTabHeight	= 0;
	m_dwTabOffset	= 0;
	m_dwSliderArea	= 0;
	m_dwTopOffset	= 0;
	m_dwBotOffset	= 0;
	m_dwWidth		= 0;
	m_dwHeight		= 0;
	m_hBarSurf		= LTNULL;
	m_hTabSurf		= LTNULL;
	m_hUpArrow		= LTNULL;
	m_hDownArrow	= LTNULL;
	m_hUpArrowPressed	= LTNULL;
	m_hDownArrowPressed	= LTNULL;
	m_nMinTabPos	= 0;
	m_nTabPos		= 0;
	m_bUpArrowPress	= LTFALSE;
	m_bDownArrowPress	= LTFALSE;
	m_bTabPress		= LTFALSE;
	m_fRepeatStartTime	= 0.0f;
	m_dwPgIncrement	= 1;
	m_nMaxTabPos	= 100;
}

CLTGUIScrollBarCtrl::~CLTGUIScrollBarCtrl()
{
}

// Create the control
LTBOOL CLTGUIScrollBarCtrl::Create(ILTClient *pClientDE, HSURFACE hBarSurf, HSURFACE hTabSurf,
								   HSURFACE hUpArrow, HSURFACE hDownArrow, HSURFACE hUpArrowPressed,
								   HSURFACE hDownArrowPressed, int nWidth, int nHeight,
								   uint32 dwIncrement, int *pnValue)
{
	// The statement order is load-bearing: VC6 reorders these stores, and
	// only this source order gives the shipped store order and register
	// assignment. The first GetSurfaceDims goes through the pClientDE
	// parameter; the other two go through the member.
	m_dwPgIncrement = dwIncrement;
	m_pClientDE = pClientDE;
	m_hBarSurf = hBarSurf;
	m_hTabSurf = hTabSurf;
	m_hUpArrow = hUpArrow;
	m_hDownArrow = hDownArrow;
	m_hUpArrowPressed = hUpArrowPressed;
	m_hDownArrowPressed = hDownArrowPressed;
	m_pnValue = pnValue;
	m_dwWidth = nWidth;
	m_dwHeight = nHeight;

	if (!pClientDE || !hBarSurf || !hTabSurf || !hUpArrow || !hDownArrow ||
		!hUpArrowPressed || !hDownArrowPressed || !pnValue)
	{
		return LTFALSE;
	}

	pClientDE->GetSurfaceDims(hUpArrow, LTNULL, &m_dwTopOffset);
	m_pClientDE->GetSurfaceDims(m_hDownArrow, LTNULL, &m_dwBotOffset);
	m_pClientDE->GetSurfaceDims(m_hTabSurf, LTNULL, &m_dwTabHeight);

	m_dwSliderArea = m_dwHeight - m_dwTopOffset - m_dwBotOffset;

	return LTTRUE;
}

// Update data
void CLTGUIScrollBarCtrl::UpdateData(LTBOOL bSaveAndValidate)
{
	if (!m_pnValue)
		return;

	if (bSaveAndValidate)
		*m_pnValue = m_nTabPos;
	else
		SetTabPos(*m_pnValue);
}

// Sets the min and max of the slider bar
void CLTGUIScrollBarCtrl::SetScrollBarRange(int nMin, int nMax)
{
	m_nMinTabPos = nMin;
	m_nMaxTabPos = nMax;

	SetTabPos(m_nTabPos);
}

// Sets the position of the slider bar
void CLTGUIScrollBarCtrl::SetTabPos(int nPos)
{
	m_nTabPos = nPos;

	if (m_nTabPos > m_nMaxTabPos)
		m_nTabPos = m_nMaxTabPos;
	if (m_nTabPos < m_nMinTabPos)
		m_nTabPos = m_nMinTabPos;
}

// Render the control
void CLTGUIScrollBarCtrl::Render(HSURFACE hDestSurf)
{
	UpdateData(LTFALSE);
	CheckRepeat();
	RenderScrollBar(hDestSurf);
}

// Check to see if the arrows should repeat their action
void CLTGUIScrollBarCtrl::CheckRepeat()
{
	if (m_bUpArrowPress)
	{
		if (m_pClientDE->GetTime() - m_fRepeatStartTime >= REPEAT_DELAY)
			IncUp();
	}
	else if (m_bDownArrowPress)
	{
		if (m_pClientDE->GetTime() - m_fRepeatStartTime >= REPEAT_DELAY)
			IncDown();
	}
}

// Render the scroll bar
void CLTGUIScrollBarCtrl::RenderScrollBar(HSURFACE hDestSurf)
{
	// Draw the bar
	LTRect rect;
	rect.left = m_pos.x;
	rect.top = m_pos.y;
	rect.right = m_pos.x + m_dwWidth;
	rect.bottom = m_pos.y + m_dwHeight;

	m_pClientDE->ScaleSurfaceToSurfaceTransparent(hDestSurf, m_hBarSurf, &rect, LTNULL, m_hTransColor);

	// Calculate the tab offset
	m_dwTabOffset = (int)(((LTFLOAT)(m_nTabPos - m_nMinTabPos) / (m_nMaxTabPos - m_nMinTabPos)) * (m_dwSliderArea - m_dwBotOffset)) + m_dwTopOffset;

	// Draw the tab
	m_pClientDE->DrawSurfaceToSurfaceTransparent(hDestSurf, m_hTabSurf, LTNULL, m_pos.x, m_pos.y + m_dwTabOffset, m_hTransColor);

	// Draw the up arrow
	HSURFACE hUpArrow;
	if (m_bUpArrowPress)
		hUpArrow = m_hUpArrowPressed;
	else
		hUpArrow = m_hUpArrow;

	m_pClientDE->DrawSurfaceToSurfaceTransparent(hDestSurf, hUpArrow, LTNULL, m_pos.x, m_pos.y, m_hTransColor);

	// Draw the down arrow
	HSURFACE hDownArrow;
	if (m_bDownArrowPress)
		hDownArrow = m_hDownArrowPressed;
	else
		hDownArrow = m_hDownArrow;

	m_pClientDE->DrawSurfaceToSurfaceTransparent(hDestSurf, hDownArrow, LTNULL, m_pos.x, m_pos.y - m_dwBotOffset + m_dwHeight, m_hTransColor);
}

void CLTGUIScrollBarCtrl::IncUp()
{
	SetTabPos(m_nTabPos-1);
	UpdateData(LTTRUE);
}

void CLTGUIScrollBarCtrl::IncDown()
{
	SetTabPos(m_nTabPos+1);
	UpdateData(LTTRUE);
}

void CLTGUIScrollBarCtrl::IncPgUp()
{
	SetTabPos(m_nTabPos - m_dwPgIncrement);
	UpdateData(LTTRUE);
}

void CLTGUIScrollBarCtrl::IncPgDown()
{
	SetTabPos(m_nTabPos + m_dwPgIncrement);
	UpdateData(LTTRUE);
}

// Handle mouse click
// NEAR (OnLButtonUp, 202 of 202 bytes): only the registers differ. The
// shipped code keeps m_pos.y in edx from entry and x/y in esi/eax; ours
// swaps these. The page-up test reads m_pos.y rather than nPosY: with nPosY
// there, VC6 drops the `y >= top` compare as redundant (189 bytes); shipped
// keeps it. Tried with no result: every nPosY/m_pos.y mix (all 128),
// nPosX/nRight/nBottom locals (each one brings back the 189-byte form),
// every order of the three flag stores, a nested in-bounds block.
LTBOOL CLTGUIScrollBarCtrl::OnLButtonUp(int x, int y)
{
	int nPosY = m_pos.y;

	m_bUpArrowPress = LTFALSE;
	m_bDownArrowPress = LTFALSE;
	m_bTabPress = LTFALSE;

	if (x < m_pos.x || x > m_pos.x + (int)m_dwWidth || y < nPosY || y > nPosY + (int)m_dwHeight)
		return LTFALSE;

	// Check the up arrow
	if (y <= nPosY + (int)m_dwTopOffset)
	{
		IncUp();
		return LTTRUE;
	}

	// Check the down arrow
	int nBarBottom = nPosY + m_dwHeight - m_dwBotOffset;
	if (y >= nBarBottom)
	{
		if (y <= nBarBottom + (int)m_dwBotOffset)
		{
			IncDown();
			return LTTRUE;
		}
	}

	// Check page up
	if (y >= m_pos.y + (int)m_dwTopOffset && y <= nPosY + (int)m_dwTabOffset)
	{
		IncPgUp();
		return LTTRUE;
	}

	// Check page down
	if (y >= nPosY + (int)m_dwTabOffset + (int)m_dwTabHeight && y <= nBarBottom)
	{
		IncPgDown();
		return LTTRUE;
	}

	return LTFALSE;
}

LTBOOL CLTGUIScrollBarCtrl::OnLButtonDown(int x, int y)
{
	int nPosY = m_pos.y;

	if (x < m_pos.x || x > m_pos.x + (int)m_dwWidth || y < nPosY || y > nPosY + (int)m_dwHeight)
		return LTFALSE;

	// Check the up arrow
	if (y <= nPosY + (int)m_dwTopOffset)
	{
		m_bUpArrowPress = LTTRUE;
		m_fRepeatStartTime = m_pClientDE->GetTime();
		return LTTRUE;
	}

	// Check the down arrow
	int nBarBottom = nPosY + m_dwHeight - m_dwBotOffset;
	if (y >= nBarBottom)
	{
		if (y <= nBarBottom + (int)m_dwBotOffset)
		{
			m_bDownArrowPress = LTTRUE;
			m_fRepeatStartTime = m_pClientDE->GetTime();
			return LTTRUE;
		}
	}

	// Check the tab
	int nTabTop = nPosY + (int)m_dwTabOffset;

	if (y >= nTabTop && y <= nTabTop + (int)m_dwTabHeight)
	{
		m_bTabPress = LTTRUE;
		return LTTRUE;
	}

	return LTFALSE;
}

LTBOOL CLTGUIScrollBarCtrl::OnMouseMove(int x, int y)
{
	// The divisor is (LTFLOAT)m_dwSliderArea, a uint32 converted to float.
	// That conversion's zero high dword is what gives the shipped code its
	// ecx zero register, which the flag compare then uses.
	if (m_bTabPress)
	{
		LTFLOAT fPercent = (LTFLOAT)(m_dwSliderArea - (y - m_pos.y - m_dwTopOffset)) / (LTFLOAT)m_dwSliderArea;


		int nPos = (int)((1.0f - fPercent) * (m_nMaxTabPos - m_nMinTabPos)) + m_nMinTabPos;

		if (nPos <= m_nMaxTabPos && nPos >= m_nMinTabPos)
		{
			SetTabPos(nPos);
			UpdateData(LTTRUE);
		}
	}

	return CLTGUICtrl::OnMouseMove(x, y);
}
