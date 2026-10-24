// *********************************************************************** //
//
//  MODULE:   BASESTREAM (.H)
//
//  PURPOSE:  The generic half of ILTStream. Three of the interface's eleven
//            virtuals have one implementation shared by every stream in the
//            image — ReadString, WriteStream and WriteString all point at
//            0x10011314, 0x10011402 and 0x100113ce from all four vtables —
//            because none of them needs to know where the bytes come from:
//            each is written in terms of the Read and Write the concrete
//            stream supplies.
//
//            The base is also what the concrete destructors restore their
//            vptr to (0x1004d2d8), which is what says it exists at all.
//
//            The class name and the file name are ours; the method names
//            are the SDK's.
//
// *********************************************************************** //
#ifndef __BASESTREAM_H__
#define __BASESTREAM_H__

#include "iltstream.h"

class CBaseStream : public ILTStream
{
public:
	// Length-prefixed: a word of length, then the bytes, and nothing else —
	// no terminator goes on the wire.
	LTRESULT ReadString(char* pStr, uint32 maxBytes);  // 0x10011314
	LTRESULT WriteStream(ILTStream& dsSource, uint32 dwMin,
	                     uint32 dwMax);                // 0x10011402
	LTRESULT WriteString(char* pStr);                         // 0x100113ce
};

#endif  // __BASESTREAM_H__
