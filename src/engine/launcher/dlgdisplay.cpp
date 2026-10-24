// *********************************************************************** //
//
//	MODULE:		DLGDISPLAY (.CPP)
//
//	PURPOSE:	The display-mode tree behind the DISPLAY dialog.  Every
//				*.REN in the game directory is a renderer DLL; asking it
//				for GetSupportedModes() yields a flat RMode list, which is
//				folded into one CCard per video card and one CDisplayMode
//				per resolution under it.
//
//				Shipped NOLF.exe v1.003: 0x401e10 - 0x403859.
//
// *********************************************************************** //

#include "stdafx.h"
#include "io.h"
#include "stdio.h"
#include "regmgr.h"
#include "launcher.h"


// *********************************************************************** //
//
//	ROUTINE:	CDisplayMode::Init   [0x401e10]
//
// *********************************************************************** //

BOOL CDisplayMode::Init(int nWidth, int nHeight, int nDepth)
{
	m_nWidth  = nWidth;
	m_nHeight = nHeight;
	m_nDepth  = nDepth;

	m_csName.Format("%i x %i x %i", nWidth, nHeight, nDepth);

	return(TRUE);
}


// *********************************************************************** //
//
//	ROUTINE:	CDisplayMode::GetDisplayName   [0x401e50]
//
// *********************************************************************** //

CString CDisplayMode::GetDisplayName()
{
	return(GetName());
}


// *********************************************************************** //
//
//	ROUTINE:	CCard::Init   [0x401e70]
//
// *********************************************************************** //

BOOL CCard::Init(const char* sName, const char* sDesc, int bHardware)
{
	m_csName    = sName;
	m_csDesc    = sDesc;
	m_bHardware = bHardware;

	return(TRUE);
}


// *********************************************************************** //
//
//	ROUTINE:	CCard::Term   [0x401ea0]
//
// *********************************************************************** //

void CCard::Term()
{
	POSITION pos = m_modes.GetHeadPosition();
	while (pos)
	{
		CDisplayMode* pMode = (CDisplayMode*)m_modes.GetNext(pos);
		if (pMode)
		{
			delete pMode;
		}
	}

	m_modes.RemoveAll();

	m_csName.Empty();

	m_bHardware = 0;
	m_pCurMode  = NULL;
}


// *********************************************************************** //
//
//	ROUTINE:	CCard::GetDisplayName   [0x401ef0]
//
//	PURPOSE:	"<internal name> (<friendly description>)"
//
// *********************************************************************** //

CString CCard::GetDisplayName()
{
	CString csName;
	csName.Format("%s (%s)", GetName(), GetDesc());

	return(csName);
}


// *********************************************************************** //
//
//	ROUTINE:	GetListSelData   [0x401fc0]
//
//	PURPOSE:	Item data of a list box's current selection, 0 if there
//				isn't one
//
// *********************************************************************** //

LRESULT CDisplayModes::GetListSelData(HWND hList)
{
	LRESULT nIndex;
	LRESULT nData;

	if (hList &&
		(nIndex = SendMessage(hList, LB_GETCURSEL, 0, 0)) != LB_ERR &&
		(nData = SendMessage(hList, LB_GETITEMDATA, nIndex, 0)) != LB_ERR)
	{
		return(nData);
	}

	return(0);
}


// *********************************************************************** //
//
//	ROUTINE:	CCard::AddMode   [0x402000]
//
// *********************************************************************** //

CDisplayMode* CCard::AddMode(int nWidth, int nHeight, int nDepth)
{
	CDisplayMode* pMode = new CDisplayMode;

	if (!pMode->Init(nWidth, nHeight, nDepth))
	{
		delete pMode;
		return(NULL);
	}

	m_modes.AddTail(pMode);

	return(pMode);
}


// *********************************************************************** //
//
//	ROUTINE:	CCard::FindMode   [0x402080]
//
// *********************************************************************** //

CDisplayMode* CCard::FindMode(int nWidth, int nHeight, int nDepth)
{
	POSITION pos = m_modes.GetHeadPosition();
	while (pos)
	{
		CDisplayMode* pMode = (CDisplayMode*)m_modes.GetNext(pos);

		if (pMode->m_nWidth == nWidth && pMode->m_nHeight == nHeight &&
			pMode->m_nDepth == nDepth)
		{
			return(pMode);
		}
	}

	return(NULL);
}


// *********************************************************************** //
//
//	ROUTINE:	CCard::SetCurMode   [0x4020c0]
//
// *********************************************************************** //

BOOL CCard::SetCurMode(int nWidth, int nHeight, int nDepth)
{
	CDisplayMode* pMode = FindMode(nWidth, nHeight, nDepth);
	if (!pMode) return(FALSE);

	m_pCurMode = pMode;

	return(TRUE);
}


// *********************************************************************** //
//
//	ROUTINE:	CCard::SelectCurMode   [0x4020f0]
//
//	PURPOSE:	Moves the list box selection to m_pCurMode
//
// *********************************************************************** //

