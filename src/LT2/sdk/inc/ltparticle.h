#pragma once
#include "ltbasetypes.h"

	// The particle structure for particle systems.
	struct LTParticle
	{
		LTVector m_Velocity;
		LTVector m_Color;	// Particle colors are 0-255.
		float	 m_Alpha;	// 0-1.
		float	 m_Size;	// Particle size
		LTParticle *m_pNext;
	};

	
	// The line structure for line system.
	struct LTLinePt
	{
		LTVector	m_Pos;
		float		r, g, b, a;	// Values 0-1.
	};

	struct LTLine
	{
		LTLinePt	m_Points[2];
	};

