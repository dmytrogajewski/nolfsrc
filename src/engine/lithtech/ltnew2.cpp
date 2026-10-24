// *********************************************************************** //
//
//  MODULE:   LTNEW2 (.CPP)  -- scratch file, NOT a drop-in TU
//
//  PURPOSE:  Thirteen tiny functions out of a 25-address range. The
//            other twelve rows are reported at the bottom of this file: six are mid-function jump targets, four are
//            compiler-generated `.CRT$XC` dynamic initialisers, and two are
//            ~200-byte functions whose enumerated size was a mis-measure.
//
//  HOME:     none of these thirteen belongs together. They fall into four
//            homes, named in each banner. Nothing here is shared with
//            src/engine/server: every one of the thirteen was checked
//            against the retail server.dll and none of the bodies is
//            present there (only the neighbouring 0x00491990 is, at
//            0x1003e050 -- see the CNode notes).
//
//  GROUND TRUTH: the retail lithtech.exe, cl 8447 /O2.
//
// *********************************************************************** //

#include <stdlib.h>          // atoi, atof, free

#include "ltbasedefs.h"      // LTRESULT, LTVector, LTransform, uint32, int32
#include "ltcodes.h"         // LT_INVALIDPARAMS
#include "de_error.h"        // RETURN_ERROR_NAMED


// ======================================================================= //
//  1. CLTClient / ILTCSBase -- one accessor off the client interface vtable
//
//  WHAT PINS THE CLASS: the vtable at 0x004a14e0 is CLTClient's; the
//  constructor at 0x00404ba0 writes it to (%esi) at 0x00404c64 and the
//  destructor at 0x00407c00 writes it back. Counting slots from 0 against
//  iltcsbase.h + iltclient.h in declaration order lands slot 47 on
//  FreeString (0x00407a70, one handle, forwards, no result), 48/49 on
//  CompareStrings / CompareStringsUpper (0x00407a80 / 0x00407ab0, two
//  handles, identical shape, different helper), 51 on GetVarValueFloat
//  (0x00407b00, `flds 0x18(%eax)`), 52 on GetVarValueString (`mov
//  0x14(%eax)`), 53/54 on GetTime/GetFrameTime (0x00407b40/0x00407b50,
//  `flds 0x125c/0x1258` off the client manager). Nine anchors in a row, so
//  slot 50 at 0x004a15a8 -> 0x00407ae0 is GetStringData and nothing else.
// ======================================================================= //

extern char  g_LTEmptyString[];              // 0x004b0ab4, four zero bytes in .data
extern char* sub_00484a20(HSTRING hString);  // 0x00484a20, HSTRING -> char*

class CLTClient_Strings
{
public:
	char* GetStringData(HSTRING hString);    // 0x00407ae0
};

// WHAT PINS IT
//   * `ret $0x4` with %ecx never read -> __thiscall member, one stack arg,
//     `this` unused. A __cdecl free function would be a bare `ret`.
//   * `test %eax,%eax; je 0x407af4` puts the NON-null arm first in source:
//     the null arm is the fall-through at the far end. `if (!h) return
//     g_LTEmptyString; return sub(h);` would invert the je into a jne.
//   * `mov $0x4b0ab4,%eax` is the ADDRESS of the datum, not a load from it
//     (b8 imm32, not a1 imm32) -> the empty string is an array, not a
//     `char*` variable. Declaring it `extern char* g_pEmpty` gives `a1`.
char* CLTClient_Strings::GetStringData(HSTRING hString)
{
	if (hString)
		return sub_00484a20(hString);
	else
		return g_LTEmptyString;
}


// ======================================================================= //
//  2. CLTTransform::GetPos (0x004896b0) -- NOT HERE any more.  Its home is
//  src/engine/lithtech/transform.cpp, which defines the whole of
//  CLTTransform, this body included; both copies compiled into lithtech.exe
//  (LNK2005), so this one is gone.
// ======================================================================= //


// ======================================================================= //
//  3. The client's ILTLightAnim -- SetLightAnimInfo
//
//  WHAT PINS THE CLASS: CLTClient's constructor writes vtable 0x004a16b8
//  to this+0x3a8 and the client manager to this+0x3ac (0x00404c21), then
//  stores &this[0x3a8] into m_pLightAnimLT at this+0x18 (0x00404ca5).
//  iltlightanim.h declares exactly four virtuals -- FindLightAnim,
//  GetNumFrames, GetLightAnimInfo, SetLightAnimInfo -- and the vtable is
//  exactly four slots wide (0x004a16b8..0x004a16c4, the next vptr constant
//  the image writes anywhere is 0x004a16c8). Slot 3 is 0x004065f0. The
//  neighbours agree: slot 1 (0x00406510) range-checks the index against
//  m_pClientMgr->[0x0c] and indexes a 96-byte stride; slot 2 (0x00406580)
//  does the same range check and then copies the entry -- and the helper
//  our slot forwards to, 0x00404af0, opens with that identical range check
//  against [0x0c] and the identical `lea (%eax,%eax,2); shl $5` stride.
//  So m_pClientMgr lives at subobject+0x04, i.e. right after the vptr.
// ======================================================================= //

class LAInfo;

// serverlightanim.cpp / servermgr.h's LightAnim, cut to what the two
// range-checked slots below reach: 96 bytes (`lea (%ecx,%ecx,2); shl $5`)
// with the frame count at +0x28 (0x0040656e `mov 0x28(%eax,%ecx,1)`).
class LightAnim
{
public:
	uint8    m_pad_00[0x28];
	uint32   m_nFrames;                      // 0x28
	uint8    m_pad_2c[0x60 - 0x2c];
};

// 0x00435230 -- serverlightanim.cpp's la_GetInfo, __cdecl (pAnim, pInfo).
void la_GetInfo(LightAnim* pAnim, LAInfo* pInfo);

