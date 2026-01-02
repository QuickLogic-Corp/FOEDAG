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

#include <QWidget>
#include <QPoint>

#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <set>
#include <memory>
#include <optional>

class QPaintEvent;

namespace FOEDAG {

struct DeviceDescriptor {
    int columns;
    int rows;
    std::set<int> dspColumns;
    std::set<int> bramColumns;
    QSize dspSize;
    QSize bramSize;
};
using DeviceDescriptorPtr = std::shared_ptr<DeviceDescriptor>;

class DeviceWidget final : public QWidget {
    Q_OBJECT

    class Region {
    public:
        void start(const QPointF& point) {
            m_startPos = point;
            m_stopPosOpt = std::nullopt;
        }
        void accept(const QPointF& point, const std::unordered_set<Tile::Index>& tiles) {
            m_stopPosOpt = point;
            m_tiles = tiles;
        }
        void reject() {
            m_stopPosOpt = std::nullopt;
        }
        void setStopPos(const QPointF& point) { m_stopPosOpt = point; }
        bool isClosed() const { return m_stopPosOpt.has_value(); }
        bool isValid() const { return isClosed() && !m_tiles.empty(); }
        const QPointF& startPos() const { return m_startPos; }
        QPointF stopPos() const { return m_stopPosOpt.value(); }

        const std::unordered_set<Tile::Index>& tiles() const { return m_tiles; }
        bool isOverllapedWith(const Region& rhs) {
            const auto& small = (m_tiles.size() < rhs.m_tiles.size()) ? m_tiles : rhs.m_tiles;
            const auto& large = (m_tiles.size() < rhs.m_tiles.size()) ? rhs.m_tiles : m_tiles;

            for (const auto& v: small) {
                if (large.contains(v)) {
                    return true;
                }
            }
            return false;
        }

    private:
        QPointF m_startPos;
        std::optional<QPointF> m_stopPosOpt;
        std::unordered_set<Tile::Index> m_tiles;
    };
public:
    explicit DeviceWidget(QWidget* parent = nullptr);
    virtual ~DeviceWidget()=default;

    void constructTiles(const DeviceDescriptorPtr& device);

protected:
    QSize sizeHint() const override final;
    QSize minimumSizeHint() const override final;
    void keyPressEvent(QKeyEvent*) override final;
    void mousePressEvent(QMouseEvent*) override final;
    void mouseReleaseEvent(QMouseEvent*) override final;
    void mouseMoveEvent(QMouseEvent*) override final;
    void wheelEvent(QWheelEvent* event) override final;
    void paintEvent(QPaintEvent*) override final;

private:
    QColor m_backgroundColor{25, 25, 28};
    QColor m_transparentColor{0, 0, 0, 0};
    QColor m_textColor{30, 30, 35};
    QColor m_selectedColor{255, 120, 120};

    double m_scale = 1.0f;

    QRectF m_viewPort;
    std::unordered_map<Tile::Index, Tile> m_tiles;
    DeviceDescriptorPtr m_device;

    void drawBackground(QPainter& p);
    void drawTilesBatched(QPainter& p);
    void drawSelectedTile(QPainter& p);
    void drawTileLabels(QPainter& p);
    void highLightTilesInArea(QPainter& p, const Region& area) const;

    // panning
    bool m_isPanning{false};
    QPointF m_lastMousePos;
    QPointF m_panPixels{0.0, 0.0};

    void startPanning(const QPoint& pos);
    void stopPanning();
    // panning

    // selection
    bool m_isSelecting{false};
    Region m_currentRegion;

    std::vector<Region> m_regions;

    void clearSelections() {
        m_regions.clear();
        update();
    }
    void startSelection(const QPoint& selection);
    void stopSelection(const QPoint& selection);
    void cancelSelection(const QString& msg);
    // selection

    QPointF toWorldCoord(const QPoint&);
};

}  // namespace FOEDAG
