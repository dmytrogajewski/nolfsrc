
%{

#include "Stdafx.h"
#include "AnimationLex.h"
#include "AnimationLexSymbols.h"
#include "AnimationParse.h"
#include <vector>

using namespace Animation;

extern "C" int yyanimationwrap();

#define YY_NEVER_INTERACTIVE 1

// STRING \"[^\"]*\"

CAnimationLex g_Lex;

%}

PROPERTIES		"[properties]"
ANIMATIONS		"[animations]"
TRANSITIONS		"[transitions]"
CHARACTER		[a-zA-Z0-9]|"-"
STRING			{CHARACTER}+
WSPACE			[\ \t\n\r]+
COMMA			","
BRACE_LEFT		"{"
BRACE_RIGHT		"}"
SET_ADD			"+"
SET_REMOVE		"~"
SET_CONSTANT	"="
SET_NOT			"!"

%%

{PROPERTIES}		{ 								return PROPERTIES;		}
{ANIMATIONS}		{ 								return ANIMATIONS;		}
{TRANSITIONS}		{								return TRANSITIONS;		}
{STRING}			{ g_Lex.HandleString();			return STRING;			}
{BRACE_LEFT}		{								return BRACE_LEFT;		}
{BRACE_RIGHT}		{								return BRACE_RIGHT;		}
{COMMA}				{								return COMMA;			}
{SET_ADD}			{								return SET_ADD;			}
{SET_REMOVE}		{								return SET_REMOVE;		}
{SET_CONSTANT}		{								return SET_CONSTANT;	}
{SET_NOT}			{								return SET_NOT;			}
"//"				{ g_Lex.HandleComment();								}
\n					{ g_Lex.HandleNewline();								}
{WSPACE}			{														}
.					{								return ERROR_TOKEN;		}

%%

CAnimationLex::CAnimationLex()
{
	m_nLine = 1;
}

CAnimationLex::~CAnimationLex()
{
	yyanimationlex_destroy();
}

void CAnimationLex::HandleComment()
{
	char ch;
	while ((ch = yyinput()) != '\n' && ch != 0 && ch != -1);
	
	m_nLine++;
}

void CAnimationLex::HandleString()  
{
	strcpy(yyanimationlval.szString, yytext); 
}

void CAnimationLex::HandleNewline()
{
	m_nLine++;
}

LTBOOL CAnimationLex::Lex(const char* szFile)
{
    m_nLine = 1;
    ILTStream* pDStream;
    LTRESULT dr = g_pLTServer->OpenFile((char*)szFile, &pDStream);

    if (dr != LT_OK) return(LTFALSE);

	// Read the file...

	struct CloseStream { ILTStream* stream; ~CloseStream() { stream->Release(); } } close{pDStream};
	const auto length = pDStream->GetLen();
	if (length > 16 * 1024 * 1024) return LTFALSE;
	// Flex borrows this buffer only for the duration of the parse.
	std::vector<char> data(static_cast<std::size_t>(length) + 2, 0);
	if (pDStream->Read(data.data(), length) != LT_OK) return LTFALSE;
	auto* buffer = yyanimation_scan_buffer(data.data(), data.size());
	if (!buffer) return LTFALSE;
	struct CloseBuffer { YY_BUFFER_STATE buffer; ~CloseBuffer() { yy_delete_buffer(buffer); } } scan{buffer};
	return yyanimationparse() == 0 ? LTTRUE : LTFALSE;
}