BOOL CCard::SelectCurMode(HWND hList)
{
	if (!hList) return(FALSE);
	if (!m_pCurMode) return(FALSE);

	int nCount = SendMessage(hList, LB_GETCOUNT, 0, 0);
	if (nCount <= 0) return(FALSE);

	for (int i = 0; i < nCount; i++)
	{
		LRESULT nData = SendMessage(hList, LB_GETITEMDATA, i, 0);
		if (nData == LB_ERR) continue;

		if (nData == (LRESULT)m_pCurMode)
		{
			SendMessage(hList, LB_SETCURSEL, i, 0);
			return(TRUE);
		}
	}

	return(FALSE);
}


// *********************************************************************** //
//
//	ROUTINE:	CCard::FillModeList   [0x402170]
//
// *********************************************************************** //

BOOL CCard::FillModeList(HWND hList)
{
	if (!hList) return(FALSE);

	SendMessage(hList, LB_RESETCONTENT, 0, 0);

	POSITION pos = m_modes.GetHeadPosition();
	while (pos)
	{
		CDisplayMode* pMode = (CDisplayMode*)m_modes.GetNext(pos);

		int nIndex = SendMessage(hList, LB_ADDSTRING, 0,
			(LPARAM)(const char*)pMode->GetDisplayName());

		if (nIndex != LB_ERR)
		{
			SendMessage(hList, LB_SETITEMDATA, nIndex, (LPARAM)pMode);
		}
	}

	if (!SelectCurMode(hList))
	{
		SendMessage(hList, LB_SETCURSEL, 0, 0);
	}

	return(TRUE);
}


// *********************************************************************** //
//
//	The modern launcher's modes (MODERN.md, M-P1-07 and M-L-01): every mode
//	of at least 640x480, wide screens included -- what the game's own
//	Display options list -- in 32 bits, or in 16 where the card offers
//	that size in nothing else; and, for a card the config names no mode
//	for, the largest 4:3 one that fits the desktop (the game was made for
//	4:3; wide modes are there to be picked).
//
// *********************************************************************** //

static BOOL ModernHasDeeperMode(RMode* pModes, RMode* pMode)
{
	for (RMode* p = pModes; p; p = p->m_pNext)
	{
		if (p->m_BitDepth > pMode->m_BitDepth && p->m_Width == pMode->m_Width &&
			p->m_Height == pMode->m_Height &&
			strcmp(p->m_InternalName, pMode->m_InternalName) == 0)
		{
			return(TRUE);
		}
	}

	return(FALSE);
}

// M-P1-17: on a Steam Deck (Steam sets SteamDeck=1, and Proton passes it
// on) the panel's own 1280x800, wide as it is, or failing that the largest
// mode that fits it: what the desktop reports there (gamescope's) can be
// far larger than the screen.
static BOOL ModernIsSteamDeck()
{
	char sValue[8];
	DWORD n = GetEnvironmentVariable("SteamDeck", sValue, sizeof(sValue));
	return(n > 0 && n < sizeof(sValue) && strcmp(sValue, "1") == 0);
}

static CDisplayMode* ModernDeckMode(CCard* pCard)
{
	CDisplayMode* pBest = NULL;
	for (POSITION pos = pCard->m_modes.GetHeadPosition(); pos; )
	{
		CDisplayMode* pMode = (CDisplayMode*)pCard->m_modes.GetNext(pos);
		if (!pMode || pMode->m_nWidth > 1280 || pMode->m_nHeight > 800) continue;
		if (!pBest || pMode->m_nWidth * pMode->m_nHeight > pBest->m_nWidth * pBest->m_nHeight ||
			(pMode->m_nWidth == pBest->m_nWidth && pMode->m_nHeight == pBest->m_nHeight &&
			 pMode->m_nDepth > pBest->m_nDepth))
		{
			pBest = pMode;
		}
	}
	return(pBest);
}

static CDisplayMode* ModernDefaultMode(CCard* pCard)
{
	if (ModernIsSteamDeck())
	{
		CDisplayMode* pDeck = ModernDeckMode(pCard);
		if (pDeck) return(pDeck);
	}

	int nMaxW = GetSystemMetrics(SM_CXSCREEN);
	int nMaxH = GetSystemMetrics(SM_CYSCREEN);
	CDisplayMode* pBest = NULL;		// the largest 4:3 mode that fits
	CDisplayMode* pAny  = NULL;		// failing that, the largest that fits

	for (POSITION pos = pCard->m_modes.GetHeadPosition(); pos; )
	{
		CDisplayMode* pMode = (CDisplayMode*)pCard->m_modes.GetNext(pos);
		if (!pMode) continue;
		if (nMaxW > 0 && nMaxH > 0 && (pMode->m_nWidth > nMaxW || pMode->m_nHeight > nMaxH)) continue;

		CDisplayMode** ppBest = (pMode->m_nWidth * 3 == pMode->m_nHeight * 4) ? &pBest : &pAny;
		CDisplayMode* p = *ppBest;
		if (!p || pMode->m_nHeight > p->m_nHeight ||
			(pMode->m_nHeight == p->m_nHeight && pMode->m_nWidth > p->m_nWidth) ||
			(pMode->m_nHeight == p->m_nHeight && pMode->m_nWidth == p->m_nWidth && pMode->m_nDepth > p->m_nDepth))
		{
			*ppBest = pMode;
		}
	}

	return(pBest ? pBest : pAny);
}