// CClientMgr as far as ClientLightAnimLT reaches it: the light-anim array
// at +0x08 and its count at +0x0c (the server's m_pLightAnims /
// m_nLightAnims, same pair).
class CClientMgr
{
public:
	uint8       m_pad_00[0x08];
	LightAnim*  m_pLightAnims;               // 0x08
	uint32      m_nLightAnims;               // 0x0c
	// 0x00425580, thiscall (entry): tell the renderer an entry changed.
	void        LightAnimChanged(LightAnim* pAnim);
};

// 0x00404af0, __cdecl: (pClientMgr, index, pInfo, bGet). Range-checks index
// against pClientMgr->[0x0c], then works on pClientMgr->[0x08] + index*96.
extern LTRESULT sub_00404af0(CClientMgr *pClientMgr, uint32 index,
                             LAInfo *pInfo, int bGet);

class CLTLightAnimClient
{
public:
	LTRESULT GetNumFrames(uint32 hLightAnim, uint32 &nFrames);   // 0x00406510
	LTRESULT GetLightAnimInfo(uint32 hLightAnim, LAInfo &info);  // 0x00406580
	LTRESULT SetLightAnimInfo(uint32 hLightAnim, LAInfo &info);  // 0x004065f0

public:
	void*        m_pVTable;      // 0x00
	CClientMgr*  m_pClientMgr;   // 0x04  (written by the CLTClient ctor)
};

// 0x00406510, slot 1 -- the client twin of serverlightanim.cpp's
// ServerLightAnimLT::GetNumFrames: the out-param cleared FIRST (0x00406515,
// before the range check), the name through a static pointer (0x004a7a0c
// -> "ClientLightAnimLT::GetNumFrames"), debug level 2.
LTRESULT CLTLightAnimClient::GetNumFrames(uint32 hLightAnim, uint32 &nFrames)
{
	nFrames = 0;

	if (hLightAnim >= m_pClientMgr->m_nLightAnims)
		RETURN_ERROR_NAMED(2, "ClientLightAnimLT::GetNumFrames", LT_INVALIDPARAMS);

	nFrames = m_pClientMgr->m_pLightAnims[hLightAnim].m_nFrames;
	return LT_OK;
}

// 0x00406580, slot 2 -- the twin of ServerLightAnimLT::GetLightAnimInfo
// (name pointer 0x004a7a34 -> "ClientLightAnimLT::GetLightAnimInfo"); the
// copy is la_GetInfo, 0x00435230, __cdecl (entry, &info).
LTRESULT CLTLightAnimClient::GetLightAnimInfo(uint32 hLightAnim, LAInfo &info)
{
	if (hLightAnim >= m_pClientMgr->m_nLightAnims)
		RETURN_ERROR_NAMED(2, "ClientLightAnimLT::GetLightAnimInfo", LT_INVALIDPARAMS);

	la_GetInfo(&m_pClientMgr->m_pLightAnims[hLightAnim], &info);
	return LT_OK;
}

// WHAT PINS IT
//   * `ret $0x8` -> member, two args. `mov 0x4(%ecx),%eax` is the only read
//     of `this`: m_pClientMgr at +0x04.
//   * four pushes then ONE `add $0x10,%esp` -> a single __cdecl call with
//     four arguments, not two calls.
//   * the arguments are loaded right-to-left (info, then hLightAnim, then
//     the member) and pushed 0 / info / hLightAnim / m_pClientMgr, so the
//     helper's parameter order is (mgr, handle, info, 0).
//   * the call's %eax is not touched before `ret` -> `return helper(...)`,
//     not `helper(...); return LT_OK;` (that would be an `xor %eax,%eax`).
LTRESULT CLTLightAnimClient::SetLightAnimInfo(uint32 hLightAnim, LAInfo &info)
{
	return sub_00404af0(m_pClientMgr, hLightAnim, &info, 0);
}

// 0x00404af0, 151 bytes -- the helper itself: the client twin of
// serverlightanim.cpp's ServerLightAnimLT::SetLightAnimInfo. Its error
// name pointer (0x004a77a0 -> "ClientLightAnimLT::SetLightAnimInfo") sits in
// the client file manager's literal pool, so its home TU is the one after
// clientwin.cpp's run, not this file; written here beside its one caller.
//   * the fourth argument short-circuits the comparison (`mov $1,%edi`),
//     otherwise la_CompareInfo (0x00435360) with the change flags in the
//     dead index slot (`lea 0x18(%esp)` == &hLightAnim's slot);
//   * la_SetInfo (0x00435290) unconditionally, then a thiscall on the
//     client manager (0x00425580) with the entry when anything changed.
LTBOOL la_CompareInfo(LightAnim* pAnim, LAInfo* pInfo, uint32* pChangeFlags);  // 0x00435360
void   la_SetInfo(LightAnim* pAnim, LAInfo* pInfo);                            // 0x00435290

LTRESULT sub_00404af0(CClientMgr *pClientMgr, uint32 index, LAInfo *pInfo, int bForce)
{
	LightAnim* pAnim;
	LTBOOL     bChanged;
	uint32     dwChangeFlags;

	if (index >= pClientMgr->m_nLightAnims)
		RETURN_ERROR_NAMED(2, "ClientLightAnimLT::SetLightAnimInfo", LT_INVALIDPARAMS);

	pAnim = &pClientMgr->m_pLightAnims[index];

	if (bForce)
		bChanged = LTTRUE;
	else
		bChanged = la_CompareInfo(pAnim, pInfo, &dwChangeFlags);

	la_SetInfo(pAnim, pInfo);

	if (bChanged)
		pClientMgr->LightAnimChanged(pAnim);

	return LT_OK;
}


