// *********************************************************************** //
//
//  MODULE:   SERVERMGR (.H)
//
//  PURPOSE:  server.dll's concrete ServerInterface. The class name is not a
//            guess: one surviving parameter-check literal in the image reads
//            "MyServerInterface::OpenFile", so that is what Monolith called
//            it. Every other method in the file reports itself as
//            "ServerInterface::<name>" — the base class — which is why the
//            concrete name shows up exactly once.
//
//            The object is a thin shell. It holds one pointer, to the
//            CServerMgr that owns the actual engine state, and every method
//            starts by checking it and returning LT_NOTINITIALIZED. The
//            console, config-file and shell-message methods below then reach
//            straight into that manager.
//
//            Method definition order in the .cpp is not free: the shipped
//            check literals sit in .data in the order their call sites were
//            first compiled, which recovers the original source order.
//            OpenFile, RunConsoleString, GetConsoleVar, GetVarValueFloat,
//            GetVarValueString, LoadConfigFile, SaveConfigFile,
//            SetAppHandler, SendToServerShell.
//
// *********************************************************************** //

#ifndef __SERVERMGR_H__
#define __SERVERMGR_H__
#include "message.h"
#include "client_state.h"
#include "client_reference.h"
#include "server_lifetime.h"
#include "client_update.h"
#include "container_query.h"
#include "filemgr.h"
#include "sound_instance.h"
#include "sound_wire.h"
#include "inter_object_link.h"
#include "engine/portable/record_pool.h"
#include "model_service.h"
#include "class_registry.h"
#include "object_properties.h"
#include "cache_entry.h"
#include "counter.h"
#include "structbank.h"
#include "object_pool.h"
#include "object_manager.h"
#include "object_data_pool.h"
#include "dhash.h"
#include "net_packet.h"
#include "net_manager.h"
#include "engine_config.h"
#include "console.h"
#include "world_record.h"
#include "world_lighting.h"
#include "world_state.h"
#include "engine/portable/message_format.h"
#include "engine/portable/allocation.h"

#include <stdarg.h>
#include "server_interface.h"
#include "iservershell.h"
#include "iltsoundmgr.h"
#include "iltstream.h"
#include "commonphysics.h"
#include "lthread.h"
#include "ltlink.h"
#include "ltcodes.h"
#include "ltobject_internal.h"

// 0x100343f2 — tell the clients a kept sound changed.
LTRESULT sm_SetSoundTrackChangeFlags(class CServerMgr*, SoundInstance*, uint32);

using CServerSoundMgr = ILTSoundMgr;

// CServerMgr's primary base: a virtual destructor (slot 0, which is what
// CreateServer's and DeleteServer's `delete` reach), three pure slots and one
// implemented one. Its vtable is in the image at 0x1004e314 (slots 1-3 are
// the shared __purecall stub), stored during construction and then replaced
// by CServerMgr's own pair — which is how the constructor betrayed the
// hierarchy. The name and slot names are ours.
// What the net manager tells its owner about. The connection slots are
// reached from CNetMgr; the packet slot is how a packet the net layer did
// not understand gets out to the application and the game (names ours).


// The thread-message bank (name ours): one global instance behind every
// LThreadQueue. Allocate hands out a constructed LThreadMessage; the frees
// go straight back onto the struct bank's list.
class LThreadMessageBank
{
public:
	virtual ~LThreadMessageBank();

	class LThreadMessage* Allocate();        // 0x10015b2e

public:
	StructBank m_Bank;                       // +0x04
	uint32     m_Unknown20;                  // +0x20
};

// Opaque constructed members, named by offset; only their constructors are
// reached from here.
// An eight-byte object Init news up and hands `this` (ctor 0x10034fb9);
// freed raw at Term, so no destructor.
// The helper seeded into every new message: it translates objects to and from
// the ids that go on the wire, and hands out the world. Lives at
// CServerMgr+0x804, and its vftable is the four-slot one at 0x1004e900 --
// bounded exactly, with a float constant (0x477de800) immediately below it and
// another class's vftable immediately above. Same shape as ServerModelLT and
// CServerLightAnim: a vptr and nothing but a back pointer to the manager,
// which every slot reaches through as 0x4(%ecx). Name ours.
//
// There is NO virtual destructor, which is why Term deletes the object raw:
// all four slots are accounted for below and none of them is a destructor.
class CMessageObjectHelper : public CMsgObjectIO {
public:
    explicit CMessageObjectHelper(CServerMgr*);
    LTRESULT ReadObject(LMessageImpl*, HOBJECT*) override;
    LTRESULT WriteObject(LMessageImpl*, HOBJECT) override;
    LTRESULT WriteFormattedHString(LMessageImpl*, int, va_list*) override;
    void GetPosRef(CPosRef**) override;
    CServerMgr* m_pServerMgr;
};

// One slot of the object-id map at CUnknown8FC+0x04: a flag byte and the
// record the id resolves to (names ours).
class CObjectMapEntry
{
public:
	uint8  m_nFlags;                         // +0x00  zeroed on removal
	uint8  m_pad_01[0x04 - 0x01];
	void*  m_pRecord;                        // +0x04  zeroed on removal
};

// The object map, CServerMgr+0x8fc: collection.h's CMoArray<CObjectMapEntry>
// itself, as CConnList and CDriverList already are, and not a look-alike of
// it. The shipped constructor (0x1002cbe3) is the template's -- vptr
// 0x1004e328, Clear()'s four zero stores, Init(0, 0) -- and that vptr is a
// full twelve-slot GenList table (GenBegin 0x1000ba8f, GenIsValid
// 0x1000b2b7, ...), so the class has no virtual destructor and no `??_G`.
// Init (0x1000c4cf) is the DefaultCache Init every other instantiation
// shares, SetSize (0x1002ccee) is its SetSize2 and AllocItems (0x1002cecf)
// its _AllocateTArray. The hand-written class that stood here (a virtual
// destructor, its own Init/SetSize/AllocItems over m_pEntries / m_nEntries /
// m_nSlack / m_nGrowBy) compiled a `??_G`, a second constructor and a second
// Init that the shipped images do not have. A typedef, not a derived class:
// an (empty) derived class has a vtable of its own, and CServerMgr's
// constructor then stores it after calling 0x1002cbe3, which the shipped one
// (0x1002c7f7) does not.
typedef CMoArray<CObjectMapEntry> CUnknown8FC;

// The object at 0x210 (only its initializer is reached; 0x10039baa).
// The attachment manager's spare-record list head sits 0x64 into the 0x390
// object; a freed Attachment's first dword becomes its free link.
// The server's worker thread (constructed 0x1003512f); its outgoing queue at
// +0x2c is what sm_ProcessThreadMessages drains.
class CServerThread : public LThread
{
public:
	CServerThread();                         // 0x1003512f
	LTBOOL Init(class CServerMgr* pMgr);     // 0x10035145
	// 0x10035152 — is a request for this file already waiting? The queue is
	// searched rather than a flag kept, so asking twice for one model costs
	// a walk and not a second load (name ours).
	LTBOOL IsRequestQueued(void* pFile);
	// 0x100351d9 — slot 0x0c, the only one this thread overrides: slots
	// 0x04 and 0x08 of its vftable (0x1004e910) are LThread's own
	// 0x100158d9 and 0x10015a2b, so ThreadTerm and ThreadRun are inherited.
	void ProcessMessage(LThreadMessage* pMsg);

	class CServerMgr* m_pServerMgr;          // +0x60
};