// *********************************************************************** //
//
//	ROUTINE:	CRenderer::Init   [0x402220]
//
//	PURPOSE:	Loads one *.REN, reads its friendly name out of string 5,
//				and folds its RMode list into cards and modes.  Only 4:3
//				modes of at least 640x480 are kept
//
// *********************************************************************** //

BOOL CRenderer::Init(const char* sDll)
{
	m_csDll = sDll;

	UINT nOldMode = SetErrorMode(SEM_NOOPENFILEERRORBOX);
	HMODULE hModule = LoadLibrary(sDll);
	SetErrorMode(nOldMode);

	if (!hModule) return(FALSE);

	FARPROC pfnGetSupportedModes = GetProcAddress(hModule, "GetSupportedModes");
	if (!pfnGetSupportedModes)
	{
		FreeLibrary(hModule);
		return(FALSE);
	}

	FARPROC pfnFreeModeList = GetProcAddress(hModule, "FreeModeList");
	if (!pfnFreeModeList)
	{
		FreeLibrary(hModule);
		return(FALSE);
	}

	char sDesc[256];
	sDesc[0] = '\0';
	if (LoadString(hModule, RDLL_DESCRIPTION_STRINGID, sDesc, 255) > 0)
	{
		m_csDesc = sDesc;
	}

	RMode* pModes = ((RMode* (*)())pfnGetSupportedModes)();
	if (!pModes)
	{
		FreeLibrary(hModule);
		return(FALSE);
	}

	RMode* pMode = pModes;
	do
	{
		if (pMode->m_Width >= 640 && pMode->m_Height >= 480 &&
			(pMode->m_BitDepth >= 32 || !ModernHasDeeperMode(pModes, pMode)))
		{
			CCard* pCard = FindCard(pMode);
			if (!pCard)
			{
				pCard = AddCard(pMode);
			}

			if (pCard)
			{
				pCard->AddMode(pMode->m_Width, pMode->m_Height, pMode->m_BitDepth);
			}
		}

		pMode = pMode->m_pNext;
	}
	while (pMode);

	((void (*)(RMode*))pfnFreeModeList)(pModes);

	FreeLibrary(hModule);

	return(TRUE);
}


// *********************************************************************** //
//
//	ROUTINE:	CRenderer::Term   [0x4023a0]
//
// *********************************************************************** //

void CRenderer::Term()
{
	POSITION pos = m_cards.GetHeadPosition();
	while (pos)
	{
		CCard* pCard = (CCard*)m_cards.GetNext(pos);
		if (pCard)
		{
			delete pCard;
		}
	}

	m_cards.RemoveAll();

	m_csDll.Empty();
	m_csDesc.Empty();

	m_pCurCard = NULL;
}


// *********************************************************************** //
//
//	ROUTINE:	CRenderer::AddCard   [0x4024d0]
//
// *********************************************************************** //

CCard* CRenderer::AddCard(const char* sName, const char* sDesc, int bHardware)
{
	if (!sName || !sName[0] || !sDesc || !sDesc[0]) return(NULL);

	CCard* pCard = new CCard;

	if (!pCard->Init(sName, sDesc, bHardware))
	{
		delete pCard;
		return(NULL);
	}

	m_cards.AddTail(pCard);

	return(pCard);
}


// *********************************************************************** //
//
//	ROUTINE:	CRenderer::AddCard   [0x4025f0]
//
// *********************************************************************** //

CCard* CRenderer::AddCard(RMode* pMode)
{
	if (!pMode) return(NULL);

	return(AddCard(pMode->m_InternalName, pMode->m_Description, pMode->m_bHardware));
}


// *********************************************************************** //
//
//	ROUTINE:	CRenderer::FindCard   [0x402620]
//
// *********************************************************************** //

CCard* CRenderer::FindCard(const char* sName)
{
	POSITION pos = m_cards.GetHeadPosition();
	while (pos)
	{
		CCard* pCard = (CCard*)m_cards.GetNext(pos);

		if (strcmp(pCard->GetName(), sName) == 0)
		{
			return(pCard);
		}
	}

	return(NULL);
}


// *********************************************************************** //
//
//	ROUTINE:	CRenderer::FindCard   [0x4026a0]
//
// *********************************************************************** //

CCard* CRenderer::FindCard(RMode* pMode)
{
	if (!pMode) return(NULL);

	return(FindCard(pMode->m_InternalName));
}