// ======================================================================= //
//  4. Command-line lookup wrapper (0x00402f70)
//
//  HOME: the client's dsi/startup file. The worker it wraps, 0x00402ec0,
//  sprintf's "%s%s" of "-" and the name into a 256-byte buffer, walks the
//  argument vector comparing each entry against BOTH spellings with
//  0x00490cd0, and on a hit returns the NEXT entry -- or g_LTEmptyString
//  (the same 0x004b0ab4 as GetStringData above) when the hit is the last
//  entry, or NULL when there is no hit. Sole caller: 0x00403a2b, which
//  passes "launch" and then loads "launch.dll".
// ======================================================================= //

// clientwin.cpp's argument vector and count, which it defines.
extern char** g_pCluArgList;   // 0x004b0ab8 -- loaded as a value, so a pointer
extern int    g_nCluArgCount;  // 0x004b13e8

// 0x00490cd0 -- case-insensitive string EQUALITY (1 when equal; see
// clientinput.cpp's sub_490cd0).  __cdecl, `add $0x8,%esp` after it.
int sub_00490cd0(const char* pStr1, const char* pStr2);

// 0x004929c4 -- the CRT's sprintf.
extern "C" int sprintf(char* pBuffer, const char* pFormat, ...);

// 0x00402ec0, 161 bytes, __cdecl (pName, argc, argv) -- argc is the second
// parameter (%ebx, the `test`/`cmp` loop bound), argv the third (%edi,
// dereferenced and stepped by 4).
//   * sprintf(buf, "%s%s", "-", pName) into the 0x100-byte frame.
//   * each entry is tested against the bare name FIRST, then the dashed
//     one; either hit jumps to the shared answer block (0x00402f36).
//   * `dec %ebx; cmp %ebx,%esi; jge` -> `if (i < argc - 1)` answers
//     argv[i + 1] (reloaded from the parameter), else g_LTEmptyString.
char* sub_00402ec0(char *pName, int argc, char **argv)
{
	char szDashed[256];
	int  i;

	sprintf(szDashed, "%s%s", "-", pName);

	for (i = 0; i < argc; i++)
	{
		if (sub_00490cd0(argv[i], pName) || sub_00490cd0(argv[i], szDashed))
		{
			if (i < argc - 1)
				return argv[i + 1];

			return g_LTEmptyString;
		}
	}

	return NULL;
}

// WHAT PINS IT
//   * bare `ret` after `add $0xc,%esp` -> __cdecl free function, one arg.
//   * the two globals are loaded BEFORE the stack argument and pushed
//     first: %eax=*(0x4b0ab8) is pushed first so it is the LAST parameter
//     (argv), %ecx=*(0x4b13e8) second so it is the middle one (argc).
//     Swapping them in the source swaps the two `mov`s.
//   * both globals are LOADED (`a1`/`8b 0d`), not addressed -- so argv is a
//     `char**` variable, not a `char*[]` array.
char* sub_00402f70(char *pArgName)
{
	return sub_00402ec0(pArgName, g_nCluArgCount, g_pCluArgList);
}


// ======================================================================= //
//  5. Guarded forward (0x004041d0)
//
//  HOME: the same client startup/interface file as (4) -- it sits 0x20
//  bytes past the deleting destructor at 0x004041b0. Sole caller is
//  0x0047bb43, which reaches it as `pClientMgr->[0x1534]` after the
//  function-pointer slot at 0x004b48d0[n] came back NULL.
// ======================================================================= //

struct CHookRecordQ17;
class  CMsgQ17;

struct SUnknown4041D0
{
	char            m_Pad00[0xE80];
	CHookRecordQ17* m_pUnknownE80;    // 0xe80, the queue 0x0042f740 makes
};

void sub_0042f7e0(CHookRecordQ17 *pRecord, CMsgQ17 *pPacket);  // 0x0042f7e0, below

// WHAT PINS IT
//   * bare `ret` after `add $0x8,%esp` -> __cdecl, two args, void.
//   * ONE test and ONE exit: `if (p) f(p->m_pUnknownE80, pArg);` with the
//     guard on the first parameter only. The second is never tested.
//   * the second argument is loaded into %ecx and pushed first, the member
//     into %edx and pushed second -> right-to-left, two parameters.
void sub_004041d0(SUnknown4041D0 *p, CMsgQ17 *pArg)
{
	if (p)
		sub_0042f7e0(p->m_pUnknownE80, pArg);
}


// ======================================================================= //
//  5b. The server-file list handler (0x0042f7e0, 320 bytes)
//
//  HOME: clientdsi.cpp -- it sits between the hook record's user-pointer
//  get/set (0x0042f7c0 / 0x0042f7d0) and the dispatch 0x0042f920, all
//  three of which clientdsi.cpp's "single registered hook" block owns.
//  Written here because (5) above is its only caller (0x004041e4).
//
//  WHAT IT DOES. Checks the incoming packet is id 50 (low six bits of
//  m_pData[0]), opens a 0x1000 write message, and for up to 5000 records
//  reads (uint16 id, uint32 n, string) and hands them to the record's first
//  callback -- clientwin.cpp's clu_fm_OnServerFile. Each id is echoed into
//  the reply, with 0x8000 set when the callback refused it; a reply that
//  has under four bytes left is stamped 0x36 and sent through the record's
//  net manager (+0x1c) on its connection (+0x20), flags 0x80, then reset.
//  Whatever is left over is sent after the loop.
//
//  The three packet readers and the writer are CALLED (0x0042f950 uint16,
//  0x0042f9d0 uint32, 0x0042fa50 WriteType<uint16>, 0x0045c280 the string),
//  so they are declared out of line on the view, as cchandler.cpp does.
// ======================================================================= //

class CMsgQ17
{
public:
	virtual void _p00()=0;
	virtual void AddRef()=0;                 // slot 1
	virtual void Release()=0;                // slot 2

