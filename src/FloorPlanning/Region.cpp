#include "Region.h"

#include <QDebug>

namespace fp {

int Region::s_idGenerator = 0;

void Region::setTiles(const std::unordered_map<Tile::Index, TilePtr>& tiles)
{
    m_tiles = tiles;
    updateTileCoordBounds();
}

void Region::updateTileCoordBounds()
{
    if (m_tiles.empty()) {
        return;
    }

    auto [minCol, maxCol] = std::minmax_element(
        m_tiles.begin(), m_tiles.end(),
        [](const auto& a, const auto& b) {
            return a.first.col < b.first.col;
        });

    auto [minRow, maxRow] = std::minmax_element(
        m_tiles.begin(), m_tiles.end(),
        [](const auto& a, const auto& b) {
            return a.first.row < b.first.row;
        });

    m_bottomLeftTileIndex = Tile::Index(minCol->first.col, minRow->first.row);
    m_topRightTileIndex = Tile::Index(maxCol->first.col, maxRow->first.row);
    // qDebug() << "~~~ regionBound m_bottomLeftIndex=" << m_bottomLeftIndex.col << m_bottomLeftIndex.row;
    // qDebug() << "~~~ regionBound m_topRightIndex=" << m_topRightIndex.col << m_topRightIndex.row;
    // qDebug() << "";
}

void Region::updateGridCoordBounds()
{
    const double unit   = Tile::unitPx();
    const double border = Tile::borderPx();
    const double pitch  = unit + border;
    const int devRows   = Tile::deviceRowsNum();

    int colMin = (int)((m_rect.left()  + pitch - 1) / pitch);
    int colMax = (int)((m_rect.right() - unit) / pitch);

    int tMin = (int)((m_rect.top()    + pitch - 1) / pitch);
    int tMax = (int)((m_rect.bottom() - unit) / pitch);

    // Convert back: row = devRows - t
    const int rowMax = devRows - tMin;
    const int rowMin = devRows - tMax;

    if ((colMin <= colMax) && (rowMin <= rowMax)) {
        m_bottomLeftGridIndex = Tile::Index{colMin, rowMin};
        m_topRightGridIndex = Tile::Index{colMax, rowMax};
    } else {
        m_bottomLeftGridIndex.reset();
        m_topRightGridIndex.reset();
    }
}

std::unordered_set<Tile::Index> Region::collectOverlappedIndexes(const Region& rhs) const
{
    std::unordered_set<Tile::Index> result;
    for (const auto& [index, tile]: rhs.tiles()) {
        if (m_tiles.find(index) != m_tiles.end()) {
            result.insert(index);
        }
    }
    return result;
}

} // namespace fp
