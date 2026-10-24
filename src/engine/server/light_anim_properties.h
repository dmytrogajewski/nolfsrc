#pragma once
#include "world_lighting.h"
#include "iltlightanim.h"
void la_GetInfo(LightAnim*,LAInfo*);
LTBOOL la_CompareInfo(LightAnim*,LAInfo*,uint32*);
void la_SetInfo(LightAnim*,LAInfo*);
int la_QuantizedVecDiffers(LTVector*,LTVector*,double);
