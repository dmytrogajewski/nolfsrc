#include "engine/portable/string.h"
#include "engine/portable/text.h"
#include <cstdio>
#include <cstring>
/****************************************************************************
;
;	 MODULE:		IPMGR (.CPP)
;
;	PURPOSE:		IP Manager Classes
;
;	HISTORY:		11/09/98 [blg] This file was created
;
;	COMMENT:		Copyright (c) 1998, Monolith Productions Inc.
;
****************************************************************************/


// Includes...

#include "Stdafx.h"
#include "iltclient.h"
#include "IpMgr.h"


// Functions...

/* *********************************************************************** */
/* CIp                                                                     */

// ----------------------------------------------------------------------- //
//
//	ROUTINE:	CIp::Init
//
//	PURPOSE:	Initialization
//
// ----------------------------------------------------------------------- //

BOOL CIp::Init(char* sIp)
{
	// Sanity checks...

	if (!sIp) return(FALSE);
	if (sIp[0] == '\0') return(FALSE);


	// Set simple members...

	strncpy(m_sIp, sIp, IPM_MAX_ADDRESS);


	// All done...

	return(TRUE);
}


/* *********************************************************************** */
/* CIpMgr                                                                     */

// ----------------------------------------------------------------------- //
//
//	ROUTINE:	CIpMgr::Init
//
//	PURPOSE:	Initialization
//
// ----------------------------------------------------------------------- //

BOOL CIpMgr::Init(ILTClient* pClientDE)
{
	// Sanity checks...

	if (!pClientDE) return(FALSE);


	// Set simple members...

	Clear();

	m_pClientDE = pClientDE;


	// All done...

	return(TRUE);
}


// ----------------------------------------------------------------------- //
//
//	ROUTINE:	CIpMgr::Term
//
//	PURPOSE:	Termination
//
// ----------------------------------------------------------------------- //

void CIpMgr::Term()
{
	// Delete all the ips...

	for (int i = 0; i < m_cIps; i++)
	{
		CIp* pIp = GetIp(i);
		if (pIp)
		{
			debug_delete(pIp);
		}
	}

	Clear();
}


// ----------------------------------------------------------------------- //
//
//	ROUTINE:	CIpMgr::ExistIp
//
//	PURPOSE:	Determines if the given ip exists
//
// ----------------------------------------------------------------------- //

BOOL CIpMgr::ExistIp(char* sIp)
{
	// Sanity checks...

	if (!sIp) return(FALSE);


	// Loop through each ip...

	for (int i = 0; i < m_cIps; i++)
	{
		CIp* pIp = GetIp(i);
		if (pIp)
		{
			if (strcmp(sIp, pIp->GetAddress()) == 0)
			{
				return(TRUE);
			}
		}
	}


	// If we get here, the ip doesn't exist...

	return(FALSE);
}


// ----------------------------------------------------------------------- //
//
//	ROUTINE:	CIpMgr::AddIp
//
//	PURPOSE:	Adds the given ip
//
// ----------------------------------------------------------------------- //

BOOL CIpMgr::AddIp(char* sIp)
{
	// Sanity checks...

	if (!sIp) return(FALSE);


	// Make sure this ip doesn't already exist...

	if (ExistIp(sIp)) return(TRUE);


	// Make sure there is room to add this ip...

	if (m_cIps >= IPM_MAX_IPS) return(FALSE);


	// Create a new ip...

	CIp* pIp = debug_new(CIp);
	if (!pIp) return(FALSE);

	if (!pIp->Init(sIp))
	{
		debug_delete(pIp);
		return(FALSE);
	}


	// Add this ip...

	m_aIps[m_cIps++] = pIp;


	// All done...

	return(TRUE);
}


// ----------------------------------------------------------------------- //
//
//	ROUTINE:	CIpMgr::RemoveIp
//
//	PURPOSE:	Removes the given ip
//
// ----------------------------------------------------------------------- //

