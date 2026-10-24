// FolderOptions.h: interface for the CFolderOptions class.
//
//////////////////////////////////////////////////////////////////////

#ifndef _FOLDER_OPTIONS_H_
#define _FOLDER_OPTIONS_H_

#pragma once

#include "BaseFolder.h"

class CFolderOptions : public CBaseFolder
{
public:
	CFolderOptions();
	virtual ~CFolderOptions();

	// Build the folder
    LTBOOL   Build();

    void    OnFocus(LTBOOL bFocus);

protected:
    uint32  OnCommand(uint32 dwCommand, uint32 dwParam1, uint32 dwParam2);

};

#endif // _FOLDER_OPTIONS_H_