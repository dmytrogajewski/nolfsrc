#include "object_pool.h"
#include "polygrid_record.h"
CLinkNodeBank g_LinkNodeBank;
namespace {
struct PolyGridPool {
    PolyGridPool() { sb_Init(&bank, sizeof(PGRecord), 32); }
    StructBank bank;
} polygrid;
}
StructBank& g_MemTrackBank = polygrid.bank;
