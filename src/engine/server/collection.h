// *********************************************************************** //
//
//  MODULE:   COLLECTION (.H)
//
//  PURPOSE:  stdlith's growable array — `CMoArray<T, C>` — and the two
//            classes it is built out of, `GenList<T>` and `LAlloc`.
//
//            THIS IS NOT A GUESS. The engine's copy of these three headers
//            did not survive, but the NOLF2-era ones in this repository do:
//
//                src/LT2/lithshared/stdlith/dynarray.h
//                src/LT2/lithshared/stdlith/genlist.h
//                src/LT2/lithshared/stdlith/l_allocator.h
//
//            and every function below is transcribed from them, with the
//            NOLF2 spellings mapped onto the SDK's (DWORD -> uint32,
//            BOOL -> LTBOOL, TRUE/FALSE -> LTTRUE/LTFALSE). What the shipped
//            bytes contribute is the confirmation, member by member: see the
//            table in collection.cpp.
//
//            The file exists so that other translation units can *derive*
//            from the array, or instantiate it. Seven shipped classes do —
//            CConnList, CDriverList, CUnknown8FC and the four CWorldArray*
//            — each with its own vtable, and one of them (CConnList)
//            overriding GenAppendList. Reconstructing those constructors is
//            impossible while the template lives inside collection.cpp; see
//            the header comment in collection.cpp for exactly what each of
//            those files has to change.
//
// *********************************************************************** //

#ifndef __COLLECTION_H__
#define __COLLECTION_H__

#include <stdlib.h>
#include <string.h>
#include "ltbasetypes.h"
#include <limits>
#include <new>
#include <type_traits>
#include <utility>


// ======================================================================= //
//  l_allocator.h
// ======================================================================= //

// The allocator every stdlith container is parameterised on. Declared and
// never defined here: the engine holds exactly one instance and it is
// external to this translation unit.
//
// The declaration ORDER is load-bearing and is confirmed by the shipped
// call sites: every allocation reaches `call *0x4(%eax)` and every release
// `call *0x8(%edx)`, so the virtual destructor takes slot 0, Alloc slot 1
// and Free slot 2.
//
// NOTE from the period header, and visible in the shipped code as the
// `if (nItems > 0)` guard around every allocation: "LAllocs MUST return NULL
// for allocation sizes of 0."
class LAlloc
{
public:
	virtual         ~LAlloc();

	virtual void*   Alloc(uint32 size);
	virtual void    Free(void* ptr);
};

// 0x10058c7c — the one allocator instance in server.dll. Every collection
// method that does not take an allocator explicitly defaults to this one,
// which is why the immediate 0x10058c7c appears at the head of so many of
// them.
extern LAlloc g_DefAlloc;


// The placement-new tag stdlith uses so that its own three-argument
// operator new cannot be confused with anybody else's. It emits no code.
class DummyClassBlahBlah
{
public:
	int asdf;
};

inline void* operator new(size_t size, void* ptr, DummyClassBlahBlah* pBlah)
{
	return ptr;
}

inline void operator delete(void* pDataPtr, void* ptr, DummyClassBlahBlah* pBlah)
{
}

// The single-object pair: the same two templates with a count of one.
// l_allocator.h carries these beside the array pair below, and they belong
// here rather than in collection.cpp because `DeleteAndClearArray2` at the
// foot of this header is written in terms of `LDelete`.
#define LNew(pAllocator, type) \
	BaseNew(pAllocator, (type*)0, 1);

#define LDelete(pAllocator, ptr) \
	BaseDelete(pAllocator, ptr, 1);


// The array-allocation macros. `LDelete_Array` takes the element count
// because stdlith deliberately does not store a cookie with the block —
// which is why the shipped release helper takes a count it may not use.
#define LNew_Array(pAllocator, type, nElements) \
	BaseNew(pAllocator, (type*)0, nElements);

#define LDelete_Array(pAllocator, ptr, nElements) \
	BaseDelete(pAllocator, ptr, nElements);


