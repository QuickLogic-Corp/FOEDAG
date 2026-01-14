#include "QdcSerializer.h"
#include "Device.h"
#include "HierarhyElement.h"
#include "Utils/FileUtils.h"
#include "Utils/StringUtils.h"

#include <QDebug>

namespace fp {

void QdcSerializer::save(const Device& device, const std::filesystem::path& filePath)
{
    FOEDAG::FileUtils::WriteToFile(filePath, serialize(device));
}

std::string QdcSerializer::serialize(const Device& device)
{
    // preprocess to unite regions by same elements list
    std::map<std::string, std::string> data;
    for (const auto& [id, region]: device.regions()) {
        if (!region->elements()) {
            continue;
        } if(region->elements()->empty()) {
            continue;
        }

        std::string elementsStr = "";
        int counter = 0;
        for (const HierarhyElement& element: *region->elements()) {
            if (element.isLeaf) {
                elementsStr += element.path;
            } else {
                elementsStr += element.path + ".*";
            }
            counter++;
            if (counter < region->elements()->size()) {
                elementsStr += ",";
                elementsStr += lineDelimiter();
            }
        }

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
        if (data.find(elementsStr) != data.end()) {
            data[elementsStr] += "," + regionStr;
        } else {
            data[elementsStr] = regionStr;
        }
    }
    // end preprocess

    std::string content;
    for (const auto& [elementsStr, regionsStr]: data) {
        std::string line = "set_region ";
        line += lineDelimiter();

        line += elementsStr;

        line += " ";
        line += lineDelimiter();

        line += regionsStr;

        line += "\n\n";

        content += line;
    }

    return content;
}

void QdcSerializer::load(Device& device, const std::filesystem::path& filePath)
{
    std::vector<std::string> lines = readLines(filePath);
    load(device, lines);
}

void QdcSerializer::load(Device& device, const std::vector<std::string>& lines)
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

    device.clearRegions();
    Region::resetIdGenerator();

    for (const std::string& line: lines) {
        if (FOEDAG::StringUtils::startsWith(line, "set_region")) {
            std::vector<std::string> tokens = FOEDAG::StringUtils::tokenize(line, " ");
            if (tokens.size() == 3) {
                // extract elements
                std::vector<std::string> pathesDirty = FOEDAG::StringUtils::tokenize(tokens[1], ",");
                HierarhyElementsPtr elements = std::make_shared<HierarhyElements>();
                for (std::string path: pathesDirty) {
                    if (FOEDAG::StringUtils::endsWith(path, ".*")) {
                        FOEDAG::StringUtils::removeSuffix(path, ".*");
                        elements->insert(HierarhyElement{path, false});
                    } else {
                        elements->insert(HierarhyElement{path, true});
                    }
                }
                // extract regions
                std::vector<std::string> regions = splitRegionsIgnoringParens(tokens[2], ',');
                for (const std::string& region: regions) {
                    std::vector<std::string> tokens = FOEDAG::StringUtils::tokenize(region, ":");
                    if (tokens.size() == 1) {
                        // special case
                        std::optional<TileDescriptor> bottomLeftGridCoordTileDescriptorOpt = extractGridCoord(tokens[0]);
                        if (bottomLeftGridCoordTileDescriptorOpt) {
                            Tile::Index bottomLeftTileIndex = bottomLeftGridCoordTileDescriptorOpt.value().index;
                            std::optional<QPointF> bottomLeftPointOpt = device.findBottomLeftPoint(bottomLeftTileIndex);
                            std::optional<QPointF> topRightPointOpt = device.findTopRightPoint(bottomLeftTileIndex);
                            if (bottomLeftPointOpt && topRightPointOpt) {
                                QPointF bottomLeftPoint = bottomLeftPointOpt.value() + 0.5*QPointF(-Tile::borderPx(), Tile::borderPx());
                                QPointF topRightPoint = topRightPointOpt.value()     + 0.5*QPointF(Tile::borderPx(), -Tile::borderPx());

                                RegionPtr region = std::make_shared<Region>(bottomLeftPoint);
                                std::unordered_set<Tile::Index> tiles = device.findTiles(QRectF(bottomLeftPoint, topRightPoint));
                                region->accept(topRightPoint, tiles, elements);
                                device.addRegion(region);
                            }
                        }
                    } else if (tokens.size() == 2) {
                        std::optional<TileDescriptor> bottomLeftGridCoordTileDescriptorOpt = extractGridCoord(tokens[0]);
                        std::optional<TileDescriptor> topRightGridCoordTileDescriptorOpt = extractGridCoord(tokens[1]);
                        if (bottomLeftGridCoordTileDescriptorOpt && topRightGridCoordTileDescriptorOpt) {
                            Tile::Index bottomLeftTileIndex = bottomLeftGridCoordTileDescriptorOpt.value().index;
                            Tile::Index topRightTileIndex = topRightGridCoordTileDescriptorOpt.value().index;

                            std::optional<QPointF> bottomLeftPointOpt = device.findBottomLeftPoint(bottomLeftTileIndex);
                            std::optional<QPointF> topRightPointOpt = device.findTopRightPoint(topRightTileIndex);
                            if (bottomLeftPointOpt && topRightPointOpt) {
                                QPointF bottomLeftPoint = bottomLeftPointOpt.value() + 0.5*QPointF(-Tile::borderPx(), Tile::borderPx());
                                QPointF topRightPoint = topRightPointOpt.value()     + 0.5*QPointF(Tile::borderPx(), -Tile::borderPx());

                                RegionPtr region = std::make_shared<Region>(bottomLeftPoint);
                                std::unordered_set<Tile::Index> tiles = device.findTiles(QRectF(bottomLeftPoint, topRightPoint));
                                region->accept(topRightPoint, tiles, elements);
                                device.addRegion(region);
                            } else {
                                qCritical() << "syntax error for regions" << QString::fromStdString(region) << "coudn't extract start and end points";
                            }
                        } else {
                            qCritical() << "syntax error for regions" << QString::fromStdString(region) << "coudn't extract bottomLeft or topRight indexes";
                        }
                    } else {
                        qCritical() << "syntax error for regions" << QString::fromStdString(region);
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
