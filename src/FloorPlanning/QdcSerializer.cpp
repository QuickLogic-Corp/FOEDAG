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
    return FOEDAG::FileUtils::WriteToFile(filePath, content);
}

std::string QdcSerializer::serialize(const DeviceGrid& device)
{
    std::string content;
    for (const auto& [id, partition]: device.partitions()) {
        if (partition->elements().empty()) {
            continue;
        }

        // [aurora2#1725 stage P1] RTL names only -- never element.vprNames, and never a
        // pattern derived from the tree's shape.
        //
        // Writing resolved VPR atom names here is what produced the defect this feature
        // exists to fix: a .qdc holding 39 enumerated atom names for an instance that has
        // 523 in the netlist, silently under-constraining it by 13x and going stale on every
        // resynthesis. An enumerated list cannot be complete, and the names are in a
        // different namespace from the atoms VPR actually places (A.P3).
        //
        // A whole-instance selection is written as the plain RTL path the tree shows --
        // "dut.instPerm20009", not "dut.instPerm20009.*". The trailing ".*" this used to
        // append to every non-leaf element was a post-synthesis-facing pattern manufactured
        // at .qdc write time, which is exactly what REQ-003 reserves for _constraints.xml
        // generation. It also misdescribed the user's own selection: the suffix appeared on
        // any row that had children, so a .qdc read back as a glob nobody had typed.
        //
        // Dropping it loses nothing, because the suffix was never what gave a token its
        // meaning. A leaf instance ("dut.tri.el0.sub2") and a literal atom name ("out[0]",
        // "$false") are both written bare and always were, so instances.json has always been
        // what tells a whole instance from an exact atom name -- and
        // GenerateIOFloorPlanConstraints() asks it about every token either way, appending
        // ".*" itself for the ones it recognises as instances.
        std::string elementsStr = "";
        int elementsCounter = 0;
        for (const HierarhyElement& element: partition->elements()) {
            elementsStr += element.path;
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

        if (!partition->comment().empty()) {
            line += "  ";
            line += partition->comment();
        }

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
            // Split any trailing comment off BEFORE tokenizing: its words would otherwise
            // count as command tokens, and the partition name -- taken only when there are
            // exactly four -- would be silently lost.
            std::string commandPart = line;
            std::string trailingComment;
            if (auto hash = line.find('#'); hash != std::string::npos) {
                trailingComment = line.substr(hash);
                trailingComment = FOEDAG::StringUtils::trim(trailingComment);
                commandPart = line.substr(0, hash);
                commandPart = FOEDAG::StringUtils::trim(commandPart);
            }

            std::vector<std::string> cmdTokens = FOEDAG::StringUtils::tokenize(commandPart, " ");
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
            partition->setComment(trailingComment);
            device.addPartition(partition);

            // extract elements
            std::vector<std::string> pathesDirtyTokens = FOEDAG::StringUtils::tokenize(cmdTokens[1], ",");
            // A ".*" suffix is still accepted: serialize() wrote one for every non-leaf
            // element until stage P1, and the testcase .qdc files are hand-written in that
            // form. It is stripped rather than kept, so the path stored here is the RTL name
            // in both cases and a load/save round trip normalises the older form to it.
            //
            // isLeaf is a note about the tree, not about the file, and a bare path cannot say
            // which it was -- so it never decides anything outside the panel, which recomputes
            // it from the rows it actually has (see fillPartitionWithSelectedElements()).
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

      // [aurora2#1725] Comments are user content and are returned intact -- whole-line ones
      // for load() to keep in m_reservedContent, trailing ones for it to carry on the
      // partition they annotate. They used to be stripped here and dropped, and since
      // m_reservedContent is filled only from the lines load() receives, every comment in
      // the .qdc was destroyed by the next save. The testcase .qdc files carry their entire
      // rationale in such headers.
      //
      // Consumers therefore have to expect them: see GenerateIOFloorPlanConstraints(),
      // which would otherwise read a comment's first word as a command name.
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
