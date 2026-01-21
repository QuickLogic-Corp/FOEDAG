#include "Region.h"

#include <QDebug>

namespace fp {

int Region::s_idGenerator = 0;

void Region::setTiles(const std::unordered_set<Tile::Index>& tiles)
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
        [](const Tile::Index& a, const Tile::Index& b) {
            return a.col < b.col;
        });

    auto [minRow, maxRow] = std::minmax_element(
        m_tiles.begin(), m_tiles.end(),
        [](const Tile::Index& a, const Tile::Index& b) {
            return a.row < b.row;
        });

    m_bottomLeftIndex = Tile::Index(minCol->col, minRow->row);
    m_topRightIndex = Tile::Index(maxCol->col, maxRow->row);
    // qDebug() << "~~~ regionBound m_bottomLeftIndex=" << m_bottomLeftIndex.col << m_bottomLeftIndex.row;
    // qDebug() << "~~~ regionBound m_topRightIndex=" << m_topRightIndex.col << m_topRightIndex.row;
    // qDebug() << "";
}

std::unordered_set<Tile::Index> Region::collectOverlappedIndexes(const Region& rhs) const
{
    std::unordered_set<Tile::Index> result;
    for (const Tile::Index& index: rhs.tiles()) {
        if (m_tiles.find(index) != m_tiles.end()) {
            result.insert(index);
        }
    }
    return result;
}

// bool Region::isOverllapedWith(const Region& rhs) {
//     const auto& small = (m_tiles.size() < rhs.m_tiles.size()) ? m_tiles : rhs.m_tiles;
//     const auto& large = (m_tiles.size() < rhs.m_tiles.size()) ? rhs.m_tiles : m_tiles;

//     for (const auto& v: small) {
//         if (large.find(v) != large.end()) {
//             return true;
//         }
//     }
//     return false;
// }

} // namespace fp
