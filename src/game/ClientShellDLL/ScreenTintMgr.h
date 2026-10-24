// ----------------------------------------------------------------------- //
//
// MODULE  : ScreenTintMgr.h
//
// PURPOSE : Definition of ScreenTintMgr class
//
// CREATED : 02/02/00
//
// (c) 2000 Monolith Productions, Inc.  All Rights Reserved
//
// ----------------------------------------------------------------------- //

#ifndef __SCREENTINT_H
#define __SCREENTINT_H

#include "ltbasedefs.h"

enum eTintEffect
{
	TINT_STUN = 0,
	TINT_POISON,
	TINT_BURN,
	TINT_ELECTROCUTE,
	TINT_NIGHTVISION,
	TINT_MINES,
	TINT_INK,
	TINT_SCREEN_FLASH,
	TINT_CONTAINER,
	NUM_TINT_EFFECTS
};

class CScreenTintMgr
{
public:
	CScreenTintMgr();
	~CScreenTintMgr();

	void Update();

    void Set(eTintEffect eEffect, LTVector *pvColor);
	void Clear(eTintEffect eEffect);

	void ClearAll();

	// M-P1-10: with EnableScreenTinting 0 the tint is drawn over the screen
	void P1DrawOverlay();		// after the scene, before the interface
	void P1Invalidate();		// the renderer was (re)initialised
private:
    LTVector m_avTints[NUM_TINT_EFFECTS];
    LTBOOL   m_bChanged;
	// M-P1-10: the overlay tint
	LTBOOL   m_bP1Overlay;		// drawing the tint as an overlay
	LTVector m_vP1Tint;			// the tint, 0..1 per channel
	HSURFACE m_hP1Surface;		// a small surface of that colour
	LTBOOL   m_bP1SurfaceOld;	// it needs refilling
};


#endif // __SCREENTINT_H