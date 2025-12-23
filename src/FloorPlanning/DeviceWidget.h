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

    void setSelected(int x, int y);
    void clearSelected();

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

    bool m_panning{false};
    QPointF m_lastMouse;

    double m_scale = 1.0f;
    QPointF m_panPx = {0.0, 0.0};       // pan in pixels

    QPoint m_selected = {1, 10};

    QRectF m_viewPort;
    std::vector<Tile> m_tiles;
    DeviceDescriptorPtr m_device;

    void drawBackground(QPainter& p);
    void drawTilesBatched(QPainter& p);
    void drawGrid(QPainter& p);
    void drawSelectedTile(QPainter& p);
    void drawTileLabels(QPainter& p);
};

}  // namespace FOEDAG
