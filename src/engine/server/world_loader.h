#pragma once
#include <memory>
#include "world_record.h"
#include <array>
#include <string>
#include <vector>
class CWorldBsp;
class ILTStream;

// Consumes one version-66 model from the current stream position. The caller
// owns the result only after complete validation; failures throw an exception.
std::unique_ptr<CWorldBsp> w_ReadWorldModel(ILTStream& stream, bool current);

struct WorldLayoutNode {
    LTVector min, max;
    std::array<uint32, 4> children{0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};
};
struct WorldGeometryFile {
    float lightmap_grid_size() const { return distance; }
    std::array<uint32, 11> header{};
    std::string information;
    float distance = 0;
    LTVector min{0,0,0}, max{0,0,0};
    std::array<uint32, 2> layout_metadata{};
    std::vector<WorldLayoutNode> layout;
    std::vector<std::unique_ptr<CServerWorldRec>> models;
};
struct WorldFileInformation {
    std::array<uint32,11> header{};
    std::string information;
};
// Reads only the bounded header/information prefix, without loading BSPs,
// textures, lights or level objects. The stream stops after the information.
WorldFileInformation w_ReadWorldInformation(ILTStream&);
std::unique_ptr<WorldGeometryFile> w_ReadWorldGeometry(ILTStream& stream);
const WorldLayoutNode* w_FindLayoutNode(std::span<const WorldLayoutNode>, const CWorldSimpleHeader&);