// Allocate and construct. The middle parameter is a type carrier: the
// macro passes `(T*)0`, which is why every shipped three-argument allocator
// takes an argument it pushes as a literal zero and never reads.
//
// This is the one template that explains the "twelve allocators, nine with
// a loop and three without" observation: `::new(pTemp, ...) T` emits nothing
// at all for an element type with no constructor, so the whole while loop
// folds away and only the null test is left behind — a `test`/`jne` onto
// the very next instruction, two bytes the shipped image really does carry
// (0x100243de, 0x1001b2af, 0x1001b8fa).
template<class T, class... Args>
inline T* BaseNew(LAlloc* pAlloc, T* dummy, uint32 nElements, const Args&... args)
{
	T *ptr, *pTemp;
	if (nElements > std::numeric_limits<uint32>::max() / sizeof(T)) return 0;

	ptr = (T*)pAlloc->Alloc(sizeof(T) * nElements);
	if (!ptr)
		return 0;

    pTemp = ptr;
    try {
        for (uint32 i = 0; i < nElements; ++i, ++pTemp) {
            if constexpr (sizeof...(Args) == 0) ::new(pTemp, (DummyClassBlahBlah*)0) T;
            else ::new(pTemp, (DummyClassBlahBlah*)0) T(args...);
        }
    } catch (...) {
        while (pTemp != ptr) (--pTemp)->~T();
        pAlloc->Free(ptr);
        throw;
    }

	return ptr;
}

template<class T>
inline void BaseDelete(LAlloc* pAlloc, T* ptr, uint32 nElements)
{
	T* pTemp;

	if (!ptr)
		return;

	// Destruct elements.
	pTemp = ptr;
	while (nElements)
	{
		--nElements;
		pTemp->~T();
		++pTemp;
	}

	pAlloc->Free(ptr);
}


// ======================================================================= //
//  genlist.h
// ======================================================================= //

class GenListPos;

// Arrays and lists only use this. In a debug build the two members are
// separate so a linked list can carry a loop index; the release build the
// engine shipped unions them, which is why the shipped `GenBegin` writes
// one word and copies an uninitialised second one out of its own frame.
class BaseGenListPos
{
public:

	void        operator=(const GenListPos& other);

	void* m_Pos = nullptr;
	uint32 m_Index = 0;
};


class GenListPos : public BaseGenListPos
{
public:
	            GenListPos() { m_Pos = nullptr; m_SubIndex.m_Pos = nullptr; }
	            GenListPos(void* pos) { m_Pos = pos; m_SubIndex.m_Pos = nullptr; }
	            GenListPos(uint32 index) { m_Pos = nullptr; m_Index = index; m_SubIndex.m_Pos = nullptr; }

	// Use this to get m_SubIndex as a GenListPos.
	inline BaseGenListPos& GetSubIndex()
	{
		return m_SubIndex;
	}

	// For GenHash.
	BaseGenListPos  m_SubIndex;
};


// The twelve-slot interface every collection vtable in server.dll starts
// with (the previous reconstruction counted thirty of them in .rdata), and
// the run of pointers is exactly this declaration order — which is how the
// anonymous slots get their names and signatures.
template<class T>
class GenList
{
public:

	// Get the start for iterating.
	virtual GenListPos  GenBegin() const = 0;

	// Returns TRUE if you're done iterating.
	virtual LTBOOL      GenIsValid(const GenListPos& pos) const = 0;

	// Get the next element and increment the iterator.
	virtual T           GenGetNext(GenListPos& pos) const = 0;

	// Get the element at the specified position without incrementing or
	// decrementing.
	virtual T           GenGetAt(GenListPos& pos) const = 0;

	virtual LTBOOL      GenAppend(T& toAppend) = 0;

	// NOTE: this trashes all iterators so you must restart iteration.
	virtual void        GenRemoveAt(GenListPos pos) = 0;

	// Remove all elements.
	virtual void        GenRemoveAll() = 0;

	// Returns the number of elements in the list.
	virtual uint32      GenGetSize() const = 0;

	// Copy another list.
	virtual LTBOOL      GenCopyList(const GenList<T>& other) = 0;

	// Append another list.
	virtual LTBOOL      GenAppendList(const GenList<T>& other) = 0;

	// Search through the list for an item. If it finds it, fills in the
	// position it was found at.
	// NOTE: this does comparisons with memcmp. This so objects that go in
	// lists don't have to define operator==.
	virtual LTBOOL      GenFindElement(const T& toFind, GenListPos& thePos) const = 0;

	// Set cache size. This is only meaningful to CMoArrays.
	virtual void        GenSetCacheSize(uint32 size) {}
};


// Inlines.
inline void BaseGenListPos::operator=(const GenListPos& other)
{
	m_Pos = other.m_Pos;
	m_Index = other.m_Index;
}


// ======================================================================= //
//  dynarray.h
// ======================================================================= //