// *********************************************************************** //
//
//	ROUTINE:	CRenderer::SetCurCard   [0x4026c0]
//
// *********************************************************************** //

BOOL CRenderer::SetCurCard(const char* sName)
{
	CCard* pCard = FindCard(sName);
	if (!pCard) return(FALSE);

	m_pCurCard = pCard;

	return(TRUE);
}


// *********************************************************************** //
//
//	ROUTINE:	CDisplayMode::~CDisplayMode   [0x403720]
//
// *********************************************************************** //

// (defined in the class body so VC6 inlines it at the delete sites)


// *********************************************************************** //
//
//	ROUTINE:	CDisplayMode::GetName   [0x403740]
//
// *********************************************************************** //

CString CDisplayMode::GetName()
{
	return(m_csName);
}


// *********************************************************************** //
//
//	ROUTINE:	CCard::~CCard   [0x403760]
//
// *********************************************************************** //

// (defined in the class body so VC6 inlines it at the delete sites)


// *********************************************************************** //
//
//	ROUTINE:	CRenderer::~CRenderer   [0x4037d0]
//
// *********************************************************************** //

// (defined in the class body so VC6 inlines it at the delete sites)


// *********************************************************************** //
//
//	ROUTINE:	CCard::GetName   [0x406e30, folded with CPreview::GetName]
//
// *********************************************************************** //

CString CCard::GetName()
{
	return(m_csName);
}


// *********************************************************************** //
//
//	ROUTINE:	CCard::GetDesc   [0x403840]
//
// *********************************************************************** //

CString CCard::GetDesc()
{
	return(m_csDesc);
}


// *********************************************************************** //
//
//	ROUTINE:	CRenderer::GetListName   [0x4023f0]
//
//	PURPOSE:	As GetDisplayName, but a renderer with no friendly string
//				in its DLL falls back to the DLL name alone
//
// *********************************************************************** //

CString CRenderer::GetListName()
{
	CString csName;

	if (m_csDesc.IsEmpty())
	{
		csName = m_csDll;
	}
	else
	{
		csName.Format("%s (%s)", GetName(), GetDesc());
	}

	return(csName);
}


// *********************************************************************** //
//
//	ROUTINE:	CRenderer::SelectCurCard   [0x4026f0]
//
// *********************************************************************** //

BOOL CRenderer::SelectCurCard(HWND hList)
{
	if (!hList) return(FALSE);
	if (!m_pCurCard) return(FALSE);

	int nCount = SendMessage(hList, LB_GETCOUNT, 0, 0);
	if (nCount <= 0) return(FALSE);

	for (int i = 0; i < nCount; i++)
	{
		LRESULT nData = SendMessage(hList, LB_GETITEMDATA, i, 0);
		if (nData == LB_ERR) continue;

		if (nData == (LRESULT)m_pCurCard)
		{
			SendMessage(hList, LB_SETCURSEL, i, 0);
			return(TRUE);
		}
	}

	return(FALSE);
}


// *********************************************************************** //
//
//	ROUTINE:	CRenderer::FillCardList   [0x402770]
//
// *********************************************************************** //

BOOL CRenderer::FillCardList(HWND hList)
{
	if (!hList) return(FALSE);

	SendMessage(hList, LB_RESETCONTENT, 0, 0);

	POSITION pos = m_cards.GetHeadPosition();
	while (pos)
	{
		CCard* pCard = (CCard*)m_cards.GetNext(pos);

		int nIndex = SendMessage(hList, LB_ADDSTRING, 0,
			(LPARAM)(const char*)pCard->GetDisplayName());

		if (nIndex != LB_ERR)
		{
			SendMessage(hList, LB_SETITEMDATA, nIndex, (LPARAM)pCard);
		}
	}

	if (!SelectCurCard(hList))
	{
		int nCount = SendMessage(hList, LB_GETCOUNT, 0, 0);
		if (nCount > 0 && SendMessage(hList, LB_SETCURSEL, nCount - 1, 0) == LB_ERR)
		{
			SendMessage(hList, LB_SETCURSEL, 0, 0);
		}
	}

	return(TRUE);
}


// *********************************************************************** //
//
//	ROUTINE:	CDisplayModes::Init   [0x402840]
//
// *********************************************************************** //

BOOL CDisplayModes::Init(const char* sDir, const char* sFile)
{
	m_csRenderDll = sDir;

	CWaitCursor wait;

	if (FindRenderers(sDir) < 0)
	{
		return(FALSE);
	}

	SetDefaultModes();

	ReadDisplayConfig(sFile);

	return(TRUE);
}


// *********************************************************************** //
//
//	ROUTINE:	CDisplayModes::Term   [0x4028f0]
//
// *********************************************************************** //

void CDisplayModes::Term()
{
	POSITION pos = m_renderers.GetHeadPosition();
	while (pos)
	{
		CRenderer* pRenderer = (CRenderer*)m_renderers.GetNext(pos);
		if (pRenderer)
		{
			delete pRenderer;
		}
	}

	m_renderers.RemoveAll();

	m_pCurRenderer = NULL;
}


