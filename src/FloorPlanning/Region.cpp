/*
Copyright 2022 The Foedag team

GPL License

Copyright (c) 2022 The Open-Source FPGA Foundation

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Region.h"

#include <QDebug>

namespace FOEDAG {

int Region::s_idGenerator = 0;

void Region::setTiles(const std::unordered_set<Tile::Index>& tiles)
{
    m_tiles = tiles;
    updateTileGridCoordBounds();
}

void Region::updateTileGridCoordBounds()
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

    Tile::Index bottomLeftIndex = Tile::Index(minCol->col, maxRow->row);
    Tile::Index topRightIndex = Tile::Index(maxCol->col, minRow->row);

    m_bottomLeftGridCoord = Tile::toGridCoord(bottomLeftIndex);
    m_topRightGridCoord   = Tile::toGridCoord(topRightIndex);

    // test
    Tile::Index bottomLeftIndexReversed = Tile::fromGridCoord(m_bottomLeftGridCoord);
    Tile::Index topRightIndexReversed = Tile::fromGridCoord(m_topRightGridCoord);

    assert(bottomLeftIndex.row == bottomLeftIndexReversed.row);
    assert(bottomLeftIndex.col == bottomLeftIndexReversed.col);
    assert(topRightIndex.row == topRightIndexReversed.row);
    assert(topRightIndex.col == topRightIndexReversed.col);
    //
}



} // namespace FOEDAG
