#pragma once

#include "NaturalSort.h"

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fp {

// [aurora2#1725] RTL instance path -> the netlist atoms its subtree owns.
//
// Lives here rather than in a widget because REQ-004 needs the same answers with no UI at
// all: the panel and the batch checker read the owner table the same way.
using AtomNameMap = std::map<std::string, std::vector<std::string>, NaturalLess>;

// RTL instance path -> the cell types its subtree holds and how many of each ("$lut",
// "sdffre", "QL_DSP4_MULT", ...), named as the netlist names them.
using AtomResourceMap = std::map<std::string, std::map<std::string, int>, NaturalLess>;

// Atom name -> its Yosys cell type ("sdffre", "$lut", "TDP36K", ...). Ground truth for the
// Type column and Partition's clb/dsp/bram counts, instead of classifyAtomType()'s name guess.
using AtomTypeMap = std::map<std::string, std::string>;

// [aurora2#1725] The owner table synthesis writes (<project>_floorplanning_atomsets.json,
// schema version 2): every atom of the netlist, named as VPR names it, and the RTL instance
// that owns it, plus the instance tree before flatten.
struct OwnerTable {
    std::string top;
    std::string netlistSha256;
    std::vector<std::string> instances;          // dotted RTL instance paths
    AtomNameMap atomNames;                       // the top holds every atom
    AtomResourceMap atomResources;               // subtree-inclusive, like atomNames
    AtomTypeMap atomTypes;
    std::map<std::string, int> ownAtomCounts;    // atoms an instance owns directly
};

// Reads the owner table. False, with `error` saying why, when the file is missing, is not
// JSON, or is not schema version 2 with a netlist hash.
bool loadOwnerTable(const std::filesystem::path& path, OwnerTable& table, std::string& error);

// The table's netlist_sha256, or "" when the file is missing, unreadable or has none.
std::string readNetlistSha256(const std::filesystem::path& path);

// Whether the panel should offer to reload after a compile: a different netlist always does,
// whatever the tier; the same netlist does only when better resource figures arrived.
bool floorplanReloadNeeded(int loadedTier, const std::string& loadedSha256, int diskTier,
                           const std::string& diskSha256);

// DSP-V4 leaf atom (QL_DSP4_*) -> an id shared by every leaf of the same physical tile.
// Flattening names each leaf after a net, so one tile's leaves can sit under different
// instances, and only one of them is the QL_DSP4_MULT that countsAsDspTile() counts.
using DspTileMap = std::map<std::string, std::string>;

// Groups the netlist's DSP-V4 leaves into tiles by the nets inside a tile, from
// <top>_post_synth_debug.json. False when the file is missing or unreadable: the debug
// dump is optional, and without it each QL_DSP4_MULT counts as one tile.
bool loadDspTiles(const std::filesystem::path& debugJsonPath, DspTileMap& outDspTiles);

// Every atom belonging to `path`, its sub-instances included.
//
// A path with no entry of its own (a generate label) is the union of the entries under it.
std::set<std::string> atomNamesFor(const AtomNameMap& atomNames, const std::string& path);

// A .qdc element with a "*" in it ("cluster[*].*", "clust*[0].*"): a pattern the flow turns
// into a regex, not an instance path.
bool isElementPattern(const std::string& element);
// True when `path` is an instance the pattern selects, or lies inside one.
bool elementPatternCovers(const std::string& pattern, const std::string& path);

// [aurora2#1725] What a set of atoms costs in tiles.
//
// The same arithmetic Partition does for its required columns, on any atom set rather than
// on a partition's: dsp and bram atoms are one tile each, clb atoms are luts and flops that
// pack many to a tile and so are divided by the atoms-per-tile hint. Shared rather than
// repeated, so the "Selected RTL Resources" table and a partition's row cannot disagree
// about what the same instances cost.
struct ResourceTally {
    int clbAtoms = 0;   // luts, flops, carries -- many to a tile
    int clbTiles = 0;   // clbAtoms over atomsPerTile, rounded up: an estimate, see Partition
    int dspAtoms = 0;
    int dsp = 0;        // tiles: see countsAsDspTile()
    int bram = 0;       // one atom, one tile
    int atomsPerTile = 1;
    int atoms() const { return clbAtoms + dspAtoms + bram; }
};

// Counts by type. Pass atoms as a set: an atom named by two selected instances -- a parent
// and its child, say -- must be paid for once.
//
// [aurora2#2377] atomTypes, when given, wins over classifyAtomType()'s name-substring
// guess for any atom it names -- see classifyAtomType()'s own realType parameter. Empty by
// default: the "Selected RTL Resources" tally and Partition's running counts must agree on
// what an atom is, so the caller passes FloorplanContext::atomTypes rather than this defaulting
// to some second, independent source of the same map.
ResourceTally tallyResources(const std::set<std::string>& atomNames, int atomsPerTile,
                             const AtomTypeMap& atomTypes = {},
                             const DspTileMap& dspTiles = {});

// [aurora2#1725] Cell type -> count over everything `paths` covers.
//
// The entries nest, so this is NOT a sum over the selected paths: selecting an instance and
// something inside it would then pay for the inner one twice. Only the outermost covered
// entries are added -- the same rule Partition::resourceContribution() applies for the same
// reason, and the same answer the atom-set union gives, arrived at without materialising
// tens of thousands of atom names.
//
// A path with no entry of its own (a generate label) counts the entries under it.
std::map<std::string, int> tallyAtomResources(const AtomResourceMap& atomResources,
                                              const std::set<std::string>& paths);

}  // namespace fp