#define CACHE_DEFAULT_SIZE  0

// Predefined types of arrays.
#define CMoPtrArray     CMoArray<void*>
#define CMoDWordArray   CMoArray<uint32>
#define CMoWordArray    CMoArray<uint16>
#define CMoByteArray    CMoArray<unsigned char>
#define DYNA_TEMPLATE template<class T, class C>


// The second template parameter, and the whole reason server.dll holds two
// sizes of collection: it decides whether the array carries its
// over-allocation bookkeeping or not.
//
// `NoCache` has no members, so a `CMoArray<T, NoCache>` is 0x0c bytes —
// vptr, m_pArray, m_nElements — and every accessor below folds to nothing.
// That is what makes `GenSetCacheSize` a bare `ret $4` in half the shipped
// vtables (0x1000bd91), and what deletes the second argument of
// `Init(size, cacheSize)` (0x10019903 accepts it and never reads it).
class NoCache
{
public:
	uint32  GetCacheSize() const        {return 0;}
	void    SetCacheSize(uint32 size)   {}
	uint32  GetWantedCache() const      {return 0;}
	void    SetWantedCache(uint32 size) {}
};


// `DefaultCache` adds two words, so a `CMoArray<T, DefaultCache>` is 0x14
// bytes: m_CacheSize at 0x0c is how many slots the last allocation took
// beyond the count, m_WantedCache at 0x10 is how many to ask for on top
// next time. Slot 11 of those vtables really does store its argument to
// 0x10 (0x1000b152).
class DefaultCache
{
public:
	uint32  GetCacheSize() const        {return m_CacheSize;}
	void    SetCacheSize(uint32 val)    {m_CacheSize = val;}
	uint32  GetWantedCache() const      {return m_WantedCache;}
	void    SetWantedCache(uint32 val)  {m_WantedCache = val;}

private:
	uint32  m_CacheSize;                 // +0x0c
	uint32  m_WantedCache;               // +0x10
};


// 0x1003d4c0 — "This is defined as a separate function because it's called
// by the virtual GenFindElement and we don't want to generate a bunch of
// template code for every type of object we use in a list." Which is why
// one 39-byte GenFindElement (0x10018c6b) serves every four-byte
// instantiation in the image.
uint32 MoArray_FindElementMemcmp(
	const void* pToFind,
	const void* pArray,
	uint32 nElements,
	uint32 elementSize);

#define BAD_INDEX ((uint32)0xffffffff)


template<class T, class C = DefaultCache>
class CMoArray : public GenList<T>
{
	public:

		// Constructors
		CMoArray(const CMoArray& other) {
		    Clear();
		    if (!CopyArray(other)) throw std::bad_alloc();
		}
		CMoArray(CMoArray&& other) noexcept {
		    m_pArray = other.m_pArray;
		    m_nElements = other.m_nElements;
		    m_Cache = other.m_Cache;
		    other.Clear();
		}
		            CMoArray(CMoArray<T, C>& copyFrom, const T& toAdd);

		            CMoArray()
		            {
		                Clear();
		                Init();
		            }

		            CMoArray(uint32 cacheSize)
		            {
		                Clear();
		                Init(0, cacheSize);
		            }

		// Destructor
		            ~CMoArray() { Term(); }


		// Member functions
		LTBOOL      Init(uint32 size = 0, uint32 cacheSize = CACHE_DEFAULT_SIZE);
		void        Term(LAlloc* pAlloc = &g_DefAlloc) { SetSize2(0, pAlloc); }

		// WARNING: use these at your own risk.
		void        CopyPointers(CMoArray<T, C>& other);
		void        TermPointers();

		// Comparison
		LTBOOL      Compare(const CMoArray<T, C>& other);

		// Assignment
        CMoArray<T, C>& operator=(const CMoArray<T, C>& other);
        void Swap(CMoArray<T, C>& other) noexcept {
            using std::swap;
            swap(m_pArray, other.m_pArray);
            swap(m_nElements, other.m_nElements);
            swap(m_Cache, other.m_Cache);
        }
		CMoArray<T, C>  operator+(const T& toAdd);

		LTBOOL      CopyGenList(const GenList<T>& theList);
		LTBOOL      CopyArray(const CMoArray<T, C>& other);
		LTBOOL      CopyArray2(const CMoArray<T, C>& other, LAlloc* pAlloc);

		LTBOOL      AppendArray(const CMoArray<T, C>& other);