class CLTServer;
using CUnknown928Obj = CLTServer;

// This is Model — commonmodel.h describes the same class from the other side.
// se_LoadModelData allocates 0x2c4 bytes and constructs one here with the
// same two-allocator constructor, then loads a model file into it, which is
// what identifies the two as one class. What the manager keeps at 0xb28 is
// therefore a Model: the placeholder that stands in for one still loading.
class Model;
typedef class Model CUnknownB28Obj;

// The engine allocator pair (0x1000d2ef, 0x1000d398).

void  dfree(void* ptr);

// 0x1000deeb — format a message from the resource module (ltmsg.dll).
int dsi_SetupMessage(char* pMsg, int maxLen, int messageCode, va_list marker);

// 0x1002bc5e — mount resource trees onto the file manager.


// The console entry points the interface forwards to — all __cdecl.
LTRESULT c_CommandHandler(ConsoleState* pState, char* pStr);
HCONSOLEVAR cc_FindConsoleVar(ConsoleState* pState, char* pName);
void cc_RunCommand(ConsoleState* pState, char* pCommand, int nSource);
LTBOOL cc_LoadConfigFile(ConsoleState* pState, char* pFilename, LTBOOL bJustVars, LTBOOL bCreate);
LTBOOL cc_SaveConfigFile(ConsoleState* pState, char* pFilename);


// What the world loader is handed. Zeroed by its own inline constructor and
// then once more by LoadWorld's explicit memset — the shipped bytes contain
// both. The hook slot holds a no-op function; /OPT:ICF folded every empty
// function in the image into one body (shared with dsi_OnReturnError), so
// which no-op the source named is unrecoverable.
// The network manager embedded in the server at 0x6e0 (the name is attested
// by a check literal elsewhere in the image). Sized by its neighbours: the
// driver array pointer follows at 0x720.
// The bank the net packet records live in (0x70-byte records, 32 deep):
// the same vptr-over-StructBank shape as CMessageBank (names ours).
// CNetBank's destructor restores TWO vftables -- its own and then
// 0x1004d260, which is the same one the eleven object banks and the message
// bank restore -- so it shares CBankBase above, and the three slots after
// the destructor are that base's.
// The one allocator instance the manager's collections are handed. It is
// stdlith's `LAlloc` (collection.h) and `g_MoAlloc` is `g_DefAlloc` -- the
// same object at 0x10058c7c -- but worldbsp.h, worldmodels.cpp, smgr.cpp
// and tcpdriver.cpp still spell it this way, so the old name stays declared
// here until each of them is converted in turn.
using CMoAllocator = LAlloc;
inline CMoAllocator& g_MoAlloc = g_DefAlloc;

// The two drivers the manager knows how to build (names ours).
class CBaseDriver* ldrv_Create();            // 0x10014d65
class CBaseDriver* tcp_Create();             // 0x10037c32

// The GenericProp fillers (names ours). ltbasedefs.h defines the record
// itself, and the clear confirms it: the shipped code zeroes offsets 0/4/8
// and 0x11c/0x120/0x124, which is m_Vec and m_Rotation exactly where the
// header puts them.
void gp_Clear(struct GenericProp* pProp);                          // 0x10011172
void gp_SetString(struct GenericProp* pProp, char* pStr);          // 0x100111b7
void gp_SetVector(struct GenericProp* pProp, LTVector* pVec);      // 0x1001126d
void gp_SetFloat(struct GenericProp* pProp, float fValue);         // 0x100112ba
void gp_SetRotation(struct GenericProp* pProp, LTVector* pAngles); // 0x100112d6

// 0x1001186d — build a rotation from three euler angles (name ours; the
// rotation is the last argument, not the first).
void rot_SetupEuler(float x, float y, float z, LTRotation* pRot);

// The game console's state at 0xa74. Only the three entry points the
// interface forwards to are reached (names ours).



// The console's own internals (names ours).









// 0x1000e4fd and 0x10000e503: the engine's own table of bindings and its
// length, which the console picks up at startup (names ours).
ConsoleBinding* dsi_GetConsoleBindings();
int             dsi_GetNumConsoleBindings();

// 0x10053d80 — the eight commands the console starts with (name ours).
extern ConsoleCommandDef g_ConsoleCommands[];

// 0x10028e97 and 0x10028f42: the two notifications the server hangs off
// console-variable creation and variable change (names ours).
//
// The allocator the console variables come from is at 0x1000d25d, and that
// address is NOT a console function: it is the eleven-byte `operator new`
// forwarder in front of dalloc at 0x1000d2ef. /OPT:ICF is off in this image
// -- the two byte-identical 76-byte SetSize2 bodies at 0x10019e14 and
// 0x1001a03d are both present -- so there is exactly one function there and
// it already has a name.
//
// The name below is kept anyway, and this is the reason: the console stores
// the address in a `void* (*)(uint32)` field, and VC6 will not let the
// address of an overloaded `operator new` be taken or cast to it (C2440).
// So `con_Alloc` is a stand-in the source can spell, not a claim that such a
// function exists -- and because the store is a masked relocation, no
// comparison can tell the two spellings apart. A call-target check catches
// the equivalent mistake at CALL sites; a function POINTER stored to data is
// still outside every oracle we have.
void  cc_OnVarCreated(ConsoleState* pState, LTCommandVar* pVar);
void  cc_OnVarChanged(ConsoleState* pState, LTCommandVar* pVar);


// The bank the file records come from: the same vptr-over-StructBank shape
// as CMessageBank and CNetBank (names ours).


// The file manager embedded at 0xa2c, opaque from here; these are the
// entry points the interface forwards to, all cdecl.


void       fs_Term(CFileMgr* pFileMgr);                           // 0x1002bc12
FileEntry* fs_GetFileList(CFileMgr* pFileMgr, char* pDirName);    // 0x1002be41
ILTStream* fs_OpenFile(CFileMgr* pFileMgr, char* pFilename);      // 0x1002bcd9
void       fs_FreeFileList(FileEntry* pList);                     // 0x10013899
char*      fs_GetFileName(CFileMgr* pFileMgr, void* pFile);       // 0x1002bfcd (name ours)

// 0x10005dc6 — the tokenizer ILTServer::Parse exposes (name ours).
int parse_Command(char* pCommand, char** pNewCommandPos, char* argBuffer,
                  char** argPointers, int* nArgs);

// 0x10035a8d — open a file by name and wrap the handle in the 12-byte
// ILTStream the interface hands out (name ours; the shipped body is fopen
// plus a three-field allocation).
ILTStream* streamsim_Open(const char* pFilename, const char* pAccess);

// The object save/restore pair (0x1000f93a, 0x1001027c) lives in a module we
// have not reached yet; ILTServer::SaveObjects/RestoreObjects are the thin
// wrappers that open the stream for it. Prefix ours.
LTRESULT sav_SaveObjects(CServerMgr* pMgr, ILTStream* pStream, ObjectList* pList,
                         uint32 dwParam, uint32 flags);
LTRESULT sav_RestoreObjects(CServerMgr* pMgr, ILTStream* pStream,
                            uint32 dwParam, uint32 flags);
LTRESULT   fs_CopyFile(CFileMgr* pFileMgr, const char* pszSourceFile,
                       const char* pszDestFile);                  // 0x1002bd97

// 0x1000d926 — format a line and hand it to the debugger. The interface
// table's DebugOut entry points straight at it (name ours).
void con_Printf(char* pMsg, ...);

