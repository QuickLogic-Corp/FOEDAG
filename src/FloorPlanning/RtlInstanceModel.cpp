#include "RtlInstanceModel.h"

#include "nlohmann_json/json.hpp"

#include <algorithm>
#include <fstream>

namespace fp {

namespace {

std::string parentOf(const std::string& path)
{
    const std::size_t dot = path.rfind('.');
    return dot == std::string::npos ? std::string{} : path.substr(0, dot);
}

}  // namespace

bool RtlInstanceModel::loadInstances(const std::filesystem::path& path)
{
    m_instances.clear();
    m_byPath.clear();
    m_top.clear();
    m_topInstance.clear();
    m_hasTopInstance = false;
    m_error.clear();

    if (!std::filesystem::exists(path)) {
        m_error = "instance list not found: " + path.string()
                + " -- run the SYNTHESIS task first";
        return false;
    }

    nlohmann::json doc;
    try {
        std::ifstream stream(path);
        stream >> doc;
    } catch (const std::exception& e) {
        m_error = "instance list is not valid JSON (" + path.string() + "): " + e.what();
        return false;
    }

    if (!doc.contains("instances") || !doc["instances"].is_array()) {
        m_error = "instance list has no \"instances\" array: " + path.string();
        return false;
    }

    m_top = doc.value("top", std::string{});
    // Present since P0b began emitting a root entry; null when it deliberately did not (see
    // isTop()). Absent entirely on an older file, which m_hasTopInstance distinguishes from
    // an explicit null -- the two must not be treated alike, since one means "ask the
    // fallback" and the other means "there is no top entry".
    m_hasTopInstance = doc.contains("top_instance");
    if (m_hasTopInstance && doc["top_instance"].is_string()) {
        m_topInstance = doc["top_instance"].get<std::string>();
    }

    for (const auto& entry : doc["instances"]) {
        if (!entry.contains("path")) {
            continue;
        }
        RtlInstance instance;
        instance.path = entry.value("path", std::string{});
        instance.component = entry.value("component", std::string{});
        instance.moduleRaw = entry.value("module_raw", std::string{});
        // src may be JSON null when the elaborated netlist was written with -noattr; stage
        // P0b rejects that case, but tolerate it here rather than throwing.
        if (entry.contains("src") && entry["src"].is_string()) {
            instance.src = entry["src"].get<std::string>();
        }
        if (entry.contains("component_src") && entry["component_src"].is_string()) {
            instance.componentSrc = entry["component_src"].get<std::string>();
        }
        // last_status is carried forward by P0b from the previous run, so the panel can show
        // a sensible state before validation has run again.
        instance.status = entry.value("last_status", std::string{"unknown"});
        // [aurora2#1725 stage P0b] The root entry holding the whole design. Flagged by
        // P0b rather than inferred here: "no dot in the path" is true of every top-level
        // instance too, so the shape of the path cannot tell them apart.
        instance.isTop = entry.value("is_top", false);
        m_instances.push_back(std::move(instance));
    }

    if (m_instances.empty()) {
        m_error = "instance list is empty: " + path.string();
        return false;
    }

    buildTree();
    return true;
}

std::map<std::string, InstancePlacement> loadPlacementVerdicts(
    const std::filesystem::path& path)
{
    std::map<std::string, InstancePlacement> placements;
    if (!std::filesystem::exists(path)) {
        return placements;
    }

    nlohmann::json doc;
    try {
        std::ifstream stream(path);
        stream >> doc;
    } catch (const std::exception&) {
        // Unreadable is treated as absent, deliberately: a placement verdict is an extra,
        // and a malformed one must not stop the panel from opening.
        return placements;
    }

    if (!doc.contains("instances") || !doc["instances"].is_object()) {
        return placements;
    }

    for (auto it = doc["instances"].begin(); it != doc["instances"].end(); ++it) {
        const auto& entry = it.value();
        InstancePlacement placement;
        placement.partition = entry.value("partition", std::string{});
        placement.region = entry.value("region", std::string{});
        placement.atomsTotal = entry.value("atoms_total", 0);
        placement.inRegion = entry.value("in_region", 0);

        if (entry.contains("outside") && entry["outside"].is_array()) {
            for (const auto& atom : entry["outside"]) {
                // ["<atom>", x, y], with x/y null for an atom no .place row mentions.
                if (!atom.is_array() || atom.empty() || !atom[0].is_string()) continue;
                PlacedAtom outside;
                outside.name = atom[0].get<std::string>();
                if (atom.size() >= 3 && atom[1].is_number_integer()
                    && atom[2].is_number_integer()) {
                    outside.x = atom[1].get<int>();
                    outside.y = atom[2].get<int>();
                    outside.located = true;
                }
                placement.outside.push_back(std::move(outside));
            }
        }
        placements[it.key()] = std::move(placement);
    }
    return placements;
}

bool RtlInstanceModel::mergeVerdicts(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path)) {
        m_error = "verdicts not found: " + path.string();
        return false;
    }

    nlohmann::json doc;
    try {
        std::ifstream stream(path);
        stream >> doc;
    } catch (const std::exception& e) {
        m_error = std::string{"verdicts are not valid JSON: "} + e.what();
        return false;
    }

    if (!doc.contains("instances") || !doc["instances"].is_object()) {
        m_error = "verdicts have no \"instances\" object: " + path.string();
        return false;
    }

    for (auto it = doc["instances"].begin(); it != doc["instances"].end(); ++it) {
        const auto found = m_byPath.find(it.key());
        if (found == m_byPath.end()) {
            // A verdict for an instance the tree does not know about. Not fatal, but it means
            // the two files came from different elaborations.
            continue;
        }
        RtlInstance& instance = m_instances[found->second];
        const auto& verdict = it.value();
        instance.status = verdict.value("verdict", std::string{"unknown"});
        instance.atomCount = verdict.value("atom_count", -1);
        if (verdict.contains("reason") && verdict["reason"].is_string()) {
            instance.statusReason = verdict["reason"].get<std::string>();
        } else if (verdict.contains("warnings") && verdict["warnings"].is_array()
                   && !verdict["warnings"].empty()) {
            // Surface the first real warning; the "check 3 not run" note is bookkeeping
            // rather than something the user needs in a tooltip.
            for (const auto& warning : verdict["warnings"]) {
                const std::string text = warning.get<std::string>();
                if (text.find("not run") == std::string::npos) {
                    instance.statusReason = text;
                    break;
                }
            }
        }
    }
    return true;
}

