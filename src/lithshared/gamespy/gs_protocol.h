#pragma once
#include "engine/portable/string.h"
#include <cstring>

namespace nolf::gamespy {
inline void validate(const char* sChallenge, const char* sKey, char* sOut)
{
	static const char s_aDigits[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	unsigned char aState[256];
	unsigned char aMixed[66];
	int nKeyLen = (int)strlen(sKey);
	int i, n;

	// key schedule
	for (i = 0; i < 256; i++) aState[i] = (unsigned char)i;
	if (nKeyLen > 0)
	{
		unsigned char j = 0;
		for (i = 0; i < 256; i++)
		{
			j = (unsigned char)(j + aState[i] + (unsigned char)sKey[i % nKeyLen]);
			unsigned char t = aState[i]; aState[i] = aState[j]; aState[j] = t;
		}
	}

	// one keystream byte per challenge character; the walk is driven by
	// the challenge itself
	unsigned char a = 0, b = 0;
	for (n = 0; sChallenge[n] && n < 64; n++)
	{
		unsigned char c = (unsigned char)sChallenge[n];
		a = (unsigned char)(a + c + 1);
		unsigned char x = aState[a];
		b = (unsigned char)(b + x);
		unsigned char y = aState[b];
		aState[b] = x;
		aState[a] = y;
		aMixed[n] = (unsigned char)(c ^ aState[(unsigned char)(x + y)]);
	}
	while (n % 3) aMixed[n++] = 0;

	// base64, without padding characters
	char* p = sOut;
	for (i = 0; i < n; i += 3)
	{
		unsigned char x = aMixed[i], y = aMixed[i+1], z = aMixed[i+2];
		*p++ = s_aDigits[x >> 2];
		*p++ = s_aDigits[((x & 3) << 4) | (y >> 4)];
		*p++ = s_aDigits[((y & 15) << 2) | (z >> 6)];
		*p++ = s_aDigits[z & 63];
	}
	*p = '\0';
}

inline bool find_key(const char* sData, const char* sKey, char* sOut, int nOutSize)
{
    if (!sData || !sKey || !sOut || nOutSize <= 0) return false;
	const char* p = sData;
	int nKeyLen = (int)strlen(sKey);

	sOut[0] = '\0';
	while (p && *p)
	{
		if (*p != '\\') { p = strchr(p, '\\'); continue; }
		p++;
		const char* pEnd = strchr(p, '\\');
		if (!pEnd) return false;

		bool bMatch = ((int)(pEnd - p) == nKeyLen && nolf::ascii_compare_n(p, sKey, nKeyLen) == 0);
		p = pEnd + 1;
		const char* pValEnd = strchr(p, '\\');
		int nLen = pValEnd ? (int)(pValEnd - p) : (int)strlen(p);

		if (bMatch)
		{
			if (nLen > nOutSize - 1) nLen = nOutSize - 1;
			memcpy(sOut, p, nLen);
			sOut[nLen] = '\0';
			return true;
		}
		p = pValEnd;
	}

	return false;
}
}
