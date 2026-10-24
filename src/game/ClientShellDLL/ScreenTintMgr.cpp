// ----------------------------------------------------------------------- //
//
// MODULE  : ScreenTintMgr.cpp
//
// PURPOSE : Implementation of ScreenTintMgr class
//
// CREATED : 02/02/00
//
// (c) 2000 Monolith Productions, Inc.  All Rights Reserved
//
// ----------------------------------------------------------------------- //

#include "Stdafx.h"
#include "ScreenTintMgr.h"
#include "GameClientShell.h"
extern CGameClientShell* g_pGameClientShell;
// M-P1-10: Intel graphics fix
#include "cshell/p1_modern.h"


CScreenTintMgr::CScreenTintMgr()
{
	for (int i = 0; i < NUM_TINT_EFFECTS; i++)
	{
		m_avTints[i].Init(0.0f,0.0f,0.0f);
	}
    m_bChanged = LTFALSE;
	// M-P1-10: the overlay tint
	m_bP1Overlay = LTFALSE;
	m_vP1Tint.Init(0.0f,0.0f,0.0f);
	m_hP1Surface = LTNULL;
	m_bP1SurfaceOld = LTTRUE;
}

CScreenTintMgr::~CScreenTintMgr()
{
	// M-P1-10: the overlay tint
	if (m_hP1Surface && g_pLTClient)
		g_pLTClient->DeleteSurface(m_hP1Surface);
}

void CScreenTintMgr::Update()
{
	// M-P1-10: switching between the camera tint and the overlay redoes it
	LTBOOL bP1Overlay = P1Config_ScreenTinting() ? LTFALSE : LTTRUE;
	if (bP1Overlay != m_bP1Overlay)
	{
		m_bP1Overlay = bP1Overlay;
		m_bChanged = LTTRUE;
	}
	if (!m_bChanged)
		return;

    LTVector vTemp;
	vTemp.Init(0.0f,0.0f,0.0f);
	for (int i = 0; i < NUM_TINT_EFFECTS; i++)
	{
		vTemp.x = Max(vTemp.x,m_avTints[i].x);
		vTemp.y = Max(vTemp.y,m_avTints[i].y);
		vTemp.z = Max(vTemp.z,m_avTints[i].z);
	}

	if (vTemp.x > 1.0f)
		vTemp.x = 1.0f;
	if (vTemp.y > 1.0f)
		vTemp.y = 1.0f;
	if (vTemp.z > 1.0f)
		vTemp.z = 1.0f;
    m_bChanged = LTFALSE;

	// M-P1-10: as an overlay, P1DrawOverlay draws it and the camera has none
	if (m_vP1Tint.x != vTemp.x || m_vP1Tint.y != vTemp.y || m_vP1Tint.z != vTemp.z)
	{
		m_vP1Tint = vTemp;
		m_bP1SurfaceOld = LTTRUE;
	}
	if (m_bP1Overlay)
	{
		vTemp.Init(0.0f,0.0f,0.0f);
	}
	HLOCALOBJ hCamera = g_pGameClientShell->GetCamera();
    g_pLTClient->SetCameraLightAdd(hCamera, &vTemp);

}

void CScreenTintMgr::Set(eTintEffect eEffect, LTVector *pvColor)
{
    m_bChanged = LTTRUE;
	VEC_COPY(m_avTints[eEffect],*pvColor);
}

void CScreenTintMgr::Clear(eTintEffect eEffect)
{
    m_bChanged = LTTRUE;
	VEC_SET(m_avTints[eEffect],0.0f,0.0f,0.0f);
}

void CScreenTintMgr::ClearAll()
{
	for (int i = 0; i < NUM_TINT_EFFECTS; i++)
	{
		Clear((eTintEffect)i);
	}
}
// ----------------------------------------------------------------------- //
//
//	M-P1-10: the screen tint as a 2D overlay
//
//	Some Intel graphics drivers never show the camera's light add, so
//	with EnableScreenTinting 0 the tint is added over the rendered scene
//	instead: a tiny surface of the tint colour, scaled over the screen
//	with additive blending (small, so that drawing it costs little).
//
// ----------------------------------------------------------------------- //

void CScreenTintMgr::P1DrawOverlay()
{
	if (!m_bP1Overlay)
		return;

	// nothing to add
	if (m_vP1Tint.x < 1.0f/255.0f && m_vP1Tint.y < 1.0f/255.0f && m_vP1Tint.z < 1.0f/255.0f)
		return;

	if (!m_hP1Surface)
	{
		m_hP1Surface = g_pLTClient->CreateSurface(4, 4);
		if (!m_hP1Surface)
			return;
		m_bP1SurfaceOld = LTTRUE;
	}

	if (m_bP1SurfaceOld)
	{
		LTRect rcFill(0, 0, 4, 4);
		HLTCOLOR hColor = SETRGB((uint8)(m_vP1Tint.x * 255.0f), (uint8)(m_vP1Tint.y * 255.0f), (uint8)(m_vP1Tint.z * 255.0f));
		g_pLTClient->FillRect(m_hP1Surface, &rcFill, hColor);
		g_pLTClient->SetSurfaceAlpha(m_hP1Surface, 1.0f);
		g_pLTClient->OptimizeSurface(m_hP1Surface, LTNULL);
		m_bP1SurfaceOld = LTFALSE;
	}

	HSURFACE hScreen = g_pLTClient->GetScreenSurface();
	uint32 nWidth = 0, nHeight = 0;
	g_pLTClient->GetSurfaceDims(hScreen, &nWidth, &nHeight);

	LTRect rcDest(0, 0, (int)nWidth, (int)nHeight);
	LTRect rcSrc(0, 0, 4, 4);

	g_pLTClient->SetOptimized2DBlend(LTSURFACEBLEND_ADD);
	g_pLTClient->ScaleSurfaceToSurface(hScreen, m_hP1Surface, &rcDest, &rcSrc);
	g_pLTClient->SetOptimized2DBlend(LTSURFACEBLEND_ALPHA);
}

void CScreenTintMgr::P1Invalidate()
{
	m_bP1SurfaceOld = LTTRUE;
}
