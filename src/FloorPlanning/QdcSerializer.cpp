#include "QdcSerializer.h"
#include "DeviceGrid.h"
#include "HierarhyElement.h"
#include "Utils/FileUtils.h"
#include "Utils/StringUtils.h"

#include <QDebug>

#include <stdexcept>
#include <utility>
#include <vector>

namespace fp {

bool QdcSerializer::save(const DeviceGrid& device, const std::filesystem::path& overrideFilePath)
{
    const std::filesystem::path filePath = overrideFilePath.empty() ? m_path : overrideFilePath;
    std::string content = serialize(device);
    if (!m_reservedContent.empty()) {
        content = m_reservedContent + content;
    }
    const bool ok = FOEDAG::FileUtils::WriteToFile(filePath, content);

    // [aurora2#1725 stage P1] Emit the machine-readable twin alongside the .qdc, so the two
    // can never drift: it is regenerated from the same in-memory model on every save and is
    // never hand-edited. The .qdc stays the user-facing source of truth and the only artifact
    // persisted between sessions; floorplan.json is derived (A.P1).
    if (ok) {
        std::filesystem::path jsonPath = filePath;
        jsonPath.replace_filename("floorplan.json");
        FOEDAG::FileUtils::WriteToFile(jsonPath, serializeFloorplanJson(device, filePath));
    }

    return ok;
}

namespace {

// Minimal JSON string escaping. RTL instance paths are HDL identifiers plus dots, but
// partition names come from the user and can contain anything.
std::string jsonEscape(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:   out += c;      break;
        }
    }
    return out;
}

}  // namespace

std::string QdcSerializer::serializeFloorplanJson(const DeviceGrid& device,
                                                  const std::filesystem::path& qdcPath) const
{
    std::string out = "{\n";
    out += "  \"generated_from\": \"" + jsonEscape(qdcPath.filename().string()) + "@"
         + jsonEscape(FOEDAG::FileUtils::calcFileContentHash(qdcPath)) + "\",\n";
    out += "  \"partitions\": [\n";

    bool firstPartition = true;
    for (const auto& [id, partition]: device.partitions()) {
        if (partition->elements().empty() || partition->regions().empty()) {
            continue;
        }

        if (!firstPartition) {
            out += ",\n";
        }
        firstPartition = false;

        // Instances are recorded as RTL paths only -- never element.vprNames. Expansion to
        // netlist names happens at emission time (REQ-003), and the resolved names are a
        // different namespace from what VPR actually places anyway (A.P3).
        std::string instances;
        bool firstElement = true;
        for (const HierarhyElement& element: partition->elements()) {
            if (!firstElement) {
                instances += ", ";
            }
            firstElement = false;
            instances += "\"" + jsonEscape(element.path) + "\"";
        }

        // Regions are emitted as integer coordinates so no consumer has to re-parse the
        // clb(x,y):clb(x,y) grammar. A partition may hold several; VPR treats them as a union.
        std::string regions;
        bool firstRegion = true;
        for (const auto& [regionId, region]: partition->regions()) {
            const Tile::Index& bottomLeft = region->bottomLeftTileIndex();
            const Tile::Index& topRight = region->topRightTileIndex();
            if (!firstRegion) {
                regions += ",\n";
            }
            firstRegion = false;
            regions += "        { \"x_low\": " + std::to_string(bottomLeft.col)
                     + ", \"y_low\": " + std::to_string(bottomLeft.row)
                     + ", \"x_high\": " + std::to_string(topRight.col)
                     + ", \"y_high\": " + std::to_string(topRight.row) + " }";
        }

        out += "    {\n";
        out += "      \"name\": \"" + jsonEscape(partition->name()) + "\",\n";
        out += "      \"instances\": [" + instances + "],\n";
        out += "      \"regions\": [\n" + regions + "\n      ]\n";
        out += "    }";
    }

    out += "\n  ]\n}\n";
    return out;
}

