#if !defined(_BUTEMGR_H_)
#define _BUTEMGR_H_

#include "limits.h"
#include "float.h"
#include <iosfwd>
#include <string>
#include <utility>
#include "mfcstub.h"
#include "attribute_table.h"
#include "../cryptmgr/cryptmgr.h"
#include "../butemgr/avector.h"
#include "../butemgr/arange.h"

#if defined(_USE_REZFILE_)
#include "rezmgr.h"
#endif

#include "../lith/lithtypes.h"


#ifndef NO_PRAGMA_LIBS
	#if defined(_DEBUG) && defined(__AFX_H__) && defined(_AFXDLL)
		#pragma comment (lib, "\\Proj\\Libs\\Debug\\ButeMgrMfcDll.lib")
	#elif !defined(_DEBUG) && defined(__AFX_H__) && defined(_AFXDLL)
		#pragma comment (lib, "\\Proj\\Libs\\Release\\ButeMgrMfcDll.lib")
	#elif defined(_DEBUG) && defined(__AFX_H__) && !defined(_AFXDLL)
		#pragma comment (lib, "\\Proj\\Libs\\Debug\\ButeMgr.lib")
	#elif !defined(_DEBUG) && defined(__AFX_H__) && !defined(_AFXDLL)
		#pragma comment (lib, "\\Proj\\Libs\\Release\\ButeMgr.lib")
	#elif defined(_DEBUG) && !defined(__AFX_H__)
		#pragma comment (lib, "\\Proj\\Libs\\Debug\\ButeMgrNoMFC.lib")
	#elif !defined(_DEBUG) && !defined(__AFX_H__)
		#pragma comment (lib, "\\Proj\\Libs\\Release\\ButeMgrNoMFC.lib")
	#endif
#endif


class CButeMgr
{
public:

	enum SymTypes { NullType, IntType, DwordType, ByteType, BoolType, DoubleType, FloatType, StringType, RectType, PointType, VectorType, RangeType };

	class CSymTabItem
	{
	public:

		SymTypes SymType;

		CSymTabItem(SymTypes t, int val) { SymType = t; data.i = new int(val); }
		CSymTabItem(SymTypes t, DWORD val) { SymType = t; data.dw = new DWORD(val); }
		CSymTabItem(SymTypes t, BYTE val) { SymType = t; data.byte = new BYTE(val); }
		CSymTabItem(SymTypes t, bool val) { SymType = t; data.b = new bool(val); }
		CSymTabItem(SymTypes t, float val) { SymType = t; data.f = new float(val); }
		CSymTabItem(SymTypes t, double val) { SymType = t; data.d = new double(val); }
		CSymTabItem(SymTypes t, const CString& val) { SymType = t; data.s = new CString(val); }
		CSymTabItem(SymTypes t, const CRect& val) { SymType = t; data.r = new CRect(val); }
		CSymTabItem(SymTypes t, const CPoint& val) { SymType = t; data.point = new CPoint(val); }
		CSymTabItem(SymTypes t, const CAVector& val) { SymType = t; data.v = new CAVector(val); }
		CSymTabItem(SymTypes t, const CARange& val) { SymType = t; data.range = new CARange(val); }

        CSymTabItem(const CSymTabItem& source) : SymType(source.SymType) {
            switch (SymType) {
            case IntType: data.i = new int(*source.data.i); break;
            case DwordType: data.dw = new DWORD(*source.data.dw); break;
            case ByteType: data.byte = new BYTE(*source.data.byte); break;
            case BoolType: data.b = new bool(*source.data.b); break;
            case DoubleType: data.d = new double(*source.data.d); break;
            case FloatType: data.f = new float(*source.data.f); break;
            case StringType: data.s = new CString(*source.data.s); break;
            case RectType: data.r = new CRect(*source.data.r); break;
            case PointType: data.point = new CPoint(*source.data.point); break;
            case VectorType: data.v = new CAVector(*source.data.v); break;
            case RangeType: data.range = new CARange(*source.data.range); break;
            default: data.i = nullptr; break;
            }
        }
        const CSymTabItem& operator=(const CSymTabItem& source) {
            if (this == &source) return *this;
            CSymTabItem copy(source);
            std::swap(SymType, copy.SymType);
            std::swap(data, copy.data);
            return *this;
        }


