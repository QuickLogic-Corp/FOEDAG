#pragma once

#include "Tile.h"
#include "Partition.h"
#include "DeviceGridDescriptor.h"

#include <QPoint>

#include <unordered_map>
#include <map>

namespace fp {

class TileDescriptor;

class DeviceGrid {
public:
    DeviceGrid()=default;
    DeviceGrid(const DeviceGridDescriptorPtr& device);
    ~DeviceGrid()=default;

    void constructTiles(const DeviceGridDescriptorPtr& device);
    void clearPartitions() {
        m_partitions.clear();
    }

    void addPartition(const PartitionPtr& partition);
    void removePartition(const PartitionPtr&);
    const PartitionPtr& partition(int partitionId) const;
    void refreshPartition(const PartitionPtr&);

    bool removeRegion(const RegionPtr&);

    const std::map<int, PartitionPtr>& partitions() const { return m_partitions; }
    const std::unordered_map<Tile::Index, TilePtr>& tiles() const { return m_tiles; }
    const std::unordered_set<Tile::Index>& overlappedConflictingIndexes() const { return m_overlappedConflictingIndexes; }
    const std::unordered_set<Tile::Index>& overlappedNonConflictingIndexes() const { return m_overlappedNonConflictingIndexes; }
    const std::unordered_map<Tile::Index, Tile::Index>& tileFragments() const { return m_tileFragments; }

    void alignRegions();
    void alignRegion(const RegionPtr&);
    void restoreRegion(const PartitionPtr& partition,
                       const Tile::Index& bottomLeftTileIndex,
                       const Tile::Index& topRightTileIndex,
                       bool excludeIoTiles = true);
    std::unordered_map<Tile::Index, TilePtr> findTiles(const QRectF&, bool excludeIoTiles = true);
    const TilePtr& tile(const Tile::Index&) const;

    void markVisibleTiles(const QRectF& visibleArea);

    std::string buildTileSymbolicName(Tile::Type, const Tile::Index& index, bool isTileFullyIncluded = true) const;

    Tile::Index toBottomLeftGridIndex(const TileDescriptor&) const;
    Tile::Index toTopRightGridIndex(const TileDescriptor&) const;

    std::unordered_set<std::string> collectErrors();

    QPointF bottomLeftPoint(const Tile::Index&) const;
    QPointF topRightPoint(const Tile::Index&) const;

    QRectF rect() const;

private:
    DeviceGridDescriptorPtr m_descriptor;

    std::unordered_map<Tile::Index, TilePtr> m_tiles;
    std::unordered_map<Tile::Index, Tile::Index> m_tileFragments;

    std::map<int, PartitionPtr> m_partitions;

    std::unordered_set<Tile::Index> m_overlappedConflictingIndexes;
    std::unordered_set<Tile::Index> m_overlappedNonConflictingIndexes;

    TilePtr m_nullPtrTile;
    PartitionPtr m_nullPtrPartition;

    void constructTile(Tile::Type type, int col, int row);
    void constructTileFragment(int col, int row, const Tile::Index& bottomLeftTileIndex);
    QRectF getAlignedRect(const Tile::Index& bottomLeftTileIndex, const Tile::Index& topRightTileIndex) const;
};

}  // namespace fp