// 0x10051c74 — the queue depth past which a client is left alone for a
// frame, and 0x1002132a — the net layer's own view of the same question
// (names ours).
extern uint32 g_MaxQueueSize;

// The net layer's own knobs (names ours): how long a connection may go
// unacknowledged before it is declared timed out (0x10051c68), whether the
// timeout is announced (0x100584dc), and the two the connection trace reads
// — the level below which a line is printed (0x100584ec) and whether the
// trace covers connections that are still coming up (0x100584e0).
extern float  g_AckTimeout;
extern int    g_NetShowTimeouts;
extern int    g_NetDebugLevel;
extern int    g_NetDebugAll;

// 0x100383b6 — the current time, in seconds, as the engine keeps it.
float dsi_GetTime();

// The engine-side server. Only the fields the interface touches are named.
// Its exact size, 0xb2c, is not a guess: CreateServer's `new CServerMgr`
// pushes it. It is polymorphic — CreateServer's failure path and DeleteServer
// both `delete` it through vtable slot 0 — so offset 0 is the vptr and every
// pad below starts four bytes in.
class CServerMgr : public CBaseMgr, public CServerSoundMgr
{
public:
    std::shared_ptr<ServerLifetime> m_Lifetime = std::make_shared<ServerLifetime>();
    bool m_Restoring = false;
    bool m_Initialized = false, m_Terminating = false, m_EndingWorld = false;
    bool m_StartingWorld = false, m_RunningWorldStart = false;
    bool m_Updating = false;
	CServerMgr();                            // 0x1002c761
	// 0x1002c929, with the deleting form at 0x1002c90d that both exports
	// delete through. The body is Term(); the nine sub-objects are the
	// compiler's, in reverse declaration order.
	virtual ~CServerMgr();

	// The rest of the primary overrides (0x10029005, 0x10029025, 0x1002903d).
	virtual LTBOOL _v04(NetConnection*, uint32 flags);
	virtual void     _v08(class NetConnection* pConn);
	virtual void     ProcessPacket(class CPacket* pPacket, uint8* senderAddr,
	                               uint16 senderPort);
	void _v04();
	void _v08();
	void _v0c();

	// Implements the shared SDK sound interface through native inheritance.
	LTRESULT PlaySound(PlaySoundInfo *pPlaySoundInfo, HLTSOUND &hResult) override;    // 0x10034037
	LTRESULT GetSoundDuration(HLTSOUND hSound, LTFLOAT &fDuration) override;          // 0x10034225
	LTRESULT IsSoundDone(HLTSOUND hSound, LTBOOL &bDone) override;                    // 0x10034274
	void SetGlobalLightObject(HOBJECT hObj);                                 // 0x10034e4f
	// 0x1003448f — record a file for the current cache pass (name ours).
	std::shared_ptr<const SoundTrack> AddCachedFile(CFileRef* file);
	LTRESULT KillSound(HLTSOUND hSound) override;                                     // 0x100342d5
	LTRESULT KillSoundLoop(HLTSOUND hSound) override;                                 // 0x10034359

	LTBOOL Init();                           // 0x10031f5f
	void InitMessageOwner(class ServerMessage* pMsg);   // 0x10034e3f (name ours)
	// 0x10033fca — tell one client which of its command bits changed this
	// frame, as a list of indices and a count (name ours).
	void NotifyCommandChanges(class Client* pClient, uint8* pChanged,
	                          int nChanged);
    LTBOOL GrowClientObjInfos(uint32 count);
	LPBASECLASS CreateObject(HCLASS hClass, ObjectCreateStruct* pStruct);  // 0x10033126
    LPBASECLASS CreateObjectWithProperties(HCLASS, ObjectCreateStruct*, std::shared_ptr<const ObjectProperties>, float reason);
	void   Term();                           // 0x1003233f

	nolf::RecordPool<SoundInstance> m_SoundInstances;
	LTList             m_List54;             // 0x054
	int                m_ErrorCode;          // 0x064
	char               m_ErrorString[300];   // 0x068  SetupError formats into it
	uint8              m_pad_194[0x198 - 0x194];
	uint32             m_Unknown198;          // 0x198  cleared when a world starts running
	uint32             m_ServerFlags;        // 0x19c  SS_PAUSED, and nothing else
	uint32             m_Unknown1a0;          // 0x1a0  flag bits; bit 0 set while
	                                         //        removing objects, bit 0x10 while
	                                         //        the shell caches files
    CServerWorld m_World;
    // Existing manager accessors alias the owned world; none reserves or
    // duplicates storage inside its runtime representation.
    LightAnim*& m_pLightAnims = m_World.m_Anims.m_pArray;
    uint32& m_nLightAnims = m_World.m_Anims.m_nElements;
    CUnknown210& m_Unknown210 = m_World.m_Tree;
    WorldShadeGrid& m_ShadeState = m_World.m_Shade;
    LTVector& m_WorldBoxMin = m_World.m_vBoxMin;
    LTVector& m_WorldBoxMax = m_World.m_vBoxMax;
    uint32& m_Unknown324 = m_World.m_Flags180;
    ILTStream*& m_pWorldStream = m_World.m_pObjectStream;
    CServerWorldRec**& m_pFileTrees = m_World.m_Worlds.m_pArray;
    uint32& m_nFileTrees = m_World.m_Worlds.m_nElements;
    uint32& m_pWorldFile = m_World.m_bLoaded;
	CUnknown390        m_Unknown390;         // 0x390
    LTLink* ObjectListHead() { return &m_Unknown390.m_Lists[OT_MODEL].m_Head; }
    LTLink* WorldModelListHead() { return &m_Unknown390.m_Lists[OT_WORLDMODEL].m_Head; }
    LTList* ObjectLists() { return m_Unknown390.m_Lists; }

	int                m_bWorldLoaded;        // 0x660  set by LoadWorld
	void*              m_hWorldFile;          // 0x664  identifier of the loaded world file
	GlobalForceControl m_ForceControl;       // 0x668  SPhysicsLT's "world" force state:
	                                         //        its m_pWorld points at this manager
	// The record ends at 0x6a8: GlobalForceControl's frame-move answer at its
	// own +0x34 is the manager's 0x69c-0x6a7, so nothing is left over.
	IObjectMover*      m_pUnknown6a8;        // 0x6a8  deleted raw by Term
	CollisionInfo*     m_pCollisionInfo;     // 0x6ac  the last collision; the flat
	                                         //        GetLastCollision copies it out
	float              m_FrameTime;          // 0x6b0
	float              m_UpdatesPerSec;      // 0x6b4  reset from g_UpdatesPerSec
	float              m_Unknown6b8;         // 0x6b8
	float              m_Unknown6bc;         // 0x6bc  world start time
	float              m_Unknown6c0;         // 0x6c0
	uint32             m_Unknown6c4;         // 0x6c4  fixed-step updates run
	float              m_Unknown6c8;         // 0x6c8
	float              m_Time;               // 0x6cc
	int                m_Unknown6d0;         // 0x6d0  updates this world
	float              m_Unknown6d4;         // 0x6d4
	float              m_Unknown6d8;         // 0x6d8  last update time
	uint32             m_Unknown6dc;         // 0x6dc

