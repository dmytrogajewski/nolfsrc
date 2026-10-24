// mfcs_rect.cpp - implementation for the MFC CRect class

#include "stdafx.h"
#include "mfcs_rect.h"

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

// CRect constructors

CRect::CRect()
{
}

CRect::CRect(const RECT &cRect)
{
	*(RECT *)this = cRect;
}

CRect::CRect(const RECT *pRect)
{
	*(RECT *)this = *pRect;
}

CRect::CRect(SDWORD l, SDWORD t, SDWORD r, SDWORD b) 
{
	left = l;
	top = t;
	right = r;
	bottom = b;
}

CRect::CRect(POINT topLeft, POINT bottomRight)
{
	left = topLeft.x;
	top = topLeft.y;
	right = bottomRight.x;
	bottom = bottomRight.y;
}


// CRect Functions

void CRect::SetRect(SDWORD l, SDWORD t, SDWORD r, SDWORD b)
{ 
	left = l; 
	top = t; 
	right = r; 
	bottom = b; 
}
 
// Note : this only works correctly if both rectangles are normalized..
BOOL CRect::IntersectRect(const RECT *lpRect1, const RECT *lpRect2)
{
	// Make sure they intersect
	if ((lpRect1->top > lpRect2->bottom) ||
		(lpRect2->top > lpRect1->bottom) ||
		(lpRect1->left > lpRect2->right) ||
		(lpRect2->left > lpRect1->right))
		return FALSE;

	top = max(lpRect1->top, lpRect2->top);
	left = max(lpRect1->left, lpRect2->left);
	bottom = min(lpRect1->bottom, lpRect2->bottom);
	right = min(lpRect1->right, lpRect2->right);

	return TRUE;
}

CPoint CRect::TopLeft() const
{
	return CPoint(left, top);
}

CPoint CRect::BottomRight() const
{
	return CPoint(right, bottom);
}

void CRect::NormalizeRect()
{
	SDWORD temp;
	if (left > right)
	{
		temp = right;
		right = left;
		left = temp;
	}
	if (top > bottom)
	{
		temp = bottom;
		bottom = top;
		top = temp;
	}
}

