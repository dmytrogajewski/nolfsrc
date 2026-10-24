// LTGUIBitmapCtrl.cpp: implementation of the CLTGUIBitmapCtrl class.
//
//////////////////////////////////////////////////////////////////////

#include "ltguimgr.h"
#include "LTGUIBitmapCtrl.h"
#include "ltguifont.h"

extern ILTClient *g_pLTClient;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CLTGUIBitmapCtrl::CLTGUIBitmapCtrl()
{
	m_hNormalSurface	= LTNULL;
	m_hSelectedSurface	= LTNULL;
	m_hDisabledSurface	= LTNULL;
	m_hClickedOnSurface	= LTNULL;
	m_bFreeSurfaces		= LTFALSE;
	m_hText				= LTNULL;
	m_pFont				= LTNULL;
	m_nNumFonts			= 1;
	m_ptOffset			= LTIntPt(0,0);
	m_bButtonDown		= LTFALSE;
	m_nNormalWidth		= 0;
	m_nNormalHeight		= 0;
	m_nSelectedWidth	= 0;
	m_nSelectedHeight	= 0;
	m_nDisabledWidth	= 0;
	m_nDisabledHeight	= 0;
	m_nClickedWidth		= 0;
	m_nClickedHeight	= 0;
}

CLTGUIBitmapCtrl::~CLTGUIBitmapCtrl()
{
	Destroy();
}

// Create the control
LTBOOL CLTGUIBitmapCtrl::Create(ILTClient *pClientDE, HSURFACE hNormalBmp, HSURFACE hSelectedBmp,
								HSURFACE hDisabledBmp, HSURFACE hClickedOnSurface,
								CLTGUICommandHandler *pCommandHandler, uint32 dwCommandID,
								uint32 dwParam1, uint32 dwParam2, HSTRING hString, CLTGUIFont *pFont,
								LTIntPt ptOffset)
{
	m_pClientDE = pClientDE;
	m_pCommandHandler = pCommandHandler;
	m_ptOffset = ptOffset;

	if (hString)
	{
		if (pFont)
			SetFont(pFont);
		SetText(hString);
	}

	if (SetBitmap(hNormalBmp, hSelectedBmp, hDisabledBmp, hClickedOnSurface))
	{
		m_dwCommandID = dwCommandID;
		m_dwParam1 = dwParam1;
		m_dwParam2 = dwParam2;

		return LTTRUE;
	}

	return LTFALSE;
}

// Sets the text for the button
void CLTGUIBitmapCtrl::SetText(HSTRING hString)
{
	if (m_hText && m_pClientDE)
	{
		m_pClientDE->FreeString(m_hText);
		m_hText = LTNULL;
	}

	if (hString && m_pClientDE)
	{
		m_hText = m_pClientDE->CopyString(hString);
	}
}

// Set the font
void CLTGUIBitmapCtrl::SetFont(CLTGUIFont *pFont)
{
	m_pFont = pFont;
}

LTBOOL CLTGUIBitmapCtrl::SetBitmap(HSURFACE hNormalBmp, HSURFACE hSelectedBmp, HSURFACE hDisabledBmp, HSURFACE hClickedBmp)
{
	if (!hNormalBmp)
		return LTFALSE;

	uint32 nWidth = 0, nHeight = 0;

	if (m_bFreeSurfaces)
		FreeSurfaces();

	m_hSelectedSurface = hSelectedBmp;
	m_hDisabledSurface = hDisabledBmp;
	m_hClickedOnSurface = hClickedBmp;
	m_hNormalSurface = hNormalBmp;

	m_pClientDE->GetSurfaceDims(hNormalBmp, &nWidth, &nHeight);
	m_nNormalWidth = nWidth;
	m_nNormalHeight = nHeight;

	if (m_hSelectedSurface)
	{
		m_pClientDE->GetSurfaceDims(m_hSelectedSurface, &nWidth, &nHeight);
		m_nSelectedWidth = nWidth;
		m_nSelectedHeight = nHeight;
	}

	if (m_hDisabledSurface)
	{
		m_pClientDE->GetSurfaceDims(m_hDisabledSurface, &nWidth, &nHeight);
		m_nDisabledWidth = nWidth;
		m_nDisabledHeight = nHeight;
	}

	if (m_hClickedOnSurface)
	{
		m_pClientDE->GetSurfaceDims(m_hClickedOnSurface, &nWidth, &nHeight);
		m_nClickedWidth = nWidth;
		m_nClickedHeight = nHeight;
	}

	return LTTRUE;
}

void CLTGUIBitmapCtrl::FreeSurfaces()
{
	if (m_hNormalSurface)
	{
		m_pClientDE->DeleteSurface(m_hNormalSurface);
		m_hNormalSurface = LTNULL;
	}

	if (m_hSelectedSurface)
	{
		m_pClientDE->DeleteSurface(m_hSelectedSurface);
		m_hSelectedSurface = LTNULL;
	}

	if (m_hDisabledSurface)
	{
		m_pClientDE->DeleteSurface(m_hDisabledSurface);
		m_hDisabledSurface = LTNULL;
	}

	if (m_hClickedOnSurface)
	{
		m_pClientDE->DeleteSurface(m_hClickedOnSurface);
		// NOTE: the shipped code clears m_hDisabledSurface here (again)
		// instead of m_hClickedOnSurface — an original Monolith bug,
		// preserved for byte-identity.
		m_hDisabledSurface = LTNULL;
	}
}

