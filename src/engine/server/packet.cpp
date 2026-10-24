// Force the CPacket template instantiations the images carry.
//
// The templates live in packet.h. VC6 parses `template void CPacket::WriteType
// (uint8);` and then emits nothing for it, so the instantiation has to be
// forced some other way. This driver is ours — it exists in no shipped binary
// and is excluded from the byte comparison.
//
// CALLING each one, which is what this did until now, DOES NOT WORK and had
// been silently producing nothing for either image. A member template defined
// in the class body is an expansion candidate with or without the keyword, so
// at /Ob1 VC6 expands every call right here into the driver and emits no
// standalone COMDAT at all — measured: seven symbols in this object, not one
// of them a ReadType or a WriteType. The same thing cost net_TracePacketQ454
// 964 bytes for the same reason at sixteen call sites.
//
// TAKING THE ADDRESS is what works. A pointer-to-member needs a real function
// to point at, so the body must be emitted, and it is emitted as the COMDAT
// the shipped images carry. The type rides in on the pointer's own type,
// which also sidesteps the thing packet.h's header note describes: VC6 cannot
// parse explicit template arguments on a member, so `&CPacket::ReadType<T>`
// is not available and deduction from the target type is the only route.
#include "packet.h"
#include "object_pool.h"
#include "ltlink.h"

typedef void   (CPacket::*PacketWriteFn8)(uint8);
typedef void   (CPacket::*PacketWriteFn16)(uint16);
typedef void   (CPacket::*PacketWriteFn32)(uint32);
typedef uint8  (CPacket::*PacketReadFn8)(uint8*);
typedef uint16 (CPacket::*PacketReadFn16)(uint16*);
typedef uint32 (CPacket::*PacketReadFn32)(uint32*);

PacketWriteFn8  g_pPacketWrite8  = &CPacket::WriteType;
PacketWriteFn16 g_pPacketWrite16 = &CPacket::WriteType;
PacketWriteFn32 g_pPacketWrite32 = &CPacket::WriteType;
PacketReadFn8   g_pPacketRead8   = &CPacket::ReadType;
PacketReadFn16  g_pPacketRead16  = &CPacket::ReadType;
PacketReadFn32  g_pPacketRead32  = &CPacket::ReadType;


// 0x1000c537, 0x1000c690 and 0x1000d020 are CMoArray<uint8, DefaultCache>'s
// Insert2, _AllocateTArray and _DeleteAndDestroyArray (collection.h), which
// the WriteType instantiations above bring into this object: CPacket::m_Data
// is a real CMoArray<uint8>. server.dll's /O1 build calls Insert2 out of line
// from every grow loop; lithtech.exe's /O2 build expands it into WriteType
// (0x004303c0 and its siblings), calling BaseNew<uint8> (0x0040c060) and
// BaseDelete<uint8> (0x004152b0) from inside the expansion.
//
// These were once reconstructed here by hand as `CPacketData::SetAtGrow`,
// AllocItems and FreeItems on a flat view of the array. The shapes the bytes
// show -- the element count cached across the guard, the buffer reloaded in
// both copy loops, the two `return LTFALSE` sites tail-merged, the thiscall
// allocator that never reads `this`, the free whose count is never read --
// are exactly the template's.


// *********************************************************************** //
//  The BSP line query's recursion, 0x10013bf5.
//
//  It follows the two link workers in the image and is declared in
//  worldsim.h as bspq_IntersectLineRec; the views below are again local so
//  this file stays free of the world headers. Everything the SDK already
//  has — LTPlane, LTVector, IntersectQuery, PolyFilterFn — comes in through
//  packet.h's ltbasedefs.h.
// *********************************************************************** //

// worldsim.h's BSPNode and the two hops from it to its plane (the walk
// WorldPolyOwner::GetPlane makes). Both low bits of the flag byte at 0x16
// mark a leaf.