	uint16 ReadTypeOOL(uint16* dummy);       // 0x0042f950
	uint32 ReadTypeOOL(uint32* dummy);       // 0x0042f9d0
	void   WriteTypeOOL(uint16 val);         // 0x0042fa50
	char*  ReadString();                     // 0x0045c280

	// serverlightanim.cpp's CMsgSpaceView::GetSpaceLeft, same expansion
	// (`sub $7; js; cmp $4; jae`).
	uint32 GetSpaceLeft()
	{
		int nLeft;

		nLeft = m_MaxDataLen - m_Pos - 7;

		if (nLeft < 0)
			nLeft = 0;

		return nLeft;
	}

	uint8   m_pad_04[0x34 - 0x04];
	uint16  m_DataLen;                       // 0x34
	uint16  m_MaxDataLen;                    // 0x36
	uint16  m_Pos;                           // 0x38
	uint8   m_pad_3a[0x54 - 0x3a];
	uint8*  m_pData;                         // 0x54
};

// clientshell.cpp's CMsgRef: the copy AddRefs, the destructor Releases.
class CMsgRefQ17
{
public:
	CMsgRefQ17(const CMsgRefQ17& other)
	{
		m_pMsg = other.m_pMsg;

		if (m_pMsg)
			m_pMsg->AddRef();
	}

	~CMsgRefQ17()
	{
		if (m_pMsg)
			m_pMsg->Release();
	}

	CMsgQ17* m_pMsg;
};

// clientshell.cpp's CMsgHold: built from the pointer the temporary
// carries, so the local is AddRef'd BEFORE the temporary's Release
// (0x0042f822 then 0x0042f833) -- an elided copy has neither.
class CMsgHoldQ17
{
public:
	CMsgHoldQ17(CMsgQ17* pMsg)
	{
		m_pMsg = pMsg;

		if (m_pMsg)
			m_pMsg->AddRef();
	}

	~CMsgHoldQ17()
	{
		if (m_pMsg)
			m_pMsg->Release();
	}

	CMsgQ17* m_pMsg;
};

// 0x0045c080 -- returns the holder by value (clientshell.cpp).
CMsgRefQ17 msg_StartWrite(uint32 nMaxSize, uint32 nSize);

class CNetMgrQ17
{
public:
	void SendTo(CMsgQ17* pMsg, uint32 nConnID, uint32 nFlags);  // 0x00456360
};

// clientdsi.cpp's CHookRecord with clientwin.cpp's descriptor names.
struct CHookRecordQ17
{
	void*        m_pUnknown00;               // 0x00
	uint16       m_nUnknown04;               // 0x04
	uint16       m_nUnknown06;               // 0x06
	int        (*m_pfnOnServerFile)(CHookRecordQ17* pRecord, char* pName,
	                                uint32 nUnknown, uint32 nID);  // 0x08
	void*        m_pfn04;                    // 0x0c
	void*        m_pfn08;                    // 0x10
	void*        m_pfn0c;                    // 0x14
	void*        m_pfn10;                    // 0x18
	CNetMgrQ17*  m_pNetMgr;                  // 0x1c
	uint32       m_nConnID;                  // 0x20
	void*        m_pUser;                    // 0x24
};

void sub_0042f7e0(CHookRecordQ17 *pRecord, CMsgQ17 *pPacket)
{
	if (pRecord && (pPacket->m_pData[0] & 0x3f) == 50)
	{
		CMsgHoldQ17 cMsg(msg_StartWrite(0x1000, 0x1000).m_pMsg);
		uint32     i;

		for (i = 0; i < 5000; i++)
		{
			uint32 nID;
			uint32 nUnknown;
			char*  pName;

			if (!(pPacket->m_DataLen - pPacket->m_Pos))
				break;

			nID      = pPacket->ReadTypeOOL((uint16*)0);
			nUnknown = pPacket->ReadTypeOOL((uint32*)0);
			pName    = pPacket->ReadString();

			if (!pRecord->m_pfnOnServerFile(pRecord, pName, nUnknown, nID))
			{
				nID |= 0x8000;
			}

			if (cMsg.m_pMsg->GetSpaceLeft() < 4)
			{
				cMsg.m_pMsg->m_pData[0] = 0x36;
				pRecord->m_pNetMgr->SendTo(cMsg.m_pMsg, pRecord->m_nConnID, 0x80);
				cMsg.m_pMsg->m_DataLen = cMsg.m_pMsg->m_Pos = 1;
			}

			cMsg.m_pMsg->WriteTypeOOL((uint16)nID);
		}

		if (cMsg.m_pMsg->m_DataLen - 1 > 0)
		{
			cMsg.m_pMsg->m_pData[0] = 0x36;
			pRecord->m_pNetMgr->SendTo(cMsg.m_pMsg, pRecord->m_nConnID, 0x80);
		}
	}
}


// ======================================================================= //
//  6. Object-bank reset (0x00404a00), vtable 0x004a14a8 slot 3
//
//  WHAT PINS THE CLASS: 0x004a14a8 is not part of a 12-slot table; it is
//  three 4-slot vtables laid back to back, and the image proves the seams
//  by writing all three as vptr immediates --
//      0x004a14a8 (0x00404a60) 0x004a14b8 (0x00404a20) 0x004a14c8 (0x004041b0)
//  0x004a14c8 is the base: its slots 1..3 are all 0x004928e7 (_purecall).
//  0x004a14a8 and 0x004a14b8 are two derived classes that share slots 1
//  and 2 (0x004049d0, 0x004049c0) and differ only in slot 3 -- ours,
//  0x00404a00, against 0x00444820. Slot 0 of each is the scalar deleting
//  destructor: 0x00404a60 rewrites the vptr to 0x004a14a8 then to the base
//  0x004a14c8 and calls free under the `test $0x1,%al` flag.
//
//  The bank at this+0x04: 0x00490b50 writes m_Unknown00 = size, m_Unknown04
//  = size rounded up to 4 (minimum 4), m_pBlockSize... = count, and zeroes
//  +0x0c..+0x18; 0x00490bd0 walks the block list at +0x14 freeing each and
//  zeroes +0x0c,+0x10,+0x14,+0x18. Init/Term of a fixed-size allocator.
// ======================================================================= //