BOOL CIpMgr::RemoveIp(char* sIp)
{
	// Sanity checks...

	if (!sIp) return(FALSE);


	// Loop through each ip...

	for (int i = 0; i < m_cIps; i++)
	{
		CIp* pIp = GetIp(i);
		if (pIp)
		{
			if (strcmp(sIp, pIp->GetAddress()) == 0)
			{
				// Delete this ip...

				debug_delete(pIp);


				// Shift the array...

				for (int j = i + 1; j < m_cIps; j++)
				{
					m_aIps[j-1] = m_aIps[j];
				}

				m_cIps--;


				// All done removing the ip...

				return(TRUE);
			}
		}
	}


	// If we get here, the ip doesn't exist so we can't remove it...

	return(FALSE);
}


// ----------------------------------------------------------------------- //
//
//	ROUTINE:	CIpMgr::GetAllIpString
//
//	PURPOSE:	Returns a big huge string with all the ip addresses
//				sperated by semi-colons.
//
// ----------------------------------------------------------------------- //

BOOL CIpMgr::GetAllIpString(char* sBuf, int nBufSize)
{
	// Sanity checks...

	if (!sBuf) return(FALSE);
	if (nBufSize <= 0) return(FALSE);


	// Start building the string, making sure we don't over-run the buffer...

	int nTotalSize = 0;

	strcpy(sBuf, "");

	for (int i = 0; i < m_cIps; i++)
	{
		CIp* pIp = GetIp(i);

		if (pIp)
		{
			char* sIp = pIp->GetAddress();
			if (sIp)
			{
				int nLen = strlen(sIp);

				if (nTotalSize + nLen + 2 < nBufSize)
				{
					if (nTotalSize > 0) strcat(sBuf, ";");
					strcat(sBuf, sIp);
					nTotalSize = strlen(sBuf);
				}
			}
		}
	}


	// All done...

	return(TRUE);
}


// ----------------------------------------------------------------------- //
//
//	ROUTINE:	CIpMgr::RemoveAll
//
//	PURPOSE:	Removes all of the ips
//
// ----------------------------------------------------------------------- //

void CIpMgr::RemoveAll()
{
	// Delete all the ips...

	for (int i = 0; i < m_cIps; i++)
	{
		CIp* pIp = GetIp(i);
		if (pIp)
		{
			debug_delete(pIp);
		}
	}

	Clear(FALSE);
}


// ----------------------------------------------------------------------- //
//
//	ROUTINE:	CIpMgr::WriteIps
//
//	PURPOSE:	Writes the ip addresses to the config file
//
// ----------------------------------------------------------------------- //

int CIpMgr::WriteIps()
{
	// Sanity checks...

	if (!m_pClientDE) return(0);


	// Write each ip address...

	char sKey[64];
	char sTemp[512];
	int  cIps = 0;

	for (int i = 0; i < m_cIps; i++)
	{
		CIp* pIp = GetIp(i);

		if (pIp)
		{
			std::snprintf(sKey,sizeof(sKey), "Ip%i", i);
			std::snprintf(sTemp,sizeof(sTemp), "+%s %s", sKey, pIp->GetAddress());
			m_pClientDE->RunConsoleString(sTemp);
			cIps++;
		}
	}


	// Write out the count...

	std::snprintf(sTemp,sizeof(sTemp), "+IpCount %i", cIps);
	m_pClientDE->RunConsoleString(sTemp);


	// All done...

	return(cIps);
}


// ----------------------------------------------------------------------- //
//
//	ROUTINE:	CIpMgr::ReadIps
//
//	PURPOSE:	Reads the ip addresses from the config file
//
// ----------------------------------------------------------------------- //

int CIpMgr::ReadIps()
{
	// Sanity checks...

	if (!m_pClientDE) return(0);


	// Read the ip address count value...

	int cIps = 0;

	HCONSOLEVAR hVar = m_pClientDE->GetConsoleVar("IpCount");
	if (hVar)
	{
		cIps = (int)m_pClientDE->GetVarValueFloat(hVar);
	}

	if (cIps <= 0) return(0);


	// Read each ip address...

	char sKey[64];
	int  count = 0;

	for (int i = 0; i < cIps; i++)
	{
		std::snprintf(sKey,sizeof(sKey), "Ip%i", i);

		HCONSOLEVAR hVar = m_pClientDE->GetConsoleVar(sKey);
		if (hVar)
		{
			char* sValue = m_pClientDE->GetVarValueString(hVar);
			if (sValue)
			{
				if (AddIp(sValue)) count++;
			}
		}
	}


	// All done...

	return(count);
}