// *********************************************************************** //
//
//	ROUTINE:	CDisplayModes::AddRenderer   [0x402930]
//
// *********************************************************************** //

CRenderer* CDisplayModes::AddRenderer(const char* sDll)
{
	if (!sDll) return(NULL);

	CRenderer* pRenderer = new CRenderer;

	if (!pRenderer->Init(sDll))
	{
		delete pRenderer;
		return(NULL);
	}

	m_renderers.AddTail(pRenderer);

	return(pRenderer);
}


// *********************************************************************** //
//
//	ROUTINE:	CDisplayModes::FindRenderer   [0x402a60]
//
// *********************************************************************** //

CRenderer* CDisplayModes::FindRenderer(const char* sDll)
{
	if (!sDll) return(NULL);
	// as the engine: d3d.ren is d3d9.ren in the modern build
	if (_stricmp(sDll, "d3d.ren") == 0) sDll = "d3d9.ren";

	POSITION pos = m_renderers.GetHeadPosition();
	while (pos)
	{
		CRenderer* pRenderer = (CRenderer*)m_renderers.GetNext(pos);
		if (pRenderer)
		{
			if (strcmp(sDll, pRenderer->GetName()) == 0)
			{
				return(pRenderer);
			}
		}
	}

	return(NULL);
}


// *********************************************************************** //
//
//	ROUTINE:	CDisplayModes::FillRendererList   [0x402ae0]
//
// *********************************************************************** //

BOOL CDisplayModes::FillRendererList(HWND hList)
{
	if (!hList) return(FALSE);

	SendMessage(hList, LB_RESETCONTENT, 0, 0);

	POSITION pos = m_renderers.GetHeadPosition();
	while (pos)
	{
		CRenderer* pRenderer = (CRenderer*)m_renderers.GetNext(pos);

		int nIndex = SendMessage(hList, LB_ADDSTRING, 0,
			(LPARAM)(const char*)pRenderer->GetListName());

		if (nIndex != LB_ERR)
		{
			SendMessage(hList, LB_SETITEMDATA, nIndex, (LPARAM)pRenderer);
		}
	}

	if (!SelectCurRenderer(hList))
	{
		SendMessage(hList, LB_SETCURSEL, 0, 0);
	}

	return(TRUE);
}


// *********************************************************************** //
//
//	ROUTINE:	CDisplayModes::SelectCurRenderer   [0x402b90]
//
// *********************************************************************** //

BOOL CDisplayModes::SelectCurRenderer(HWND hList)
{
	if (!hList) return(FALSE);
	if (!m_pCurRenderer) return(FALSE);

	int nCount = SendMessage(hList, LB_GETCOUNT, 0, 0);
	if (nCount <= 0) return(FALSE);

	for (int i = 0; i < nCount; i++)
	{
		LRESULT nData = SendMessage(hList, LB_GETITEMDATA, i, 0);
		if (nData == LB_ERR) continue;

		if (nData == (LRESULT)m_pCurRenderer)
		{
			SendMessage(hList, LB_SETCURSEL, i, 0);
			return(TRUE);
		}
	}

	return(FALSE);
}


// *********************************************************************** //
//
//	ROUTINE:	CDisplayModes::UpdateLists   [0x403220]
//
//	PURPOSE:	Reacts to a selection change in any of the three lists.
//				A change higher up refills everything below it
//
//	ONE changed-flag serves both levels (shipped homes it in hRenderer's
//	dead slot and tests the same ebp for the card and the mode); separate
//	bRendererChanged/bCardChanged flags cost 4 bytes.
//
// *********************************************************************** //

BOOL CDisplayModes::UpdateLists(HWND hRenderer, HWND hCard, HWND hMode)
{
	if (!hRenderer) return(FALSE);
	if (!hCard) return(FALSE);
	if (!hMode) return(FALSE);

	BOOL bChanged = FALSE;

	CRenderer* pCurRenderer = m_pCurRenderer;
	CRenderer* pRenderer = (CRenderer*)GetListSelData(hRenderer);
	if (pCurRenderer != pRenderer)
	{
		m_pCurRenderer = pRenderer;

		if (!SelectCurRenderer(hRenderer)) return(FALSE);

		if (pRenderer)
		{
			pRenderer->FillCardList(hCard);
		}

		bChanged = TRUE;
	}

	if (!pRenderer) return(FALSE);

	CCard* pCurCard = pRenderer->m_pCurCard;
	CCard* pCard = (CCard*)pRenderer->GetListSelData(hCard);
	if (pCurCard != pCard || bChanged)
	{
		pRenderer->m_pCurCard = pCard;

		if (!pRenderer->SelectCurCard(hCard)) return(FALSE);

		if (pCard)
		{
			pCard->FillModeList(hMode);
		}

		bChanged = TRUE;
	}

	if (!pCard) return(FALSE);

	CDisplayMode* pCurMode = pCard->m_pCurMode;
	CDisplayMode* pMode = (CDisplayMode*)pCard->GetListSelData(hMode);
	if (pCurMode != pMode || bChanged)
	{
		pCard->m_pCurMode = pMode;
	}

	return(TRUE);
}


