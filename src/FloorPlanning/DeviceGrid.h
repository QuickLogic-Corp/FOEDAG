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
    void refreshPartitions();

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

    // [aurora2#1725] RTL path -> how many atoms belong to that instance DIRECTLY, i.e. sit
    // in it rather than in one of its sub-instances. Counted from atomsets.json by
    // FloorPlanningWidget::setAtomNames(). Only the counts, not the names: checkIssues()
    // needs to know whether an instance has own logic at stake, not what it is.
    void setOwnAtomCounts(std::map<std::string, int> ownAtomCounts) {
        m_ownAtomCounts = std::move(ownAtomCounts);
    }

    // [aurora2#1725 stage P4] Instances graded "deleted" in validation.json. A .qdc may
    // still name one; checkIssues() says so, because such a constraint matches no atom.
    void setDeletedInstances(std::unordered_set<std::string> deletedInstances) {
        m_deletedInstances = std::move(deletedInstances);
    }

    QPointF bottomLeftPoint(const Tile::Index&) const;
    QPointF topRightPoint(const Tile::Index&) const;

    QRectF rect() const;

private:
    DeviceGridDescriptorPtr m_descriptor;

    IssuesPtr m_issues;

    std::unordered_map<Tile::Index, TilePtr> m_tiles;
    std::unordered_map<Tile::Index, Tile::Index> m_tileFragments;

    std::map<int, PartitionPtr> m_partitions;

    std::map<std::string, int> m_ownAtomCounts;
    std::unordered_set<std::string> m_deletedInstances;

    std::unordered_set<Tile::Index> m_overlappedConflictingIndexes;
    std::unordered_set<Tile::Index> m_overlappedNonConflictingIndexes;

    TilePtr m_nullPtrTile;
    PartitionPtr m_nullPtrPartition;

    void constructTile(Tile::Type type, int col, int row);
    void constructTileFragment(int col, int row, const Tile::Index& bottomLeftTileIndex);
    QRectF getAlignedRect(const Tile::Index& bottomLeftTileIndex, const Tile::Index& topRightTileIndex) const;
};

}  // namespace fp
