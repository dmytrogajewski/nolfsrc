
// Console variable tracker.. makes it easy to get and set the value of
// console variables.

#ifndef __CVARTRACK_H__
#define __CVARTRACK_H__


    #include "iltserver.h"


	class CVarTrack
	{
	public:
#ifdef NOLFSRC_NATIVE
		// Module statics can survive multiple engine instances. A new shell
		// retires every cached interface and console handle before using them.
		static void BeginSession() { ++s_Session; }
#endif

					CVarTrack()
					{
                        m_hVar = LTNULL;
                        m_pServerDE = LTNULL;
						m_pVarName = NULL;
					}

        LTBOOL       Init(ILTServer *pServerDE, char *pVarName, char *pStartVal, float fStartVal)
		{
			char tempStr[128];
			m_pServerDE = LTNULL;
			m_hVar = LTNULL;
			if (!pServerDE || !pVarName || !*pVarName) return LTFALSE;

			m_pVarName = pVarName;
			if(!pStartVal)
			{
				sprintf(tempStr, "%5f", fStartVal);
				pStartVal = tempStr;
			}

			m_hVar = pServerDE->GetGameConVar(pVarName);
			if(!m_hVar)
			{
				pServerDE->SetGameConVar(pVarName, pStartVal);
				m_hVar = pServerDE->GetGameConVar(pVarName);
				if(!m_hVar)
				{
                    return LTFALSE;
				}
			}

			m_pServerDE = pServerDE;
#ifdef NOLFSRC_NATIVE
			m_Session = s_Session;
#endif
            return LTTRUE;
		}

        LTBOOL       IsInitted()
		{
			return m_pServerDE && m_hVar
#ifdef NOLFSRC_NATIVE
				&& m_Session == s_Session
#endif
				;
		}

		float		GetFloat(float defVal=0.0f)
		{
			if(IsInitted())
				return m_pServerDE->GetVarValueFloat(m_hVar);
			else
				return defVal;
		}

		char*		GetStr(char *pDefault="")
		{
			char *pRet;

			if(IsInitted())
			{
				if(pRet = m_pServerDE->GetVarValueString(m_hVar))
					return pRet;
			}
			return pDefault;
		}

		void		SetFloat(float val)
		{
			char str[128];

			if(!IsInitted() || !m_pVarName)
				return;

			sprintf(str, "%f", val);
			m_pServerDE->SetGameConVar(m_pVarName, str);
		}

		void		SetStr(char *pStr)
		{
			if(!IsInitted() || !m_pVarName)
				return;

			m_pServerDE->SetGameConVar(m_pVarName, pStr);
		}

	protected:
#ifdef NOLFSRC_NATIVE
		inline static uint64 s_Session = 1;
		uint64 m_Session = 0;
#endif

		HCONVAR		m_hVar;
        ILTServer   *m_pServerDE;
		char		*m_pVarName;
	};


#endif  // __CVARTRACK_H__

