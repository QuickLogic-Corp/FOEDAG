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

// object[key] when it holds a T, else fallback. A key present with another type is named in
// `problem` (first one only): json::value() would throw it out of the Qt slot reading the file.
template <typename T>
T typedValue(const nlohmann::json& object, const std::string& owner, const char* key, T fallback,
             std::string& problem)
{
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return fallback;
    }
    try {
        return it->get<T>();
    } catch (const nlohmann::json::exception&) {
        if (problem.empty()) {
            problem = "\"" + std::string{key} + "\" of " + owner + " has the wrong type";
        }
        return fallback;
    }
}

}  // namespace

void RtlInstanceModel::load(const OwnerTable& table)
{
    m_instances.clear();
    m_error.clear();
    m_top = table.top;
    if (!m_top.empty()) {
        RtlInstance top;
        top.path = m_top;
        m_instances.push_back(std::move(top));
    }
    for (const std::string& path : table.instances) {
        RtlInstance instance;
        instance.path = path;
        m_instances.push_back(std::move(instance));
    }
    buildTree();
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
        // A wrong-typed field reads as absent, like an unreadable file above.
        std::string ignored;
        InstancePlacement placement;
        placement.partition = typedValue(entry, it.key(), "partition", std::string{}, ignored);
        placement.region = typedValue(entry, it.key(), "region", std::string{}, ignored);
        placement.atomsTotal = typedValue(entry, it.key(), "atoms_total", 0, ignored);
        placement.inRegion = typedValue(entry, it.key(), "in_region", 0, ignored);

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

    std::string problem;
    for (auto it = doc["instances"].begin(); it != doc["instances"].end(); ++it) {
        const auto found = m_byPath.find(it.key());
        if (found == m_byPath.end()) {
            // A verdict for an instance the tree does not know about. Not fatal, but it means
            // the two files came from different elaborations.
            continue;
        }
        RtlInstance& instance = m_instances[found->second];
        const auto& verdict = it.value();
        const std::string owner = "'" + it.key() + "'";
        instance.status = typedValue(verdict, owner, "verdict", std::string{"unknown"}, problem);
        instance.atomCount = typedValue(verdict, owner, "atom_count", -1, problem);
        instance.notes.clear();
        // REQ-008: how much of the instance the owner rule inferred rather than recorded.
        const int inferred = typedValue(verdict, owner, "inferred", 0, problem);
        if (inferred > 0) {
            const int recorded = typedValue(verdict, owner, "recorded", 0, problem);
            instance.notes.push_back(std::to_string(inferred) + " of " +
                                     std::to_string(recorded + inferred) +
                                     " atoms owned by connectivity rather than recorded "
                                     "by synthesis");
        }
        if (verdict.contains("notes") && verdict["notes"].is_array()) {
            for (const auto& note : verdict["notes"]) {
                if (note.is_string()) instance.notes.push_back(note.get<std::string>());
            }
        }
        if (verdict.contains("reason") && verdict["reason"].is_string()) {
            instance.statusReason = verdict["reason"].get<std::string>();
        } else if (verdict.contains("warnings") && verdict["warnings"].is_array()
                   && !verdict["warnings"].empty()) {
            // Surface the first real warning; a "... not run" note is bookkeeping
            // rather than something the user needs in a tooltip.
            for (const auto& warning : verdict["warnings"]) {
                if (!warning.is_string()) {
                    continue;
                }
                const std::string text = warning.get<std::string>();
                if (text.find("not run") == std::string::npos) {
                    instance.statusReason = text;
                    break;
                }
            }
        }
    }
    if (!problem.empty()) {
        m_error = "verdicts are malformed (" + path.string() + "): " + problem;
        return false;
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

std::string RtlInstanceModel::topInstance() const
{
    return find(m_top) != nullptr ? m_top : std::string{};
}

}  // namespace fp