struct SObjectBank
{
	uint32 m_Unknown00;      // 0x00  item size as passed
	uint32 m_Unknown04;      // 0x04  item size rounded up to 4, min 4
	uint32 m_Unknown08;      // 0x08  items per block
	uint32 m_Unknown0c;      // 0x0c
	uint32 m_Unknown10;      // 0x10
	void*  m_pBlocks;        // 0x14  block list, freed by 0x00490bd0
	void*  m_Unknown18;      // 0x18
};

extern void sub_00490b50(SObjectBank *pBank, int itemSize, int itemsPerBlock);
extern void sub_00490bd0(SObjectBank *pBank, int bFree);

class CClass4A14A8
{
public:
	void Reset();            // 0x00404a00

public:
	void*        m_pVTable;  // 0x00
	SObjectBank  m_Bank;     // 0x04
};

// WHAT PINS IT
//   * bare `ret` with `this` in %ecx -> __thiscall member, no args, void.
//   * `lea 0x4(%ecx),%esi` computed ONCE and reused for both calls -- the
//     same lvalue `&m_Bank` twice, not two different expressions.
//   * a SINGLE `add $0x14,%esp` after the second call cleans five dwords,
//     i.e. VC6 merged the cleanup of both __cdecl calls. That only happens
//     when the two calls are adjacent statements with nothing between.
//   * literal order 0x28 then 0x20 pushed as 0x20 first -> (bank, 0x28,
//     0x20): item size 40, 32 items per block.
void CClass4A14A8::Reset()
{
	sub_00490bd0(&m_Bank, 1);
	sub_00490b50(&m_Bank, 0x28, 0x20);
}


// ======================================================================= //
//  7. Guarded tail-call (0x0048b270), vtable 0x004a38a4 slot 4
//
//  WHAT PINS THE CLASS: 0x004a38a4 is written as a vptr immediate at
//  0x0048abf3 and 0x0048ac45, and the next such constant is 0x004a38d8 --
//  so the vtable is exactly ten slots, 0x004a38a4..0x004a38c8, and
//  0x0048b270 sits at 0x004a38b4 = index 4. Its four immediate siblings
//  are the same shape and pin the two members:
//      0x0048b290  if (m_p18 && m_p20) tail-jmp 0x0048b440; else 2
//      0x0048b2b0  if (m_p18) return sub_0048b410() ? 0 : 2; else 2
//  -- so m_p18 is the "are we up" pointer of this object and 2 is its
//  not-ready code (0 is the success code, produced by the
//  `neg/sbb/and $0x2` in 0x0048b2b0).
// ======================================================================= //

class CClass4A38A4
{
public:
	LTRESULT Slot04();       // 0x0048b270
	LTRESULT Slot04Impl();   // 0x0048b820, the tail-jump target

public:
	char   m_Pad00[0x18];    // 0x00
	void*  m_pUnknown18;     // 0x18
	char   m_Pad1c[0xa0 - 0x1c];
	uint32 m_UnknownA0;      // 0xa0
};

// WHAT PINS IT
//   * bare `ret` -> __thiscall, no args.
//   * `test %eax,%eax; je L` then `cmpl $0x1,0xa0(%ecx); jne L` -- one
//     shared failure label, reached by two conditional jumps: a
//     short-circuit `&&`. An `||` would jump the other way and duplicate
//     the exit; an if/else through a result variable would materialise 2
//     into a register before the branch.
//   * `e9 rel32` to 0x0048b820 with %ecx untouched -> a tail-jumped
//     __thiscall call with no stack arguments. VC6 only turns `return
//     f();` into `jmp f` when there is nothing to clean, which is why
//     0x00491ad0 below (one argument) keeps its `call`.
//   * `mov $0x2,%eax` is 4 bytes wide -> an int-sized return, not a bool.
LTRESULT CClass4A38A4::Slot04()
{
	if (m_pUnknown18 && m_UnknownA0 == 1)
		return Slot04Impl();

	return 2;
}


// ======================================================================= //
//  8. Virtual re-dispatch (0x0048d1f0), vtable 0x004a38d8 slot 11
//
//  WHAT PINS THE CLASS: 0x004a38d8 is written as a vptr immediate at
//  0x0048c6e7 (a constructor that then threads five 16-byte circular list
//  heads at +0x04..+0x53, each carrying a back-pointer to the object, and
//  sets +0x54 = its argument and +0x58 = -1) and again at 0x0048c720.
//  The run is fifteen slots wide, 0x004a38d8..0x004a3910, and 0x0048d1f0
//  is at 0x004a3904 = index 11. Slot 1, the one this body re-dispatches
//  to, is 0x0042d1b0.
// ======================================================================= //

class CClass4A38D8
{
public:
	virtual void   Slot00();
	virtual uint32 Slot01(void *a, void *b, void *c, void *d, void *e);
	virtual void   Slot02();
	virtual void   Slot03();
	virtual void   Slot04();
	virtual void   Slot05();
	virtual void   Slot06();
	virtual void   Slot07();
	virtual void   Slot08();
	virtual void   Slot09();
	virtual void   Slot10();
	virtual uint32 Slot11(void *a, void *b, void *c, void *d);  // 0x0048d1f0
};