std::string QdcSerializer::serialize(const DeviceGrid& device)
{
    std::string content;
    for (const auto& [id, partition]: device.partitions()) {
        if (partition->elements().empty()) {
            continue;
        }

        // [aurora2#1725 stage P1] RTL names only -- never element.vprNames.
        //
        // Writing resolved VPR atom names here is what produced the defect this feature
        // exists to fix: a .qdc holding 39 enumerated atom names for an instance that has
        // 523 in the netlist, silently under-constraining it by 13x and going stale on every
        // resynthesis. An enumerated list cannot be complete, and the names are in a
        // different namespace from the atoms VPR actually places (A.P3).
        //
        // The whole-instance form (path + ".*") is expanded at emission time instead, which
        // is REQ-003: expansion happens when the constraints XML is generated, never at .qdc
        // write time and never in the UI.
        std::string elementsStr = "";
        int elementsCounter = 0;
        for (const HierarhyElement& element: partition->elements()) {
            if (element.isLeaf) {
                elementsStr += element.path;
            } else {
                elementsStr += element.path + ".*";
            }
            elementsCounter++;
            if (elementsCounter < partition->elements().size()) {
                elementsStr += ",";
                elementsStr += lfCommandDelimiter();
            }
        }

        // collect regions
        std::string regionsStr;
        int regionCounter = 0;
        for (const auto& [id, region]: partition->regions()) {
            const TilePtr& blTile = device.tile(region->bottomLeftTileIndex());
            const TilePtr& trTile = device.tile(region->topRightTileIndex());

            const bool isBottomLeftTileFullyIncluded = region->contains(blTile->index());
            const bool isTopRightTileFullyIncluded = region->contains(trTile->index());

            std::string bottomLeftStr;
            if (isBottomLeftTileFullyIncluded) {
                bottomLeftStr = blTile->name().toStdString();
            } else {
                bottomLeftStr = device.buildTileSymbolicName(Tile::Type::Clb, region->bottomLeftTileIndex());
            }

            std::string topRightStr;
            if (isTopRightTileFullyIncluded) {
                topRightStr = trTile->name().toStdString();
            } else {
                topRightStr = device.buildTileSymbolicName(Tile::Type::Clb, region->topRightTileIndex());
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
                regionsStr += lfCommandDelimiter();
            }
        }

        // aggregate line
        std::string line = "set_region ";
        line += lfCommandDelimiter();

        line += elementsStr;

        line += " ";
        line += lfCommandDelimiter();

        line += regionsStr;

        line += " ";
        line += lfCommandDelimiter();

        line += partition->name();

        line += "\n\n";

        // add line to context
        content += line;
    }

    return content;
}

void QdcSerializer::load(DeviceGrid& device, const std::filesystem::path& overrideFilePath)
{
    const std::filesystem::path filePath = overrideFilePath.empty() ? m_path : overrideFilePath;
    std::vector<std::string> lines = readCommands(filePath);
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

    m_reservedContent.clear();
    device.clearPartitions();
    Partition::resetIdGenerator();
    Region::resetIdGenerator();

    for (const std::string& line: lines) {
        if (FOEDAG::StringUtils::startsWith(line, "set_region")) {
            std::vector<std::string> cmdTokens = FOEDAG::StringUtils::tokenize(line, " ");
            // extract partition name [optionally]
            std::string partitionName = "";
            if (cmdTokens.size() == 4) {
                partitionName = cmdTokens[3];
            }

            // [aurora2#1725] Parse every region BEFORE anything reaches the device.
            //
            // The partition used to be added first and each region restored as it parsed, so
            // one bad coordinate left a partition holding elements and no region -- and
            // serialize() skips only partitions with no ELEMENTS, so the next save rewrote it
            // without the region and the user's placement constraint was gone for good. A
            // typo in a hand-edited .qdc silently destroyed the constraint it was in.
            //
            // A line we cannot fully parse is kept in m_reservedContent instead, which save()
            // writes back, so the file survives the round trip untouched and the user can fix
            // the typo by hand. The partition is not created: a half-loaded one is exactly
            // what caused the loss.
            std::vector<std::pair<Tile::Index, Tile::Index>> parsedRegions;
            bool regionsOk = true;

            if (cmdTokens.size() >= 3) {
                std::vector<std::string> regionsStr = splitRegionsIgnoringParens(cmdTokens[2], ',');
                for (const std::string& regionStr: regionsStr) {
                    std::vector<std::string> regionTokens = FOEDAG::StringUtils::tokenize(regionStr, ":");
                    std::optional<TileDescriptor> bottomLeftOpt;
                    std::optional<TileDescriptor> topRightOpt;

                    if (regionTokens.size() == 1) {
                        // special case: a single tile is its own bottom-left and top-right
                        bottomLeftOpt = extractGridCoord(regionTokens[0]);
                        topRightOpt = bottomLeftOpt;
                        if (!bottomLeftOpt) {
                            qCritical() << "syntax error for regions" << QString::fromStdString(regionStr) << "coudn't extract start and end points";
                        }
                    } else if (regionTokens.size() == 2) {
                        bottomLeftOpt = extractGridCoord(regionTokens[0]);
                        topRightOpt = extractGridCoord(regionTokens[1]);
                        if (!bottomLeftOpt || !topRightOpt) {
                            qCritical() << "syntax error for regions" << QString::fromStdString(regionStr) << "coudn't extract bottomLeft or topRight indexes";
                        }
                    } else {
                        qCritical() << "syntax error for regions" << QString::fromStdString(regionStr);
                    }

                    if (!bottomLeftOpt || !topRightOpt) {
                        regionsOk = false;
                        break;
                    }
                    parsedRegions.emplace_back(device.toBottomLeftGridIndex(bottomLeftOpt.value()),
                                               device.toTopRightGridIndex(topRightOpt.value()));
                }
            } else {
                regionsOk = false;
            }

            if (!regionsOk) {
                qCritical() << "keeping the unparsable line as-is so saving cannot discard it:"
                            << QString::fromStdString(line);
                m_reservedContent += line + "\n";
                continue;
            }

            PartitionPtr partition = std::make_shared<Partition>(partitionName);
            device.addPartition(partition);

            // extract elements
            std::vector<std::string> pathesDirtyTokens = FOEDAG::StringUtils::tokenize(cmdTokens[1], ",");
            for (std::string path: pathesDirtyTokens) {
                if (FOEDAG::StringUtils::endsWith(path, ".*")) {
                    FOEDAG::StringUtils::removeSuffix(path, ".*");
                    partition->addElement(HierarhyElement{path, false});
                } else {
                    partition->addElement(HierarhyElement{path, true});
                }
            }

            for (const auto& [bottomLeftIndex, topRightIndex]: parsedRegions) {
                device.restoreRegion(partition, bottomLeftIndex, topRightIndex);
            }
        } else {
            m_reservedContent += line + "\n";
        }
    }
}

