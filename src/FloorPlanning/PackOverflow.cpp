#include "PackOverflow.h"

#include <QXmlStreamReader>

#include <algorithm>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <tuple>

namespace fp {

namespace {

bool sameRegions(const std::vector<PackOverflow::Region>& a,
                 const std::vector<PackOverflow::Region>& b)
{
    return std::is_permutation(a.begin(), a.end(), b.begin(), b.end(),
                               [](const PackOverflow::Region& l, const PackOverflow::Region& r) {
                                   return std::tie(l.xLow, l.yLow, l.xHigh, l.yHigh) ==
                                          std::tie(r.xLow, r.yLow, r.xHigh, r.yHigh);
                               });
}

struct ConstraintPartition {
    std::string name;
    std::vector<PackOverflow::Region> regions;
    std::vector<std::string> literals;
    std::vector<std::regex> patterns;

    bool names(const std::string& atom) const {
        if (std::find(literals.begin(), literals.end(), atom) != literals.end()) return true;
        return std::any_of(patterns.begin(), patterns.end(),
                           [&atom](const std::regex& re) { return std::regex_search(atom, re); });
    }
};

std::vector<ConstraintPartition> readPartitions(const std::string& constraintsXml)
{
    std::vector<ConstraintPartition> partitions;
    QXmlStreamReader xml(QString::fromStdString(constraintsXml));
    while (!xml.atEnd()) {
        if (!xml.readNextStartElement()) continue;
        const auto attrs = xml.attributes();
        if (xml.name() == QLatin1String("partition")) {
            partitions.push_back({attrs.value("name").toString().toStdString(), {}, {}, {}});
        } else if (partitions.empty()) {
            continue;
        } else if (xml.name() == QLatin1String("add_region")) {
            partitions.back().regions.push_back({attrs.value("x_low").toInt(),
                                                 attrs.value("y_low").toInt(),
                                                 attrs.value("x_high").toInt(),
                                                 attrs.value("y_high").toInt()});
        } else if (xml.name() == QLatin1String("add_atom")) {
            const std::string pattern = attrs.value("name_pattern").toString().toStdString();
            if (attrs.value("is_regex") == QLatin1String("true")) {
                partitions.back().patterns.emplace_back(pattern);
            } else {
                partitions.back().literals.push_back(pattern);
            }
        }
    }
    return partitions;
}

// Every named block nested in each of `clusters`, the cluster's own name included: the .net's
// outermost blocks are the placed clusters, and what they contain are the atoms.
std::map<std::string, std::set<std::string>> clusterAtoms(const std::string& netXml,
                                                          const std::set<std::string>& clusters)
{
    std::map<std::string, std::set<std::string>> atoms;
    QXmlStreamReader xml(QString::fromStdString(netXml));
    int depth = 0;
    std::string cluster;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QLatin1String("block")) {
            ++depth;
            const std::string name = xml.attributes().value("name").toString().toStdString();
            if (depth == 2) cluster = clusters.count(name) ? name : "";
            if (depth >= 2 && !cluster.empty() && name != "open") atoms[cluster].insert(name);
        } else if (xml.isEndElement() && xml.name() == QLatin1String("block")) {
            --depth;
        }
    }
    return atoms;
}

}  // namespace

std::vector<PackOverflow> findPackOverflows(const std::string& vprLog,
                                            const std::string& constraintsXml)
{
    static const std::regex kAssigned(
        R"(A partition including the following regions has been assigned (\d+) blocks of type (\S+), but only has (\d+) tiles)");
    static const std::regex kRegion(R"(Region \((\d+), (\d+), \d+\) to \((\d+), (\d+), \d+\))");

    std::vector<PackOverflow> last;
    std::vector<PackOverflow> current;
    std::istringstream lines(vprLog);
    std::string line;
    std::smatch m;
    while (std::getline(lines, line)) {
        if (std::regex_search(line, m, kAssigned)) {
            current.push_back({"", m[2], std::stoi(m[1]), std::stoi(m[3]), {}});
        } else if (!current.empty() && std::regex_search(line, m, kRegion)) {
            current.back().regions.push_back(
                {std::stoi(m[1]), std::stoi(m[2]), std::stoi(m[3]), std::stoi(m[4])});
        } else if (line.find("Floorplan regions are overfull") != std::string::npos ||
                   line.find("densely enough to fit") != std::string::npos) {
            if (!current.empty()) last = std::move(current);
            current.clear();
        }
    }
    if (!current.empty()) last = std::move(current);

    const std::vector<ConstraintPartition> partitions = readPartitions(constraintsXml);
    for (PackOverflow& overflow : last) {
        for (const ConstraintPartition& partition : partitions) {
            if (sameRegions(partition.regions, overflow.regions)) {
                overflow.partition = partition.name;
                break;
            }
        }
    }
    return last;
}