// WHAT PINS IT
//   * `ret $0x10` -> __thiscall member, four stack arguments.
//   * `mov (%ecx),%edx; call *0x4(%edx)` -> a VIRTUAL call through `this`,
//     slot 1. VC6 does not devirtualise a call on `this`; had the source
//     named a non-virtual member this would be `call rel32`.
//   * five pushes for four parameters, and the first parameter is pushed
//     TWICE (`push %eax; push %eax`, no reload between them). Reading the
//     four `mov 0x10(%esp),%eax` reloads back through the growing stack
//     gives, right-to-left: d, c, b, a, a. So the callee's argument list is
//     (a, a, b, c, d) -- the same value in its first two slots.
//   * %eax is not touched between `call` and `ret` -- consistent with
//     `return`; a void call would be byte-identical (COIN-FLIP, see below).
uint32 CClass4A38D8::Slot11(void *a, void *b, void *c, void *d)
{
	return Slot01(a, a, b, c, d);
}


// ======================================================================= //
//  9-13. The INI/section-tree family at 0x00491a50..0x00492850
//
//  WHAT PINS THE FAMILY: five functions, one class plus its list member.
//  The readers above them name every field:
//
//    0x00491e70  N* Find(sec,key)  { N* p = FindSection(sec); if (!p)
//                                    return 0; return p->FindChild(key); }
//    0x00491ea0  Get(sec,key,int*) { N* p = Find(sec,key); if (!p) return 0;
//                                    N* q = p->GetFirstChild();      <-- 0x00491a90
//                                    if (!q) return 0;
//                                    return q->GetValue(pInt); }     <-- 0x00491a50
//    0x00491ee0  the same with GetValue(float*)                      <-- 0x00491a70
//
//  and 0x00441a20 walks the result with `mov (%esi),%esi` between calls to
//  0x00491a50, so the node's next pointer is at +0x00. 0x00491a90 returns
//  *(this+0x0c) -- the head of the child list -- which is the same dword
//  0x00491ad0 returns on the null-name path, so the list object's first
//  member IS the head. The two list members are 0x0c and 0x18, exactly
//  twelve bytes apart, which fixes the list at three dwords.
//
//  The unwind funclets at 0x004a0a60/0x004a0a6b destruct subobjects at
//  `this+0x08` and `this+0x0c` by jumping to 0x00492850 -- so 0x00492850
//  is a destructor, and both the value string and the child list own a
//  malloc'd block through the same one-pointer wrapper.
//
//  0x00491990, the sibling string-list search 0xc0 bytes below this run,
//  IS byte-present in server.dll at 0x1003e050 -- so the translation unit
//  is compiled into both images and /OPT:REF kept only that one function
//  on the server side. Anything added here should be treated as shared.
// ======================================================================= //

class CNode;

class CNodeList
{
public:
	CNode* Find(const char *pName);      // 0x00491af0

public:
	CNode*  m_pHead;                     // 0x00 of the list
	CNode** m_ppTail;                    // 0x04
	uint32  m_nCount;                    // 0x08
};

// The +0x08 owner of the value text (the unwind funclets destroy +0x08
// with the same one-pointer wrapper destructor, 0x00492850).
class CNodeText
{
public:
	const char* GetText() { return m_pText; }

	char* m_pText;
};

class CNode
{
public:
	int     GetValue(int32 *pVal);       // 0x00491a50
	int     GetValue(float *pVal);       // 0x00491a70
	CNode*  GetChild(const char *pName); // 0x00491ad0
	CNode*  GetSection(const char *pName); // 0x00491e50

public:
	CNode*     m_pNext;        // 0x00  stepped by `mov (%esi),%esi`
	uint32     m_Unknown04;    // 0x04
	CNodeText  m_Value;        // 0x08  the token text, atoi'd/atof'd below
	CNodeList  m_Children;     // 0x0c  head returned bare by 0x00491a90
	CNodeList  m_Sections;     // 0x18
};

// WHAT PINS IT (0x00491a50)
//   * `ret $0x4` -> member, one arg.
//   * `mov 0x8(%ecx),%ecx` then `push %ecx` -> the member is the ONLY
//     argument to the helper; 0x004937a7 is a one-argument forwarder to
//     0x0049371c (atol) -- i.e. atoi.
//   * the load goes into %ecx (overwriting `this`), not %eax: the text is
//     read through an inline accessor on the +0x08 wrapper object, which
//     makes `this+8` the accessor's `this` in %ecx.  A bare `char*` field
//     loads into %eax and pushes %eax (size-exact, one block per function;
//     the float twin additionally swaps the out-pointer to %ecx).
//   * the out-pointer is reloaded from `0x8(%esp)` AFTER the push, so it is
//     read once, after the call: `*pVal = atoi(...)`, not a temporary.
//   * `mov $0x1,%eax` is four bytes wide, so the return type is int-sized.
//     A `bool` return would be `mov $0x1,%al`.
int CNode::GetValue(int32 *pVal)
{
	*pVal = atoi(m_Value.GetText());
	return 1;
}

// WHAT PINS IT (0x00491a70)
//   * identical to the above except the helper is 0x00493515, which builds
//     an %ebp frame and returns in st(0) -- atof -- and the store is
//     `fstps (%eax)`, a 4-byte store. `fstpl` would be a double out-param;
//     the narrowing cast is in the source.
int CNode::GetValue(float *pVal)
{
	*pVal = (float)atof(m_Value.GetText());
	return 1;
}