	// 0x6e0-0x7e3: the whole network manager. What used to be carved here as
	// direct CServerMgr fields (the driver array at 0x720, m_pSelf794, the
	// bandwidth pair at 0x7c0, the app guid at 0x7d0, the main driver at
	// 0x7e0) are CNetMgr members reached through it.
	CNetMgr            m_NetMgr;             // 0x6e0
	LTList             m_ClientList;         // 0x7e4  data links to Client
	uint32             m_Unknown7f4;         // 0x7f4
	uint32             m_Unknown7f8;         // 0x7f8
	uint32             m_Unknown7fc;         // 0x7fc  bytes queued this frame;
	                                         //        zeroed by the ctor and each flush
	uint32             m_Unknown800;         // 0x800  bytes dropped this frame;
	                                         //        zeroed by the ctor and each flush
	CMessageObjectHelper* m_Unknown804;          // 0x804  seeded into every new message; deleted raw by Term
	ILTStream* m_pPacketTrace = nullptr;
	// 0x80c-0x83b: the class manager, which owns the shell pointer, the
	// per-class records, the class hash and this module's slot.
	CClassMgr          m_ClassMgr;           // 0x80c
	LTLink             m_Link83c;            // 0x83c  the queued-object list; retied per world
	InterObjectLinks m_InterObjectLinks;
	uint32             m_Unknown8b8;         // 0x8b8  = 200 at Init
	uint32             m_Unknown8bc;         // 0x8bc  count for the list below
	void*              m_hHash8c0;           // 0x8c0  freed like the model hash
	LTLink             m_Link8c4;            // 0x8c4  drained onto g_FreeLinks by Term
	LTLink             m_Link8d0;            // 0x8d0
	LTList             m_List8dc;            // 0x8dc  every live object, threaded
	                                         //        through SObjData::m_StateLink;
	                                         //        inactive ones live at the tail
    ClientReferences m_ClientReferences;
	// 0x8fc-0x90f: the object map, whose grow-by is what Init sets to 100.
	CUnknown8FC        m_Unknown8fc;         // 0x8fc
	LTObject*          m_pChangedObjects;    // 0x910  intrusive list through SObjData
	SoundInstance*     m_pChangedSounds;     // 0x914  linked through m_pNextChanged
	void*              m_hModelHash = nullptr;          // 0x918  one entry per model reference
    std::vector<CachedFileEntry> m_CachedFiles;
	CUnknown928Obj*    m_pUnknown928;        // 0x928  deleted through slot 71
	SkyDef             m_SkyDef;             // 0x92c  the SDK sky box: Init sets
	                                         //        (-10,-10,-10)/(+10,+10,+10),
	                                         //        view box zeroed
	uint16             m_SkyObjects[30];     // 0x95c  object ids; 0xffff = free slot
	                                         //        (memset to 0xff per world)
	HOBJECT            m_hGlobalLightObject; // 0x998  Get/SetGlobalLightObject
	StructBank         m_ObjectListBank;    // native ObjectList records
	StructBank         m_ObjectLinkBank;    // native ObjectLink records
	CMessageBank       m_MessageBank;        // 0x9f0
	CFileMgr           m_FileMgr;
    SoundCache m_Sounds{m_FileMgr};
    ModelService m_Models{m_FileMgr};            // 0xa2c
    std::shared_ptr<const ObjectProperties> m_Properties;
    std::vector<std::byte> m_GameInformation;
	ConsoleState       m_ConsoleState;       // 0xa74
	ServerAppHandler*  m_pAppHandler;        // 0xac0
	CServerThread      m_Thread;             // 0xac4
	std::shared_ptr<Model> m_PlaceholderModelOwner;
	class Model*       m_pPlaceholderModel = nullptr;  // 0xb28  the object's last member;
	                                         //        stands in for a model that
	                                         //        is still loading

	// 0x100327d9 — walks the client list rooted at 0x7e8.
	Client* FindClient(uint32 nClientId);

	// Publish this manager through the globals (0x100330fc); every API entry
	// calls it first.
	void SetupGlobals();                                         // 0x100330fc

	// Record an error code and format its ltmsg string into m_ErrorString
	// (0x10034660). Varargs, so cdecl with `this` as a hidden stack argument.
	LTRESULT SetupError(int nError, ...);

	// The engine-side halves the interface wrappers forward to.
	LTBOOL AddResources(char** pResources, uint32 nResources);   // 0x10032631
	LTBOOL LoadBinaries();                                       // 0x10032696
	LTBOOL SetGameInfo(void* pGameInfo, uint32 nGameInfoLen);
	int    GetNumClients();                                      // 0x100326f4
	LTBOOL GetClientName(int index, char* pName, int maxChars);  // 0x100326fb
	void   GetClientName(class Client* pClient, char* pName, int maxChars);  // 0x1003401a
	LTBOOL GetClientInfo(int index, ClientInfo* pInfo);          // 0x10032740
	LTBOOL BootClient(uint32 dwClientID);                        // 0x100327ad
	int    GetErrorCode();                                       // 0x1000ab4f
	void   GetErrorString(char* pString, int maxLen);            // 0x1003310f
	LTBOOL Update(long flags, float fTime);                      // 0x10032e69
	// 0x10032dcd — the hook for a model that finished loading on this thread
	// rather than on the worker's; it takes the same message the worker
	// would have sent back (name ours).
	void OnModelLoaded(class LThreadMessage* pMsg);

	class ServerMessage* StartMessage();                         // 0x10034ddd
	LTRESULT FreeUnusedModels();                                 // 0x10033d9d
	LTRESULT LoadWorld(ILTStream* pStream, char* pWorldName);    // 0x1003318d
	LTRESULT DoStartWorld(char* pszWorldName, uint32 flags, float fTime);  // 0x100339a4
    LTBOOL PrecacheModels();
	LTRESULT DoRunWorld();                                       // 0x10033c1e
	void     EndWorld(int nMode);                                // 0x10033c9f
	void     ProcessThreadMessages();                            // 0x10032e2f

	// Update's helpers (names ours, from what they walk). UpdateSounds runs
	// each live SoundInstance's timer; UpdateObjects is the 868-byte tick
	// body; RemoveDeadSounds unlinks killed/expired sounds and returns them
	// to the bank at 0x28.
	void UpdateSounds(float fFrameTime);                         // 0x10032ceb
	void UpdateObjects();                                        // 0x10032854
	void RemoveDeadSounds();                                     // 0x10032d25
};

// The default update rate, 30.0f in .data at 0x10051d20 (the name is ours).
extern float g_UpdatesPerSec;

// 1.0f at 0x10051d24, scales the fixed-step frame time (the name is ours).
extern float g_TimeScale;

// Update's console toggles and profile counters (names ours; the last two
// are printed as "ILTServer::FindObjectsTouchingSphere ticks/count").
extern int   g_ShowGameTime;        // 0x10058534
extern int   g_ShowTickCounts;      // 0x1005853c
extern thread_local float g_Unknown585a0;        // 0x100585a0
extern thread_local uint32 g_nIsectQueries;     // 0x100585a4 (objbank.cpp)
extern thread_local uint32 g_nIsectTime;        // 0x100585a8 (objbank.cpp)
extern thread_local int   g_Unknown58728;        // 0x10058728
extern thread_local int   g_Unknown5872c;        // 0x1005872c
extern int   g_FindSphereTicks;     // 0x10058be8
extern int   g_FindSphereCount;     // 0x10058be4

// Update's cdecl helpers (names ours). sm_ProcessPackets services the net
// drivers and incoming packets, returning nonzero to abort the update;
// sm_UpdateClients walks the client list (variable-step path only).
int  sm_ProcessPackets(CServerMgr* pMgr);                        // 0x10029e00
void sm_UpdateClients(CServerMgr* pMgr);                         // 0x10032bf0