// Destruction
void CLTGUIBitmapCtrl::Destroy()
{
	if (m_bFreeSurfaces)
		FreeSurfaces();

	if (m_hText && m_pClientDE)
	{
		m_pClientDE->FreeString(m_hText);
		m_hText = LTNULL;
	}
}

// Render the control
// One if/else-if/else chain with a single RenderText at the bottom: VC6
// duplicates the RenderText+epilogue tail into each arm itself (the shipped
// layout).  Early returns per arm made it cross-merge the tails instead.
void CLTGUIBitmapCtrl::Render(HSURFACE hDestSurf)
{
	int state = GetState();

	if (state == LGCS_SELECTED && m_hSelectedSurface)
	{
		if (m_bButtonDown)
		{
			m_pClientDE->DrawSurfaceToSurfaceTransparent(hDestSurf, m_hClickedOnSurface, LTNULL,
														 m_pos.x, m_pos.y, m_hTransColor);
		}
		else
		{
			m_pClientDE->DrawSurfaceToSurfaceTransparent(hDestSurf, m_hSelectedSurface, LTNULL,
														 m_pos.x, m_pos.y, m_hTransColor);
		}
	}
	else if ((state == LGCS_DISABLED || !m_bEnabled) && m_hDisabledSurface)
	{
		m_pClientDE->DrawSurfaceToSurfaceTransparent(hDestSurf, m_hDisabledSurface, LTNULL,
													 m_pos.x, m_pos.y, m_hTransColor);
	}
	else
	{
		m_pClientDE->DrawSurfaceToSurfaceTransparent(hDestSurf, m_hNormalSurface, LTNULL,
													 m_pos.x, m_pos.y, m_hTransColor);

		if (state == LGCS_SELECTED)
		{
			// No selected bitmap - draw a highlight frame around the normal bitmap
			LTRect rcLeft(m_pos.x, m_pos.y, m_pos.x+2, m_pos.y+m_nNormalHeight);
			LTRect rcTop(m_pos.x, m_pos.y, m_pos.x+m_nNormalWidth, m_pos.y+2);
			LTRect rcRight(m_pos.x+m_nNormalWidth-2, m_pos.y, m_pos.x+m_nNormalWidth, m_pos.y+m_nNormalHeight);
			LTRect rcBottom(m_pos.x, m_pos.y+m_nNormalHeight-2, m_pos.x+m_nNormalWidth, m_pos.y+m_nNormalHeight);

			g_pLTClient->FillRect(hDestSurf, &rcLeft, SETRGB(255,255,255));
			g_pLTClient->FillRect(hDestSurf, &rcTop, SETRGB(255,255,255));
			g_pLTClient->FillRect(hDestSurf, &rcRight, SETRGB(255,255,255));
			g_pLTClient->FillRect(hDestSurf, &rcBottom, SETRGB(255,255,255));
		}
	}

	RenderText(hDestSurf);
}

void CLTGUIBitmapCtrl::RenderText(HSURFACE hDestSurf)
{
	if (!m_pFont)
		return;

	if (!m_hText)
		return;

	int x = m_pos.x + m_ptOffset.x;
	int y = m_pos.y + m_ptOffset.y;

	HLTCOLOR color = GetCurrentColor();

	m_pFont->Draw(m_hText, hDestSurf, x, y, LTF_JUSTIFY_LEFT, color);
}

// Width/Height calculations
int CLTGUIBitmapCtrl::GetWidth()
{
	const int state = GetState();

	if (state == LGCS_SELECTED)
	{
		if (m_hSelectedSurface)
			return m_nSelectedWidth;
	}
	else if (state == LGCS_DISABLED)
	{
		if (m_hDisabledSurface)
			return m_nDisabledWidth;
	}

	return m_nNormalWidth;
}

int CLTGUIBitmapCtrl::GetHeight()
{
	const int state = GetState();

	if (state == LGCS_SELECTED)
	{
		if (m_hSelectedSurface)
			return m_nSelectedHeight;
	}
	else if (state == LGCS_DISABLED)
	{
		if (m_hDisabledSurface)
			return m_nDisabledHeight;
	}

	return m_nNormalHeight;
}

// Handle the enter key
LTBOOL CLTGUIBitmapCtrl::OnEnter()
{
	if (m_pCommandHandler)
	{
		if (m_pCommandHandler->SendCommand(m_dwCommandID, m_dwParam1, m_dwParam2))
			return LTTRUE;
	}

	return LTFALSE;
}

LTBOOL CLTGUIBitmapCtrl::OnLButtonDown(int x, int y)
{
	m_bButtonDown = LTTRUE;
	return LTFALSE;
}

LTBOOL CLTGUIBitmapCtrl::OnLButtonUp(int x, int y)
{
	m_bButtonDown = LTFALSE;

	if (m_pCommandHandler)
	{
		if (m_pCommandHandler->SendCommand(m_dwCommandID, m_dwParam1, m_dwParam2))
			return LTTRUE;
	}

	return LTFALSE;
}