// WHAT PINS IT (0x00491ad0)
//   * `ret $0x4` on both arms -> member, one arg.
//   * `test %eax,%eax; jne 0x491ade` puts the NULL-name arm FIRST in the
//     source and the search second. Writing `if (pName) return Find(...);
//     return m_pHead;` inverts the jne to a je and swaps the two blocks.
//   * `mov 0xc(%ecx),%eax` and `add $0xc,%ecx` address the SAME offset --
//     one member, read directly on one arm and used as `this` on the
//     other. That is an embedded object, not a pointer field (a pointer
//     would be loaded before the call, not adjusted in place).
//   * no `add $0x4,%esp` after the call -> the callee cleans, i.e.
//     0x00491af0 is itself __thiscall with one stack argument.
//   * `call` + `ret $0x4` rather than `jmp`: VC6 will not tail-jump when
//     there are stack arguments (contrast 0x0048b270 above, zero args).
CNode* CNode::GetChild(const char *pName)
{
	if (!pName)
		return m_Children.m_pHead;

	return m_Children.Find(pName);
}

// WHAT PINS IT (0x00491e50): byte-for-byte the same function on the other
// list -- 0x18 in place of 0x0c, same jne, same shared `ret $0x4`.
CNode* CNode::GetSection(const char *pName)
{
	if (!pName)
		return m_Sections.m_pHead;

	return m_Sections.Find(pName);
}


// ----------------------------------------------------------------------- //
//  0x00492850 -- the one-pointer owning wrapper's destructor
// ----------------------------------------------------------------------- //

class COwnedBlock
{
public:
	~COwnedBlock();          // 0x00492850

public:
	void* m_pData;           // 0x00
};

// WHAT PINS IT
//   * bare `ret`, `this` taken from %ecx into %esi -> __thiscall, no args,
//     void. Reached only by `jmp` from 0x004a0a66 and 0x004a0a71, which are
//     `mov -0x10(%ebp),%ecx; add $0x8/$0xc,%ecx; jmp` -- EH unwind funclets.
//     A funclet target is a destructor, not a Free() method.
//   * the pointer is nulled AFTER the free and only inside the guard:
//     `if (p) { free(p); p = 0; }`. Nulling unconditionally would hoist the
//     `movl $0x0,(%esi)` out past the `je`.
//   * `push %eax; call; add $0x4,%esp` -> plain __cdecl free (0x004290c0,
//     the partner of the 0x004290b0 malloc used all over this run).
COwnedBlock::~COwnedBlock()
{
	if (m_pData)
	{
		free(m_pData);
		m_pData = NULL;
	}
}


// ======================================================================= //
//  14. The engine clock (0x00489600, 43 bytes) and its seed
//
//  HOME: the client build of the TU src/engine/server/worldtree.cpp's
//  section B reconstructs for server.dll (0x100383aa / 0x100383b6) -- the
//  client twin sits at 0x004895e0 / 0x00489600, directly in front of
//  CLTTransform. Same body: timeGetTime() less the value captured at
//  static-initialisation time (0x004b4d28), the unsigned difference
//  converted through a zero-extended fildll, times 0.001f (0x004a1a54).
//  Twenty call sites, 0x0042c610..0x00485cf8.
// ======================================================================= //

extern "C" __declspec(dllimport) unsigned long __stdcall timeGetTime(void);

// 0x004b4d28; its dynamic initialiser is 0x004895e0 -> 0x004895f0.
static uint32 g_StartTime = timeGetTime();

// M-P1-23: the clock from the performance counter.  timeGetTime counts
// whole milliseconds, so at 300 frames a second (3.3 ms) frames measured 3
// or 4 ms in turn, and every frame-time user jittered with it.
extern "C" __declspec(dllimport) int __stdcall QueryPerformanceCounter(__int64* pCount);
extern "C" __declspec(dllimport) int __stdcall QueryPerformanceFrequency(__int64* pFreq);

static __int64 g_P1Start;
static __int64 g_P1Freq;		// 0: not asked yet, -1: no counter

float time_GetTime()
{
	__int64 nNow;

	if (!g_P1Freq)
	{
		if (QueryPerformanceFrequency(&g_P1Freq) && g_P1Freq > 0)
		{
			QueryPerformanceCounter(&nNow);
			// the same zero as timeGetTime's below
			g_P1Start = nNow - (__int64)(timeGetTime() - g_StartTime) * g_P1Freq / 1000;
		}
		else
			g_P1Freq = -1;
	}

	if (g_P1Freq < 0)
		return (float)(timeGetTime() - g_StartTime) * 0.001f;

	QueryPerformanceCounter(&nNow);
	return (float)((double)(nNow - g_P1Start) / (double)g_P1Freq);
}


