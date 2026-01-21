#include "QdcSerializer.h"
#include "DeviceGrid.h"
#include "HierarhyElement.h"
#include "Utils/FileUtils.h"
#include "Utils/StringUtils.h"

#include <QDebug>

namespace fp {

void QdcSerializer::save(const DeviceGrid& device, const std::filesystem::path& filePath)
{
    std::string content = serialize(device);
    if (!content.empty()) {
        FOEDAG::FileUtils::WriteToFile(filePath, content);
    }
}

std::string QdcSerializer::serialize(const DeviceGrid& device)
{
    std::string content;
    for (const auto& [id, partition]: device.partitions()) {
        if (!partition->elements()) {
            continue;
        } if(partition->elements()->empty()) {
            continue;
        }

        // collect netlist elements
        std::string elementsStr = "";
        int elementsCounter = 0;
        for (const HierarhyElement& element: *partition->elements()) {
            if (element.isLeaf) {
                elementsStr += element.path;
            } else {
                elementsStr += element.path + ".*";
            }
            elementsCounter++;
            if (elementsCounter < partition->elements()->size()) {
                elementsStr += ",";
                elementsStr += lineDelimiter();
            }
        }

        // collect regions
        std::string regionsStr;
        int regionCounter = 0;
        for (const auto& [id, region]: partition->regions()) {
            const Tile& blTile = device.tile(region->bottomLeftIndex());
            const Tile& trTile = device.tile(region->topRightIndex());

            const bool isBottomLeftTileFullyIncluded = region->contains(blTile.index());
            const bool isTopRightTileFullyIncluded = region->contains(trTile.index());

            std::string bottomLeftStr;
            if (isBottomLeftTileFullyIncluded) {
                bottomLeftStr = blTile.name().toStdString();
            } else {
                bottomLeftStr = device.buildTileSymbolicName(Tile::Type::Clb, region->bottomLeftIndex());
            }

            std::string topRightStr;
            if (isTopRightTileFullyIncluded) {
                topRightStr = trTile.name().toStdString();
            } else {
                topRightStr = device.buildTileSymbolicName(Tile::Type::Clb, region->topRightIndex());
            }

            std::string regionStr{bottomLeftStr};
            if (topRightStr != bottomLeftStr) {
                regionStr += ":";
                regionStr += topRightStr;
            }

            regionsStr += regionStr;

            regionCounter++;
            if (regionCounter < partition->regions().size()) {
                regionsStr += ",";
                regionsStr += lineDelimiter();
            }
        }

        // aggregate line
        std::string line = "set_partition ";
        line += lineDelimiter();

        line += elementsStr;

        line += " ";
        line += lineDelimiter();

        line += regionsStr;

        line += " ";
        line += lineDelimiter();

        line += partition->name();

        line += "\n\n";

        // add line to context
        content += line;
    }

    return content;
}

void QdcSerializer::load(DeviceGrid& device, const std::filesystem::path& filePath)
{
    std::vector<std::string> lines = readLines(filePath);
    if (!lines.empty()) {
        load(device, lines);
    }
}

void QdcSerializer::load(DeviceGrid& device, const std::vector<std::string>& lines)
{
    auto splitRegionsIgnoringParens = [](const std::string& s, char delimiter)->std::vector<std::string> {
        std::vector<std::string> result;
        std::string current;

        int depth = 0;

        for (char c : s) {
            if (c == '(') {
                depth++;
                current += c;
            } else if (c == ')') {
                depth--;
                current += c;
            } else if (c == delimiter && depth == 0) {
                result.push_back(current);
                current.clear();
            } else {
                current += c;
            }
        }

        if (!current.empty()) {
            result.push_back(current);
        }

        return result;
    };

    device.clearPartitions();
    Partition::resetIdGenerator();
    Region::resetIdGenerator();

    for (const std::string& line: lines) {
        if (FOEDAG::StringUtils::startsWith(line, "set_region") || FOEDAG::StringUtils::startsWith(line, "set_partition")) {
            std::vector<std::string> cmdTokens = FOEDAG::StringUtils::tokenize(line, " ");
            // extract partition name [optionally]
            std::string partitionName = "";
            if (cmdTokens.size() == 4) {
                partitionName = cmdTokens[3];
            }

            PartitionPtr partition = std::make_shared<Partition>(partitionName);
            device.addPartition(partition);

            if (cmdTokens.size() >= 3) {
                // extract elements
                std::vector<std::string> pathesDirtyTokens = FOEDAG::StringUtils::tokenize(cmdTokens[1], ",");
                HierarhyElementsPtr elements = std::make_shared<HierarhyElements>();
                for (std::string path: pathesDirtyTokens) {
                    if (FOEDAG::StringUtils::endsWith(path, ".*")) {
                        FOEDAG::StringUtils::removeSuffix(path, ".*");
                        elements->insert(HierarhyElement{path, false});
                    } else {
                        elements->insert(HierarhyElement{path, true});
                    }
                }
                partition->setElements(elements);

                // extract regions
                std::vector<std::string> regionsStr = splitRegionsIgnoringParens(cmdTokens[2], ',');
                for (const std::string& regionStr: regionsStr) {
                    std::vector<std::string> regionTokens = FOEDAG::StringUtils::tokenize(regionStr, ":");
                    if (regionTokens.size() == 1) {
                        // special case
                        std::optional<TileDescriptor> bottomLeftGridCoordTileDescriptorOpt = extractGridCoord(regionTokens[0]);
                        if (bottomLeftGridCoordTileDescriptorOpt) {
                            Tile::Index bottomLeftTileIndex = bottomLeftGridCoordTileDescriptorOpt.value().index;
                            Tile::Index topRightTileIndex = bottomLeftTileIndex;
                            if (!device.restoreRegion(partition, bottomLeftTileIndex, topRightTileIndex)) {
                                qCritical() << "syntax error for regions" << QString::fromStdString(regionStr) << "coudn't extract start and end points";
                            }
                        }
                    } else if (regionTokens.size() == 2) {
                        std::optional<TileDescriptor> bottomLeftGridCoordTileDescriptorOpt = extractGridCoord(regionTokens[0]);
                        std::optional<TileDescriptor> topRightGridCoordTileDescriptorOpt = extractGridCoord(regionTokens[1]);

                        if (bottomLeftGridCoordTileDescriptorOpt && topRightGridCoordTileDescriptorOpt) {
                            Tile::Index bottomLeftTileIndex = bottomLeftGridCoordTileDescriptorOpt.value().index;
                            Tile::Index topRightTileIndex = topRightGridCoordTileDescriptorOpt.value().index;
                            if (!device.restoreRegion(partition, bottomLeftTileIndex, topRightTileIndex)) {
                                qCritical() << "syntax error for regions" << QString::fromStdString(regionStr) << "coudn't extract start and end points";
                            }
                        } else {
                            qCritical() << "syntax error for regions" << QString::fromStdString(regionStr) << "coudn't extract bottomLeft or topRight indexes";
                        }
                    } else {
                        qCritical() << "syntax error for regions" << QString::fromStdString(regionStr);
                    }
                }
            }
        }
    }
}

std::vector<std::string> QdcSerializer::readLines(const std::filesystem::path& filePath) const
{
    std::string content = FOEDAG::FileUtils::GetFileContent(filePath);
    FOEDAG::StringUtils::replaceAllInPlace(content, "  ", " ");
    FOEDAG::StringUtils::replaceAllInPlace(content, lineDelimiter(), "");
    FOEDAG::StringUtils::replaceAllInPlace(content, "\n\n", "\n");
    return FOEDAG::StringUtils::tokenize(content, "\n");
}

std::optional<TileDescriptor> QdcSerializer::extractGridCoord(const std::string& data)
{
    static auto extractGridCoord = [](const std::string& idxStr, Tile::Type type)->std::optional<TileDescriptor> {
        std::vector<std::string> tokens = FOEDAG::StringUtils::tokenize(idxStr, ",");
        if (tokens.size() == 2) {
            int col = std::stoi(tokens[0]);
            int row = std::stoi(tokens[1]);
            return TileDescriptor{Tile::Index(col, row), type};
        }
        return std::nullopt;
    };

    if (FOEDAG::StringUtils::startsWith(data, "clb")) {
        std::string idxStr = FOEDAG::StringUtils::extractWildcardSegment(data, "clb(*)");
        return extractGridCoord(idxStr, Tile::Type::Clb);
    } else if (FOEDAG::StringUtils::startsWith(data, "bram")) {
        std::string idxStr = FOEDAG::StringUtils::extractWildcardSegment(data, "bram(*)");
        return extractGridCoord(idxStr, Tile::Type::Bram);
    } else if (FOEDAG::StringUtils::startsWith(data, "dsp")) {
        std::string idxStr = FOEDAG::StringUtils::extractWildcardSegment(data, "dsp(*)");
        return extractGridCoord(idxStr, Tile::Type::Dsp);
    }
    return std::nullopt;
};

} // namespace fp