		~CSymTabItem()
		{
			switch (SymType)
			{
			case IntType:
				delete data.i;
				break;
			case DwordType:
				delete data.dw;
				break;
			case ByteType:
				delete data.byte;
				break;
			case BoolType:
				delete data.b;
				break;
			case DoubleType:
				delete data.d;
				break;
			case FloatType:
				delete data.f;
				break;
			case StringType:
				delete data.s;
				break;
			case RectType:
				delete data.r;
				break;
			case PointType:
				delete data.point;
				break;
			case VectorType:
				delete data.v;
				break;
			case RangeType:
				delete data.range;
				break;
			}
		}

		union
		{
			int* i;
			DWORD* dw;
			BYTE* byte;
			bool* b;
			double* d;
			float* f;
			CString* s;
			CRect* r;
			CPoint* point;
			CAVector* v;
			CARange* range;
		} data;
	};


public:

	CButeMgr();
	~CButeMgr() { };

	void Init();
	void Init(void (*pF)(const char* szMsg));

	void Term() { };

	DWORD GetChecksum() { return m_checksum; }
	void SetDisplayFunc(void (*pF)(const char* szMsg)) { m_pDisplayFunc = pF; }
	CString GetErrorString() { return m_sErrorString; }

#if defined(_USE_REZFILE_)
	bool Parse(CRezItm* pItem, int decryptCode = 0);
	bool Parse(CRezItm* pItem, const char* cryptKey);
#endif
	bool Parse(CString sAttributeFilename, int decryptCode = 0);
	bool Parse(CString sAttributeFilename, const char* cryptKey);
	bool Parse(void* pData, unsigned long size, int decryptCode = 0);
	bool Parse(void* pData, unsigned long size, const char* cryptKey);

	bool Save(const char* szNewFileName = NULL);

	void GetTags(void (*pCallback)(const char* szTag, void* pAux), void* pAux = NULL);
	void GetKeys(const char* szTag, void (*pCallback)(const char* szKey, CSymTabItem* pData, void* pAux), void* pAux = NULL);

	int GetInt(const char* szTagName, const char* szAttName, int defVal);
	int GetInt(const char* szTagName, const char* szAttName);
	void SetInt(const char* szTagName, const char* szAttName, int val);

	DWORD GetDword(const char* szTagName, const char* szAttName, DWORD defVal);
	DWORD GetDword(const char* szTagName, const char* szAttName);
	void SetDword(const char* szTagName, const char* szAttName, DWORD val);

	BYTE GetByte(const char* szTagName, const char* szAttName, BYTE defVal);
	BYTE GetByte(const char* szTagName, const char* szAttName);
	void SetByte(const char* szTagName, const char* szAttName, BYTE val);

	bool GetBool(const char* szTagName, const char* szAttName, bool defVal);
	bool GetBool(const char* szTagName, const char* szAttName);
	void SetBool(const char* szTagName, const char* szAttName, bool val);

	float GetFloat(const char* szTagName, const char* szAttName, float defVal);
	float GetFloat(const char* szTagName, const char* szAttName);
	void SetFloat(const char* szTagName, const char* szAttName, float val);

	double GetDouble(const char* szTagName, const char* szAttName, double defVal);
	double GetDouble(const char* szTagName, const char* szAttName);
	void SetDouble(const char* szTagName, const char* szAttName, double val);

	CString& GetString(const char* szTagName, const char* szAttName, CString& defVal);
	CString& GetString(const char* szTagName, const char* szAttName);
	void SetString(const char* szTagName, const char* szAttName, const CString& val);

	void GetString(const char* szTagName, const char* szAttName, const char* defVal, char *szResult, DWORD maxLen);
	void GetString(const char* szTagName, const char* szAttName, char *szResult, DWORD maxLen);

