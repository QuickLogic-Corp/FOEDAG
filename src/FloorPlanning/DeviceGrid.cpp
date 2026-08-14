#include "DeviceGrid.h"
#include "TileDescriptor.h"

#include <QDebug>

namespace fp {

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
        // [aurora2#1725] required vs. available resources -- a partition whose
        // regions don't have room for the atoms assigned to it can never be placed,
        // so this is an error, not a warning, same severity as the presence checks
        // above.
        auto checkResource = [&](const char* label, int required, int available) {
            if (required > available) {
                m_issues->errors.insert({"Partition '" + partition->name() + "' needs " +
                                          std::to_string(required) + " " + label + " but only " +
                                          std::to_string(available) + " available", ""});
            }
        };
        checkResource("clb", partition->clbRequiredCount(), partition->clbAvailableCount());
        checkResource("dsp", partition->dspRequiredCount(), partition->dspAvailableCount());
        checkResource("bram", partition->bramRequiredCount(), partition->bramAvailableCount());
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