void RtlInstanceModel::buildTree()
{
    m_byPath.clear();
    for (std::size_t i = 0; i < m_instances.size(); ++i) {
        m_byPath[m_instances[i].path] = i;
    }

    for (RtlInstance& instance : m_instances) {
        instance.children.clear();
        instance.isLeaf = true;
    }

    for (const RtlInstance& instance : m_instances) {
        const std::string parent = parentOf(instance.path);
        if (parent.empty()) {
            continue;
        }
        const auto found = m_byPath.find(parent);
        if (found == m_byPath.end()) {
            continue;
        }
        RtlInstance& parentInstance = m_instances[found->second];
        parentInstance.children.push_back(instance.path);
        parentInstance.isLeaf = false;
    }
}

const RtlInstance* RtlInstanceModel::find(const std::string& path) const
{
    const auto found = m_byPath.find(path);
    return found == m_byPath.end() ? nullptr : &m_instances[found->second];
}

bool RtlInstanceModel::isTop(const std::string& path) const
{
    const RtlInstance* instance = find(path);
    if (instance == nullptr) {
        return false;
    }
    if (m_hasTopInstance) {
        // The file states the answer, including stating that there is none.
        return !m_topInstance.empty() && path == m_topInstance;
    }
    // Older file: is_top is absent too, so "top" is all there is to go on.
    return instance->isTop || (!m_top.empty() && path == m_top);
}

std::string RtlInstanceModel::topInstance() const
{
    // Same precedence as isTop(), expressed once: the document's own answer wins, including
    // its answer that there is none, and only an older file falls back to "top".
    if (m_hasTopInstance) {
        return (!m_topInstance.empty() && find(m_topInstance) != nullptr) ? m_topInstance
                                                                         : std::string{};
    }
    for (const RtlInstance& instance : m_instances) {
        if (instance.isTop) {
            return instance.path;
        }
    }
    return (!m_top.empty() && find(m_top) != nullptr) ? m_top : std::string{};
}

bool RtlInstanceModel::isInstanceOrAncestor(const std::string& path) const
{
    if (m_byPath.find(path) != m_byPath.end()) {
        return true;
    }
    // m_byPath is ordered by path, so every path with "path." as a prefix (if any)
    // sorts immediately at or after "path." itself -- one lookup, no linear scan.
    const std::string prefix = path + ".";
    const auto next = m_byPath.lower_bound(prefix);
    return next != m_byPath.end()
        && next->first.compare(0, prefix.size(), prefix) == 0;
}

std::vector<std::string> RtlInstanceModel::roots() const
{
    std::vector<std::string> result;
    for (const RtlInstance& instance : m_instances) {
        const std::string parent = parentOf(instance.path);
        if (parent.empty() || m_byPath.find(parent) == m_byPath.end()) {
            result.push_back(instance.path);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

HierarhyElements RtlInstanceModel::toHierarhyElements() const
{
    HierarhyElements elements;
    for (const RtlInstance& instance : m_instances) {
        if (instance.deleted()) {
            continue;
        }
        // No vprNames: the .qdc and this model carry RTL names only, and expansion to netlist
        // names happens at emission time (REQ-002 / REQ-003).
        elements.insert(HierarhyElement{instance.path, instance.isLeaf});
    }
    return elements;
}

}  // namespace fp
