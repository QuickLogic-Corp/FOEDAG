#pragma once

#include "AtomSets.h"
#include "HierarhyElement.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fp {

// [aurora2#1725] The RTL instance tree the FloorPlanning panel shows, from the owner table
// synthesis writes: the tree before flatten, so an instance synthesis deleted is still in it.
//
// Verdicts from stage P4 (validation.json) are merged in when available, so the panel can
// render an instance's state -- greyed out when synthesis deleted it -- rather than showing
// every instance as equally usable.
struct RtlInstance {
    // "i_mul_24", or "i_a.i_b" when nested. The top module's own entry is named after the
    // top and does not prefix its children's paths: they stay as the netlist names them.
    std::string path;

    // From stage P4 when validation.json was merged; "unknown" until then. An unknown
    // status must never be presented as usable -- absence of a verdict is not a pass.
    std::string status = "unknown";
    std::string statusReason;  // why it was deleted, or what check failed
    std::vector<std::string> notes;
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
    // The tree from the owner table: the top, then every instance under it.
    void load(const OwnerTable& table);

    // Merges stage P4 verdicts. Optional -- the tree is usable without them, every instance
    // simply stays "unknown". A wrong-typed field leaves that field at its default, the rest
    // merged, and returns false with error() naming it.
    bool mergeVerdicts(const std::filesystem::path& path);

    bool empty() const { return m_instances.empty(); }
    std::size_t size() const { return m_instances.size(); }
    const std::string& top() const { return m_top; }

    // The path of the entry that IS the top, or empty when the tree has none. The panel
    // roots its tree at it.
    std::string topInstance() const;
    const std::string& error() const { return m_error; }

    const std::vector<RtlInstance>& instances() const { return m_instances; }
    const RtlInstance* find(const std::string& path) const;

private:
    void buildTree();

    std::string m_top;
    std::string m_error;
    std::vector<RtlInstance> m_instances;
    std::map<std::string, std::size_t> m_byPath;
};

}  // namespace fp
