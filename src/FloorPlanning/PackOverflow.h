#pragma once

#include <string>
#include <vector>

namespace fp {

// [aurora2#1725] A partition VPR could not pack into its regions.
struct PackOverflow {
    struct Region {
        int xLow, yLow, xHigh, yHigh;
    };
    std::string partition;  // empty when no partition in the constraints has these regions
    std::string tileType;
    int assigned = 0;   // blocks of tileType VPR packed the partition into
    int available = 0;  // tiles of tileType in its regions
    std::vector<Region> regions;
};

// The overfull partitions of VPR's last packing attempt in `vprLog`, named by matching their
// regions against `constraintsXml` (<project>_constraints.xml). Earlier attempts are ignored:
// VPR retries, and only the attempt it gave up on says how many blocks the partition needs.
std::vector<PackOverflow> findPackOverflows(const std::string& vprLog,
                                            const std::string& constraintsXml);

std::string packOverflowMessage(const PackOverflow& overflow);

// [aurora2#1725] A packed block VPR found no free tile for in its initial placement.
struct UnplacedBlock {
    std::string block;
    std::string tileType;
    std::vector<std::string> partitions;  // those naming any atom the block holds
};

// The blocks VPR's last initial-placement iteration in `vprLog` could not place, with the
// partitions that pin them: a block's atoms come from `netXml` (<project>_post_synth.net) and
// are matched against `constraintsXml`'s add_atom patterns as VPR matches them.
std::vector<UnplacedBlock> findUnplacedBlocks(const std::string& vprLog,
                                              const std::string& netXml,
                                              const std::string& constraintsXml);

std::string unplacedBlockMessage(const UnplacedBlock& block);

// Why the floorplan check passed a region the packer or placer then could not fill.
std::string packOverflowNote();

}  // namespace fp
