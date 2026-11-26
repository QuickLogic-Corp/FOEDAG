#include "DeviceGrid.h"
#include "TileDescriptor.h"

#include <QDebug>

#include <algorithm>
#include <limits>
#include <regex>

namespace fp {

namespace {

// [aurora2#1725] How much room above the estimated clb requirement counts as comfortable.
// The clb estimate runs optimistic, so a region that only just meets it can still fail to
// pack; 25% keeps that case flagged while not nagging about regions with real room. A
// heuristic on top of a heuristic -- the packer remains the only authority.
constexpr int kClbHeadroomPercent = 25;

}  // namespace

DeviceGrid::DeviceGrid()
: m_issues(std::make_shared<DeviceGrid::Issues>())
{

}

DeviceGrid::DeviceGrid(const DeviceGridDescriptorPtr& device)
    : m_issues(std::make_shared<DeviceGrid::Issues>())
{
    constructTiles(device);
}

void DeviceGrid::constructTile(Tile::Type type, int col, int row)
{
    Tile::Index index{col, row};
    QSize size = m_descriptor->elementSize(type);
    std::string label = buildTileSymbolicName(type, index);
    m_tiles[index] = std::make_shared<Tile>(type, col, row, size.width(), size.height(), QString::fromStdString(label));
}

void DeviceGrid::constructTileFragment(int col, int row, const Tile::Index& bottomLeftTileIndex)
{
    if ((col != bottomLeftTileIndex.col) || (row != bottomLeftTileIndex.row)) {
        m_tileFragments[Tile::Index{col, row}] = bottomLeftTileIndex;
        //qDebug() << "~~~ tile fragment=" << col << row <<  "point to" << bottomLeftTileIndex.col << bottomLeftTileIndex.row;
    }
}

void DeviceGrid::constructTiles(const DeviceGridDescriptorPtr& device) {
    m_descriptor = device;
    m_tiles.clear();

    bool step_on_bram = false;
    bool step_on_dsp = false;
    int stepCounter = 0;
    m_tiles.reserve(device->columns() * device->rows());
    Tile::Index bottomLeftIndex;
    for (int col = 1; col <= device->columns(); ++col) {
        for (int row = 1; row <= device->rows(); ++row) {
            if (row == 1 || (row == device->rows()) || col == 1 || (col == device->columns())) {
                constructTile(Tile::Type::Io, col, row);
            } else if (device->isBramSupported() && device->isBramColumn(col)) {
                if (!step_on_bram) {
                    constructTile(Tile::Type::Bram, col, row);
                    bottomLeftIndex = Tile::Index(col, row);
                    step_on_bram = true;
                }
                if (step_on_bram) {
                    constructTileFragment(col, row, bottomLeftIndex);
                    stepCounter++;
                    if (stepCounter == device->bramSize()->height()) {
                        bottomLeftIndex.reset();
                        step_on_bram = false;
                        stepCounter = 0;
                    }
                }

            } else if (device->isDspSupported() && device->isDspColumn(col)) {
                if (!step_on_dsp) {
                    bottomLeftIndex = Tile::Index(col, row);
                    constructTile(Tile::Type::Dsp, col, row);
                    step_on_dsp = true;
                }
                if (step_on_dsp) {
                    constructTileFragment(col, row, bottomLeftIndex);
                    stepCounter++;
                    if (stepCounter == device->dspSize()->height()) {
                        bottomLeftIndex.reset();
                        step_on_dsp = false;
                        stepCounter = 0;
                    }
                }
            } else {
                constructTile(Tile::Type::Clb, col, row);
            }
        }
    }
}

void DeviceGrid::markVisibleTiles(const QRectF& visibleArea)
{
    for (auto& [idx, tile]: m_tiles) {
        tile->setVisible(tile->rect().intersects(visibleArea));
    }
}

const PartitionPtr& DeviceGrid::partition(int partitionId) const
{
    auto it = m_partitions.find(partitionId);
    if (it != m_partitions.end()) {
        return it->second;
    }
    return m_nullPtrPartition;
}

void DeviceGrid::removePartition(const PartitionPtr& partition)
{
    auto it = m_partitions.find(partition->id());
    if (it != m_partitions.end()) {
        m_partitions.erase(it);
    } else {
        // normally shouldn't go here
        qCritical() << "unable to find partition id for partition name" << QString::fromStdString(partition->name());
    }
}

void DeviceGrid::addPartition(const PartitionPtr& partition)
{
    m_partitions[partition->id()] = partition;
}

void DeviceGrid::refreshPartition(const PartitionPtr& partition)
{
    for (const auto& [id, region]: partition->regions()) {
        region->rebuildHandles();
        region->setTiles(findTiles(region->rect()));
    }
}

void DeviceGrid::refreshPartitions()
{
    for (const auto& [id, partition]: m_partitions) {
        refreshPartition(partition);
    }
}

bool DeviceGrid::removeRegion(const RegionPtr& region)
{
  const PartitionPtr& p = partition(region->partitionId());
  if (p) {
    p->removeRegion(region);
    return true;
  }
  return false;
}

void DeviceGrid::alignRegions()
{
    for (const auto& [partitionId, partition]: m_partitions) {
        for (const auto& [regionId, region]: partition->regions()) {
            alignRegion(region);
        }
    }
}

QRectF DeviceGrid::getAlignedRect(const Tile::Index& bottomLeftIndex, const Tile::Index& topRightIndex) const
{
    //qDebug() << "getAlignedRect" << bottomLeftIndex.col << bottomLeftIndex.row << topRightIndex.col << topRightIndex.row;
    QPointF bottomLeft = bottomLeftPoint(bottomLeftIndex);
    QPointF topRight = topRightPoint(topRightIndex);
    bottomLeft += 0.5*QPointF(-Tile::borderPx(), Tile::borderPx());
    topRight   += 0.5*QPointF(Tile::borderPx(), -Tile::borderPx());
    return QRectF(bottomLeft, topRight);
}

void DeviceGrid::alignRegion(const RegionPtr& region)
{
    // QRectF rect = getAlignedRect(region->bottomLeftTileIndex(), region->topRightTileIndex());
    QRectF rect = getAlignedRect(region->bottomLeftGridIndex(), region->topRightGridIndex());
    region->setRect(rect);
}

void DeviceGrid::restoreRegion(const PartitionPtr& partition, const Tile::Index& bottomLeftIndex, const Tile::Index& topRightIndex, bool excludeIoTiles)
{
    QRectF rect = getAlignedRect(bottomLeftIndex, topRightIndex);
    RegionPtr region = std::make_shared<Region>(m_context->nextRegionId++, rect);
    const auto tiles = findTiles(rect, excludeIoTiles);
    region->setTiles(tiles);
    partition->addRegion(region);
}

std::unordered_map<Tile::Index, TilePtr> DeviceGrid::findTiles(const QRectF& rect, bool excludeIoTiles)
{
    std::unordered_map<Tile::Index, TilePtr> tiles;
    for (const auto& [index, tile]: m_tiles) {
        if (excludeIoTiles && (tile->type() == Tile::Type::Io)) {
            continue;
        }
        if (tile->isInArea(rect)) {
            tiles[index] = tile;
        }
    }
    return tiles;
}

QPointF DeviceGrid::bottomLeftPoint(const Tile::Index& idx) const
{
    return Tile::buildRect(idx, 1, 1).bottomLeft();
}

QPointF DeviceGrid::topRightPoint(const Tile::Index& idx) const
{
    return Tile::buildRect(idx, 1, 1).topRight();
}

Tile::Index DeviceGrid::toBottomLeftGridIndex(const TileDescriptor& tileDescriptor) const
{
    return tileDescriptor.index;
}

Tile::Index DeviceGrid::toTopRightGridIndex(const TileDescriptor& tileDescriptor) const
{
    Tile::Index result = tileDescriptor.index;
    switch(tileDescriptor.type) {
    case Tile::Type::Dsp:
        if (m_descriptor->isDspSupported()) result.row += m_descriptor->dspSize()->height() - 1;
        break;
    case Tile::Type::Bram:
        if (m_descriptor->isBramSupported()) result.row += m_descriptor->bramSize()->height() - 1;
        break;
    default: break;
    }

    return result;
}

std::string DeviceGrid::buildTileSymbolicName(Tile::Type type, const Tile::Index& index, bool isTileFullyIncluded) const
{
    int row = index.row;
    if (!isTileFullyIncluded) {
        // if tile is invisible we forcelly use clb coordinate since it has minimal height
        type = Tile::Type::Clb;
    }
    switch(type) {
    case Tile::Type::Clb:  return QString("clb(%1,%2)").arg(index.col).arg(row).toStdString(); break;
    case Tile::Type::Io:   return QString("io(%1,%2)").arg(index.col).arg(row).toStdString(); break;
    case Tile::Type::Bram: return QString("bram(%1,%2)").arg(index.col).arg(row).toStdString(); break;
    case Tile::Type::Dsp:  return QString("dsp(%1,%2)").arg(index.col).arg(row).toStdString(); break;
    case Tile::Type::Empty: return "";
    }
    return "";
}

const TilePtr& DeviceGrid::tile(const Tile::Index& index) const
{
    Tile::Index bottomLeftTileIndex{index};
    if (auto it = m_tileFragments.find(index); it != m_tileFragments.end()) {
        bottomLeftTileIndex = it->second;
    }

    if (auto it = m_tiles.find(bottomLeftTileIndex); it != m_tiles.end()) {
        return it->second;
    }

    qCritical() << "tile on index" << index.col << index.row << "resolved index=" << bottomLeftTileIndex.col << bottomLeftTileIndex.row << "wasn't found";
    return m_nullPtrTile;
}

namespace {

// A .qdc glob as generate_floorplanning.py reads it: "*" spans dots, "?" is one character,
// "[*]" is any index, and everything else is literal.
std::regex globRegex(const std::string& glob)
{
    std::string re;
    for (std::size_t i = 0; i < glob.size(); ++i) {
        if (glob.compare(i, 3, "[*]") == 0) {
            re += "\\[\\d+\\]";
            i += 2;
        } else if (glob[i] == '*') {
            re += ".*";
        } else if (glob[i] == '?') {
            re += ".";
        } else if (std::string("\\^$.|+()[]{}").find(glob[i]) != std::string::npos) {
            re += std::string("\\") + glob[i];
        } else {
            re += glob[i];
        }
    }
    return std::regex(re);
}

}  // namespace

// [aurora2#1725] A pad sits on an IO tile, and a region holds only clb, bram and dsp tiles,
// so a pad in a partition can never be placed. A bare output name is not a pad: it stands for
// the block driving that net.
bool DeviceGrid::isPad(const std::string& name) const
{
    if (name.compare(0, 4, "out:") == 0 || m_inputPorts.count(name) != 0) {
        return true;
    }
    if (name.find_first_of("*?") == std::string::npos || name == "*") {
        return false;
    }
    // Like the constraint writer, a glob that matches an instance is about instances. The top
    // is not one: it is named outright, as "*" or its own name.
    const std::string scope = (name.size() > 2 && name.compare(name.size() - 2, 2, ".*") == 0)
                                  ? name.substr(0, name.size() - 2)
                                  : name;
    const std::regex re = globRegex(scope);
    for (const std::string& path : m_rtlInstances) {
        if (path == m_context->top) {
            continue;
        }
        for (std::size_t dot = path.find('.'); ; dot = path.find('.', dot + 1)) {
            if (std::regex_match(path.substr(0, dot), re)) {
                return false;
            }
            if (dot == std::string::npos) {
                break;
            }
        }
    }
    const auto matches = [&](const std::set<std::string>& ports) {
        return std::any_of(ports.begin(), ports.end(),
                           [&](const std::string& port) { return std::regex_match(port, re); });
    };
    return matches(m_inputPorts) || matches(m_outputPorts);
}

bool DeviceGrid::isRtlInstanceOrParent(const std::string& path) const
{
    if (m_rtlInstances.count(path) != 0) {
        return true;
    }
    // A generate label ("cluster[0]") is only the parent of instances; they sort right after
    // "<path>." in the set.
    const std::string prefix = path + ".";
    const auto next = m_rtlInstances.lower_bound(prefix);
    return (next != m_rtlInstances.end()) && (next->compare(0, prefix.size(), prefix) == 0);
}

// [aurora2#1725 F5] An element lists every atom of its subtree, so a partition on a parent
// would also count the atoms of a child another partition constrains. Each atom belongs to
// the deepest element that lists it -- a nested path is longer than its ancestor's, and the
// whole design ("*", <top>, <top>.*) is shallower than any instance, though "<top>" is not
// a prefix of the instance paths -- and an atom named outright beats any instance, as
// generate_floorplanning.py assigns it.
std::map<int, std::set<std::string>> DeviceGrid::atomsByDeepestElement() const
{
    const std::string& top = m_context->top;
    std::map<std::string, std::pair<std::size_t, int>> owner;  // atom -> (depth, id)
    for (const auto& [id, partition] : m_partitions) {
        for (const HierarhyElement& element : partition->elements()) {
            const bool namedAtom = !isRtlInstanceOrParent(element.path) &&
                                   m_context->atomTypes.count(element.path) != 0;
            const bool wholeDesign =
                element.path == "*" ||
                (!top.empty() && (element.path == top || element.path == top + ".*"));
            const std::size_t depth = namedAtom     ? std::numeric_limits<std::size_t>::max()
                                      : wholeDesign ? 0
                                                    : element.path.size();
            for (const std::string& atom : element.vprNames) {
                const auto [it, inserted] = owner.emplace(atom, std::make_pair(depth, id));
                if (!inserted && depth > it->second.first) {
                    it->second = {depth, id};
                }
            }
        }
    }
    std::map<int, std::set<std::string>> atoms;
    for (const auto& [atom, by] : owner) {
        atoms[by.second].insert(atom);
    }
    return atoms;
}

const DeviceGrid::IssuesPtr& DeviceGrid::checkIssues()
{
    m_issues->clear();
    m_overlappedConflictingIndexes.clear();
    m_overlappedNonConflictingIndexes.clear();
    std::map<int, std::set<std::string>> ownedAtoms = atomsByDeepestElement();

    // [aurora2#1725] Every path any partition constrains, so the ancestor check below can
    // tell "this instance is assigned somewhere" from "nobody constrains it".
    std::unordered_set<std::string> constrainedPaths;
    for (const auto& [partitionId, partition]: m_partitions) {
        for (const HierarhyElement& element: partition->elements()) {
            constrainedPaths.insert(element.path);
        }
    }

    // Instance whose own logic nothing constrains -> how many instances inside it are
    // constrained, and by which partitions. Filled in the loop below, reported after it.
    struct OwnLogicGap {
        int nestedConstrained = 0;
        std::set<std::string> partitionNames;
    };
    std::map<std::string, OwnLogicGap> unassignedOwnLogic;

    for (const auto& [partitionId, partition]: m_partitions) {
        // element presence
        if (partition->elements().empty()) {
            m_issues->errors.insert({"Partition '" + partition->name() + "' has no elements assigned to it", ""});
        } else if (ownedAtoms[partitionId].empty() &&
                   std::any_of(partition->elements().begin(), partition->elements().end(),
                               [](const HierarhyElement& e) { return !e.vprNames.empty(); })) {
            // A warning, not an error: the constraint writer skips such a partition and the
            // compile goes on (design.md §5).
            m_issues->warnings.insert({"Partition '" + partition->name() +
                                           "' is left with no atom and is skipped",
                                       "Each atom goes to the deepest constrained instance that "
                                       "holds it, and other partitions constrain instances inside "
                                       "this partition's elements that take all their atoms. Drop "
                                       "this partition, or constrain the inner instances here."});
        }
        // region presence
        if (partition->regions().empty()) {
            m_issues->errors.insert({"Partition '" + partition->name() + "' has no any region", ""});
        }
        // tiles presence in region
        for (const auto& [regionId, region]: partition->regions()) {
            if (region->tiles().empty()) {
                m_issues->errors.insert({"Partition '" + partition->name() + "' has region with no any tiles", ""});
            }
        }
        // [aurora2#1725] required vs. available resources. dsp and bram are exact -- one atom
        // occupies one whole tile -- so a shortfall is an error. The clb figure divides the
        // atom count by an atoms-per-tile hint, and real packing density varies with the
        // logic, so by decision D4 it is a warning naming the partition; VPR's packer is the
        // hard check, and its own message names only the region box.
        auto checkResource = [&](const std::string& label, int required, int available) {
            if (required <= available) {
                return;
            }
            const bool estimate = (label == "clb");
            const std::string tip = estimate
                ? "Estimated from this partition's clb atoms at "
                  + std::to_string(m_context->atomsPerTile)
                  + " atoms per tile. Real density varies with the logic, so the shortfall is "
                    "approximate -- but the region likely has to cover more clb tiles, or some "
                    "elements belong in another partition."
                : "One " + label + " atom occupies one " + label + " tile, so the region has to "
                  "cover more of them, or some elements belong in another partition.";
            (estimate ? m_issues->warnings : m_issues->errors)
                .insert({"Partition '" + partition->name() + "' needs " +
                         std::to_string(required) + " " + label + " tiles but only " +
                         std::to_string(available) + " available", tip});
        };
        const ResourceTally need = tallyResources(ownedAtoms[partitionId], m_context->atomsPerTile,
                                                  m_context->atomTypes, m_context->dspTiles);
        checkResource("clb", need.clbTiles, partition->clbAvailableCount());
        checkResource("dsp", need.dsp, partition->dspAvailableCount());
        checkResource("bram", need.bram, partition->bramAvailableCount());

        // [aurora2#1725] Enough clb tiles by the estimate, but only just -- which is where
        // packing breaks, because the estimate can sit below what the packer needs. A warning,
        // like the clb shortfall above (D4). clb only: dsp/bram need no packing slack.
        const int clbRequired = need.clbTiles;
        const int clbAvailable = partition->clbAvailableCount();
        if ((clbRequired > 0) && (clbAvailable >= clbRequired)
            && (clbAvailable < clbRequired + (clbRequired * kClbHeadroomPercent + 99) / 100)) {
            m_issues->warnings.insert({"Partition '" + partition->name() + "' has " +
                                       std::to_string(clbAvailable) +
                                       " clb tiles for an estimated " +
                                       std::to_string(clbRequired) + " -- little packing slack",
                                       "The estimate can be lower than what the packer needs, so "
                                       "a region this tight may fail to pack even though it "
                                       "looks big enough. Allow roughly "
                                       + std::to_string(kClbHeadroomPercent)
                                       + "% more clb tiles than the estimate."});
        }

        // [aurora2#1725 stage P4] Elements the .qdc names that synthesis deleted. Loading such
        // a .qdc is normal -- it may predate the deletion, and whether an instance survives
        // depends on generics and constant folding -- but the constraint matches no atom, and
        // the tree cannot show it as checked because the row is not checkable. So report it:
        // otherwise the entry looks constrained while doing nothing, and disappears without
        // explanation the next time the partition is edited and saved.
        int deletedElements = 0;
        for (const HierarhyElement& element: partition->elements()) {
            if (m_deletedInstances.count(element.path) != 0) {
                ++deletedElements;
            }
        }
        if (deletedElements > 0) {
            m_issues->warnings.insert({"Partition '" + partition->name() + "' names " +
                                       std::to_string(deletedElements) +
                                       " instance(s) that synthesis deleted",
                                       "Those instances have no atoms, so the constraint has no "
                                       "effect and VPR reports them as 'was not found, skipping "
                                       "atom'. They are shown greyed out in the tree and cannot "
                                       "be checked; editing this partition and saving the .qdc "
                                       "drops them."});
        }

        std::string pads;
        for (const HierarhyElement& element: partition->elements()) {
            if (isPad(element.path)) {
                pads += (pads.empty() ? "'" : ", '") + element.path + "'";
            }
        }
        if (!pads.empty()) {
            m_issues->errors.insert({"Partition '" + partition->name() + "' names pads: " + pads,
                                     "A pad sits on an IO tile, and a region holds only clb, "
                                     "bram and dsp tiles, so the pad can never be placed there. "
                                     "Place pads with set_io_side in the .qdc, or pin them in "
                                     "the .pcf."});
        }

        // The constraint writer also takes an atom of the owner table, a net (for the atom
        // driving it) or an input pad, and stops on any other name. Nets and pads are not in
        // the table, so a name outside it is only a warning here.
        if (!m_rtlInstances.empty()) {
            for (const HierarhyElement& element: partition->elements()) {
                const std::string& path = element.path;
                if ((path.find('*') != std::string::npos) || isRtlInstanceOrParent(path) ||
                    isPad(path)) {
                    continue;
                }
                const auto atom = m_context->atomTypes.find(path);
                if (atom == m_context->atomTypes.end()) {
                    m_issues->warnings.insert({"Partition '" + partition->name() + "' names '" +
                                               path + "', which is neither an RTL instance nor "
                                               "an atom",
                                               "Constraint generation reads it as a net, "
                                               "constraining the atom that drives it, or as an "
                                               "input pad, and stops if it is neither. Use the "
                                               "name of the RTL instance that holds this logic, "
                                               "or check the spelling."});
                } else if (atom->second == "$const") {
                    m_issues->warnings.insert({"Partition '" + partition->name() + "' names '" +
                                               path + "', a constant driver, not an RTL instance",
                                               "Synthesis adds this atom to drive a constant "
                                               "value. VPR places it, but it is not in the RTL "
                                               "hierarchy, so the panel shows no atoms for it."});
                }
            }
        }

        // [aurora2#1725] Collect instances whose sub-instances are constrained while they
        // themselves are not -- see the warning emitted after this loop.
        for (const HierarhyElement& element: partition->elements()) {
            std::size_t dot = element.path.rfind('.');
            while (dot != std::string::npos) {
                const std::string ancestor = element.path.substr(0, dot);
                dot = ancestor.rfind('.');
                if (constrainedPaths.count(ancestor) != 0) {
                    continue;   // assigned in its own right, so its logic is accounted for
                }
                const auto own = m_ownAtomCounts.find(ancestor);
                if ((own == m_ownAtomCounts.end()) || (own->second <= 0)) {
                    continue;   // pure hierarchy, no logic of its own to lose
                }
                ++unassignedOwnLogic[ancestor].nestedConstrained;
                unassignedOwnLogic[ancestor].partitionNames.insert(partition->name());
            }
        }
    }

    // [aurora2#1725] Constraining sub-instances of X without constraining X leaves X's OWN
    // logic -- the cells sitting directly in it, not in any sub-instance -- assigned to
    // nothing, free for the packer to place anywhere. Nothing downstream catches it: stage P7
    // grades the instances that were constrained, and an unconstrained one is in no partition
    // to grade.
    //
    // Reported per instance rather than per partition -- with the sub-instances split across
    // p1 and p2 the same gap would otherwise be announced twice -- and only reported: which
    // partition should absorb the instance is the user's call, and guessing would silently
    // redraw their floorplan.
    for (const auto& [instance, gap]: unassignedOwnLogic) {
        std::string partitions;
        for (const std::string& name: gap.partitionNames) {
            partitions += (partitions.empty() ? "'" : ", '") + name + "'";
        }
        m_issues->warnings.insert({"Instance '" + instance + "' is in no partition while " +
                                   std::to_string(gap.nestedConstrained) +
                                   " instance(s) inside it are constrained -- its own " +
                                   std::to_string(m_ownAtomCounts.at(instance)) +
                                   " atoms are unconstrained",
                                   "Those atoms sit directly in '" + instance + "' rather than "
                                   "in one of its sub-instances, so no region holds them and the "
                                   "packer may place them anywhere. The nested instances are "
                                   "constrained by " + partitions + ". Assign the whole instance "
                                   "to one partition, or move its remaining sub-instances into "
                                   "the same one."});
    }

    // tiles overlapping between different partitions
    for (auto pit1 = m_partitions.begin(); pit1 != m_partitions.end(); ++pit1) {
        for (auto pit2 = std::next(pit1); pit2 != m_partitions.end(); ++pit2) {
            const PartitionPtr& p1 = pit1->second;
            const PartitionPtr& p2 = pit2->second;

            // this doesn't work, it could be due to partition rect is not calculated yet
            // if (!p1->rect().intersects(p2->rect())) {
            //     continue;
            // }

            const auto& regs1 = p1->regions();
            const auto& regs2 = p2->regions();

            for (const auto& [r1id, r1]: regs1) {
                for (const auto& [r2id, r2]: regs2) {
                    if (r1->rect().intersects(r2->rect())) {
                        std::unordered_set<Tile::Index> indexes = r1->collectOverlappedIndexes(*r2);
                        for (const Tile::Index& index: indexes) {
                            m_issues->warnings.insert({"Overlapping tile at ("+std::to_string(index.col)+","+std::to_string(index.row)+") in partitions: '"+p1->name()+"' and '"+p2->name()+"'",
                                                      VPR_DOC_FRAGMENT_OVERLAP_IN_DIFFERENT_PARTITIONS});
                        }
                        m_overlappedNonConflictingIndexes.insert(indexes.begin(), indexes.end());
                    }
                }
            }
        }
    }

    // tiles overlapping within partition
    for (auto& [pid, part]: m_partitions) {
        auto& regs = part->regions();

        for (auto rit1 = regs.begin(); rit1 != regs.end(); ++rit1) {
            for (auto rit2 = std::next(rit1); rit2 != regs.end(); ++rit2) {
                const RegionPtr& r1 = rit1->second;
                const RegionPtr& r2 = rit2->second;
                if (r1->rect().intersects(r2->rect())) {
                    std::unordered_set<Tile::Index> indexes = r1->collectOverlappedIndexes(*r2);
                    for (const Tile::Index& index: indexes) {
                      m_issues->errors.insert({"Overlapping tile at ("+std::to_string(index.col)+","+std::to_string(index.row)+") between regions belonging to '"+part->name()+"' partition",
                      VPR_DOC_FRAGMENT_OVERLAP_IN_ONE_PARTITION});
                    }
                    m_overlappedConflictingIndexes.insert(indexes.begin(), indexes.end());
                }
            }
        }
    }

    // elements overlapping
    for (auto pit1 = m_partitions.begin(); pit1 != m_partitions.end(); ++pit1) {
        for (auto pit2 = std::next(pit1); pit2 != m_partitions.end(); ++pit2) {
            const PartitionPtr& p1 = pit1->second;
            const PartitionPtr& p2 = pit2->second;

            std::unordered_set<std::string> elements = p1->collectOverlappedElements(*p2);
            for (const std::string& element: elements) {
                m_issues->warnings.insert({"Overlapping element '"+element+"' in partitions: '"+p1->name()+"' and '"+p2->name()+"'", ""});
            }
        }
    }

    return m_issues;
}

QRectF DeviceGrid::rect() const
{
  const int cols = m_descriptor->columns();
  const int rows = m_descriptor->rows();

  if ((cols <= 0) || (rows <= 0)) {
    return QRectF();
  }

  const double tileDistance = Tile::unitPx() + Tile::borderPx();
  // rect start from tileDistance because we constructTileMap starting X from index 1, not 0, to mach vpr coord system.
  // For Y pos is other storry, because we flipp coordinate vertically, this case is taken into account.
  // To see this visually, please do QPainter::drawRect()
  return QRectF(tileDistance,0,
                cols*tileDistance - Tile::borderPx(),
                rows*tileDistance - Tile::borderPx());
}

} // namespace fp
