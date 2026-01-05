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
#include "Region.h"

#include <QWidget>
#include <QPoint>

#include <unordered_map>
#include <set>
#include <memory>


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

public:
    explicit DeviceWidget(QWidget* parent = nullptr);
    virtual ~DeviceWidget()=default;

    void constructTiles(const DeviceDescriptorPtr& device);

    void onSelectedPinsChanged(const std::set<std::string>& pins);

signals:
    void updatePinsSelectionRequested(std::set<std::string>);

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
    std::set<std::string> m_selectedPins;
    QColor m_backgroundColor{25, 25, 28};
    QColor m_transparentColor{0, 0, 0, 0};
    QColor m_textColor{30, 30, 35};
    QColor m_regionColor{50, 160, 50}; // green
    QColor m_editRegionColor{50, 50, 160}; // blue
    QColor m_editRegionTransparentColor{50, 50, 160, 150}; // blue

    double m_scale = 1.0f;

    bool m_isMousePressed = false;

    QRectF m_viewPort;
    std::unordered_map<Tile::Index, Tile> m_tiles;
    DeviceDescriptorPtr m_device;

    void drawBackground(QPainter& p);
    void drawTilesBatched(QPainter& p);
    void drawRegions(QPainter& p);
    void drawTileLabels(QPainter& p);
    void highLightTilesInRegion(QPainter& p, const Region& area) const;

    // panning
    bool m_isPanning{false};
    QPointF m_lastMousePos;
    QPointF m_panPixels{0.0, 0.0};

    void startPanning(const QPoint& pos);
    void stopPanning();
    // panning

    // selection
    bool m_isSelectingNewRegion{false};
    RegionPtr m_currentRegion;

    RegionPtr m_regionToEdit;
    std::optional<Region::HandlerRole> m_editRoleOpt;
    bool trySelectRegionToEdit(const QPointF& worldCoord);

    std::unordered_map<int, RegionPtr> m_regions;

    void clearSelections() {
        m_regions.clear();
        m_currentRegion.reset();
        resetEditRegion();
        update();
    }
    void resetEditRegion() {
        m_regionToEdit.reset();
        m_editRoleOpt.reset();
    }
    void startNewSelection(const QPointF& worldCoord);
    void stopSelection(const QPointF& selection);
    void cancelSelection(const QString& msg = "");
    // selection

    QPointF toWorldCoord(const QPoint&);
    void refreshRegion(const RegionPtr&);
    void removeRegion(const RegionPtr&);
};

}  // namespace FOEDAG