// *********************************************************************** //
//
//	ROUTINE:	CCard::GetListSelData   [0x401fc0, folded]
//
// *********************************************************************** //

LRESULT CCard::GetListSelData(HWND hList)
{
	LRESULT nIndex;
	LRESULT nData;

	if (hList &&
		(nIndex = SendMessage(hList, LB_GETCURSEL, 0, 0)) != LB_ERR &&
		(nData = SendMessage(hList, LB_GETITEMDATA, nIndex, 0)) != LB_ERR)
	{
		return(nData);
	}

	return(0);
}


// *********************************************************************** //
//
//	ROUTINE:	CRenderer::GetListSelData   [0x401fc0, folded]
//
// *********************************************************************** //

LRESULT CRenderer::GetListSelData(HWND hList)
{
	LRESULT nIndex;
	LRESULT nData;

	if (hList &&
		(nIndex = SendMessage(hList, LB_GETCURSEL, 0, 0)) != LB_ERR &&
		(nData = SendMessage(hList, LB_GETITEMDATA, nIndex, 0)) != LB_ERR)
	{
		return(nData);
	}

	return(0);
}


// *********************************************************************** //
//
//	ROUTINE:	CRenderer::GetName   [0x406e30, folded]
//
// *********************************************************************** //

CString CRenderer::GetName()
{
	return(m_csDll);
}


// *********************************************************************** //
//
//	ROUTINE:	CRenderer::GetDesc   [0x403840, folded]
//
// *********************************************************************** //

CString CRenderer::GetDesc()
{
	return(m_csDesc);
}


// *********************************************************************** //
//
//	ROUTINE:	CDisplayModes::FindRenderers   [0x402c10]
//
//	PURPOSE:	One CRenderer per *.REN in sDir.  Returns how many loaded
//
//	The BOOL loop flag is real (the shipped `setne %al; test %eax,%eax`
//	on entry), and is CLEARED by an if in the body rather than reassigned
//	from the compare: `bMore = (_findnext(...) != -1)` folds it away.
//
// *********************************************************************** //

int CDisplayModes::FindRenderers(const char* sDir)
{
	char sSpec[256];
	struct _finddata_t fileinfo;

	if (!sDir || strlen(sDir) == 0)
	{
		strcpy(sSpec, "*.REN");
	}
	else if (sDir[strlen(sDir) - 1] != '\\')
	{
		sprintf(sSpec, "%s\\*.REN", sDir);
	}
	else
	{
		sprintf(sSpec, "%s*.REN", sDir);
	}

	int nCount = 0;

	long hFind = _findfirst(sSpec, &fileinfo);

	BOOL bMore = (hFind != -1);
	while (bMore)
	{
		if (AddRenderer(fileinfo.name))
		{
			nCount++;
		}

		if (_findnext(hFind, &fileinfo) == -1) bMore = FALSE;
	}

	return(nCount);
}


// *********************************************************************** //
//
//	ROUTINE:	CDisplayModes::FillLists   [0x403340]
//
//	PURPOSE:	Cascades the three list boxes: renderers, then the cards of
//				the selected renderer, then that card's modes
//
// *********************************************************************** //

BOOL CDisplayModes::FillLists(HWND hRenderer, HWND hCard, HWND hMode)
{
	if (!hRenderer) return(FALSE);
	if (!hCard) return(FALSE);
	if (!hMode) return(FALSE);

	FillRendererList(hRenderer);

	CRenderer* pRenderer = (CRenderer*)GetListSelData(hRenderer);
	if (!pRenderer) return(FALSE);

	pRenderer->FillCardList(hCard);

	CCard* pCard = (CCard*)pRenderer->GetListSelData(hCard);
	if (!pCard) return(FALSE);

	pCard->FillModeList(hMode);

	return(TRUE);
}


// *********************************************************************** //
//
//	ROUTINE:	CDisplayModes::SetDefaultModes   [0x4035e0]
//
//	PURPOSE:	Gives every card that hasn't got one a starting mode.
//				Hardware cards aim high, software rasterizers low
//
//	NEAR (291 vs 309).  The software branch has to come first: with
//	`if (m_bHardware)` leading, the merged tail call lands after three
//	FindMode calls instead of five and the whole layout shifts.  With the
//	polarity right, every call site sits at the same relative offset and
//	what is left is six bytes: SP3 still knows what it stored into m_pos
//	and SP6 reloads it, once per loop.
//
// *********************************************************************** //