	CRect& GetRect(const char* szTagName, const char* szAttName, CRect& defVal);
	CRect& GetRect(const char* szTagName, const char* szAttName);
	void SetRect(const char* szTagName, const char* szAttName, const CRect& val);

	CPoint& GetPoint(const char* szTagName, const char* szAttName, CPoint& defVal);
	CPoint& GetPoint(const char* szTagName, const char* szAttName);
	void SetPoint(const char* szTagName, const char* szAttName, const CPoint& val);

	CAVector& GetVector(const char* szTagName, const char* szAttName, CAVector& defVal);
	CAVector& GetVector(const char* szTagName, const char* szAttName);
	void SetVector(const char* szTagName, const char* szAttName, const CAVector& val);

	CARange& GetRange(const char* szTagName, const char* szAttName, CARange& defVal);
	CARange& GetRange(const char* szTagName, const char* szAttName);
	void SetRange(const char* szTagName, const char* szAttName, const CARange& val);

	bool AddTag(const char *szTagName);

	CButeMgr::SymTypes GetType(const char* szTagName, const char* szAttName);  // returns NullType if tag/key doesn't exist

	bool Success() { return m_bSuccess; }
	bool Exist(const char* szTagName, const char* szAttName = NULL);

private:

	void Reset();

	void DisplayMessage(const char* szMsg, ...);

	// Parser stuff
	bool Statement();
	bool StatementList();
	bool Tag();
	bool TagList();

	// Scanner stuff
	bool Match(int tok);
	void ConsumeChar();
	short CharClass(unsigned char currentChar);
	short Action( short State, unsigned char currentChar );
	short NextState( short State, unsigned char currentChar );
	void LookupCodes( short State, unsigned char currentChar);
	bool ScanTok();

	DWORD m_decryptCode;
	DWORD m_checksum;
	int m_lineNumber;
	bool m_bLineCounterFlag;

	bool m_bSuccess;
	bool m_bErrorFlag;
	CString m_sErrorString;

	void (*m_pDisplayFunc)(const char* szMsg);

	typedef nolf::attributes::Table<CSymTabItem> TableOfItems;
	typedef nolf::attributes::Table<TableOfItems> TableOfTags;

	TableOfTags m_tagTab;

	TableOfItems* m_pCurrTabOfItems;

	TableOfTags m_auxTagTab;
	TableOfTags m_newTagTab;

	CSymTabItem* GetItem(const char *szTagName, const char* szAttName);

	static void AuxTabItemsSave(const char* sz, CSymTabItem* pItem, void* p);
	static void NewTabsSave(const char* sz, TableOfItems* pTabOfItems, void* p);

	struct TagExtraData
	{
		TagExtraData(void (*pCallback)(const char* tag, void* pAux), void* pAux) 
		{ 
			m_pCallback = pCallback;
			m_pAux = pAux;
		}
		void (*m_pCallback)(const char* tag, void* pAux);
		void* m_pAux;
	};
	static void TagTraverseFunc(const char* szTag, TableOfItems* pData, void* ExtraData);

	struct KeyExtraData
	{
		KeyExtraData(void (*pCallback)(const char* szKey, CSymTabItem* pData, void* pAux), void* pAux) 
		{ 
			m_pCallback = pCallback;
			m_pAux = pAux;
		}
		void (*m_pCallback)(const char* szKey, CSymTabItem* pData, void* pAux);
		void* m_pAux;
	};
	static void KeyTraverseFunc(const char* szKey, CSymTabItem* pData, void* ExtraData);

	std::istream* m_pData = nullptr;
	std::ostream* m_pSaveData = nullptr;
	std::string m_sourceText;
	bool ParseText(std::string text, int decryptCode);

	unsigned char  m_currentChar;
	short m_token;
	short m_tokenMinor;

	char m_szTokenString[4096];

	CString m_sTagName;
	CString m_sAttribute;

	CString m_sAttributeFilename;

	bool m_bPutChar;
	bool m_bSaving = false;

	bool m_bCrypt;
	CCryptMgr m_cryptMgr;
};


#endif // _BUTEMGR_H_
