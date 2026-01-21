#pragma once

#include "Tile.h"
#include "Partition.h"
#include "DeviceGridDescriptor.h"

#include <QPoint>

#include <unordered_map>
#include <map>
#include <optional>

namespace fp {

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
    PartitionPtr findPartition(int partitionId) const;
    void refreshPartition(const PartitionPtr&);

    const std::map<int, PartitionPtr>& partitions() const { return m_partitions; }
    const std::unordered_map<Tile::Index, Tile>& tiles() const { return m_tiles; }
    const std::unordered_set<Tile::Index>& overlappedIndexes() const { return m_overlappedIndexes; }

    void alignRegions();
    void alignRegion(const RegionPtr&);
    bool restoreRegion(const PartitionPtr& partition,
                       const Tile::Index& bottomLeftTileIndex,
                       const Tile::Index& topRightTileIndex,
                       bool excludeIoTiles = true);
    std::unordered_set<Tile::Index> findTiles(const QRectF&, bool excludeIoTiles = true);
    const Tile& tile(const Tile::Index&) const;

    void markVisibleTiles(const QRectF& visibleArea);

    std::string buildTileSymbolicName(Tile::Type, const Tile::Index& index, bool isTileFullyIncluded = true) const;

    std::optional<QPointF> findBottomLeftPoint(const Tile::Index&) const;
    std::optional<QPointF> findTopRightPoint(const Tile::Index&) const;

    std::unordered_set<std::string> collectErrors();

private:
    DeviceGridDescriptorPtr m_descriptor;

    std::unordered_map<Tile::Index, Tile> m_tiles;
    std::unordered_map<Tile::Index, Tile::Index> m_tileFragments;

    std::map<int, PartitionPtr> m_partitions;

    std::unordered_set<Tile::Index> m_overlappedIndexes;

    QPointF bottomLeftPoint(const Tile::Index&) const;
    QPointF topRightPoint(const Tile::Index&) const;

    Tile m_nullTile;

    void constructTile(Tile::Type type, int col, int row);
    void constructTileFragment(int col, int row, const Tile::Index& bottomLeftTileIndex);
};

}  // namespace fp
