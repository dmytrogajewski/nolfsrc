#pragma once
#include "ltbasetypes.h"

// Preserve the engine's numeric key kinds. Historically mode 1 folds ASCII
// case and mode 2 compares literal bytes, despite the recovered macro names.
#define DHASHKEY_WORD 0
#define DHASHKEY_STRING 1
#define DHASHKEY_NOCASE 2
#define DHASHKEY_FILENAME 3
#define DHASHKEY_COUNT 4

class CHashTable;
void* dhash_Create(int slots, int key_type);
void dhash_Free(void* table);
void* dhash_AddElement(void* table, const void* key, uint32 key_length);
void* dhash_FindElement(void* table, const void* key, uint32 key_length);
void* dhash_FindNextElement(void* table, void* element, const void* key, uint32 key_length);
void dhash_Remove(void* table, void* element);
void* dhash_GetFirst(void* table);
// Advances before returning; callers may remove the returned element.
void* dhash_GetNext(void** iterator);
char* dhash_GetKey(void* element, uint32* length);
void* dhash_GetData(void* element);
void dhash_SetData(void* element, void* data);
uint32 dhash_GetSlot(CHashTable*, const void* key, uint32 length);
