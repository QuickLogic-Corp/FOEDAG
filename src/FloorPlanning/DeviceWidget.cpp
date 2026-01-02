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

#include "DeviceWidget.h"
#include "Utils/TimerUtils.h"

#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QMessageBox>
#include <QDebug>

namespace FOEDAG {
	
DeviceWidget::DeviceWidget(QWidget* parent)
    : QWidget(parent)
{
  setAutoFillBackground(false);
  setMouseTracking(true);

  QPushButton* bnClearSelections = new QPushButton("x", this);
  QObject::connect(bnClearSelections, &QPushButton::clicked, this, &DeviceWidget::clearSelections);
}

QSize DeviceWidget::sizeHint() const {
  return QSize(800, 600);
}

QSize DeviceWidget::minimumSizeHint() const {
  return QSize(200, 200);
}

void DeviceWidget::constructTiles(const DeviceDescriptorPtr& device) {
    m_device = device;
    m_tiles.clear();

    bool step_on_bram = false;
    bool step_on_dsp = false;
    int stepCounter = 0;
    m_tiles.reserve(device->columns * device->rows);
    for (int col = 0; col < device->columns; ++col) {
        for (int row = 0; row < device->rows; ++row) {
            if (row == 0 || row == device->rows -1 || col == 0 || col == device->columns - 1) {
                m_tiles[Tile::Index{col, row}] = Tile{Tile::Type::Io, col, row, 1, 1};
            } else if (device->bramColumns.find(col) != device->bramColumns.end()) {
                if (!step_on_bram) {
                    m_tiles[Tile::Index{col, row}] = Tile{Tile::Type::Bram, col, row, device->bramSize.width(), device->bramSize.height()};
                    step_on_bram = true;
                }
                if (step_on_bram) {
                    stepCounter++;
                    if (stepCounter == device->bramSize.height()) {
                        step_on_bram = false;
                        stepCounter= 0;
                    }
                }

            } else if (device->dspColumns.find(col) != device->dspColumns.end()) {
                if (!step_on_dsp) {
                    m_tiles[Tile::Index{col, row}] = Tile{Tile::Type::Dsp, col, row, device->dspSize.width(), device->dspSize.height()};
                    step_on_dsp = true;
                }
                if (step_on_dsp) {
                    stepCounter++;
                    if (stepCounter == device->dspSize.height()) {
                        step_on_dsp = false;
                        stepCounter= 0;
                    }
                }
            } else {
                m_tiles[Tile::Index{col, row}] = Tile{Tile::Type::Clb, col, row, 1, 1};
            }
        }
    }

    assert(!step_on_bram);
    assert(!step_on_dsp);

    update();
}

void DeviceWidget::startSelection(const QPoint& screenCoord)
{
    m_isSelecting = true;
    m_currentRegion.start(toWorldCoord(screenCoord));
    update();
}

void DeviceWidget::stopSelection(const QPoint& pos)
{
    m_isSelecting = false;

    std::unordered_set<Tile::Index> indexes;
    QRectF outer(m_currentRegion.startPos(), m_currentRegion.stopPos());
    for (const auto& [index, tile]: m_tiles) {
        if (tile.isLocated(outer)) {
            indexes.insert(tile.index());
        }
    }

    if (indexes.empty()) {
        cancelSelection("Selection doesn't contains any tiles. Current selection removed..");
        return;
    }

    m_currentRegion.accept(toWorldCoord(pos), indexes);
    for (const Region& region: m_regions) {
        if (m_currentRegion.isOverllapedWith(region)) {
            cancelSelection("Selected region is overllaped with other region. Current selection removed.");
            return;
        }
    }

    m_regions.push_back(m_currentRegion);
    update();
}

void DeviceWidget::cancelSelection(const QString& msg)
{
    QMessageBox::information(this,
                             "Selection canceled",
                             msg);
    m_currentRegion.reject();
    update();
}

void DeviceWidget::keyPressEvent(QKeyEvent* event)
{
    qInfo() << "todo: DeviceWidget::keyPressEvent";
}

void DeviceWidget::mousePressEvent(QMouseEvent* event)
{
    switch(event->button()) {
    case Qt::LeftButton: {
        startSelection(event->pos());
        break;
    }
    case Qt::MiddleButton:
    case Qt::RightButton: {
        startPanning(event->pos());
        break;
    }
    default: break;
    }
}

void DeviceWidget::mouseReleaseEvent(QMouseEvent* event) {
    switch(event->button()) {
    case Qt::LeftButton: {
        stopSelection(event->pos());
        break;
    }
    case Qt::MiddleButton:
    case Qt::RightButton: {
        stopPanning();
        break;
    }
    default: break;
    }
}

void DeviceWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isPanning) {
        QPointF delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();

        m_panPixels -= delta;
        update();
    }
    if (m_isSelecting) {
        m_currentRegion.setStopPos(toWorldCoord(event->pos()));
        update();
    }
}