// ======================================================================= //
//  THE OTHER TWELVE ROWS -- no code, and why
// ======================================================================= //
//
//  A. SIX ARE MID-FUNCTION JUMP TARGETS / FALL-THROUGHS, not functions.
//     Each is enumerated only because a `gap`/`init`/`ptr`
//     heuristic fired; each is inside a function that starts earlier.
//
//       0x00444480  the `m_Unknown20 != 0` arm of the function at
//                   0x00444440 (WaitForSingleObject / 0x00444190). Reached
//                   by `jne 0x444480` at 0x0044446d. It opens `push %edi`
//                   but ends `pop %edi; pop %esi; pop %ebx` -- two pops it
//                   never pushed. 0x00444440's declared size, 64, ends
//                   exactly here.
//
//       0x00466fca  the fall-through of the ten-byte function at
//                   0x00466fc0 (`mov 0x4b48c8,%eax; test; jne 0x466fca;
//                   ret`). It uses %eax, which only 0x00466fc0 sets.
//
//       0x00466114  the loop body of the function at 0x00466100. Its own
//                   backward branch, `jne 0x466115`, lands one byte past
//                   0x00466114 -- after the `push %edi` that the entry
//                   makes once. Uses %eax/%edx set by 0x00466100.
//
//       0x00447646  a tail inside the 1174-byte function at 0x004471b0.
//                   Nine `jmp`s reach it (0x00447359, 0x00447395,
//                   0x004473d9, 0x00447421, 0x00447452, 0x00447476,
//                   0x0044749a, 0x004474c6, 0x00447516) and it ends
//                   `pop edi/esi/ebp/ebx; mov %ebp,%esp; pop %ebp` -- a
//                   frame-pointer epilogue with no matching prologue.
//
//       0x00490065  the middle of the function at 0x00490050. It consumes
//                   the `push %eax` at 0x00490064 (the result of the
//                   0x00490180 call) and reads %esi/%ebp set at the entry.
//                   Its only two "pointers" are 0x004b79e4 and 0x004b7c32
//                   -- both in .rsrc, i.e. coincidental bitmap bytes.
//
//       0x0049018d  the non-NULL arm of the function at 0x00490180
//                   (`mov 0x4(%esp),%edx; test; jne 0x49018d; xor %eax,%eax;
//                   ret $0x4`). Its `divl (%ecx)` uses %ecx from the
//                   caller's `this`; it is the string-hash tail.
//
//  B. FOUR ARE COMPILER-GENERATED C++ DYNAMIC INITIALISERS, not source
//     functions. The table at 0x004a7000..0x004a70d8 in .data is the
//     `.CRT$XC` list -- a NULL, fifty-three .text pointers, a NULL --
//     followed at 0x004a70e0 by the C-initialiser/terminator lists. Every
//     one of these four addresses is either an entry in that table or the
//     body an entry calls:
//
//       0x0040c0e0  table entry @0x004a700c. `jmp 0x0040c0f0` + eleven
//                   NOPs; the body sets *(void**)0x004b1758 = 0x004a1ab4.
//                   Source: a file-scope object of the class whose vtable
//                   is 0x004a1ab4 (slot 0 = 0x0045f0e0), constructor body
//                   empty.
//       0x0040c100  table entry @0x004a7010. The same, for the second
//                   instance of the SAME class at 0x004b15c0.
//       0x004895e0  table entry @0x004a70cc. `jmp 0x004895f0`; the body is
//                   `timeGetTime(); mov %eax,0x004b4d28`. Source: a
//                   file-scope `uint32` seeded from timeGetTime(). The
//                   reader is 0x00489600, `(timeGetTime() - that) *
//                   0x004a1a54` (a float constant) -- the client clock.
//       0x004440b0  NOT a table entry: 0x004440a0 is (@0x004a70a4), and it
//                   is `call 0x004440b0; jmp 0x004440d0`, the standard
//                   VC6 initialiser group -- construct, then register the
//                   destructor (0x004440d0 is `push $0x004440e0; call
//                   atexit`). 0x004440b0 is therefore the INLINED
//                   constructor of a file-scope object at 0x004b4040:
//                   vptr = 0x004a2638, then 0x00490b50(&[0x4b4044], 0x28,
//                   0x20) -- the same object-bank init as CClass4A14A8
//                   above, same 40-byte item, same 32 per block.
//
//     The `jmp <next 16-byte boundary>` + eleven NOPs shape on the first
//     three is not a source construct: it is the tail-called body of a
//     COMDAT'd `??__E` emitted as `E9 rel32` (the target is a separate
//     COMDAT, so the compiler cannot shorten it to `EB`) with the callee
//     aligned to 16. It appears 31 times in this image and once in
//     server.dll (0x1003dd70, whose target is `mov $0x10058c80,%ecx; jmp
//     <ctor>` -- the same construct with the object's address in %ecx).
//     Reproducing them means declaring the right global in the right
//     translation unit, not writing a function.
//
//  C. TWO ARE MIS-SIZED -- real functions, but not 29 bytes.
//
//       0x004429d0  and
//       0x00442bd0  are genuine __thiscall entries (`push ebx/ebp/esi; mov
//                   %ecx,%esi; push $0xc; call 0x004290b0`) that allocate a
//                   12-byte node. The 29 in the enumeration is the distance to
//                   0x004429ed / 0x00442bed, which are the `jne` targets
//                   five instructions in -- the enumeration
//                   then charges the remainder to a 168- / 150-byte "gap"
//                   row. The real extents are 0x004429d0..0x00442a9d and
//                   0x00442bd0..0x00442c8d, ~200 bytes each, and they should
//                   be enumerated at their true size.
//
// ======================================================================= //
//  COIN-FLIPS I COULD NOT SETTLE
// ======================================================================= //
//
//   1. 0x0048d1f0 -- `return Slot01(a, a, b, c, d);` versus
//      `Slot01(a, a, b, c, d);` with a `void` return. %eax is simply not
//      touched between the `call` and the `ret $0x10`, which both spellings
//      produce. Nothing in the ten callers of the vtable disambiguates.
//
//   2. 0x0048b270 -- `if (m_pUnknown18 && m_UnknownA0 == 1) return
//      Slot04Impl(); return 2;` versus the inverted guard `if
//      (!m_pUnknown18 || m_UnknownA0 != 1) return 2; return Slot04Impl();`.
//      Both put the single `mov $0x2,%eax; ret` at the far end and reach it
//      with `je`/`jne` in that order. Disproved: an if/else through a
//      result variable, which would materialise the 2 before the branch.
//
//   3. 0x00404a00 -- the second argument of 0x00490bd0 is `1` here and `0`
//      at 0x00490c10, but 0x00490bd0 never reads it in the range that is
//      visible; the parameter is declared `int bFree` on the strength of
//      the call sites alone.
//
//   4. The literal `2` in 0x0048b270 is written as a bare 2, not
//      LT_FINISHED: ltcodes.h maps 2 to LT_FINISHED, which reads wrong for
//      a "not ready" return, and the sibling at 0x0048b2b0 pairs it with 0
//      through `neg/sbb/and $0x2`. Whatever the enum is called, the byte is
//      `b8 02 00 00 00`.
