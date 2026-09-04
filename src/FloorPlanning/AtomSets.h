#pragma once

#include "NaturalSort.h"

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fp {

// [aurora2#1725] atomsets.json as the panel and the batch checker both need it: RTL instance
// path -> the netlist atoms belonging to that instance.
//
// Lives here rather than in a widget because REQ-004 needs the same answers with no UI at
// all. A second implementation for batch mode would be a second set of rules to keep in
// step, and the whole point of the requirement is that a rule added to one path is present
// in the other.
using AtomNameMap = std::map<std::string, std::vector<std::string>, NaturalLess>;

// [aurora2#1725] The other half of an atomsets.json entry: RTL instance path -> the raw
// synthesized cell types it holds and how many of each ("$lut", "sdffre", "adder_carry",
// "TDP_ECC36K_BRAM_...", ...). Names are the netlist's own, not friendlier labels: they are
// what the Atom List column shows and what a user greps the .blif for.
//
// Subtree-inclusive exactly like the atom lists, and consistent with them: a parent's
// counts equal the sum over its children.
using AtomResourceMap = std::map<std::string, std::map<std::string, int>, NaturalLess>;

// Reads atomsets.json. Returns false if the file is missing or unreadable; atomsPerTile is
// left untouched unless the file states it.
bool loadAtomSets(const std::filesystem::path& path, AtomNameMap& atomNames,
                  int& atomsPerTile);

// As above, and also the per-instance cell-type counts. A separate overload rather than a
// wider AtomNameMap: the batch checker (REQ-004) sizes regions and has no use for them.
bool loadAtomSets(const std::filesystem::path& path, AtomNameMap& atomNames,
                  AtomResourceMap& atomResources, int& atomsPerTile);

// [aurora2#2377] Atom name -> its real Yosys cell type ("sdffre", "$lut",
// "TDP_ECC36K_FIFO_ASYNC_...", ...), read from <top>_post_synth_debug.json -- the full
// write_json netlist dump floorplanning_post_synth.tcl produces alongside atomsets.json.
//
// classifyAtomType() otherwise has only the atom's own auto-generated NAME to go on, and
// that is not always the atom's own type: a hard macro instance (a BRAM, say) keeps its
// original RTL instance name through synthesis rather than getting a name Yosys derives
// from its type, so name-substring matching silently misclassifies it as CLB. This map is
// ground truth instead of a guess, verified byte-for-byte against real atomsets.json atom
// names.
using AtomTypeMap = std::map<std::string, std::string>;

// Reads <top>_post_synth_debug.json's modules.*.cells map. Returns false if the file is
// missing or unreadable -- not a failure a caller need report, since it only means the type
// column falls back to the atom-name heuristic, same as an atomsets.json with no
// "resources" field today.
bool loadAtomTypes(const std::filesystem::path& debugJsonPath, AtomTypeMap& outTypes);

// Every atom belonging to `path`, its sub-instances included.
//
// An instance with no entry of its own does NOT mean no atoms: floorplanning_atomsets.tcl derives
// an instance path as everything before a cell name's last dot, so a scope holding only
// sub-instances gets no entry. The top instance is always such a scope -- on fft256 "dut"
// had no entry while its children held 19089 atoms between them. Entries are
// subtree-inclusive, so the union over descendants is the full set.
std::set<std::string> atomNamesFor(const AtomNameMap& atomNames, const std::string& path);

// RTL path -> atoms belonging to that instance DIRECTLY, i.e. sitting in it rather than in
// one of its sub-instances. DeviceGrid warns when a partition takes an instance's
// sub-instances and leaves this logic unconstrained.
std::map<std::string, int> ownAtomCounts(const AtomNameMap& atomNames);

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
    int dsp = 0;        // one atom, one tile
    int bram = 0;       // one atom, one tile
    int atoms() const { return clbAtoms + dsp + bram; }
};

// Counts by type. Pass atoms as a set: an atom named by two selected instances -- a parent
// and its child, say -- must be paid for once.
//
// [aurora2#2377] atomTypes, when given, wins over classifyAtomType()'s name-substring
// guess for any atom it names -- see classifyAtomType()'s own realType parameter. Empty by
// default: the "Selected RTL Resources" tally and Partition's running counts must agree on
// what an atom is, so the caller passes Partition::atomTypes() rather than this defaulting
// to some second, independent source of the same map.
ResourceTally tallyResources(const std::set<std::string>& atomNames, int atomsPerTile,
                             const AtomTypeMap& atomTypes = {});

// [aurora2#1725] Cell type -> count over everything `paths` covers.
//
// The entries nest, so this is NOT a sum over the selected paths: selecting an instance and
// something inside it would then pay for the inner one twice. Only the outermost covered
// entries are added -- the same rule Partition::resourceContribution() applies for the same
// reason, and the same answer the atom-set union gives, arrived at without materialising
// tens of thousands of atom names.
//
// A path with no entry of its own is not empty: floorplanning_atomsets.tcl gives no entry to
// a scope that holds only sub-instances, so its descendants' entries are what count.
std::map<std::string, int> tallyAtomResources(const AtomResourceMap& atomResources,
                                              const std::set<std::string>& paths);

}  // namespace fp