void CDisplayModes::SetDefaultModes()
{
	m_pos = m_renderers.GetHeadPosition();
	if (m_pos)
	{
	  do
	  {
		CRenderer* pRenderer = (CRenderer*)m_renderers.GetNext(m_pos);
		if (!pRenderer) return;

		pRenderer->m_pos = pRenderer->m_cards.GetHeadPosition();
		if (pRenderer->m_pos)
		{
		  do
		  {
			CCard* pCard = (CCard*)pRenderer->m_cards.GetNext(pRenderer->m_pos);
			if (!pCard) break;

			if (!pCard->m_pCurMode)
			{
				CDisplayMode* pMode;

				if (!pCard->m_bHardware)
				{
					pMode = pCard->FindMode(320, 200, 16);
					if (!pMode) pMode = pCard->FindMode(320, 240, 16);
					if (!pMode) pMode = pCard->FindMode(512, 384, 16);
					if (!pMode) pMode = pCard->FindMode(640, 400, 16);
					if (!pMode) pMode = pCard->FindMode(640, 480, 16);
				}
				else
				{
					pMode = ModernDefaultMode(pCard);
					if (!pMode)
					pMode = pCard->FindMode(640, 480, 16);
					if (!pMode) pMode = pCard->FindMode(512, 384, 16);
					if (!pMode) pMode = pCard->FindMode(800, 600, 16);
				}

				pCard->m_pCurMode = pMode;
			}
		  }
		  while (pRenderer->m_pos);
		}
	  }
	  while (m_pos);
	}
}


// *********************************************************************** //
//
//	ROUTINE:	CDisplayModes::GetCommands   [0x4033d0]
//
//	PURPOSE:	The console-variable command line the current selection
//				implies
//
//	The three mode fields are named locals, read depth-first after
//	csMode is constructed: that is what puts the mode in eax (not the
//	card's esi) and loads depth/height/width into ecx/edx/eax as shipped.
//	Passing pMode->m_nWidth etc. straight to Format reuses esi.
//
// *********************************************************************** //

CString CDisplayModes::GetCommands()
{
	if (!m_pCurRenderer) return(CString(""));

	CString csRenderDll = m_pCurRenderer->GetName();
	if (csRenderDll.IsEmpty()) return(CString(""));

	CCard* pCard = m_pCurRenderer->m_pCurCard;
	if (!pCard) return(CString(""));

	CString csDesc = pCard->GetDesc();
	if (csDesc.IsEmpty()) return(CString(""));

	CString csName = pCard->GetName();

	CString csCommands;

	csCommands  = "++RenderDll ";
	csCommands += csRenderDll;
	csCommands += " ++CardDesc ";
	csCommands += csName;
	csCommands += " ++Renderer ";
	csCommands += "\"";
	csCommands += csDesc;
	csCommands += "\"";

	CDisplayMode* pMode = pCard->m_pCurMode;
	if (pMode)
	{
		CString csMode;
		int nDepth = pMode->m_nDepth;
		int nHeight = pMode->m_nHeight;
		int nWidth = pMode->m_nWidth;
		csMode.Format(" ++screenwidth %i ++screenheight %i ++bitdepth %i",
			nWidth, nHeight, nDepth);

		csCommands += csMode;
	}

	return(csCommands);
}


// *********************************************************************** //
//
//	ROUTINE:	DisplayDlgProc   [0x405b10]
//
//	PURPOSE:	Dialog procedure for the DISPLAY dialog - three cascading
//				list boxes over the renderer/card/mode tree
//
// *********************************************************************** //

BOOL CALLBACK DisplayDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
		case WM_INITDIALOG:
		{
			HICON hIcon = LoadIcon(g_hInst, "ICON");
			if (hIcon)
			{
				SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
			}

			if (g_pDisplayModes)
			{
				g_pDisplayModes->FillLists(GetDlgItem(hWnd, 1034),
					GetDlgItem(hWnd, 1032), GetDlgItem(hWnd, 1033));

				g_pDisplayModes->UpdateLists(GetDlgItem(hWnd, 1034),
					GetDlgItem(hWnd, 1032), GetDlgItem(hWnd, 1033));
			}

			return(TRUE);
		}

		case WM_COMMAND:
		{
			if (wParam == 1)
			{
				if (g_pDisplayModes)
				{
					g_pDisplayModes->UpdateLists(GetDlgItem(hWnd, 1034),
						GetDlgItem(hWnd, 1032), GetDlgItem(hWnd, 1033));
				}

				EndDialog(hWnd, 1);
				return(TRUE);
			}

			if (wParam == 2)
			{
				if (g_pDisplayModes)
				{
					g_pDisplayModes->m_pCurRenderer = NULL;
				}

				EndDialog(hWnd, 0);
				return(TRUE);
			}

			if (HIWORD(wParam) == LBN_SELCHANGE)
			{
				if (g_pDisplayModes)
				{
					g_pDisplayModes->UpdateLists(GetDlgItem(hWnd, 1034),
						GetDlgItem(hWnd, 1032), GetDlgItem(hWnd, 1033));
				}
			}

			return(TRUE);
		}
	}

	return(FALSE);
}