		// Find and compare with T::operator==.
		uint32      FindElement(const T& x) const {
            if constexpr (requires (const T& a, const T& b) { a == b; }) {
                for (uint32 i = 0; i < m_nElements; ++i)
                    if (m_pArray[i] == x) return i;
                return BAD_INDEX;
            } else {
                return MoArray_FindElementMemcmp(&x, m_pArray, m_nElements, sizeof(T));
            }
        }

		LTBOOL      Append(const T& toAdd) { return Insert(m_nElements, toAdd); }
		LTBOOL      Insert(uint32 index, const T& toInsert);
		LTBOOL      Insert2(uint32 index, const T& toInsert, LAlloc* pAlloc);
		void        Remove(uint32 index);
		void        Remove2(uint32 index, LAlloc* pAlloc);

		// You can use it like a stack with these...
		LTBOOL      Push(const T& toAdd);
		void        Pop();


		// Accessors
		LTBOOL      IsValid();

		T&          Last() const;
		uint32      LastI() const;

		T&          Next(uint32 index) const;
		T&          Prev(uint32 index) const;
		uint32      NextI(uint32 i) const;
		uint32      PrevI(uint32 i) const;

		// Number of elements
		            operator unsigned long() const;

		// Array-like access.
		T&          operator[](const uint32 index) const { return Get(index); }

		// Returns FALSE if there isn't enough memory.
		//
		// THE ONE-LINE WRAPPERS ARE LOAD-BEARING AT /O2, AND THE CALLERS
		// MUST USE THEM. `SetSize(n)` and `SetSize2(n, &g_DefAlloc)` are the
		// same bytes at /O1 -- /Os expands the wrapper and leaves the
		// SetSize2 call behind -- so a caller written either way matches
		// server.dll. They are NOT the same at /O2, and the difference is
		// exactly one INLINE LEVEL, which is what decides whether the
		// innermost `BaseNew`/`BaseDelete` is EXPANDED or CALLED:
		//
		//   depth 1-2   expanded    depth 4+   called
		//
		// Measured off functions that already match lithtech.exe, not
		// inferred:
		//
		//   * `SetSize2` standalone (0x00414fa0) reaches _AllocateTArray at
		//     depth 1 and BaseNew at depth 2, and EXPANDS BaseNew.
		//     `InternalNiceSetSize<AnimKeyFrame>` (0x0044b720) does the same
		//     with the 61-byte constructor-bearing BaseNew.
		//   * `GenCopyList` below spells it `SetSize(...)`, so BaseNew sits
		//     at depth 4 -- and its three shipped bodies (0x00459e10,
		//     0x00447f90, 0x004482e0) all CALL BaseNew. `GenRemoveAt`
		//     (0x004481e0, via Remove->Remove2), `GenAppend` (0x004480f0,
		//     via Append->Insert->Insert2) and the compiler-generated
		//     `CWorldSimple::operator=` (0x004281c0, via operator=->
		//     CopyArray->CopyArray2) say the same at depths 4 and 5.
		//
		// So a caller that spells the explicit `*2(..., &g_DefAlloc)` form
		// loses a level and expands a helper the shipped image calls. That
		// is one systemic cause behind several unmatched callers in
		// commonmodel.cpp and netmgr.cpp; the fix is at the CALL SITE, not
		// here -- no keyword in this header can separate two call sites.
		LTBOOL      SetSize(uint32 newSize) { return SetSize2(newSize, &g_DefAlloc); }
		LTBOOL      SetSize2(uint32 newSize, LAlloc* pAlloc);

		LTBOOL      SetSizeInit(uint32 newSize, T& val);
		LTBOOL      SetSizeInit2(uint32 newSize, T val);
		LTBOOL      SetSizeInit3(uint32 newSize, T& val, LAlloc* pAlloc);   // defined below, from dynarray.h:703
		LTBOOL      SetSizeInit4(uint32 newSize, T val, LAlloc* pAlloc);

		// Same as SetSize but preserves the old contents.
		LTBOOL      NiceSetSize(uint32 newSize)                     {return InternalNiceSetSize(newSize, LTFALSE, &g_DefAlloc);}
		LTBOOL      NiceSetSize2(uint32 newSize, LAlloc* pAlloc)    {return InternalNiceSetSize(newSize, LTFALSE, pAlloc);}

