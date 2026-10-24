#pragma once
#include "ltlink.h"
class LTObject;
class PGRecord {
public:
    LTLink m_Link, m_Link0c;
    LTObject* m_pObject;
    void* m_pLeaf;
};
