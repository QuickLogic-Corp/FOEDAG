#include "PackOverflow.h"

#include <QXmlStreamReader>

#include <algorithm>
#include <regex>
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

std::vector<std::pair<std::string, std::vector<PackOverflow::Region>>> partitionRegions(
    const std::string& constraintsXml)
{
    std::vector<std::pair<std::string, std::vector<PackOverflow::Region>>> partitions;
    QXmlStreamReader xml(QString::fromStdString(constraintsXml));
    while (!xml.atEnd()) {
        if (!xml.readNextStartElement()) continue;
        const auto attrs = xml.attributes();
        if (xml.name() == QLatin1String("partition")) {
            partitions.push_back({attrs.value("name").toString().toStdString(), {}});
        } else if (xml.name() == QLatin1String("add_region") && !partitions.empty()) {
            partitions.back().second.push_back({attrs.value("x_low").toInt(),
                                                attrs.value("y_low").toInt(),
                                                attrs.value("x_high").toInt(),
                                                attrs.value("y_high").toInt()});
        }
    }
    return partitions;
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

    const auto partitions = partitionRegions(constraintsXml);
    for (PackOverflow& overflow : last) {
        for (const auto& [name, regions] : partitions) {
            if (sameRegions(regions, overflow.regions)) {
                overflow.partition = name;
                break;
            }
        }
    }
    return last;
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
    return who + " needs " + std::to_string(overflow.assigned) + " " + overflow.tileType +
           " tiles after packing but only " + std::to_string(overflow.available) +
           " available. Extend its regions.";
}

std::string packOverflowNote()
{
    return "Note: the floorplan check estimates clb tiles from the number of atoms in a "
           "partition. The packer's real count also depends on how those atoms connect and "
           "how many clb input pins they need: unrelated logic, such as the glue around a "
           "BRAM, spreads over more clbs than its atom count suggests. So a region can pass "
           "the check and still be too small.";
}

}  // namespace fp
