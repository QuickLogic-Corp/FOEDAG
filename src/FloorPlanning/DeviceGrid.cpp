#include "DeviceGrid.h"
#include "TileDescriptor.h"

#include <QDebug>

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
            } else if (device->isBramColumn(col)) {
                if (!step_on_bram) {
                    constructTile(Tile::Type::Bram, col, row);
                    bottomLeftIndex = Tile::Index(col, row);
                    step_on_bram = true;
                }
                if (step_on_bram) {
                    constructTileFragment(col, row, bottomLeftIndex);
                    stepCounter++;
                    if (stepCounter == device->bramSize().height()) {
                        bottomLeftIndex.reset();
                        step_on_bram = false;
                        stepCounter = 0;
                    }
                }

            } else if (device->isDspColumn(col)) {
                if (!step_on_dsp) {
                    bottomLeftIndex = Tile::Index(col, row);
                    constructTile(Tile::Type::Dsp, col, row);
                    step_on_dsp = true;
                }
                if (step_on_dsp) {
                    constructTileFragment(col, row, bottomLeftIndex);
                    stepCounter++;
                    if (stepCounter == device->dspSize().height()) {
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
    RegionPtr region = std::make_shared<Region>(rect);
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
    case Tile::Type::Dsp: result.row += m_descriptor->dspSize().height() - 1; break;
    case Tile::Type::Bram: result.row += m_descriptor->bramSize().height() - 1; break;
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

const DeviceGrid::IssuesPtr& DeviceGrid::checkIssues()
{
    m_issues->clear();
    m_overlappedConflictingIndexes.clear();
    m_overlappedNonConflictingIndexes.clear();

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
        // [aurora2#1725] required vs. available resources. ERRORS, like the presence checks
        // above: a region too small to hold what its partition constrains leaves the packer
        // nowhere to put the surplus, so this blocks saving the .qdc in the panel and is
        // reported as an error by the batch check rather than left for the flow to discover.
        //
        // The clb figure is an ESTIMATE and it is not a safe minimum. It divides the atom
        // count by atomsets.json's atoms_per_tile hint, but real packing density varies with
        // the logic: carry chains want consecutive slots, and the packer caps cluster
        // input-pin use, so a small pin-heavy instance packs far less densely than a whole
        // design's average. The tip still says the number is approximate -- but short by an
        // estimate that runs optimistic is short in the direction that fails, and the remedy
        // does not depend on the exact figure: a bigger region, or fewer elements. dsp/bram
        // carry no such uncertainty -- one atom occupies one whole tile.
        auto checkResource = [&](const std::string& label, int required, int available) {
            if (required <= available) {
                return;
            }
            const std::string tip = (label == "clb")
                ? "Estimated from this partition's clb atoms at "
                  + std::to_string(Partition::atomsPerTile())
                  + " atoms per tile. Real density varies with the logic, so the shortfall is "
                    "approximate -- but the region has to cover more clb tiles, or some "
                    "elements belong in another partition."
                : "One " + label + " atom occupies one " + label + " tile, so the region has to "
                  "cover more of them, or some elements belong in another partition.";
            m_issues->errors.insert({"Partition '" + partition->name() + "' needs " +
                                     std::to_string(required) + " " + label + " tiles but only " +
                                     std::to_string(available) + " available", tip});
        };
        checkResource("clb", partition->clbRequiredCount(), partition->clbAvailableCount());
        checkResource("dsp", partition->dspRequiredCount(), partition->dspAvailableCount());
        checkResource("bram", partition->bramRequiredCount(), partition->bramAvailableCount());

        // [aurora2#1725] Enough clb tiles by the estimate, but only just -- which is where
        // packing actually breaks, precisely because the estimate can sit below what the
        // packer needs, and a region filled to the last tile leaves it no room to be wrong.
        //
        // An ERROR, like the outright shortfall above. It was a warning on the grounds that
        // the region is not actually short and only the packer could settle it; but the
        // estimate it is measured against runs optimistic, so "not short by the estimate"
        // does not mean "not short", and letting a region this tight through only moves the
        // failure to VPR, where it arrives without the partition's name or these numbers.
        // clb only: dsp/bram are exact, one atom to one tile, and need no packing slack.
        const int clbRequired = partition->clbRequiredCount();
        const int clbAvailable = partition->clbAvailableCount();
        if ((clbRequired > 0) && (clbAvailable >= clbRequired)
            && (clbAvailable < clbRequired + (clbRequired * kClbHeadroomPercent + 99) / 100)) {
            m_issues->errors.insert({"Partition '" + partition->name() + "' has " +
                                     std::to_string(clbAvailable) +
                                     " clb tiles for an estimated " +
                                     std::to_string(clbRequired) + " -- little packing slack",
                                     "The estimate can be lower than what the packer needs, so a "
                                     "region this tight may fail to pack even though it looks "
                                     "big enough. Allow roughly "
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

    // [aurora2#1725] Promote every advisory once all of them have been collected, rather
    // than at each insertion point, so the option cannot be forgotten by a check added later.
    // The issues list then shows them as errors and hasErrors() blocks saving the .qdc.
    if (m_treatWarningsAsErrors) {
        m_issues->errors.insert(m_issues->warnings.begin(), m_issues->warnings.end());
        m_issues->warnings.clear();
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
