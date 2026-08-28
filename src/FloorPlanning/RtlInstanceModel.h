#pragma once

#include "HierarhyElement.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fp {

// [aurora2#1725 stage P0b/P1] The RTL instance tree the FloorPlanning panel shows. Sourced
// from instances.json, which stage P0b derives from an *elaborated* netlist -- not from
// anything post-synthesis, and not from a VPR subprocess. That distinction is the point:
// synthesis dissolves the hierarchy and can delete instances outright (e.g. one that gets
// constant-folded away), so a tree derived from a synthesised netlist could never show them,
// and the user could never constrain or even see them.
//
// Verdicts from stage P4 (validation.json) are merged in when available, so the panel can
// render an instance's state -- greyed out when synthesis deleted it, flagged when its atom
// set is only partially trustworthy -- rather than showing every instance as equally usable.
// See pipeline.md (A.P0, A.P4).
struct RtlInstance {
    std::string path;          // "i_mul_24", or "i_a.i_b" when nested
    std::string component;     // unmangled component name, e.g. "mul_24"
    std::string moduleRaw;     // raw module name, e.g. "mul_24_default(rtl)"
    std::string src;           // instantiation site, "fpu_single.vhd:225.2-225.10"
    std::string componentSrc;  // entity declaration, "mul_24.vhd:52.8-52.14"

    // From stage P4 when validation.json was merged; "unknown" until then. An unknown
    // status must never be presented as usable -- absence of a verdict is not a pass.
    std::string status = "unknown";
    std::string statusReason;  // why it was deleted, or what check failed
    int atomCount = -1;        // -1 = not known yet

    std::vector<std::string> children;  // direct children, by full path
    bool isLeaf = true;

    bool deleted() const { return status == "deleted"; }
    bool partial() const { return status == "partial"; }
    // Only a graded, surviving instance is safe to offer as a constraint target.
    bool constrainable() const { return status == "complete" || status == "partial"; }
};

// [aurora2#1725 stage P7] Reads <project>_floorplanning_placement.json. A missing file is
// ordinary -- placement may simply not have run yet -- and yields an empty map, which the
// panel renders as "no status", never as "nothing is placed".
std::map<std::string, InstancePlacement> loadPlacementVerdicts(
    const std::filesystem::path& path);

class RtlInstanceModel {
public:
    // Reads instances.json. Fails, rather than returning an empty tree, when the file is
    // missing or malformed: the panel must say "run synthesis first" instead of silently
    // showing nothing.
    bool loadInstances(const std::filesystem::path& path);

    // Merges stage P4 verdicts. Optional -- the tree is usable without them, every instance
    // simply stays "unknown".
    bool mergeVerdicts(const std::filesystem::path& path);

    bool empty() const { return m_instances.empty(); }
    std::size_t size() const { return m_instances.size(); }
    const std::string& top() const { return m_top; }
    const std::string& error() const { return m_error; }

    const std::vector<RtlInstance>& instances() const { return m_instances; }
    const RtlInstance* find(const std::string& path) const;

    // [aurora2#1725 stage P5] True if "path" names a real instance itself, OR is a
    // strict ancestor of one -- e.g. a SystemVerilog generate-block label like
    // "cluster[0]" is never itself a recorded instance (only "cluster[0].clb", the
    // actual module instantiation inside it, is), but a whole-subtree selection under
    // that label is still real RTL and still needs the "." + "*" wildcard suffix at
    // emission time. False for a plain leaf atom/net name (e.g. "out[0]") that instances
    // does not know about at all.
    bool isInstanceOrAncestor(const std::string& path) const;

    // Paths with no parent in the model, i.e. the top level of the tree.
    std::vector<std::string> roots() const;

    // The element set the partition model consumes. Deleted instances are excluded: they have
    // no atoms, so constraining one is meaningless. Their intent is still preserved in the
    // .qdc and reported by stage P5's manifest, which is what lets the UI grey them out
    // rather than dropping them silently.
    HierarhyElements toHierarhyElements() const;

private:
    void buildTree();

    std::string m_top;
    std::string m_error;
    std::vector<RtlInstance> m_instances;
    std::map<std::string, std::size_t> m_byPath;
};

}  // namespace fp
