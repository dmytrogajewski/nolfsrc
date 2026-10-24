#include "world_grid.h"
#include "world_node.h"
#include "world_polygon.h"
#include "iltstream.h"
#include "engine/portable/wire.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
int cell_index(float value, float origin, float size, int count) {
    if (count == 1) return 0;
    const double index = (double(value)-origin)/size;
    return static_cast<int>(std::clamp(index,0.0,double(count-1)));
}
template<class T> bool read_value(ILTStream& stream, T& value) {
    std::array<std::byte, sizeof(T)> bytes{};
    if (stream.Read(bytes.data(), static_cast<uint32>(bytes.size())) != LT_OK) return false;
    value = nolf::wire::load<T>(bytes.data());
    return true;
}
bool read_vector(ILTStream& stream, LTVector& value) {
    return read_value(stream, value.x) && read_value(stream, value.y) && read_value(stream, value.z) &&
        std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
const char* valid_tree(const CSubRecord& cell) {
    const auto valid = [&](uint16 index) { return index >= 0xfffe || index < cell.m_nCount; };
    if (!valid(cell.m_nIndex)) return "cell root index";
    for (uint32 i = 0; i < cell.m_nCount; ++i)
        if (!valid(cell.m_pNodes[i].m_nChild[0]) || !valid(cell.m_pNodes[i].m_nChild[1])) return "cell child index";
    // Validate every component, including unused records, before physics can
    // traverse it. An iterative walk bounds host stack use on deep trees.
    std::vector<uint8> color(cell.m_nCount, 0);
    std::vector<std::pair<uint16, bool>> stack;
    for (uint32 i = 0; i < cell.m_nCount; ++i) {
        if (color[i]) continue;
        stack.emplace_back(static_cast<uint16>(i), false);
        while (!stack.empty()) {
            const auto [index, leaving] = stack.back(); stack.pop_back();
            if (index >= 0xfffe) continue;
            if (leaving) {
                color[index] = 2;
                continue;
            }
            if (color[index] == 1) return "cell tree cycle";
            if (color[index] == 2) continue;
            color[index] = 1;
            stack.emplace_back(index, true);
            stack.emplace_back(cell.m_pNodes[index].m_nChild[1], false);
            stack.emplace_back(cell.m_pNodes[index].m_nChild[0], false);
        }
    }
    return nullptr;
}
}
CWorldSub108::~CWorldSub108() { Term(); }
void CWorldSub108::Term() {
    for (uint32 i = 0; i < m_nRecords; ++i) delete[] m_pRecords[i].m_pNodes;
    delete[] m_pRecords;
    m_pRecords = nullptr;
    m_nRecords = 0;
    m_nCountX = m_nCountY = m_nCountZ = m_nSliceStride = 0;
    m_vCell.Init(); m_vOrigin.Init();
}
void CWorldSub108::Swap(CWorldSub108& other) noexcept {
    using std::swap;
    swap(m_vCell, other.m_vCell); swap(m_vOrigin, other.m_vOrigin);
    swap(m_nCountX, other.m_nCountX); swap(m_nCountY, other.m_nCountY); swap(m_nCountZ, other.m_nCountZ);
    swap(m_nSliceStride, other.m_nSliceStride); swap(m_nRecords, other.m_nRecords); swap(m_pRecords, other.m_pRecords);
}
CWorldSub108::CWorldSub108(const CWorldSub108& other) {
    CWorldSub108 copy;
    auto records = std::make_unique<CSubRecord[]>(other.m_nRecords);
    copy.m_nRecords = other.m_nRecords;
    copy.m_pRecords = records.release();
    copy.m_vCell = other.m_vCell; copy.m_vOrigin = other.m_vOrigin;
    copy.m_nCountX = other.m_nCountX; copy.m_nCountY = other.m_nCountY; copy.m_nCountZ = other.m_nCountZ;
    copy.m_nSliceStride = other.m_nSliceStride;
    for (uint32 i = 0; i < other.m_nRecords; ++i) {
        const auto& source = other.m_pRecords[i];
        auto nodes = std::make_unique<WSNode[]>(source.m_nCount);
        if (source.m_nCount) std::copy_n(source.m_pNodes, source.m_nCount, nodes.get());
        copy.m_pRecords[i].m_pNodes = nodes.release();
        copy.m_pRecords[i].m_nCount = source.m_nCount;
        copy.m_pRecords[i].m_nIndex = source.m_nIndex;
    }
    Swap(copy);
}
CWorldSub108& CWorldSub108::operator=(const CWorldSub108& other) {
    if (this != &other) { CWorldSub108 copy(other); Swap(copy); }
    return *this;
}
CWorldSub108::CWorldSub108(CWorldSub108&& other) noexcept { Swap(other); }
CWorldSub108& CWorldSub108::operator=(CWorldSub108&& other) noexcept {
    if (this != &other) { Term(); Swap(other); }
    return *this;
}
LTBOOL CWorldSub108::Load(ILTStream* stream, const char** error) {
    if (error) *error = nullptr;
    const auto fail = [&](const char* reason) { if (error) *error = reason; return LTFALSE; };
    if (!stream) return fail("missing stream");
    try {
        CWorldSub108 next;
        if (!read_value(*stream, next.m_nCountX) || !read_value(*stream, next.m_nCountY) || !read_value(*stream, next.m_nCountZ) ||
            next.m_nCountX < 0 || next.m_nCountY < 0 || next.m_nCountZ < 0 ||
            !read_vector(*stream, next.m_vCell) || !read_vector(*stream, next.m_vOrigin)) return fail("grid header");
        const uint64 slice = uint64(next.m_nCountX) * uint64(next.m_nCountY);
        if (slice > std::numeric_limits<int>::max()) return fail("grid slice overflow");
        const uint64 count = slice * uint64(next.m_nCountZ);
        uint64 allocation = count * sizeof(CSubRecord);
        uint32 position = 0, length = 0;
        if (stream->GetPos(&position) != LT_OK || stream->GetLen(&length) != LT_OK || position > length ||
            count > (length - position) / 4 || count > (256 * 1024 * 1024) / sizeof(CSubRecord)) return fail("grid cell count");
        const auto valid_axis = [](float size, int cells) { return size > 0 || (size == 0 && cells == 1); };
        if (count && (!valid_axis(next.m_vCell.x, next.m_nCountX) || !valid_axis(next.m_vCell.y, next.m_nCountY) ||
            !valid_axis(next.m_vCell.z, next.m_nCountZ))) return fail("grid cell dimensions");
        next.m_nSliceStride = static_cast<int>(slice);
        next.m_pRecords = count ? new CSubRecord[static_cast<std::size_t>(count)]{} : nullptr;
        next.m_nRecords = static_cast<uint32>(count);
        for (uint32 i = 0; i < count; ++i) {
            auto& cell = next.m_pRecords[i];
            if (!read_value(*stream, cell.m_nCount) || cell.m_nCount > 0xfffe || !read_value(*stream, cell.m_nIndex) ||
                stream->GetPos(&position) != LT_OK || position > length || cell.m_nCount > (length - position) / 6) return fail("cell header");
            allocation += uint64(cell.m_nCount) * sizeof(WSNode);
            if (allocation > 256ull * 1024 * 1024) return fail("grid allocation budget");
            auto nodes = std::make_unique<WSNode[]>(cell.m_nCount);
            cell.m_pNodes = nodes.release();
            std::vector<std::byte> bytes(std::size_t(cell.m_nCount) * 6);
            if (!bytes.empty() && stream->Read(bytes.data(), static_cast<uint32>(bytes.size())) != LT_OK) return fail("cell data");
            for (uint32 n = 0; n < cell.m_nCount; ++n) {
                const auto* row = bytes.data() + n * 6;
                cell.m_pNodes[n].m_nPoly = nolf::wire::load<uint16>(row);
                cell.m_pNodes[n].m_nChild[0] = nolf::wire::load<uint16>(row + 2);
                cell.m_pNodes[n].m_nChild[1] = nolf::wire::load<uint16>(row + 4);
            }
            if (const auto* reason = valid_tree(cell)) return fail(reason);
        }
        if (stream->ErrorStatus() != LT_OK) return fail("stream error");
        Swap(next);
        return LTTRUE;
    } catch (const std::bad_alloc&) { return fail("allocation failure"); }
}
CSubRecord* CWorldSub108::GetPointStatus(LTVector point) {
    if (!m_pRecords || !m_nRecords || !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) return nullptr;
    const auto x = cell_index(point.x, m_vOrigin.x, m_vCell.x, m_nCountX);
    const auto y = cell_index(point.y, m_vOrigin.y, m_vCell.y, m_nCountY);
    const auto z = cell_index(point.z, m_vOrigin.z, m_vCell.z, m_nCountZ);
    return &m_pRecords[x + z * m_nSliceStride + y * m_nCountX];
}

