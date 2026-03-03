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
  const char* VPR_DOC_FRAGMENT_OVERLAP_IN_ONE_PARTITION = "The regions within one partition must not overlap with each other (in order to ease processing when loading in the file). \nMore information in https://docs.verilogtorouting.org/en/latest/vpr/placement_constraints/";
  const char* VPR_DOC_FRAGMENT_OVERLAP_IN_DIFFERENT_PARTITIONS = "It is strongly recommended that different partitions do not overlap. The packing algorithm compares the number of clustered blocks and the number of physical blocks in a region to decide if it should pack atoms inside a partition more aggressively when there are not enough resources in a partition. Overlapping partitions cause some physical blocks to be counted in more than one partition, which will degrade the packing algorithm’s ability to create a clustering that can be placed given the floorplan constraints. \nMore information in https://docs.verilogtorouting.org/en/latest/vpr/placement_constraints/";
public:
  struct Issues {
    std::unordered_map<std::string, std::string> errors;
    std::unordered_map<std::string, std::string> warnings;
    void clear() {
      errors.clear();
      warnings.clear();
    }
    bool isEmpty() const { return (errors.empty() && warnings.empty()); }
  };
  using IssuesPtr = std::shared_ptr<Issues>;

public:
    DeviceGrid();
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

    const IssuesPtr& checkIssues();
    bool hasErrors() const { return !m_issues->errors.empty(); }

    QPointF bottomLeftPoint(const Tile::Index&) const;
    QPointF topRightPoint(const Tile::Index&) const;

    QRectF rect() const;

private:
    DeviceGridDescriptorPtr m_descriptor;

    IssuesPtr m_issues;

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