		// Same as NiceSetSize, but uses memcpy instead of operator=.
		LTBOOL      Fast_NiceSetSize(uint32 newSize)                    {return InternalNiceSetSize(newSize, LTTRUE, &g_DefAlloc);}
		LTBOOL      Fast_NiceSetSize2(uint32 newSize, LAlloc* pAlloc)   {return InternalNiceSetSize(newSize, LTTRUE, pAlloc);}

		uint32      GetSize() const { return m_nElements; }

		// Sets the cache size
		void        SetCacheSize(uint32 size)
		{
		    m_Cache.SetWantedCache(size);
		}

		// Get and set
		T&          Get(uint32 index) const
		{
		    return m_pArray[index];
		}

		void        Set(uint32 index, T& data) { m_pArray[index] = data; }

		// Returns a pointer to the internal array..
		// IN-CLASS, as dynarray.h:235 has it, and the body matters: an
		// in-class accessor returning a value makes VC6 materialise into a
		// register before a push, where a plain `m_pArray` folds into
		// `push mem`. CWorldSimple::Load's two w_GetTreeNode calls
		// (0x1000a97e, 0x1000a9c5) load 0x6c(%esi) and 0x70(%esi) into
		// registers first, which only happens when the source reads
		// GetArray() / GetSize().
		T*          GetArray()  { return m_pArray; }
        const T*    GetArray() const { return m_pArray; }


	// Accessors for MFC compatibility.
	public:

		T&          GetAt(uint32 index) const { return m_pArray[index]; }
		void        SetAt(uint32 index, T data) { m_pArray[index] = data; }

		void        RemoveAll() { SetSize(0); }
		LTBOOL      Add(const T& toAdd) { return Insert(m_nElements, toAdd); }


	// GenList implementation.
	public:

		virtual GenListPos  GenBegin() const
		{
			return GenListPos((uint32)0);
		}

		virtual LTBOOL      GenIsValid(const GenListPos& pos) const
		{
			return pos.m_Index < GetSize();
		}

		virtual T           GenGetNext(GenListPos& pos) const
		{
			T& ret = Get(pos.m_Index);
			++pos.m_Index;
			return ret;
		}

		virtual T           GenGetAt(GenListPos& pos) const
		{
			return Get(pos.m_Index);
		}

		virtual LTBOOL      GenAppend(T& toAppend)
		{
			return Append(toAppend);
		}

		virtual void        GenRemoveAt(GenListPos pos)
		{
			Remove(pos.m_Index);
		}

		virtual void        GenRemoveAll()
		{
			Term();
		}

		virtual uint32      GenGetSize() const
		{
			return GetSize();
		}

		virtual LTBOOL      GenCopyList(const GenList<T>& theList)
		{
			GenListPos pos;
			uint32 iCurOut;

			if(!SetSize(theList.GenGetSize()))
				return LTFALSE;

			iCurOut = 0;
			for(pos=theList.GenBegin(); theList.GenIsValid(pos); )
			{
				// BAD bug in theList!
				if(iCurOut >= GetSize())
				{
					Term();
					return LTFALSE;
				}

				(*this)[iCurOut] = theList.GenGetNext(pos);
				iCurOut++;
			}

			return LTTRUE;
		}

		virtual LTBOOL      GenAppendList(const GenList<T>& theList)
		{
			GenListPos pos;
			uint32 iCurOut, prevSize;


			prevSize = GetSize();

			if(!NiceSetSize(prevSize + theList.GenGetSize()))
				return LTFALSE;

			iCurOut = prevSize;
			for(pos=theList.GenBegin(); theList.GenIsValid(pos); )
			{
				// BAD bug in theList!
				if(iCurOut >= GetSize())
				{
					Term();
					return LTFALSE;
				}

				(*this)[iCurOut] = theList.GenGetNext(pos);
				iCurOut++;
			}

			return LTTRUE;
		}

		virtual LTBOOL      GenFindElement(const T& toFind, GenListPos& thePos) const
		{
			thePos.m_Index = FindElement(toFind);

			return thePos.m_Index != BAD_INDEX;
		}

		virtual void        GenSetCacheSize(uint32 size)
		{
			SetCacheSize(size);
		}


	private:

		void    _InitArray(uint32 wantedCache);
		void    _DeleteAndDestroyArray(LAlloc* pAlloc, uint32 nElements);
		T*      _AllocateTArray(uint32 nElements, LAlloc* pAlloc);