// The console printer again (0x1000e3f9, the same function dsi_ServerError
// names), but through the fixed two-argument prototype Update's tick-count
// stats were compiled against: VC6 defers stack cleanup across consecutive
// fixed-prototype cdecl calls (one add esp,0x10) where varargs call sites
// pop after each call — the shipped bytes say these two sites had a fixed
// prototype. The name is ours.

// Per-client half of sm_UpdateClients (0x10027782) and the detach notice a
// dying attached sound sends through 0x100139d0 (names ours).
void sm_UpdateClient(CServerMgr* pMgr, class Client* pClient);

// One object's update (0x1002a431, name ours) — UpdateObjects times it.
void sm_UpdateObject(CServerMgr* pMgr, class LTObject* pObject);

// The flush cluster around sm_FlushObjectQueue (names ours).
// sm_DrainObjectQueue processes every queued object once (more may queue
// while it runs, so the caller loops); sm_ApplyChangeFlags folds the
// changed-object and changed-sound lists into each in-world client's table;
// sm_SendClientUpdates writes and sends each client's update packet.
void sm_DrainObjectQueue(CServerMgr* pMgr);                       // 0x1002ac7c
void sm_ApplyChangeFlags(CServerMgr* pMgr);                       // 0x10032c54
void sm_SendClientUpdates(CServerMgr* pMgr);                      // 0x10032c1b
LTRESULT sm_RemoveObjectFromWorld(CServerMgr* pMgr, class BaseClass* pClass);  // 0x100336c6 (attested name)
// A world portal record (names ours): only the flags word is reached.

WorldPortal* w_FindPortal(CServerWorld* pWorld, char* pName,
                          uint32* pnWorld, uint32* pnPortal);   // 0x10007dfa

// Attach/detach one client's view to another (names ours): the flat
// AttachClient/DetachClient entries forward here.
LTRESULT sm_AttachClient(CServerMgr* pMgr, class Client* pParent, class Client* pChild);  // 0x10027155
LTRESULT sm_DetachClient(CServerMgr* pMgr, class Client* pClient);                        // 0x10027259

// The attachment manager embedded at 0x390 (names ours): create takes the
// parent, the child's network id, and the socket/node index, handing back
// the record; remove is the named "ILTServer::RemoveAttachment" worker.
LTRESULT am_CreateAttachment(CUnknown390* pAttachMgr, LTObject* pParent, LTObject* pChild,
                             int iNode, LTVector* pOffset, LTRotation* pRotOffset,
                             class Attachment** ppAttachment);          // 0x10025984
LTRESULT am_RemoveAttachment(CUnknown390* pAttachMgr, LTObject* pParent,
                             class Attachment* pAttachment);            // 0x10025a1b

// 0x1005852c (name ours) — the auto-deactivation gate SetObjectState and
// SetDeactivationTime consult.
extern int g_bAutoDeactivate;

// The world-model move/rotate request (names ours): only the fields the
// rotator reads are mapped; the last dword is cleared with the pointers.
// 0x10020a53 — rotate a world model through the simulation (name ours).
void wm_RotateWorldModel(WMMoveState* pState, LTRotation* pRotation, int bUnknown);

// 0x1002ade4 — move an object between the active and inactive halves of the
// 0x8dc list (names ours): nStateBits is 8/0x10/0x20, the SObjData change
// bits GetNextObject's 0x38 mask tests.
void sm_SetObjectState(CServerMgr* pMgr, LTObject* pObj, uint32 nStateBits);
// 0x1002a239 — is the object one the sweep may deactivate? (name ours)
// 0x1002a239 -- INLINE and in this header, which is where the original had
// it. lithtech.exe's /Ot expands it at both sites (0x0047c568 inside
// CObjectMover::CanDeactivate and 0x00469588) and has no standalone copy at
// all; server.dll's /Os declines it and calls 0x1002a239 from both sites,
// so this cannot move server codegen. The body was in smgr.cpp, where
// /Ob1 would never have expanded it and where it sat BELOW its own caller.
// The pSData local is what makes /Os decline: without it server.dll expands
// the body at both sites too (46/70 bytes against shipped 13/52); with it
// /Os calls the COMDAT, /Ot still expands, and the codegen is unchanged. A
// single result local also makes /Os decline but gives the wrong
// standalone (one `ret`, 13 insns against 14).
inline LTBOOL sm_CanDeactivate(LTObject* pObj)
{
	SObjData* pSData;
	uint32 nFlags;

	pSData = pObj->m_pSData;
	if (!pSData->m_SFXMessage.m_pMsg)
	{
		nFlags = pObj->m_Flags;

		if (nFlags & 0x21213001)
		{
			if (nFlags & 0x10000000)
				return LTTRUE;
		}
		else
		{
			return LTTRUE;
		}
	}

	return LTFALSE;
}

// 0x1002a205 — refresh the object's boxes in the world containers after a
// flag change (name ours). LTRESULT, not void as this first read it: the
// shipped body ends `xor %eax,%eax; ret` on every path, so it answers LT_OK
// deliberately, including where it did nothing. Its one call site discards
// the result.
LTRESULT sm_UpdateObjectBBox(CServerMgr* pMgr, LTObject* pObj);
// 0x1003491b — reactivate what the object can see (PingObjects' worker).
void sm_PingObjects(CServerMgr* pMgr, LTObject* pObj);

LTRESULT sm_CreateInterObjectLink(CServerMgr* pMgr, LTObject* pOwner,
                                  LTObject* pLinked, int nFlags);       // 0x10013b63
void     sm_BreakInterObjectLink(CServerMgr* pMgr, LTObject* pOwner,
                                 LTObject* pLinked, int nFlags);        // 0x100139d0

// The named SetPortalFlags worker (0x10034985) and the console broadcast
// BPrint rides (0x10034521; names ours).
LTRESULT sm_SetPortalFlags(CServerMgr* pMgr, char* pPortalName, uint32 flags);
void     sm_BroadcastPrint(char* pMsg);

// 0x100297b8 — queue one message to every client (names ours).
void sm_BroadcastToClients(CServerMgr* pMgr, int nPacketID, class ServerMessage* pMsg);

// 0x10034cf9 — tell every client to cache one file (names ours); the gate
// at 0x10051c60 is set only inside ServerShell::CacheFiles.
void sm_BroadcastCacheFile(CServerMgr* pMgr, uint32 fileType, uint16 nFileID);
extern int g_bCachingEnabled;                    // 0x10051c60

void sm_MarkSkyChanged(CServerMgr* pMgr);                         // 0x10028685 (name ours)
LTRESULT sm_RemoveObjectFromSky(CServerMgr* pMgr, class LTObject* pObject);     // 0x10028858 (name ours)
class LTObject* sm_FindObjectByID(CServerMgr* pMgr, uint16 nID);  // 0x1002acd3 (name ours)

// sm_RemoveObjectFromWorld's teardown helpers (names ours, by module):
// break the object's attachments (0x10013a87, the sm_DetachSound region),
// free the thing at SObjData+0x28 (0x10031f2f), unlink the model trackers
// (0x1001e7ef/0x1001e828), release engine-side object memory (0x1000d8a8),
// final smgr bookkeeping (0x100333fc), remove from the world tree
// (0x1002bab8), and drop it from the 0x390 container (0x1002592e).
void sm_BreakAttachments(CServerMgr* pMgr, class LTObject* pObject, int a, int b);  // 0x10013a87
void sm_Free28(CServerMgr* pMgr, void* p);                        // 0x10031f2f

