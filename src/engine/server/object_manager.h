#pragma once
#include "ltobject_internal.h"
#include "structbank.h"
#include <array>
#include <memory>
#include <unordered_map>

class CObjectBankBase {
public:
    virtual ~CObjectBankBase() = default;
    virtual LTObject* Allocate() = 0;
    virtual void Free(LTObject*) = 0;
    virtual void Init() = 0;
};

// Shared by object allocation, attachments, physics and the server. All banks
// use sizeof/alignof their native records, and lists belong to this manager.
class CUnknown390 {
public:
    CUnknown390();
    ~CUnknown390();
    CUnknown390(const CUnknown390&) = delete;
    CUnknown390& operator=(const CUnknown390&) = delete;
    int Lock();
    int GetLockCount();
    int32 m_LockCount = -1;
    LTLink m_Link;
    StructBank m_Bank14, m_Bank30, m_Bank4c;
    std::array<std::unique_ptr<CObjectBankBase>, NUM_OBJECTTYPES> m_Banks;
    CObjectBankBase* m_pBanks[NUM_OBJECTTYPES]{};
    LTList m_Lists[NUM_OBJECTTYPES];
    std::unordered_map<LTObject*,uint8> m_Allocated;
    std::unordered_map<Attachment*,LTObject*> m_Attachments;
};

int un390_Init(CUnknown390*, int dedicated);
int un390_Term(CUnknown390*);
LTRESULT om_CreateObject(CUnknown390*, ObjectCreateStruct*, LTObject**);
LTRESULT un390_RemoveObject(CUnknown390*, LTObject*);
LTRESULT am_CreateAttachment(CUnknown390*, LTObject*, LTObject* child, int node,
                             LTVector*, LTRotation*, Attachment**);
LTRESULT am_RemoveAttachment(CUnknown390*, LTObject*, Attachment*);
Attachment* am_FindAttachment(CUnknown390*, HATTACHMENT);
void am_DetachObject(CUnknown390*, LTObject*);
bool am_VisitChildren(CUnknown390*, LTObject*, bool (*visitor)(void*,LTObject&), void* context);
