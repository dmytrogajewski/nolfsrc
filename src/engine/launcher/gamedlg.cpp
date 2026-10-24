// *********************************************************************** //
//
//	MODULE:		GAMEDLG (.CPP)
//
//	PURPOSE:	The main launcher dialog (GAME) - the splash window with
//				the Launch / Display / Advanced / Quit buttons and the
//				clickable web-link hotspots over the splash art.
//
// *********************************************************************** //

#include "stdafx.h"
#include "mmsystem.h"
#include "regmgr.h"
#include "dibmgr.h"
#include "launcher.h"


// Globals...

int		g_bActive;				// 0x447c

RECT	g_rcWebNews;			// 0x4250 - www.nolfnews.com
RECT	g_rcWebOperative;		// 0x4270 - www.the-operative.com
RECT	g_rcWebLith;			// 0x4400 - www.lith.com


// Prototypes...

BOOL	InitGameDlg(HWND hWnd);

BOOL	DoDisplayDialog(HWND hWnd);
void	DoCustomizeDialog(HWND hWnd);
int		DoAdvancedDialog(HWND hWnd);
BOOL	OpenWebLink(HWND hWnd, int nUrlID, int nErrID);
void	StopMusic(HWND hWnd);
BOOL	PlayMusic(const char* sSound);


// *********************************************************************** //
//
//	ROUTINE:	GameDlgProc
//
//	PURPOSE:	Dialog procedure for the main GAME dialog
//
//	WM_LBUTTONDOWN precedes WM_LBUTTONUP: VC6 hands a switch's shared
//	tail to the case LAST in source order, and the shipped code merges the
//	WM_SETCURSOR/WM_LBUTTONDBLCLK `return TRUE` into WM_LBUTTONUP's tail
//	(0x40475b).  In the other order it lands on WM_LBUTTONDOWN's and the
//	0x203 dispatch branch no longer fits a short jump.
//
// *********************************************************************** //

BOOL CALLBACK GameDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
		case WM_ACTIVATEAPP:
		{
			g_bActive = wParam;
			// Repaint through WM_PAINT.  Under Wine (Proton) a splash
			// drawn here, while the activation is changing, comes out in
			// false colours and stays that way; the same draw from
			// WM_PAINT is right.  WM_PAINT reads g_bActive, so the
			// palette is still selected as retail selects it here.
			InvalidateRect(hWnd, NULL, FALSE);
			return(FALSE);
		}

		case WM_PAINT:
		{
			PaintSplash(hWnd, g_bActive == 0);
			return(FALSE);
		}

		case WM_INITDIALOG:
		{
			InitGameDlg(hWnd);
			return(TRUE);
		}

		case WM_COMMAND:
		{
			if (wParam == 1 || wParam == 1000)
			{
				StopMusic(hWnd);
				EndDialog(hWnd, 1);
				return(TRUE);
			}
			else if (wParam == 8)
			{
				EndDialog(hWnd, 5);
				return(TRUE);
			}
			else if (wParam == 2)
			{
				EndDialog(hWnd, 0);
				return(TRUE);
			}
			else if (wParam == 1005 || wParam == 3)
			{
				EndDialog(hWnd, 2);
				return(TRUE);
			}
			else if (wParam == 1007)
			{
				DoAdvancedDialog(hWnd);
				return(TRUE);
			}
			else if (wParam == 9)
			{
				DoDisplayDialog(hWnd);
				InvalidateRect(hWnd, NULL, FALSE);
				return(TRUE);
			}
			else if (wParam == 4)
			{
				DoCustomizeDialog(hWnd);
				InvalidateRect(hWnd, NULL, FALSE);
				return(TRUE);
			}
			else if (wParam == 10)
			{
				OpenWebLink(hWnd, 54, 12);
				return(TRUE);
			}
			else if (wParam == 6)
			{
				OpenWebLink(hWnd, 53, 52);
				return(TRUE);
			}
			else if (wParam == 5)
			{
				DoPreviewDialog(g_hInst, hWnd);
				return(TRUE);
			}

			return(FALSE);
		}

		case WM_MOUSEMOVE:
		{
			HitTest(hWnd, wParam, LOWORD(lParam), HIWORD(lParam));
			return(TRUE);
		}

		case WM_LBUTTONDOWN:
		{
			HitTest(hWnd, wParam, LOWORD(lParam), HIWORD(lParam));
			return(TRUE);
		}

		case WM_LBUTTONUP:
		{
			int nHit = HitTest(hWnd, wParam, LOWORD(lParam), HIWORD(lParam));

			if (nHit == 1)
			{
				OpenWebLink(hWnd, 55, 52);
			}
			else if (nHit == 2)
			{
				OpenWebLink(hWnd, 53, 52);
			}
			else if (nHit == 3)
			{
				OpenWebLink(hWnd, 73, 52);
			}

			return(TRUE);
		}

		case WM_LBUTTONDBLCLK:
		case WM_SETCURSOR:
		{
			return(TRUE);
		}

		case 0x311:
		{
			if ((HWND)wParam != hWnd)
			{
				PaintSplash(hWnd, g_bActive == 0);
			}
			return(FALSE);
		}
	}

	return(FALSE);
}