// The two the manager's connection slots forward to (names ours): a new
// connection becomes a client, and a dead one stops being one.
// Returns the client it made: 0x00463cb1 `mov eax,esi` puts it in %eax on the
// way out. Declared LTRESULT here for as long as it was undefined.
class Client* sm_AddClientForConnection(CServerMgr* pMgr,
                                        class NetConnection* pConn,
                                        int bLocal);                      // 0x10026d23
void     sm_DropConnection(CServerMgr* pMgr, class NetConnection* pConn);  // 0x10027135

// 0x100338b8 — hand the world over to the shell to cache what it wants, then
// let go of every model nothing is using (name ours).

// 0x1003481c — whatever the queued-object list still owes, done now; and
// 0x1003471d hands its nodes back afterwards (names ours).
void sm_FlushList44(CServerMgr* pMgr);

// 0x10034757 — the sound side of the same sweep (name ours).
void sm_FreeUnusedSounds(CServerMgr* pMgr);
void obj_UnlinkTrackersA(class LTObject* pObject);                // 0x1001e7ef
void obj_UnlinkTrackersB(class LTObject* pObject);                // 0x1001e828
void obj_ReleaseMemory(class LTObject* pObject);                       // 0x1000d8a8
LTRESULT sm_ObjectRemoved(CServerMgr* pMgr, class LTObject* pObject); // 0x100333fc
// Per-object-type handlers, two entries each; only the first is reached
// from here (name ours). 0x1005402c.
class ObjTypeHandlers
{
public:
	uint32 (*m_pApplyFilenames)(class CServerMgr* pMgr, class LTObject* pObject,
	                            ObjectCreateStruct* pStruct);   // 0x00
	uint32 (*m_pRemove)(class CServerMgr* pMgr, class LTObject* pObject);  // 0x04
};

extern ObjTypeHandlers g_ObjTypeHandlers[NUM_OBJECTTYPES];

uint32 sm_RemoveFromWorldTree(CServerMgr* pMgr, class LTObject* pObject);  // 0x1002bab8
LTRESULT un390_RemoveObject(class CUnknown390* p, class LTObject* pObject);  // 0x1002592e
void sm_ClearChangedObjects(CServerMgr* pMgr);                    // 0x10032805
void sm_ClearChangedSounds(CServerMgr* pMgr);                     // 0x10032832
void sm_WriteClientUpdate(CServerMgr* pMgr, class Client* pClient);   // 0x10027e8b
void sm_FlushClientPackets(CServerMgr* pMgr, class Client* pClient);  // 0x100277c2
int  sm_SetClientState(CServerMgr* pMgr, class Client* pClient, int nState);   // 0x100274dc
LTRESULT sm_UpdateClientState2(CServerMgr* pMgr, class Client* pClient);  // 0x10026859

// Client state transitions (names ours): prep may refuse the world entry,
// enter announces it, exit is run when a client leaves state 3.
// 0x1002759a (enter) is declared in smgr.cpp, as sm_ClientEnterWorld2 returning
// LTRESULT. A stale `void sm_ClientEnterWorld` prototype used to sit here and
// had no definition. Adding or removing any declaration in this header moves
// TCPDriver::HandleConnPacket (0x10036ee5), whose two parameter loads flip
// order at a symbol-count threshold. Measure it before adding one.
int  sm_PrepClientEnterWorld(CServerMgr* pMgr, class Client* pClient);  // 0x10027533
void sm_ClientExitWorld(CServerMgr* pMgr, class Client* pClient);       // 0x100276eb

// The file-tree view the handshake walks (names ours): each tree at 0x330
// holds a list whose records are 0x24 apart with a flags word at +0x06.
using CFileRec = WorldPortal;
using CFileList = CWorldBsp;
using CFileTree = CServerWorldRec;

// The world-entry senders (names ours; purposes inferred from position) and
// the direct-to-client send (0x10029653). 0x10026b4d is sm_SendObjectInfo2,
// declared in smgr.cpp (LTRESULT); the stale void prototype that stood here
// is gone (see the note at sm_PrepClientEnterWorld).
void sm_SendWorldInfo(CServerMgr* pMgr, class Client* pClient);   // 0x1002666c
void sm_SendSkyInfo(CServerMgr* pMgr, class Client* pClient);     // 0x100286a9
void sm_SendToClient(CServerMgr* pMgr, class Client* pClient, int nPacketID,
                     class ServerMessage* pMsg, int nFlag, int nPriority);   // 0x10029653
// The queued send the flat SendToClient rides (names ours).
//
// DECLARED HERE AND NEVER DEFINED, and that is deliberate until someone can
// tell it apart from sm_QueueMessageToClient2 (smgr.cpp, 0x100296d3), which
// has the same role and a different signature -- LTRESULT/int against this
// void/uint8. It has been proposed that they are one function with a stale
// declaration, on the grounds that "the shipped mangling says LTRESULT and
// int". THAT ARGUMENT IS CIRCULAR: server.dll carries no symbols at all, so
// the only mangled name available is the one OUR compiler produced from THIS
// declaration.
//
// It was tried anyway and MEASURED: reconciling them costs a match.
// sm_SFXSendToOwner (0x10034b49, 49 bytes) passes pInfo->m_nPacketID here and
// reproduces exactly while the parameter is uint8; widening it to int breaks
// that function and nothing else improves. So the callers want the narrow
// spelling whatever the definition wants, and the two stay separate until
// real evidence -- not a mangling we generated ourselves -- says otherwise.
LTRESULT sm_QueueMessageToClient(CServerMgr* pMgr, class Client* pClient, uint8 nPacketID,
                             class ServerMessage* pMsg, uint32 flags);       // 0x100296d3
// SFX plumbing (names ours): stow an object's SFX message; send a positioned
// SFX message to every client that can see the position.
void sm_SetObjectSFXMessage(CServerMgr* pMgr, LTObject* pObj,
                            class ServerMessage* pMsg);                      // 0x1002ad4a
LTRESULT sm_SendSFXMessage(CServerMgr* pMgr, int nPacketID, class ServerMessage* pMsg,
                           LTObject* pObj, LTVector* pPos, uint32 flags);    // 0x10034a6d
uint32 sm_GetObjectChangeFlags(CServerMgr* pMgr, class LTObject* pObject);   // 0x1002a264

// Packet dispatch (names ours): the handler table at 0x100587ac, indexed by
// the packet's first byte & 0x3f; a nonzero handler return aborts the whole
// update. Unhandled packets from a known client go to file transfer. The
// reset clears all 256 native function pointers; 64 IDs are reachable.
typedef int (*PacketHandlerFn)(CServerMgr* pMgr, class ServerMessage* pMsg,
                               class Client* pClient);
extern PacketHandlerFn g_PacketHandlers[256];
class Client* sm_FindClientByConnection(CServerMgr* pMgr, NetConnection* pConn);  // 0x10028658

// Client+0x18c is the file-transfer state servermessage.cpp calls
// CClientFiles, and these two are that family's message handler and its
// frame pump — not a packet queue. Both were called msg_QueuePacket /
// msg_FlushPackets here on nothing but the shape of their call sites; the
// bodies (reconstructed in servermessage.cpp, 0x1000e9ee byte-identical and
// 0x1000e941 a documented near) read the xfer state's own fields and call
// the xfer_* family. The handler ANSWERS whether it took the packet, which
// is what every other entry in the dispatch table answers too.
class CClientFiles;
int  xfer_HandleMessage(CClientFiles* pState, class ServerMessage* pMsg);  // 0x1000e941
void xfer_Update(CClientFiles* pState, float fTime);                       // 0x1000e9ee

