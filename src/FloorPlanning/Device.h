#pragma once

#include "Tile.h"
#include "Region.h"
#include "DeviceDescriptor.h"

#include <QPoint>

#include <unordered_map>
#include <map>
#include <optional>

namespace fp {

class Device {
public:
    Device()=default;
    Device(const DeviceDescriptorPtr& device);
    ~Device()=default;

    void constructTiles(const DeviceDescriptorPtr& device);
    void clearRegions() {
        m_regions.clear();
    }

    void addRegion(const RegionPtr& region);
    void removeRegion(const RegionPtr&);
    RegionPtr findRegion(const QPointF& worldCoord) const;
    void refreshRegion(const RegionPtr&);

    const std::map<int, RegionPtr>& regions() const { return m_regions; }
    const std::unordered_map<Tile::Index, Tile>& tiles() const { return m_tiles; }

    std::unordered_set<Tile::Index> findTiles(const QRectF&, bool excludeIo = true);
    const Tile& tile(const Tile::Index&) const;

    void markVisibleTiles(const QRectF& visibleArea);

    std::string buildTileSymbolicName(Tile::Type, const Tile::Index& index, bool isTileFullyIncluded = true) const;

    std::optional<QPointF> findBottomLeftPoint(const Tile::Index&) const;
    std::optional<QPointF> findTopRightPoint(const Tile::Index&) const;

private:
    DeviceDescriptorPtr m_descriptor;

    std::unordered_map<Tile::Index, Tile> m_tiles;
    std::unordered_map<Tile::Index, Tile::Index> m_tileFragments;

    std::map<int, RegionPtr> m_regions;

    QPointF bottomLeftPoint(const Tile::Index&) const;
    QPointF topRightPoint(const Tile::Index&) const;

    Tile m_nullTile;

    void constructTile(Tile::Type type, int col, int row);
    void constructTileFragment(int col, int row, const Tile::Index& bottomLeftTileIndex);
};

}  // namespace fp