// *********************************************************************** //
//
//	ROUTINE:	CDisplayModes::ReadDisplayConfig   [0x402cf0]
//
//	PURPOSE:	Restores the saved renderer, card and mode by reading the
//				console variables back out of autoexec.cfg
//
// *********************************************************************** //

BOOL CDisplayModes::ReadDisplayConfig(const char* sFile)
{
	if (!sFile) return(FALSE);

	OFSTRUCT of;
	if (OpenFile(sFile, &of, OF_EXIST) == HFILE_ERROR) return(FALSE);

	FILE* pFile = fopen(sFile, "r");
	if (!pFile) return(FALSE);

	char sLine[512];
	char sRenderer[128];
	char sRenderDll[128];
	char sCardDesc[128];
	char sWidth[64];
	char sHeight[64];
	char sBitDepth[64];

	BOOL bRenderDll = FALSE;
	BOOL bRenderer  = FALSE;
	BOOL bCardDesc  = FALSE;
	BOOL bWidth     = FALSE;
	BOOL bHeight    = FALSE;
	BOOL bBitDepth  = FALSE;

	strcpy(sWidth,    "640");
	strcpy(sHeight,   "480");
	strcpy(sBitDepth, "16");

	while (fgets(sLine, 512, pFile))
	{
		char* pPos = sLine;
		if (sLine[0] == '"')
		{
			pPos = &sLine[1];
		}

		if (strnicmp(pPos, "RenderDll", 9) == 0)
		{
			pPos += 9;
			while (*pPos == '"' || *pPos == ' ') pPos++;

			strcpy(sRenderDll, pPos);

			char* pEnd = &sRenderDll[strlen(sRenderDll) - 1];
			while ((*pEnd == '"' || *pEnd == '\n') && pEnd > sRenderDll) pEnd--;
			pEnd[1] = '\0';

			bRenderDll = TRUE;
		}
		else if (strnicmp(pPos, "Renderer", 8) == 0)
		{
			pPos += 8;
			while (*pPos == '"' || *pPos == ' ') pPos++;

			strcpy(sRenderer, pPos);

			char* pEnd = &sRenderer[strlen(sRenderer) - 1];
			while ((*pEnd == '"' || *pEnd == '\n') && pEnd > sRenderer) pEnd--;
			pEnd[1] = '\0';

			bRenderer = TRUE;
		}
		else if (strnicmp(pPos, "CardDesc", 8) == 0)
		{
			pPos += 8;
			while (*pPos == '"' || *pPos == ' ') pPos++;

			strcpy(sCardDesc, pPos);

			char* pEnd = &sCardDesc[strlen(sCardDesc) - 1];
			while ((*pEnd == '"' || *pEnd == '\n') && pEnd > sCardDesc) pEnd--;
			pEnd[1] = '\0';

			bCardDesc = TRUE;
		}
		else if (strnicmp(pPos, "ScreenWidth", 11) == 0)
		{
			pPos += 11;
			while (*pPos == '"' || *pPos == ' ') pPos++;

			strcpy(sWidth, pPos);

			char* pEnd = &sWidth[strlen(sWidth) - 1];
			while ((*pEnd == '"' || *pEnd == '\n') && pEnd > sWidth) pEnd--;
			pEnd[1] = '\0';

			bWidth = TRUE;
		}
		else if (strnicmp(pPos, "ScreenHeight", 12) == 0)
		{
			pPos += 12;
			while (*pPos == '"' || *pPos == ' ') pPos++;

			strcpy(sHeight, pPos);

			char* pEnd = &sHeight[strlen(sHeight) - 1];
			while ((*pEnd == '"' || *pEnd == '\n') && pEnd > sHeight) pEnd--;
			pEnd[1] = '\0';

			bHeight = TRUE;
		}
		else if (strnicmp(pPos, "BitDepth", 8) == 0)
		{
			pPos += 8;
			while (*pPos == '"' || *pPos == ' ') pPos++;

			strcpy(sBitDepth, pPos);

			char* pEnd = &sBitDepth[strlen(sBitDepth) - 1];
			while ((*pEnd == '"' || *pEnd == '\n') && pEnd > sBitDepth) pEnd--;
			pEnd[1] = '\0';

			bBitDepth = TRUE;
		}

		if (bRenderDll && bRenderer && bCardDesc && bWidth && bHeight && bBitDepth)
		{
			break;
		}
	}

	if (bRenderDll)
	{
		m_pCurRenderer = FindRenderer(sRenderDll);

		if (m_pCurRenderer)
		{
			if (bCardDesc)
			{
				m_pCurRenderer->SetCurCard(sCardDesc);
			}

			CCard* pCard = m_pCurRenderer->m_pCurCard;
			if (pCard)
			{
				pCard->SetCurMode(atoi(sWidth), atoi(sHeight), atoi(sBitDepth));
			}
		}
	}

	fclose(pFile);

	return(TRUE);
}


// EOF...