// ---- sm_WriteClientUpdate's machinery (names ours) ---------------------- //

class CUpdateContext;

// The visibility gather handed to the world query (ctor 0x100283c6): center,
// radius, two callbacks and their context.
using IObjectQuery = CUnknown210;

// The gather record GetPointContainers hands the point query (names ours).

int  sm_SkipClientUpdate(class Client* pClient);                            // 0x100285de
void sm_WriteUpdateHeader(CServerMgr* pMgr, CUpdateContext* pCtx,
                          class Client* pClient);                           // 0x10027c97
void sm_Unknown26755(CServerMgr* pMgr, class Client* pClient);              // 0x10026755
void sm_WriteChangedObjects(CServerMgr* pMgr, class CUnknown390* p390,
                            CUpdateContext* pCtx);                          // 0x10027e26
void sm_WriteObjectUpdate(CUpdateContext* pCtx, class LTObject* pObject,
                          CObjInfo* pInfo);                                 // 0x100279d9
void sm_EndUpdatePacket(CUpdateContext* pCtx, class ServerMessage* pMsg,
                        int nA, int nB);                                    // 0x100278ad
void sm_WriteRemovedObjects(CUpdateContext* pCtx, class ServerMessage* pMsg);  // 0x10028454
void sm_WriteIDList(CUpdateContext* pCtx, ClientIDList* pList);             // 0x10027d39
void sm_Unknown27c71(CServerMgr* pMgr, class Client* pClient,
                     class ServerMessage* pMsg);                            // 0x10027c71
void sm_Unknown2873d(CServerMgr* pMgr, class Client* pClient);              // 0x1002873d

// The two gather callbacks the query calls per candidate object.
void sm_VisCallbackA(LTObject* object, void* context);                            // 0x10027c3c

// CNetMgr's globals and C helpers (names ours).
extern int   g_NetLogPackets;                // 0x100584f0
extern float g_NetResendTime;                // 0x100584b8
extern float g_NetAckTime;                   // 0x10053368  1/3 second
extern int   g_NetShowStats;                 // 0x100584ac
extern int   g_NetTimeouts;                  // 0x10051c80
extern char  g_NetNameBuf[];                 // 0x10056ff4
// 0x10028ace — the net layer's release. Not specific to a service: the
// packet queues hand their records and their payloads to the same function
// (name ours).
void  net_Free(void* pMem);
// The third argument is a BYTE. 0x10021cef and 0x10021d16 are both
// `cmpb $0x0,0x10(%ebp)`, which an `int` parameter cannot produce -- and
// LTBOOL will not do either, since the engine typedefs it 32 bits wide.
void  net_LogPacket(class ServerMessage* pMsg, uint32 nLen, uint8 bOut);  // 0x10021cbe
float dsi_GetTime();                         // 0x100383b6


extern int           g_NetBandwidth;        // 0x10058484  target bytes/sec
extern ClientIDList* g_pFillIDList;         // 0x100587a4  the list being refilled
extern uint32        g_Unknown587a0;        // 0x100587a0  gather-tick accumulator
extern int           g_Unknown5845c;        // 0x1005845c

// UpdateObjects' console toggles and accumulator (names ours).
extern int g_bAutoDeactivate;  // 0x1005852c  gates the next-update countdown (declared above too; "AutoDeactivate")
extern int g_ShowClassTicks;   // 0x10058538
extern int g_TotalTicks;       // 0x10058438  frame sum of every class's ticks

// More fixed prototypes of the 0x1000e3f9 printer (see dsi_PrintStat): the
// class-tick dump batches its stack cleanups, so none of these sites was
// compiled varargs. Names ours.
void dsi_PrintString(char* pString);
LTRESULT sm_AttachSound(CServerMgr*, LTObject*, SoundInstance*);
void sm_RemoveObjectAttachments(CServerMgr*, LTObject*);
void sm_DetachSound(CServerMgr* pMgr, class LTObject* pObject,
                    SoundInstance* pSound, int nFlag);

// 0x100257f2 — tear down the object at 0x390.
// int, not void: `xor %eax,%eax` sits inside the epilogue at 0x1002585b,
// exactly as its sibling un390_Init's does. objtypes.cpp had to drop this
// header entirely over the conflict, because a return type is not
// overloadable.
int un390_Term(class CUnknown390* p);

// 0x1000dbe0 — free a hash table outright.


// The engine's shared LTLink free pool (0x10058bdc): Term threads spent links
// onto it through their m_pPrev. Not a global of its own -- it is the free
// list of the link bank at 0x10058bc0 (+0x1c), which is how smgr.cpp reaches
// it; nothing defines or references this name any more.
extern LTLink* g_FreeLinks;

// 0x10035c81 — install (or clear) the console print hook; Term passes the
// server's own hook at 0x10032617 and NULL user data.

void sm_ErrorHook(char* pMsg);               // 0x10032617

// 0x10028fbb — tear down the console state.
void cc_TermState(ConsoleState* pState);

// Empty in release; /OPT:ICF folded it with every other empty function, so
// the identity is unrecoverable (the ctor pairs it with the class manager).


// The generic hash container the model references live in (0x1000dxxx,
// beside dalloc/dfree; the names are ours).




// Keyed lookup over the same tables (names ours): the object-name hash at
// CServerMgr+0x8c0 is what FindNamedObjects walks with these two.



// ILTServer's flat sphere gather and its release (the SDK's
// FindObjectsTouchingSphere / RelinquishList function-pointer entries).
// What the box walk carries for the sphere test: the list being filled and
// the sphere itself, pre-squared and pre-doubled (names ours).
class SphereQuery
{
public:
	struct ObjectList* m_pList;              // +0x00
	LTVector*          m_pPosition;          // +0x04
	float              m_Radius;             // +0x08
	float              m_RadiusSqr;          // +0x0c
	float              m_Diameter;           // +0x10
};

// The two world queries the interface forwards to (names ours). The segment
// test takes the manager's object container and a flag; the shade lookup
// answers in bytes, blue first.
LTBOOL w_IntersectSegment(IntersectQuery* pQuery, IntersectInfo* pInfo,
                          class CUnknown210* pObjects, int nFlags);   // 0x1000edd7
void   w_GetPointShade(WorldShadeGrid* pShadeState, LTVector* pPoint, uint8* pColor);  // 0x1002cf33

// 0x1002f861 — one candidate from the box walk (name ours).
int sm_SphereGatherCB(LTObject* pObj, void* pUser);

struct ObjectList* sm_FindObjectsTouchingSphere(LTVector* pPosition, float radius);   // 0x1002f90d
void sm_RelinquishList(struct ObjectList* pList);                                     // 0x1002f9f4

// DoRunWorld's helpers, all cdecl over the manager (names ours, from what
// they do): 0x100338e1 caches every object's model and skin files;
// 0x100338b8 runs the shell's CacheFiles and frees unused models;
// 0x10032bb8 drains the queued-object list at 0x83c.
LTRESULT sm_CacheObjectFiles(CServerMgr* pMgr);
LTRESULT sm_CacheFiles(CServerMgr* pMgr);

