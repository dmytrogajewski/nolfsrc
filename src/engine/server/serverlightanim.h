// *********************************************************************** //
//  MODULE:   SERVERLIGHTANIM (.H)
//  PURPOSE:  ServerLightAnimLT — the server's ILTLightAnim, at
//            0x1002df1e-0x1002e071. Light animations are baked by the
//            preprocessor into one array on the server; the interface is
//            three bounds-checked accessors over it.
// *********************************************************************** //
#ifndef __SERVERLIGHTANIM_H__
#define __SERVERLIGHTANIM_H__

#include "iltlightanim.h"
#include "light_anim_properties.h"
#include "servermgr.h"

// 0x1002896d — find a client's queued change for one animation, by animation
// id, handing back its index. Returns 1 if it found one and 0 if it did not,
// and writes the index only on a hit. The compare is sixteen bits wide on
// both sides (`mov (%eax),%si; cmp 0xc(%esp),%si`), so the id parameter is a
// uint16 and not the uint32 HLIGHTANIM the caller holds (name ours).
int la_FindClientChange(class Client* pClient, uint32 hLightAnim, uint32* pnIndex);

// 0x100289a2 — tell the clients a light animation changed.
void sm_LightAnimChanged(CServerMgr* pServerMgr, HLIGHTANIM hLightAnim, uint32 dwChangeFlags);


class ServerLightAnimLT : public ILTLightAnim
{
public:
	// In-class: CLTServer's constructor expands it (0x1002d3f4).
	ServerLightAnimLT(CServerMgr* pServerMgr) { m_pServerMgr = pServerMgr; }

	LTRESULT FindLightAnim(const char* pName, HLIGHTANIM& hLightAnim);
	LTRESULT GetNumFrames(HLIGHTANIM hLightAnim, uint32& nFrames);
	LTRESULT GetLightAnimInfo(HLIGHTANIM hLightAnim, LAInfo& info);
	LTRESULT SetLightAnimInfo(HLIGHTANIM hLightAnim, LAInfo& info);

public:
	CServerMgr* m_pServerMgr;                // 0x04 (0x00 is the vtable)
};

#endif
