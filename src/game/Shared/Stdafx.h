// stdafx.h : include file for standard system include files,
//  or project specific include files that are used frequently, but
//      are changed infrequently
//

#ifndef __STDAFX_H__
#define __STDAFX_H__

#include <stdio.h>
#include <limits.h>
#include "AssertMgr.h"

#include "mfcstub.h"

#include "DebugNew.h"

#include "iltclient.h"
#include "iltserver.h"
#include "iltmessage.h"
#include "Globals.h"

#include "iltmodel.h"
#include "ilttransform.h"
#include "iltphysics.h"
#include "iltmath.h"

#include "Factory.h"

#include "CommonUtilities.h"
#ifdef _CLIENTBUILD
#include "../ClientShellDLL/VKDefs.h"
#endif

#endif // __STDAFX_H__
