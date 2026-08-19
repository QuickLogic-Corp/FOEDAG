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

// Reads atomsets.json. Returns false if the file is missing or unreadable; atomsPerTile is
// left untouched unless the file states it.
bool loadAtomSets(const std::filesystem::path& path, AtomNameMap& atomNames,
                  int& atomsPerTile);

// Every atom belonging to `path`, its sub-instances included.
//
// An instance with no entry of its own does NOT mean no atoms: aurora_atomsets.tcl derives
// an instance path as everything before a cell name's last dot, so a scope holding only
// sub-instances gets no entry. The top instance is always such a scope -- on fft256 "dut"
// had no entry while its children held 19089 atoms between them. Entries are
// subtree-inclusive, so the union over descendants is the full set.
std::set<std::string> atomNamesFor(const AtomNameMap& atomNames, const std::string& path);

// RTL path -> atoms belonging to that instance DIRECTLY, i.e. sitting in it rather than in
// one of its sub-instances. DeviceGrid warns when a partition takes an instance's
// sub-instances and leaves this logic unconstrained.
std::map<std::string, int> ownAtomCounts(const AtomNameMap& atomNames);

}  // namespace fp
