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

#pragma once

#include "Tile.h"
#include "HierarhyElement.h"

#include <QPoint>
#include <QRect>
#include <QLine>

#include <memory>
#include <optional>
#include <unordered_set>
#include <map>

namespace FOEDAG {

class Region {
    static int s_idGenerator;
public:
    enum HandlerRole {
        TL, TR, BL, BR, MOVE, REMOVE
    };

    Region(const QPointF& point) {
        m_id = s_idGenerator++;
        m_startPos = point;
        m_stopPosOpt = std::nullopt;
        m_elements = std::make_shared<HierarhyElements>();
    }
    int id() const { return m_id; }

    std::optional<HandlerRole> checkHandlerClick(const QPointF& point) {
        for (const auto& [role, rect]: handlers) {
            if (rect.contains(point)) {
                return role;
            }
        }
        return std::nullopt;
    }

    void setTiles(const std::unordered_set<Tile::Index>& tiles);
    void setElements(const HierarhyElementsPtr& elements);

    void accept(const QPointF& point, const std::unordered_set<Tile::Index>& tiles, const HierarhyElementsPtr& elements) {
        m_stopPosOpt = point;
        m_rect = QRectF(startPos(), stopPos());
        setTiles(tiles);
        setElements(elements);
        buildHandles();
    }
    void reject() {
        m_stopPosOpt = std::nullopt;
    }
    const QRectF& rect() const { return m_rect; }
    QRectF& rect() { return m_rect; }

    void setStopPos(const QPointF& point) {
        m_stopPosOpt = point;
        m_rect = QRectF(startPos(), stopPos());
    }
    bool isClosed() const { return m_stopPosOpt.has_value(); }
    bool isValid() const { return isClosed() && !m_tiles.empty(); }

    const std::unordered_set<Tile::Index>& tiles() const { return m_tiles; }
    const Tile::Index& bottomLeftIndex() const { return m_bottomLeftIndex; }
    const Tile::Index& topRightIndex() const { return m_topRightIndex; }

    const HierarhyElementsPtr& elements() const { return m_elements; }

    bool isOverllapedWith(const Region& rhs) {
        const auto& small = (m_tiles.size() < rhs.m_tiles.size()) ? m_tiles : rhs.m_tiles;
        const auto& large = (m_tiles.size() < rhs.m_tiles.size()) ? rhs.m_tiles : m_tiles;

        for (const auto& v: small) {
            if (large.find(v) != large.end()) {
                return true;
            }
        }
        return false;
    }

    std::map<HandlerRole, QRectF> handlers;

    void buildHandles() {
        const double handleSize = 0.5 * Tile::unitPx();

        QRectF r = m_rect.normalized();

        handlers[HandlerRole::TL] = QRectF(r.left(),                 r.top(),                  handleSize, handleSize);
        handlers[HandlerRole::TR] = QRectF(r.right() - handleSize,   r.top(),                  handleSize, handleSize);
        handlers[HandlerRole::BL] = QRectF(r.left(),                 r.bottom() - handleSize,  handleSize, handleSize);
        handlers[HandlerRole::BR] = QRectF(r.right() - handleSize,   r.bottom() - handleSize,  handleSize, handleSize);

        const QPointF moveCenter(r.center().x(), r.center().y());
        handlers[HandlerRole::MOVE] = centeredSquare(moveCenter, handleSize);

        const QPointF removeCenter(r.center().x(), r.top());
        handlers[HandlerRole::REMOVE] = centeredSquare(removeCenter, handleSize);
    }

private:
    int m_id = -1;
    QPointF m_startPos;
    std::optional<QPointF> m_stopPosOpt;
    std::unordered_set<Tile::Index> m_tiles;
    Tile::Index m_bottomLeftIndex;
    Tile::Index m_topRightIndex;
    HierarhyElementsPtr m_elements;
    QRectF m_rect;

    const QPointF& startPos() const { return m_startPos; }
    QPointF stopPos() const { return m_stopPosOpt.value(); }

    QRectF centeredSquare(const QPointF& c, qreal size) const {
        const qreal h = size * 0.5;
        return QRectF(c.x() - h, c.y() - h, size, size);
    }

    void updateTileGridCoordBounds();
};
using RegionPtr = std::shared_ptr<Region>;

}  // namespace FOEDAG
