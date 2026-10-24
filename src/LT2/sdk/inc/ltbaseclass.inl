// SDK base behavior is inline so its C++ type information is available to
// both the native engine and dynamically loaded object modules.
#include <cstdio>
inline void BaseClass::AddAggregate(LPAGGREGATE pAggregate)
{
	pAggregate->m_pNextAggregate = m_pFirstAggregate;
	m_pFirstAggregate = pAggregate;
}


inline LTBOOL BaseClass::RemoveAggregate(LPAGGREGATE pAggregate)
{
	if (!m_pFirstAggregate)
		return LTFALSE;
	
	// See if the aggregate is the first thing on the list...
	if(m_pFirstAggregate == pAggregate)
	{
		m_pFirstAggregate = m_pFirstAggregate->m_pNextAggregate;
		return LTTRUE;
	}
	else
	{
		// Start on the second item on the list...
		LPAGGREGATE pPrevAggregate = m_pFirstAggregate;
		LPAGGREGATE pCurAggregate = pPrevAggregate->m_pNextAggregate;;

		while (pCurAggregate)
		{
			if (pCurAggregate == pAggregate)
			{
				pPrevAggregate->m_pNextAggregate = pCurAggregate->m_pNextAggregate;
				return LTTRUE;
			}

			pPrevAggregate = pCurAggregate;
			pCurAggregate = pCurAggregate->m_pNextAggregate;
		}
	}

	return LTFALSE;
}


inline uint32 BaseClass::EngineMessageFn(uint32 messageID, void *pData, float fData)
{
	LPAGGREGATE pAggregate;
	ObjectCreateStruct *pStruct;
	GenericProp genProp;
	uint32 i, iSkin;
	char skinPropName[128];
	ConParse conParse;
	ILTServer *pServerLT;


	pServerLT = m_pServerDE;

	// Handle ReadProp.
	switch(messageID)
	{
		case MID_PRECREATE:
		{
			pStruct = (ObjectCreateStruct*)pData;
			
			// If they haven't already set the type, set it to whatever the constructor set.
			if(pStruct->m_ObjectType == OT_NORMAL)
			{
				pStruct->m_ObjectType = m_nType;
			}

			// Get the props.
			if( fData == PRECREATE_WORLDFILE || fData == PRECREATE_STRINGPROP )
			{
				if( pServerLT->GetPropGeneric( "Name", &genProp ) == LT_OK )
				{
					SAFE_STRCPY(pStruct->m_Name, genProp.m_String);
					pStruct->m_Name[MAX_CS_FILENAME_LEN] = '\0';
				}
				if( pServerLT->GetPropGeneric( "Pos", &genProp ) == LT_OK )
				{
					pStruct->m_Pos = genProp.m_Vec;
				}
				if( pServerLT->GetPropGeneric( "Rotation", &genProp ) == LT_OK )
				{
					pStruct->m_Rotation = genProp.m_Rotation;
				}
				if( pServerLT->GetPropGeneric( "Flags", &genProp ) == LT_OK )
				{
					pStruct->m_Flags = genProp.m_Long;
				}
				if( pServerLT->GetPropGeneric( "Visible", &genProp ) == LT_OK )
				{
					if( genProp.m_Bool )
						pStruct->m_Flags |= FLAG_VISIBLE;
					else
						pStruct->m_Flags &= ~FLAG_VISIBLE;
				}
				if( pServerLT->GetPropGeneric( "Shadow", &genProp ) == LT_OK )
				{
					if( genProp.m_Bool )
						pStruct->m_Flags |= FLAG_SHADOW;
					else
						pStruct->m_Flags &= ~FLAG_SHADOW;
				}
				if( pServerLT->GetPropGeneric( "RotateableSprite", &genProp ) == LT_OK )
				{
					if( genProp.m_Bool )
						pStruct->m_Flags |= FLAG_ROTATEABLESPRITE;
					else
						pStruct->m_Flags &= ~FLAG_ROTATEABLESPRITE;
				}
				if( pServerLT->GetPropGeneric( "Solid", &genProp ) == LT_OK )
				{
					if( genProp.m_Bool )
						pStruct->m_Flags |= FLAG_SOLID;
					else
						pStruct->m_Flags &= ~FLAG_SOLID;
				}
				if( pServerLT->GetPropGeneric( "Gravity", &genProp ) == LT_OK )
				{
					if( genProp.m_Bool )
						pStruct->m_Flags |= FLAG_GRAVITY;
					else
						pStruct->m_Flags &= ~FLAG_GRAVITY;
				}
				if( pServerLT->GetPropGeneric( "TouchNotify", &genProp ) == LT_OK )
				{
					if( genProp.m_Bool )
						pStruct->m_Flags |= FLAG_TOUCH_NOTIFY;
					else
						pStruct->m_Flags &= ~FLAG_TOUCH_NOTIFY;
				}
				
				if( pServerLT->GetPropGeneric( "Rayhit", &genProp ) == LT_OK )
				{
					if( genProp.m_Bool )
						pStruct->m_Flags |= FLAG_RAYHIT;
					else
						pStruct->m_Flags &= ~FLAG_RAYHIT;
				}
				
				if( pServerLT->GetPropGeneric( "Filename", &genProp ) == LT_OK )
				{
					SAFE_STRCPY(pStruct->m_Filename, genProp.m_String);
					pStruct->m_Filename[MAX_CS_FILENAME_LEN] = '\0';
				}
				
				if( pServerLT->GetPropGeneric( "Skin", &genProp ) == LT_OK )
				{
					iSkin = 0;
					conParse.Init(genProp.m_String);
					while(pServerLT->Common()->Parse(&conParse) == LT_OK)
					{
						if(conParse.m_nArgs > 0)
						{
							SAFE_STRCPY(pStruct->m_SkinNames[iSkin], conParse.m_Args[0]);
							iSkin++;
						}

						if(iSkin >= MAX_MODEL_TEXTURES)
							break;
					}
					pStruct->m_SkinName[MAX_CS_FILENAME_LEN] = '\0';
				}

				if(pServerLT->GetPropGeneric("Chromakey", &genProp) == LT_OK)
				{
					if(genProp.m_Bool)
						pStruct->m_Flags2 |= FLAG2_CHROMAKEY;
					else
						pStruct->m_Flags2 &= ~FLAG2_CHROMAKEY;
				}
				
				for(i=0; i < MAX_MODEL_TEXTURES; i++)
				{
					sprintf(skinPropName, "Skin%d", i);

					if( pServerLT->GetPropGeneric( skinPropName, &genProp ) == LT_OK )
					{
						SAFE_STRCPY(pStruct->m_SkinNames[i], genProp.m_String);
					}
				}

				break;
			}
		}
	}

	// Call the aggregates.
	pAggregate = m_pFirstAggregate;
	while(pAggregate)
	{
		pAggregate->EngineMessageFn(this, messageID, pData, fData);
		pAggregate = pAggregate->m_pNextAggregate;
	}

	// Default return is 1.
	return 1;
}


inline uint32 BaseClass::ObjectMessageFn(HOBJECT hSender, uint32 messageID, HMESSAGEREAD hRead)
{
	LPAGGREGATE pAggregate;

	// Call the aggregates.
	pAggregate = m_pFirstAggregate;
	while(pAggregate)
	{
		pAggregate->ObjectMessageFn(this, hSender, messageID, hRead);
		pAggregate = pAggregate->m_pNextAggregate;
	}

	return 1;
}