void DeviceWidget::wheelEvent(QWheelEvent* event)
{
    const double factor = (event->angleDelta().y() > 0) ? 1.15 : 1.0 / 1.15;

    QPointF mouse = event->pos();
    QPointF before = (mouse + m_panPixels) / m_scale;

    m_scale = qBound(0.2, m_scale * factor, 10.0);

    QPointF after = before * m_scale;
    m_panPixels = after - mouse;

    update();
}

void DeviceWidget::startPanning(const QPoint& pos)
{
    m_isPanning = true;
    m_lastMousePos = pos;
    setCursor(Qt::ClosedHandCursor);
}

void DeviceWidget::stopPanning()
{
    m_isPanning = false;
    unsetCursor();
}

void DeviceWidget::paintEvent(QPaintEvent* event) 
{
  ScopedTimer st("DeviceWidget::paintEvent");

  Q_UNUSED(event);

  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::TextAntialiasing, true);

  p.save();

  p.translate(-m_panPixels);
  p.scale(m_scale, m_scale);

  m_viewPort = QRectF(
      m_panPixels.x() / m_scale,
      m_panPixels.y() / m_scale,
      width()     / m_scale,
      height()    / m_scale
      );

  // ---- Draw tile fills (optional) ----
  // Only draw labels if tiles are large enough
  // const bool drawText = (s >= 22.0);
  const bool isDrawingText = true;

  drawTilesBatched(p);
  drawSelectedTile(p);

   p.restore();

  // if (isDrawingText) {
  //      p.save();
  //      p.scale(1/m_scale, 1/m_scale);
  //      drawTileLabels(p, visibleIndexesArea);
  //      p.restore();
  // }
}

void DeviceWidget::drawBackground(QPainter& p)
{
  p.fillRect(rect(), m_backgroundColor);
}

void DeviceWidget::drawTilesBatched(QPainter& p)
{
    QVector<QRectF> clbRects, ioRects, bramRects, dspRects;
    // todo: calc num properly based on available resources
    // int num = m_device->columns * m_device->rows;
    // clbRects.reserve(num);
    // ioRects.reserve(num);
    // bramRects.reserve(num);
    // dspRects.reserve(num);
    //

    for (const auto& [index, tile]: m_tiles) {
        if (!tile.isVisible(m_viewPort)) {
            continue;
        }
        switch (tile.type()) {
        case Tile::Type::Clb:  clbRects.append(tile.rect()); break;
        case Tile::Type::Io:   ioRects.append(tile.rect()); break;
        case Tile::Type::Bram: bramRects.append(tile.rect()); break;
        case Tile::Type::Dsp:  dspRects.append(tile.rect()); break;
        default: break;
        }
    }

    qInfo() << "draw rectangles num" << clbRects.size() + ioRects.size() + bramRects.size() + dspRects.size();
    p.setPen(Qt::NoPen);

    p.setBrush(Tile::color(Tile::Type::Clb));  p.drawRects(clbRects);
    p.setBrush(Tile::color(Tile::Type::Io));   p.drawRects(ioRects);
    p.setBrush(Tile::color(Tile::Type::Bram)); p.drawRects(bramRects);
    p.setBrush(Tile::color(Tile::Type::Dsp));  p.drawRects(dspRects);
}

void DeviceWidget::drawSelectedTile(QPainter& p)
{
    QPen pen(m_selectedColor);
    pen.setCosmetic(true);
    pen.setWidth(3);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    if (m_isSelecting) {
        if (m_currentRegion.isClosed()) {
            QRectF selectionRect(m_currentRegion.startPos(), m_currentRegion.stopPos());
            p.drawRect(selectionRect.adjusted(1, 1, -1, -1));
        }
    }

    for (const Region& area: m_regions) {
        QRectF selectionRect(area.startPos(), area.stopPos());
        p.drawRect(selectionRect.adjusted(1, 1, -1, -1));

        highLightTilesInArea(p, area);
    }
}

void DeviceWidget::highLightTilesInArea(QPainter& p, const Region& area) const {
    if (!area.isValid()) {
        return;
    }

    for (const Tile::Index& index: area.tiles()) {
        p.drawRect(m_tiles.at(index).rect().adjusted(1, 1, -1, -1));
    }
}

void DeviceWidget::drawTileLabels(QPainter& p)
{
    p.setPen(m_textColor);

    for (const auto& [index, tile]: m_tiles) {
        if (!tile.isVisible(m_viewPort)) {
            continue;
        }
        p.drawText(tile.rect().adjusted(2, 1, -2, -1), Qt::AlignCenter | Qt::AlignBottom, tile.label());
    }
}

QPointF DeviceWidget::toWorldCoord(const QPoint& screenCoord)
{
    return (QPointF(screenCoord) + m_panPixels) / m_scale;
}

} // namespace FOEDAG
