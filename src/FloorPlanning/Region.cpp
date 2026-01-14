#include "Region.h"

#include <QDebug>

namespace fp {

int Region::s_idGenerator = 0;

void Region::setTiles(const std::unordered_set<Tile::Index>& tiles)
{
    m_tiles = tiles;
    updateTileCoordBounds();
}

void Region::setElements(const HierarhyElementsPtr& elemenets)
{
    if (elemenets) {
        elemenets->print(QString("Region(%1)::setElements").arg(m_id).toStdString());
    }
    m_elements = elemenets;
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
    qInfo() << "~~~ regionBound m_bottomLeftIndex=" << m_bottomLeftIndex.col << m_bottomLeftIndex.row;
    qInfo() << "~~~ regionBound m_topRightIndex=" << m_topRightIndex.col << m_topRightIndex.row;
    qInfo() << "";
}

} // namespace fp