PhysicsCandidates CWorldSub108::QueryBox(LTVector minimum, LTVector maximum, std::span<WorldPolyRef> polygons) const {
    PhysicsCandidates result;
    if (!m_pRecords || !m_nRecords) return result;
    std::array<int,3> first, last;
    const std::array<int,3> counts{m_nCountX,m_nCountY,m_nCountZ};
    for (uint32 axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) || minimum[axis] > maximum[axis])
            throw std::invalid_argument("invalid physics query bounds");
        first[axis] = cell_index(minimum[axis],m_vOrigin[axis],m_vCell[axis],counts[axis]);
        last[axis] = cell_index(maximum[axis],m_vOrigin[axis],m_vCell[axis],counts[axis]);
    }
    result.multiple_cells = first != last;
    if (!result.multiple_cells) return result;
    std::array<double,3> center, half;
    for (uint32 axis = 0; axis < 3; ++axis) {
        center[axis] = (double(minimum[axis])+maximum[axis])*.5;
        half[axis] = (double(maximum[axis])-minimum[axis])*.5;
    }
    std::vector<uint8> emitted(polygons.size());
    std::vector<uint16> pending;
    for (int z = first[2]; z <= last[2]; ++z) for (int y = first[1]; y <= last[1]; ++y) for (int x = first[0]; x <= last[0]; ++x) {
        const auto& cell = m_pRecords[x+z*m_nSliceStride+y*m_nCountX];
        std::vector<uint8> visited(cell.m_nCount);
        pending.push_back(cell.m_nIndex);
        while (!pending.empty()) {
            const auto index = pending.back(); pending.pop_back();
            if (index >= 0xfffe) continue;
            if (index >= cell.m_nCount) throw std::runtime_error("physics query node index");
            if (visited[index]) continue;
            visited[index] = 1;
            const auto* node = &cell.m_pNodes[index];
            if (node->m_nPoly >= polygons.size()) throw std::runtime_error("physics query polygon index");
            const auto* plane = polygons[node->m_nPoly].GetPlane();
            const double distance = plane->m_Normal.x*center[0] + plane->m_Normal.y*center[1] + plane->m_Normal.z*center[2] - plane->m_Dist;
            const double radius = std::fabs(plane->m_Normal.x)*half[0] + std::fabs(plane->m_Normal.y)*half[1] + std::fabs(plane->m_Normal.z)*half[2] + .01;
            if (distance > radius) pending.push_back(node->m_nChild[1]);
            else if (distance < -radius) pending.push_back(node->m_nChild[0]);
            else {
                if (!emitted[node->m_nPoly]) { emitted[node->m_nPoly] = 1; result.nodes.push_back(node); }
                pending.push_back(node->m_nChild[0]); pending.push_back(node->m_nChild[1]);
            }
        }
    }
    return result;
}
