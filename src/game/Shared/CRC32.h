//////////////////////////////////////////////////////////////////////////////
// CRC-32 calculation toolkit

#ifndef __CRC32_H__
#define __CRC32_H__

namespace CRC32
{
	uint32 CalcFileCRC(const char *pFilename);
	uint32 CalcDataCRC(const void *pData, uint32 nDataSize);
	uint32 CalcRezFileCRC(const char *pFilename);
#ifdef NOLFSRC_NATIVE
	// Imported retail identity used by the original LAN protocol. Fails when
	// metadata is absent instead of treating the stripped executable as valid.
	bool ClientShellCompatibilityCRC(uint32& result);
#endif
};

#endif //__CRC32_H__