std::vector<UnplacedBlock> findUnplacedBlocks(const std::string& vprLog,
                                              const std::string& netXml,
                                              const std::string& constraintsXml)
{
    static const std::regex kUnplaced(
        R"(Block (.+) \(# \d+\) of type (\S+) could not be placed during initial placement iteration (\d+))");

    std::vector<UnplacedBlock> blocks;
    int lastIteration = -1;
    std::istringstream lines(vprLog);
    std::string line;
    std::smatch m;
    while (std::getline(lines, line)) {
        if (!std::regex_search(line, m, kUnplaced)) continue;
        const int iteration = std::stoi(m[3]);
        if (iteration != lastIteration) {
            blocks.clear();
            lastIteration = iteration;
        }
        blocks.push_back({m[1], m[2], {}});
    }
    if (blocks.empty()) return blocks;

    std::set<std::string> names;
    for (const UnplacedBlock& block : blocks) names.insert(block.block);
    const auto atoms = clusterAtoms(netXml, names);
    const std::vector<ConstraintPartition> partitions = readPartitions(constraintsXml);
    for (UnplacedBlock& block : blocks) {
        const auto it = atoms.find(block.block);
        if (it == atoms.end()) continue;
        for (const ConstraintPartition& partition : partitions) {
            if (std::any_of(it->second.begin(), it->second.end(),
                            [&partition](const std::string& atom) {
                                return partition.names(atom);
                            })) {
                block.partitions.push_back(partition.name);
            }
        }
    }
    return blocks;
}

std::string unplacedBlockMessage(const UnplacedBlock& block)
{
    if (block.partitions.empty()) {
        return "Block '" + block.block + "' of type " + block.tileType + " found no free tile.";
    }
    std::string names;
    for (const std::string& partition : block.partitions) {
        names += (names.empty() ? "'" : ", '") + partition + "'";
    }
    return "Block '" + block.block + "' of partition " + names + " found no free " +
           block.tileType + " tile in its regions. Extend its regions.";
}

std::string packOverflowMessage(const PackOverflow& overflow)
{
    std::string who;
    if (!overflow.partition.empty()) {
        who = "Partition '" + overflow.partition + "'";
    } else {
        who = "Partition with regions";
        for (const PackOverflow::Region& r : overflow.regions) {
            who += " (" + std::to_string(r.xLow) + "," + std::to_string(r.yLow) + "):(" +
                   std::to_string(r.xHigh) + "," + std::to_string(r.yHigh) + ")";
        }
    }
    return who + " packs into " + std::to_string(overflow.assigned) + " " + overflow.tileType +
           " blocks but its regions have only " + std::to_string(overflow.available) + " " +
           overflow.tileType + " tiles. Give it at least " + std::to_string(overflow.assigned) +
           ", plus spare tiles for placement.";
}

std::string packOverflowNote()
{
    return "Note: the floorplan check estimates clb tiles from the number of atoms in a "
           "partition. The packer's real count also depends on how those atoms connect and "
           "how many clb input pins they need: unrelated logic, such as the glue around a "
           "BRAM, spreads over more clbs than its atom count suggests. Placement then needs "
           "spare tiles too: chained carry blocks must sit next to each other, and a block "
           "that mixes a partition's atoms with free ones still takes one of its tiles. So a "
           "region can pass the check and still be too small.";
}

}  // namespace fp
