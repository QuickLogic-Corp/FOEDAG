#include "Device.h"

#include <QDebug>

namespace fp {

Device::Device(const DeviceDescriptorPtr& device)
{
    constructTiles(device);
}

void Device::constructTile(Tile::Type type, int col, int row)
{
    Tile::Index index{col, row};
    QSize size = m_descriptor->elementSize(type);
    std::string label = buildTileSymbolicName(type, index);
    m_tiles[index] = Tile{type, col, row, size.width(), size.height(), QString::fromStdString(label)};
}

void Device::constructTileFragment(int col, int row, const Tile::Index& bottomLeftTileIndex)
{
    if ((col != bottomLeftTileIndex.col) || (row != bottomLeftTileIndex.row)) {
        m_tileFragments[Tile::Index{col, row}] = bottomLeftTileIndex;
        qInfo() << "~~~ tile fragment=" << col << row <<  "point to" << bottomLeftTileIndex.col << bottomLeftTileIndex.row;
    }
}

void Device::constructTiles(const DeviceDescriptorPtr& device) {
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

    assert(!step_on_bram);
    assert(!step_on_dsp);
}

void Device::markVisibleTiles(const QRectF& visibleArea)
{
    for (auto& [idx, tile]: m_tiles) {
        tile.setVisible(tile.rect().intersects(visibleArea));
    }
}

RegionPtr Device::findRegion(const QPointF& worldCoord) const
{
    for (const auto& [id, region]: m_regions) {
        if (region->rect().contains(worldCoord)) {
            return region;
        }
    }
    return nullptr;
}

void Device::removeRegion(const RegionPtr& region)
{
    m_regions.erase(m_regions.find(region->id()));
}

void Device::addRegion(const RegionPtr& region)
{
    m_regions[region->id()] = region;
}

void Device::refreshRegion(const RegionPtr& region)
{
    region->buildHandles();
    region->setTiles(findTiles(region->rect()));
}

std::unordered_set<Tile::Index> Device::findTiles(const QRectF& rect, bool excludeIoTiles)
{
    std::unordered_set<Tile::Index> indexes;
    for (const auto& [index, tile]: m_tiles) {
        if (excludeIoTiles && (tile.type() == Tile::Type::Io)) {
            continue;
        }
        if (tile.isLocated(rect)) {
            indexes.insert(tile.index());
        }
    }
    return indexes;
}

QPointF Device::bottomLeftPoint(const Tile::Index& idx) const
{
    return Tile::buildRect(idx, 1, 1).bottomLeft();
}

QPointF Device::topRightPoint(const Tile::Index& idx) const
{
    return Tile::buildRect(idx, 1, 1).topRight();
}

std::optional<QPointF> Device::findBottomLeftPoint(const Tile::Index& idx) const
{
    if (auto it = m_tiles.find(idx); it != m_tiles.end()) {
        const Tile& tile = it->second;
        return tile.rect().bottomLeft();
    }
    if (auto it = m_tileFragments.find(idx); it != m_tileFragments.end()) {
        return bottomLeftPoint(idx);
    }

    return std::nullopt;
}

std::optional<QPointF> Device::findTopRightPoint(const Tile::Index& idx) const
{
    if (auto it = m_tiles.find(idx); it != m_tiles.end()) {
        const Tile& tile = it->second;
        return tile.rect().topRight();
    }
    if (auto it = m_tileFragments.find(idx); it != m_tileFragments.end()) {
        return topRightPoint(idx);
    }

    return std::nullopt;
}

std::string Device::buildTileSymbolicName(Tile::Type type, const Tile::Index& index, bool isTileFullyIncluded) const
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

const Tile& Device::tile(const Tile::Index& index) const
{
    Tile::Index bottomLeftTileIndex{index};
    if (auto it = m_tileFragments.find(index); it != m_tileFragments.end()) {
        bottomLeftTileIndex = it->second;
    }

    if (auto it = m_tiles.find(bottomLeftTileIndex); it != m_tiles.end()) {
        return it->second;
    }

    qCritical() << "tile on index" << index.col << index.row << "resolved index=" << bottomLeftTileIndex.col << bottomLeftTileIndex.row << "wasn't found";
    return m_nullTile;
}

} // namespace fp
