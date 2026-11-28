#pragma once

#include "AtomSets.h"
#include "HierarhyElement.h"

#include <memory>

namespace fp {

// Per-floorplan state: ID counters and what the netlist says about its atoms. Each DeviceGrid
// owns one, so the batch checker's grid never resets or overwrites the open panel's
// (aurora2#1725 review U1).
struct FloorplanContext {
    static constexpr int kDefaultAtomsPerTile = 14;

    int nextPartitionId = 0;
    int nextRegionId = 0;

    int atomsPerTile = kDefaultAtomsPerTile;
    DesignResources designResources;
    std::string netlistSha256;
    std::string top;  // the owner table's top: its atoms are the whole design
    AtomTypeMap atomTypes;
    std::map<std::string, std::vector<std::string>> mergedAtomOwners;  // OwnerTable's

    void setAtomsPerTile(int value) { atomsPerTile = (value > 0) ? value : 1; }
    void resetIds() { nextPartitionId = nextRegionId = 0; }
};
using FloorplanContextPtr = std::shared_ptr<FloorplanContext>;

}  // namespace fp