std::vector<std::string> QdcSerializer::readCommands(const std::filesystem::path& filePath)
{
    std::vector<std::string> commands;
    std::string content = FOEDAG::FileUtils::GetFileContent(filePath);
    FOEDAG::StringUtils::replaceAllInPlace(content, "  ", " ");

    // Fixes CRLF -> LF, valid case for WIN32 platform
    FOEDAG::StringUtils::replaceAllInPlace(content, "\r", "");

    // remove all command delimiters, 1 line = 1 whole command
    FOEDAG::StringUtils::replaceAllInPlace(content, lfCommandDelimiter(), "");

    FOEDAG::StringUtils::replaceAllInPlace(content, "\n\n", "\n");

    std::vector<std::string> lines = FOEDAG::StringUtils::tokenize(content, "\n");
    for (std::string line: lines) {
      line = FOEDAG::StringUtils::trim(line);

      if (line.empty()){
        continue; // Skip empty line
      }

      // [aurora2#1725] A whole-line comment is user content, not a command, and it is
      // returned verbatim so load() can put it in m_reservedContent and save() can write it
      // back. It used to be stripped to an empty string and skipped here, so it never
      // reached load() at all -- and since m_reservedContent is filled only from the lines
      // load() receives, every comment in the .qdc was destroyed by the next save. The
      // testcase .qdc files carry their entire rationale in such headers.
      if (line.front() == '#') {
        commands.push_back(line);
        continue;
      }

      // drop comment part
      if (auto pos = line.find("#"); pos != std::string::npos) {
        line = line.substr(0, pos); // drop commented part of line
        line = FOEDAG::StringUtils::trim(line);
      }

      if (line.empty()){
        continue; // Skip empty line
      }

      commands.push_back(line);
    }

    return commands;
}

std::optional<TileDescriptor> QdcSerializer::extractGridCoord(const std::string& data)
{
    static auto extractGridCoord = [](const std::string& idxStr, Tile::Type type)->std::optional<TileDescriptor> {
        std::vector<std::string> tokens = FOEDAG::StringUtils::tokenize(idxStr, ",");
        if (tokens.size() == 2) {
            // [aurora2#1725] The .qdc is user-editable, so these tokens are untrusted:
            // "clb(abc,2)" makes std::stoi throw invalid_argument and a large value makes it
            // throw out_of_range. Nothing anywhere in the load path catches -- not
            // QdcSerializer, not DeviceGrid, not FloorPlanningWidget -- and load() runs from
            // a Qt slot on panel open and on the Load button, so the throw took the IDE down
            // over a typo. Reported as a syntax error instead, which is what every other
            // malformed-region path here already does.
            try {
                int col = std::stoi(tokens[0]);
                int row = std::stoi(tokens[1]);
                return TileDescriptor{Tile::Index(col, row), type};
            } catch (const std::exception&) {
                return std::nullopt;
            }
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