		LTBOOL  InternalNiceSetSize(uint32 newSize, LTBOOL bFast, LAlloc* pAlloc);
		inline uint32 GetNumAllocatedElements() {return m_nElements + m_Cache.GetCacheSize();}


	private:

		void    Clear()
		{
			m_pArray = 0;
			m_nElements = 0;
			m_Cache.SetCacheSize(0);
			m_Cache.SetWantedCache(0);
		}

		// Member variables

	public:
		T       *m_pArray;                   // +0x04
		uint32  m_nElements;                  // +0x08

		C       m_Cache;                      // +0x0c
};


// ----------------------------------------------------------------------- //
// Out-of-line members.
//
// Init is a real function in the shipped image for the DefaultCache
// instantiations (0x1000c38c and its seven siblings, 53 bytes) and gets
// inlined for the NoCache ones (see the 52-byte constructors) — the same
// expansion budget that decides whether _AllocateTArray or BaseNew is the
// one that survives as a call. Nothing in the source distinguishes them.
// ----------------------------------------------------------------------- //

DYNA_TEMPLATE
LTBOOL CMoArray<T, C>::Init( uint32 size, uint32 cacheSize )
{
	Term();
	_InitArray(cacheSize);

	return SetSize(size);
}


// Left out of the first transcription, and needed: the element class
// reconstructed at the end of collection.cpp assigns two embedded arrays,
// and the shipped assignment reaches CopyArray2 directly (0x10019588),
// which is this body inlined twice over.
DYNA_TEMPLATE
CMoArray<T, C>& CMoArray<T, C>::operator=(const CMoArray<T,C> &other)
{
	CopyArray(other);
	return *this;
}


DYNA_TEMPLATE
LTBOOL CMoArray<T, C>::CopyArray(const CMoArray<T,C> &other)
{
	return CopyArray2(other, &g_DefAlloc);
}


DYNA_TEMPLATE
LTBOOL CMoArray<T, C>::CopyArray2(const CMoArray<T,C> &other, LAlloc *pAlloc)
{
	uint32 i;
	if (this == &other) return LTTRUE;

	if( m_pArray )
	{
		_DeleteAndDestroyArray(pAlloc, GetNumAllocatedElements());
	}

	m_nElements = other.m_nElements;
	m_Cache.SetCacheSize(other.m_Cache.GetCacheSize());
	m_Cache.SetWantedCache(other.m_Cache.GetWantedCache());

	if( m_nElements + m_Cache.GetCacheSize() > 0  )
	{
		m_pArray = _AllocateTArray( m_nElements + m_Cache.GetCacheSize(), pAlloc );
	}
	else
	{
		m_nElements = 0;
		m_Cache.SetCacheSize(0);
		m_pArray = 0;
		return LTTRUE;
	}

	// Could it allocate the array?
	if( !m_pArray )
	{
		m_nElements = 0;
		m_Cache.SetCacheSize(0);
		return LTFALSE;
	}

	for( i=0; i < m_nElements; i++ )
		m_pArray[i] = other.m_pArray[i];

	return LTTRUE;
}


DYNA_TEMPLATE
LTBOOL CMoArray<T, C>::Insert( uint32 index, const T &toInsert )
{
	return Insert2(index, toInsert, &g_DefAlloc);
}


DYNA_TEMPLATE
LTBOOL CMoArray<T, C>::Insert2( uint32 index, const T &toInsert, LAlloc *pAlloc )
{
	T		*pNewArray;
	uint32	newSize, i;

	if(index > m_nElements)
		return LTFALSE;

	// Create a new array (possibly).
	newSize = m_nElements + 1;

	if( m_Cache.GetCacheSize() == 0 )
	{
		pNewArray = _AllocateTArray( newSize + m_Cache.GetWantedCache(), pAlloc );
		if( !pNewArray )
			return LTFALSE;

		// Copy the old array into the new one, start inserting at index.
		for( i=0; i < index; i++ )
			pNewArray[i] = m_pArray[i];

		for( i=index; i < m_nElements; i++ )
			pNewArray[i+1] = m_pArray[i];

		// Insert the new item into the array
		pNewArray[index] = toInsert;

		// Free the old array and set our pointer to the new one
		if( m_pArray )
		{
			_DeleteAndDestroyArray(pAlloc, GetNumAllocatedElements());
		}

		m_Cache.SetCacheSize(m_Cache.GetWantedCache());
		m_pArray = pNewArray;
	}
	else
	{
		for( i=m_nElements; i > index; i-- )
			m_pArray[i] = m_pArray[i-1];

		m_Cache.SetCacheSize(m_Cache.GetCacheSize() - 1);

		m_pArray[index] = toInsert;
	}

	++m_nElements;

	return LTTRUE;
}


