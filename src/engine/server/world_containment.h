#pragma once
#include "world_node.h"
int bsp_PointInside(BSPNode* root, LTVector* point);
int bsp_SphereInside(BSPNode* root, LTVector* point, float radius);