// *********************************************************************** //
//
//	ROUTINE:	PaintSplash   [0x404de0]
//
//	PURPOSE:	Stretches the splash DIB over the placeholder static control
//				on the main dialog
//
//	pt is address-taken by ScreenToClient, so the plain copies nX/nY are
//	needed (without them VC6 reloads pt and the frame is 4 bytes short),
//	and they come before nWidth/nHeight: that order schedules the nY store
//	between the two decs (0x404e6e).  The draw is the 5-argument
//	CDib::Blt, which reads m_nWidth/m_nHeight after the depth test
//	(`cmpl $8,0x440(%eax)`); StretchBlt with GetWidth()/GetHeight() loads
//	them first.
//
// *********************************************************************** //

void PaintSplash(HWND hWnd, BOOL bBackPal)
{
	HWND hCtrl = GetDlgItem(hWnd, 1003);

	if (!g_pDibMgr || !g_pDib || !hCtrl) return;

	RECT rc;
	GetWindowRect(hCtrl, &rc);

	POINT pt;
	pt.x = rc.left;
	pt.y = rc.top;
	ScreenToClient(hWnd, &pt);

	int nX = pt.x;
	int nY = pt.y;
	int nWidth  = rc.right - rc.left - 1;
	int nHeight = rc.bottom - rc.top - 1;

	HDC hDC = g_pDibMgr->GetDC(TRUE, bBackPal);

	SetStretchBltMode(hDC, COLORONCOLOR);

	g_pDib->Blt(hDC, nX, nY, nWidth, nHeight);

	g_pDibMgr->ReleaseDC(hDC);
}


// *********************************************************************** //
//
//	ROUTINE:	HitTest   [0x405c70]
//
//	PURPOSE:	Which web-link hotspot the mouse is over, and the cursor
//				that goes with it
//
// *********************************************************************** //

int HitTest(HWND hWnd, UINT nFlags, int x, int y)
{
	if (!hWnd) return(0);

	if (x >= g_rcWebLith.left && x <= g_rcWebLith.right &&
		y >= g_rcWebLith.top && y <= g_rcWebLith.bottom)
	{
		if (nFlags & 1)
		{
			if (g_hCursorClick) SetCursor(g_hCursorClick);
		}
		else
		{
			if (g_hCursorHand) SetCursor(g_hCursorHand);
		}

		return(1);
	}

	if (x >= g_rcWebOperative.left && x <= g_rcWebOperative.right &&
		y >= g_rcWebOperative.top && y <= g_rcWebOperative.bottom)
	{
		if (nFlags & 1)
		{
			if (g_hCursorClick) SetCursor(g_hCursorClick);
		}
		else
		{
			if (g_hCursorHand) SetCursor(g_hCursorHand);
		}

		return(2);
	}

	if (x >= g_rcWebNews.left && x <= g_rcWebNews.right &&
		y >= g_rcWebNews.top && y <= g_rcWebNews.bottom)
	{
		if (nFlags & 1)
		{
			if (g_hCursorClick) SetCursor(g_hCursorClick);
		}
		else
		{
			if (g_hCursorHand) SetCursor(g_hCursorHand);
		}

		return(3);
	}

	SetCursor(g_hCursorArrow);

	return(0);
}


