#pragma once

#include "Tile.h"

#include <QPoint>
#include <QRect>
#include <QLine>

#include <memory>
#include <optional>
#include <unordered_set>
#include <map>

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

#ifdef USE_TESTS
    bool operator==(const Partition& rhs) const {
        if (bottomLeftIndex() != rhs.bottomLeftIndex()) {
            return false;
        }
        if (topRightIndex() != rhs.topRightIndex()) {
            return false;
        }
        // the rects could be slightly different, better to test comparison based on included tiles indexes
        // qInfo() << rect() << rhs.rect();
        // if (rect() != rhs.rect()) {
        //  return false;
        //}
        if (tiles() != rhs.tiles()) {
            return false;
        }
        return true;
    }
#endif // USE_TESTS

    int id() const { return m_id; }

    std::optional<HandlerRole> checkHandlerClick(const QPointF& point) {
        for (const auto& [role, rect]: handles) {
            if (rect.contains(point)) {
                return role;
            }
        }
        return std::nullopt;
    }

    void setTiles(const std::unordered_set<Tile::Index>& tiles);

    bool contains(const Tile::Index& index) const { return m_tiles.find(index) != m_tiles.end(); }

    void accept(const QPointF& point, const std::unordered_set<Tile::Index>& tiles) {
        m_stopPosOpt = point;
        updateRect();
        setTiles(tiles);
        rebuildHandles();
    }

    const QRectF& rect() const { return m_rect; }
    QRectF& rect() { return m_rect; }

    void setStopPos(const QPointF& point) {
        m_stopPosOpt = point;
        updateRect();
    }
    bool isClosed() const { return m_stopPosOpt.has_value(); }
    bool isValid() const { return isClosed() && !m_tiles.empty(); }

    const std::unordered_set<Tile::Index>& tiles() const { return m_tiles; }
    const Tile::Index& bottomLeftIndex() const { return m_bottomLeftIndex; }
    const Tile::Index& topRightIndex() const { return m_topRightIndex; }

    void setPoints(const QPointF& bottomLeft, const QPointF& topRight) {
        m_startPos = bottomLeft;
        m_stopPosOpt = topRight;
        updateRect();
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
    Tile::Index m_bottomLeftIndex;
    Tile::Index m_topRightIndex;
    QRectF m_rect;

    std::unordered_set<Tile::Index> m_tiles;

    const QPointF& startPos() const { return m_startPos; }
    QPointF stopPos() const { return m_stopPosOpt.value(); }

    QRectF centeredSquare(const QPointF& c, qreal size) const {
        const qreal h = size * 0.5;
        return QRectF(c.x() - h, c.y() - h, size, size);
    }

    void updateRect() {
        m_rect = QRectF(startPos(), stopPos()).normalized(); // normalized here is to swap corner to avoid negative height
    }
    void updateTileCoordBounds();
};
using RegionPtr = std::shared_ptr<Region>;

}  // namespace fp
