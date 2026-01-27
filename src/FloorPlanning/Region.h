#pragma once

#include "Tile.h"

#include <QPoint>
#include <QRect>
#include <QLine>

#include <memory>
#include <optional>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace fp {

class Region {
    static int s_idGenerator;
public:
    static void resetIdGenerator() { s_idGenerator = 0; }

    enum HandlerRole {
        TL, TR, BL, BR, MOVE, REMOVE
    };

    Region(const QPointF& point) {
        m_startPos = point;
        m_stopPosOpt = std::nullopt;

        m_id = s_idGenerator++;
    }
    Region(const QPointF& startPos, const QPointF& stopPos)
    {
        m_startPos = startPos;
        setStopPos(stopPos);

        m_id = s_idGenerator++;
    }
    Region(const QRectF& rect)
    {
        setRect(rect);

        m_id = s_idGenerator++;
    }

    int id() const { return m_id; }

    std::optional<HandlerRole> checkHandlerClick(const QPointF& point) {
        for (const auto& [role, rect]: handles) {
            if (rect.contains(point)) {
                return role;
            }
        }
        return std::nullopt;
    }

    void setTiles(const std::unordered_map<Tile::Index, TilePtr>& tiles);

    bool contains(const Tile::Index& index) const { return m_tiles.find(index) != m_tiles.end(); }

    void accept(const QPointF& point, const std::unordered_map<Tile::Index, TilePtr>& tiles) {
        m_stopPosOpt = point;
        updateRectFromPositions();
        setTiles(tiles);
        rebuildHandles();
    }

    const QRectF& rect() const { return m_rect; }

    void setBottomLeft(const QPointF& p) {
        m_rect.setBottomLeft(p);
        updatePositionsFromRect();
        updateGridCoordBounds();
    }

    void setBottomRight(const QPointF& p) {
        m_rect.setBottomRight(p);
        updatePositionsFromRect();
        updateGridCoordBounds();
    }

    void setTopRight(const QPointF& p) {
        m_rect.setTopRight(p);
        updatePositionsFromRect();
        updateGridCoordBounds();
    }

    void setTopLeft(const QPointF& p) {
        m_rect.setTopLeft(p);
        updatePositionsFromRect();
        updateGridCoordBounds();
    }

    void moveCenter(const QPointF& p) {
        m_rect.moveCenter(p);
        updatePositionsFromRect();
        updateGridCoordBounds();
    }

    void setStopPos(const QPointF& point) {
        m_stopPosOpt = point;
        updateRectFromPositions();
    }
    bool isClosed() const { return m_stopPosOpt.has_value(); }
    bool isValid() const { return isClosed() && !m_tiles.empty(); }

    const std::unordered_map<Tile::Index, TilePtr>& tiles() const { return m_tiles; }

    const Tile::Index& bottomLeftTileIndex() const { return m_bottomLeftTileIndex; }
    const Tile::Index& topRightTileIndex() const { return m_topRightTileIndex; }

    const Tile::Index& bottomLeftGridIndex() const { return m_bottomLeftGridIndex; }
    const Tile::Index& topRightGridIndex() const { return m_topRightGridIndex; }

    void setPoints(const QPointF& bottomLeft, const QPointF& topRight) {
        m_startPos = bottomLeft;
        m_stopPosOpt = topRight;
        updateRectFromPositions();
    }

    void setRect(const QRectF& rect) {
        m_startPos = rect.bottomLeft();
        m_stopPosOpt = rect.topRight();
        updateRectFromPositions();
    }

    std::unordered_set<Tile::Index> collectOverlappedIndexes(const Region& rhs) const;

    std::map<HandlerRole, QRectF> handles;

    void rebuildHandles(double scaleFactor = 1.0) {
        const double handleSize = 0.5 * Tile::unitPx() * scaleFactor;

        QRectF r = m_rect.normalized();

        handles[HandlerRole::TL] = QRectF(r.left(),                 r.top(),                  handleSize, handleSize);
        handles[HandlerRole::TR] = QRectF(r.right() - handleSize,   r.top(),                  handleSize, handleSize);
        handles[HandlerRole::BL] = QRectF(r.left(),                 r.bottom() - handleSize,  handleSize, handleSize);
        handles[HandlerRole::BR] = QRectF(r.right() - handleSize,   r.bottom() - handleSize,  handleSize, handleSize);

        const QPointF moveCenter(r.center().x(), r.center().y());
        handles[HandlerRole::MOVE] = centeredSquare(moveCenter, handleSize);

        const QPointF removeCenter(r.center().x(), r.top()+0.5*handleSize);
        handles[HandlerRole::REMOVE] = centeredSquare(removeCenter, handleSize);
    }

private:
    int m_id = -1;

    QPointF m_startPos;
    std::optional<QPointF> m_stopPosOpt;

    Tile::Index m_bottomLeftTileIndex;
    Tile::Index m_topRightTileIndex;

    Tile::Index m_bottomLeftGridIndex;
    Tile::Index m_topRightGridIndex;

    QRectF m_rect;

    std::unordered_map<Tile::Index, TilePtr> m_tiles;

    const QPointF& startPos() const { return m_startPos; }
    QPointF stopPos() const { return m_stopPosOpt.value(); }

    QRectF centeredSquare(const QPointF& c, qreal size) const {
        const qreal h = size * 0.5;
        return QRectF(c.x() - h, c.y() - h, size, size);
    }

    void updateRectFromPositions() {
        m_rect = QRectF(startPos(), stopPos()).normalized(); // normalized here is to swap corner to avoid negative height
        updateGridCoordBounds();
    }

    void updatePositionsFromRect()
    {
        m_startPos = m_rect.bottomLeft();
        m_stopPosOpt = m_rect.topRight();
    }

    void updateTileCoordBounds();
    void updateGridCoordBounds();
};
using RegionPtr = std::shared_ptr<Region>;

}  // namespace fp