// *********************************************************************** //
//
//	ROUTINE:	InitGameDlg   [0x4047c0]
//
//	PURPOSE:	Builds the splash DIB and palette, starts the music, and
//				caches the client-space rectangles of the three web links
//
// *********************************************************************** //

BOOL InitGameDlg(HWND hWnd)
{
	if (!hWnd) return(FALSE);

	HICON hIcon = LoadIcon(g_hInst, "ICON");
	if (hIcon)
	{
		SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
	}

	if (g_pDibMgr)
	{
		delete g_pDibMgr;
	}

	g_pDibMgr = new CDibMgr;

	if (g_pDibMgr)
	{
		if (g_pDibMgr->Init(g_hInst, hWnd, 0))
		{
			g_pPal = g_pDibMgr->AddPal(g_sGame, 1);
			if (!g_pPal) g_pPal = g_pDibMgr->AddPal("GAME", 1);
			if (!g_pPal) g_pPal = g_pDibMgr->AddPal("GAME1", 1);

			g_pDibMgr->SetCurPal(g_pPal);

			g_pDib = g_pDibMgr->AddDib(g_sGame, 0);
		}
	}

	PlayMusic(g_sGame);

	HWND hCtrl = GetDlgItem(hWnd, 1011);
	if (hCtrl)
	{
		POINT pt;

		GetWindowRect(hCtrl, &g_rcWebLith);

		pt.x = g_rcWebLith.left;
		pt.y = g_rcWebLith.top;
		ScreenToClient(hWnd, &pt);
		g_rcWebLith.left = pt.x;
		g_rcWebLith.top  = pt.y;

		pt.x = g_rcWebLith.right;
		pt.y = g_rcWebLith.bottom;
		ScreenToClient(hWnd, &pt);
		g_rcWebLith.right  = pt.x;
		g_rcWebLith.bottom = pt.y;
	}
	else
	{
		g_rcWebLith.left = -999;
	}

	hCtrl = GetDlgItem(hWnd, 1013);
	if (hCtrl)
	{
		POINT pt;

		GetWindowRect(hCtrl, &g_rcWebOperative);

		pt.x = g_rcWebOperative.left;
		pt.y = g_rcWebOperative.top;
		ScreenToClient(hWnd, &pt);
		g_rcWebOperative.left = pt.x;
		g_rcWebOperative.top  = pt.y;

		pt.x = g_rcWebOperative.right;
		pt.y = g_rcWebOperative.bottom;
		ScreenToClient(hWnd, &pt);
		g_rcWebOperative.right  = pt.x;
		g_rcWebOperative.bottom = pt.y;
	}
	else
	{
		g_rcWebOperative.left = -999;
	}

	hCtrl = GetDlgItem(hWnd, 1014);
	if (hCtrl)
	{
		POINT pt;

		GetWindowRect(hCtrl, &g_rcWebNews);

		pt.x = g_rcWebNews.left;
		pt.y = g_rcWebNews.top;
		ScreenToClient(hWnd, &pt);
		g_rcWebNews.left = pt.x;
		g_rcWebNews.top  = pt.y;

		pt.x = g_rcWebNews.right;
		pt.y = g_rcWebNews.bottom;
		ScreenToClient(hWnd, &pt);
		g_rcWebNews.right  = pt.x;
		g_rcWebNews.bottom = pt.y;
	}
	else
	{
		g_rcWebNews.left = -999;
	}

	return(TRUE);
}


// EOF...
