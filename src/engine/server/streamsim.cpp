// Stream module lifetime and the legacy SDK open entry point.
#include "engine/portable/file_stream.h"
#include "engine/portable/allocation.h"
#include "ltlink.h"
#include <string_view>

ILTStream* streamsim_Open(const char* filename, const char* mode) {
    if (!filename || !*filename || !mode) return nullptr;
    using nolf::platform::FileAccess;
    FileAccess access;
    const std::string_view requested(mode);
    if (requested == "rb") access = FileAccess::Read;
    else if (requested == "wb") access = FileAccess::Write;
    else if (requested == "r+b" || requested == "rb+") access = FileAccess::ReadWrite;
    else return nullptr;
    try { return nolf::OpenFileStream(std::filesystem::u8path(filename),access); }
    catch (const std::exception&) { return nullptr; }
}

LTLink g_StreamList;
static uint32 g_nStreamRefs;

// 0x10035c2a and 0x10035c49 — reference counted, and the first caller in ties
// the list off rather than trusting the .bss zeroes: a zeroed head is not an
// empty circular list, it is a broken one.
//
// TieOff is the SDK's `m_pPrev = m_pNext = this`, which assigns right to left,
// so the next pointer is stored before the prev one -- exactly the order the
// shipped code writes 0x10058bfc then 0x10058bf8 in, at both call sites.
void stream_Init()
{
	if (g_nStreamRefs == 0)
		g_StreamList.TieOff();

	g_nStreamRefs++;
}

// The drain walks the list through m_pPrev, not m_pNext -- backwards from the
// head, so the most recently added stream is released first. Each node's own
// link is read before its data is freed, because the free takes the whole
// record the link sits in.
void stream_Term()
{
	if (g_nStreamRefs && --g_nStreamRefs == 0)
	{
		LTLink* pHead = &g_StreamList;
		LTLink* pLink = pHead->m_pPrev;

		while (pLink != pHead)
		{
			LTLink* pPrev = pLink->m_pPrev;

			dfree(pLink->m_pData);

			pLink = pPrev;
		}

		pHead->TieOff();
	}
}