DYNA_TEMPLATE
void CMoArray<T, C>::Remove( uint32 index )
{
	Remove2(index, &g_DefAlloc);
}


DYNA_TEMPLATE
void CMoArray<T, C>::Remove2( uint32 index, LAlloc *pAlloc )
{
	if (index >= m_nElements) return;
	uint32 i, newSize, newAllocSize;
	T *pNewArray;
	LTBOOL bSlideDown;


	bSlideDown = LTTRUE;
	if( m_Cache.GetCacheSize() >= (m_Cache.GetWantedCache()*2) )
	{
		newSize = m_nElements - 1;
		newAllocSize = newSize + m_Cache.GetWantedCache();
		pNewArray = _AllocateTArray(newAllocSize, pAlloc);

		// Make sure it allocated the array .. if it didn't, just have
		// it slide all the elements down (this guarantees that Remove()
		// won't fail.)
		if( pNewArray || newAllocSize == 0 )
		{
			for( i=0; i < index; i++ )
				pNewArray[i] = m_pArray[i];

			for( i=index; i < m_nElements-1; i++ )
				pNewArray[i] = m_pArray[i+1];

			_DeleteAndDestroyArray(pAlloc, GetNumAllocatedElements());
			m_pArray = pNewArray;

			m_Cache.SetCacheSize(m_Cache.GetWantedCache());
			bSlideDown = LTFALSE;
		}
	}


	if( bSlideDown )
	{
		// Slide them all down one.
		m_Cache.SetCacheSize(m_Cache.GetCacheSize() + 1);

		for( i=index; i < m_nElements-1; i++ )
			m_pArray[i] = m_pArray[i+1];
	}

	--m_nElements;
}


DYNA_TEMPLATE
LTBOOL CMoArray<T, C>::SetSizeInit(uint32 size, T& value)
{
    return SetSizeInit4(size, value, &g_DefAlloc);
}
DYNA_TEMPLATE
LTBOOL CMoArray<T, C>::SetSizeInit2(uint32 size, T value)
{
    return SetSizeInit4(size, std::move(value), &g_DefAlloc);
}
DYNA_TEMPLATE
LTBOOL CMoArray<T, C>::SetSizeInit3(uint32 size, T& value, LAlloc* allocator)
{
    return SetSizeInit4(size, value, allocator);
}
DYNA_TEMPLATE
LTBOOL CMoArray<T, C>::SetSizeInit4(uint32 size, T value, LAlloc* allocator)
{
    if (!SetSize2(size, allocator)) return LTFALSE;
    for (uint32 i = 0; i < m_nElements; ++i) m_pArray[i] = value;
    return LTTRUE;
}

DYNA_TEMPLATE
LTBOOL CMoArray<T, C>::SetSize2( uint32 newSize, LAlloc *pAlloc )
{
	// Uhhh ok...
	if((newSize == m_nElements) && (newSize != 0))
	{
		return LTTRUE;
	}

	if( m_pArray )
	{
		_DeleteAndDestroyArray(pAlloc, GetNumAllocatedElements());
	}

	m_nElements = newSize;
	if( newSize > 0 )
	{
		m_pArray = _AllocateTArray( newSize + m_Cache.GetWantedCache(), pAlloc );
		if( !m_pArray )
		{
			m_nElements = 0;
			m_Cache.SetCacheSize(0);
			return LTFALSE;
		}

		m_Cache.SetCacheSize(m_Cache.GetWantedCache());
	}

	return LTTRUE;
}