LPBASECLASS sm_AllocateObject(CServerMgr* pMgr, struct ClassDef* pClassDef);   // 0x10001c54
LTRESULT    sm_CreateObjectInWorld(CServerMgr* pMgr, LPBASECLASS pObject,
                                   struct ClassDef* pClassDef, ObjectCreateStruct* pStruct,
                                   uint32 nObjectID, float fData,
                                   LTObject** ppObject);                       // 0x100334fe
void        sm_FreeObject(CServerMgr* pMgr, struct ClassDef* pClassDef,
                          LPBASECLASS pObject);                                // 0x1002abf8
LTRESULT sm_CacheFile(CServerMgr* pMgr, uint32 fileType, char* pFilename);   // 0x10034b7a
void     sm_FlushObjectQueue(CServerMgr* pMgr);

// Term's helpers (names ours, from what they do): remove one client
// (0x10027308, logs "Removing client..."), remove objects (0x100325bd;
// LTTRUE removes even the protected ones), free every model at shutdown
// (0x1002a365), and return m_List44's nodes to their bank (0x1003471d).
void sm_RemoveClient(CServerMgr* pMgr, Client* pClient);
void sm_EndWorldA(CServerMgr* pMgr);                        // 0x1002af34
void sm_EndWorldB(CServerMgr* pMgr);                        // 0x1002aec2
void sm_EndWorldC(CServerMgr* pMgr);                        // 0x10033d65 (in smgr.cpp)
void sm_EndWorldD(CServerMgr* pMgr);                        // 0x100347a4
LTRESULT sm_StartWorldA(CServerMgr* pMgr, int n);               // 0x1003385a

// Init's helpers.
int   un390_Init(CUnknown390* p, int n);                    // 0x100256c7
void  cc_InitState(CServerMgr* pMgr, ConsoleState* pState); // 0x10028f52
void  cc_TermState(ConsoleState* pState);                   // 0x10028fbb (name ours)
CUnknown928Obj* sm_CreateUnknown928(CServerMgr* pMgr);      // 0x1002d34a
void  fs_Init(CFileMgr* pFileMgr, CServerMgr* pMgr);        // 0x1002bbc9
void  sm_ResetProfileCounters();                            // 0x10029ee7
extern uint32 g_Unknown58bec;                               // 0x10058bec
LTRESULT sm_LoadWorldObjects(CServerMgr* pMgr, ILTStream* pStream,
                             char* pszWorldName, uint32 bFlag);  // 0x1002a8b6

// File-manager helpers reached from DoStartWorld.
char* fs_GetFilename(CFileMgr* pFileMgr, void* hFile);           // 0x1002bfcd
int   fs_GetFileIdentifier(CFileMgr* pFileMgr, char* pFilename,
                           int nType, void** phFile);            // 0x1002be8c

// The record behind fs_GetFileIdentifier's handle (opaque elsewhere); the
// client file messages carry its id word (names ours).


// 0x1002b4dc — load (or find) a model by name, handing back the Model and
// its file identifier (names ours; ThreadLoadModel's entry).
LTRESULT sm_LoadModel(CServerMgr* pMgr, char* pFilename, class Model** ppModel,
                      CFileIdentifier** ppFile, int bThreaded, int nFlags);

// 0x1002b5fe — unload a model by name (names ours; UnloadModel's entry).
LTRESULT sm_UnloadModel(CServerMgr* pMgr, char* pFilename, CFileIdentifier* pFile);
void sm_RemoveObjects(CServerMgr* pMgr, int bRemoveAll);
void sm_TermModels(CServerMgr* pMgr);
void sm_FreeList44(CServerMgr* pMgr);

// A second global CServerMgr pointer, in .data at 0x10058bf0, used by the
// old-API wrappers (GetTime, RemoveObject) instead of going through the
// interface object. SetupGlobals refreshes it and its class-manager
// companion (0x10058bf4) on every API entry.
extern CServerMgr* g_pServerMgr;
extern CClassMgr*  g_pClassMgr;

// 0x1002ac20 — remove an object from the world.
void sm_RemoveObject(CServerMgr* pServerMgr, HOBJECT hObj);

class MyServerInterface : public ServerInterface
{
public:
	// g_ServerInterface's initializer 0x1002bff1 (expanded: the two zero
	// stores, +0x08 first, then the vptr). Shipped also has an empty
	// non-virtual destructor (atexit thunk 0x1002c016, the vptr re-store
	// alone; the vtable at 0x1004e24c starts with SetAppHandler). Declared
	// below: with it this TU emits the shipped four-piece initializer group
	// (0x1002bfe7 call/jmp, 0x1002bff1 constructor, 0x1002c00a the atexit
	// registration, 0x1002c016 the vptr re-store). It used to be left out
	// because one more declaration here flipped tcpdriver.cpp's
	// HandleConnPacket (a header symbol-count threshold); measured
	// with it back, HandleConnPacket (0x10036ee5) stays identical.
	NOLF_FORCE_INLINE MyServerInterface()
	{
		m_bInUpdate  = LTFALSE;
		m_pServerMgr = LTNULL;
	}
	~MyServerInterface() {}


	LTRESULT SetAppHandler(ServerAppHandler *pHandler);
	LTRESULT RunConsoleString(char *pStr);
	LTRESULT GetConsoleVar(char *pName, HCONSOLEVAR *hVar, char *pDefaultVal);
	LTRESULT GetVarValueFloat(HCONSOLEVAR hVar, float *val);
	LTRESULT GetVarValueString(HCONSOLEVAR hVar, char *pStr, uint32 maxLen);
	LTRESULT LoadConfigFile(char *pFilename);
	LTRESULT SaveConfigFile(char *pFilename);
	LTRESULT SendToServerShell(char *pInfo, int nLen);
	LTBOOL AddResources(char **pResources, uint32 nResources);
	LTBOOL LoadBinaries();
	FileEntry* GetFileList(char *pDirName);
	void FreeFileList(FileEntry *pList);
	LTRESULT OpenFile(char *pFilename, ILTStream **pStream);
	LTRESULT CopyFile(const char *pszSourceFile, const char *pszDestFile);
	LTBOOL SetGameInfo(void *pGameInfo, uint32 nGameInfoLen);
	int GetNumClients();
	LTBOOL GetClientName(int index, char *pName, int maxChars);
	LTBOOL GetClientInfo(int index, ClientInfo* pInfo);
	LTBOOL BootClient(uint32 dwClientID);
	LTRESULT GetClientPing(uint32 nClientId, float &ping);
	int GetErrorCode();
	void GetErrorString(char *pString, int maxLen);
	LTBOOL Update(long flags);
	LTBOOL InitNetworking(char *pDriver, uint32 dwFlags);
	LTBOOL GetServiceList(NetService *&pListHead);
	LTBOOL FreeServiceList(NetService *pListHead);
	LTBOOL SelectService(HNETSERVICE hNetService);
	LTBOOL UpdateSessionName(const char* sName);
	LTBOOL HostGame(NetHost &hostInfo);
	LTBOOL GetTcpIpAddress(char* sAddress, uint32 dwBufferSize, unsigned short &hostPort);
	LTRESULT SendTo(const void *pData, uint32 len, const char *sAddr, uint32 port);

public:
	CServerMgr* m_pServerMgr;                // 0x04 (0x00 is the vtable)
	LTBOOL      m_bInUpdate;                 // 0x08  set across CServerMgr::Update
};

// The one server interface, in .data at 0x10058bb0 (server.cpp). Its
// m_pServerMgr (0x10058bb4) doubles as CreateServer's already-instanced flag.
extern MyServerInterface g_ServerInterface;

#endif  // __SERVERMGR_H__