DYNA_TEMPLATE
LTBOOL CMoArray<T, C>::InternalNiceSetSize( uint32 newSize, LTBOOL bFast, LAlloc *pAlloc )
{
	T *pNewArray;
	uint32 i, nToCopy;

	// Trivial reject..
	if(newSize < m_nElements)
	{
		m_Cache.SetCacheSize(m_Cache.GetCacheSize() + (m_nElements - newSize));
		m_nElements = newSize;
		return LTTRUE;
	}
	else if(newSize > m_nElements && (m_nElements + m_Cache.GetCacheSize()) >= newSize)
	{
		m_Cache.SetCacheSize(m_Cache.GetCacheSize() - (newSize - m_nElements));
		m_nElements = newSize;
		return LTTRUE;
	}
	else if(newSize == m_nElements)
	{
		// uhhh ok..
		return LTTRUE;
	}

	if (newSize > std::numeric_limits<uint32>::max() - m_Cache.GetWantedCache()) return LTFALSE;
	pNewArray = _AllocateTArray(newSize + m_Cache.GetWantedCache(), pAlloc);
	if(!pNewArray)
		return LTFALSE;

	nToCopy = m_nElements;
	if(nToCopy > newSize)
		nToCopy = newSize;

	// Copy as many elements as we can.
    bool copied = false;
    if constexpr (std::is_trivially_copyable_v<T>) {
        if (bFast) {
            if (nToCopy) memcpy(pNewArray, m_pArray, sizeof(T)*nToCopy);
            copied = true;
        }
    }
	if (!copied)
	{
		for(i=0; i < nToCopy; i++)
		{
			pNewArray[i] = m_pArray[i];
		}
	}

	// Get rid of the old array and point at the new one.
	_DeleteAndDestroyArray(pAlloc, GetNumAllocatedElements());
	m_pArray = pNewArray;
	m_nElements = newSize;
	m_Cache.SetCacheSize(m_Cache.GetWantedCache());

	return LTTRUE;
}


DYNA_TEMPLATE
void CMoArray<T, C>::_InitArray( uint32 wantedCache )
{
	m_pArray = 0;
	m_nElements = 0;

	m_Cache.SetWantedCache(wantedCache);
	m_Cache.SetCacheSize(0);
}


DYNA_TEMPLATE
T *CMoArray<T, C>::_AllocateTArray( uint32 nElements, LAlloc *pAlloc )
{
	T *tPtr = LNew_Array(pAlloc, T, nElements);

	return tPtr;
}


DYNA_TEMPLATE
void CMoArray<T, C>::_DeleteAndDestroyArray(LAlloc *pAlloc, uint32 nElements)
{
	if(m_pArray)
	{
		LDelete_Array(pAlloc, m_pArray, nElements);
		m_pArray = 0;
	}

	m_Cache.SetCacheSize(0);
}


// ----------------------------------------------------------------------- //
// dynarray.h's own free functions, at the foot of the header. Moved here
// from collection.cpp, which is where they had been transcribed: the period
// header carries them, so every translation unit that includes this one can
// reach them, and one of them is the reason this matters.
//
// The template parameter is the ARRAY type, not the element type, which is
// why the image holds five copies of `DeleteAndClearArray2` that differ only
// in the BaseDelete they reach: every one of them indexes by four
// (`mov (%eax,%edi,4),%eax`) and Terms through the four-byte NoCache
// SetSize2 at 0x1001a03d, so the array is always `CMoArray<T*, NoCache>`
// and what varies is T.
//
// The `if(theArray[i])` guard makes BaseDelete's own null test redundant,
// and where BaseDelete is small enough to inline -- an element type with no
// destructor -- VC6 folds the two together and emits one `test`. That is the
// whole difference between the 56-byte 0x1001a1dd and its 60-byte siblings.
//
// WHY THE ONE-LINE WRAPPER IS NOT DECORATION. `DeleteAndClearArray` adds no
// code: it is `DeleteAndClearArray2` with the default allocator filled in,
// and at /O1 the two spellings are the same bytes. At /O2 they are NOT, and
// the difference is one INLINE LEVEL -- which is what decides whether the
// innermost `BaseNew`/`BaseDelete` is expanded or called. The same holds for
// every wrapper pair in this header: `SetSize`/`SetSize2`, `Remove`/`Remove2`,
// `Insert`/`Insert2`, `Term`/`SetSize2(0,...)`, `Append`/`Insert`. See the
// note above `SetSize` for the measured evidence.
// ----------------------------------------------------------------------- //

template<class T>
void DeleteAndClearArray2(T& theArray, LAlloc* pAlloc)
{
	uint32 i;

	for (i = 0; i < theArray.GetSize(); i++)
	{
		if (theArray[i])
		{
			LDelete(pAlloc, theArray[i]);
		}
	}

	theArray.SetSize2(0, pAlloc);
}

template<class T>
void DeleteAndClearArray(T& theArray, LAlloc* pAlloc = &g_DefAlloc)
{
	DeleteAndClearArray2(theArray, pAlloc);
}


#endif  // __COLLECTION_H__
