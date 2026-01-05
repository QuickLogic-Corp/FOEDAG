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
                        stepCounter = 0;
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

void DeviceWidget::onSelectedPinsChanged(const std::set<std::string>& pins)
{
    m_selectedPins = pins;
    if (m_regionToEdit) {
        m_regionToEdit->setPins(pins);
    }
}

bool DeviceWidget::trySelectRegionToEdit(const QPointF& worldCoord)
{
    resetEditRegion();
    for (const auto& [id, region]: m_regions) {
        if (region->rect().contains(worldCoord)) {
            m_regionToEdit = region;
            emit updatePinsSelectionRequested(region->pins());
            update();
            return true;
        }
    }
    return false;
}

void DeviceWidget::startNewSelection(const QPointF& worldCoord)
{
    m_isSelectingNewRegion = true;
    m_currentRegion = std::make_shared<Region>(worldCoord);
    update();
}

void DeviceWidget::stopSelection(const QPointF& worldCoord)
{
    m_isSelectingNewRegion = false;

    if (!m_currentRegion) {
        return;
    }

    std::unordered_set<Tile::Index> indexes;
    for (const auto& [index, tile]: m_tiles) {
        if (tile.isLocated(m_currentRegion->rect())) {
            indexes.insert(tile.index());
        }
    }

    if (indexes.empty()) {
        cancelSelection();
        return;
    }

    m_currentRegion->accept(worldCoord, indexes, m_selectedPins);

    for (const auto& [id, region]: m_regions) {
        if (m_currentRegion->isOverllapedWith(*region)) {
            cancelSelection("Overllaped with other region");
            return;
        }
    }

    if (m_currentRegion->isValid()) {
        // selected pins are baked inside the m_currentRegion
        emit updatePinsSelectionRequested({});
        m_selectedPins.clear();
        //

        m_regions[m_currentRegion->id()] = m_currentRegion;
        m_currentRegion.reset();
        update();
    }
}

void DeviceWidget::cancelSelection(const QString& msg)
{
    if (!m_currentRegion) {
        return;
    }
    if (!msg.isEmpty()) {
    QMessageBox::information(this,
                             "Selection declined",
                             msg);
    }

    m_currentRegion->reject();
    update();
}

void DeviceWidget::keyPressEvent(QKeyEvent* event)
{
    qInfo() << "todo: DeviceWidget::keyPressEvent";
}

void DeviceWidget::mousePressEvent(QMouseEvent* event)
{
    m_isMousePressed = true;

    const QPointF worldCoord{toWorldCoord(event->pos())};
    switch(event->button()) {
    case Qt::LeftButton: {
        if (m_regionToEdit) {
            std::optional<Region::HandlerRole> roleOpt = m_regionToEdit->checkHandlerClick(worldCoord);
            if (roleOpt) {
                m_editRoleOpt = roleOpt;
                return;
            }
        }

        if (trySelectRegionToEdit(worldCoord)) {
            return;
        }

        startNewSelection(worldCoord);
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
    m_isMousePressed = false;

    const QPointF worldCoord{toWorldCoord(event->pos())};
    switch(event->button()) {
    case Qt::LeftButton: {
        stopSelection(worldCoord);
        break;
    }
    case Qt::MiddleButton:
    case Qt::RightButton: {
        stopPanning();
        break;
    }
    default: break;
    }

    if (m_regionToEdit && m_editRoleOpt) {
        if (m_editRoleOpt == Region::HandlerRole::REMOVE) {
            removeRegion(m_regionToEdit);
            resetEditRegion();
            update();
        }
    }
}

void DeviceWidget::removeRegion(const RegionPtr& region)
{
    m_regions.erase(m_regions.find(region->id()));
}

void DeviceWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF worldCoord = toWorldCoord(event->pos());

    if (m_isPanning) {
        QPointF delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();

        m_panPixels -= delta;
        update();
        return;
    }
    if (m_isSelectingNewRegion) {
        m_currentRegion->setStopPos(worldCoord);
        update();
        return;
    }
    if (m_isMousePressed && m_regionToEdit && m_editRoleOpt) {
        switch (m_editRoleOpt.value()) {
        case Region::HandlerRole::BL: m_regionToEdit->rect().setBottomLeft(worldCoord); break;
        case Region::HandlerRole::BR: m_regionToEdit->rect().setBottomRight(worldCoord); break;
        case Region::HandlerRole::TR: m_regionToEdit->rect().setTopRight(worldCoord); break;
        case Region::HandlerRole::TL: m_regionToEdit->rect().setTopLeft(worldCoord); break;
        case Region::HandlerRole::MOVE: m_regionToEdit->rect().moveCenter(worldCoord); break;
        default: break;
        }

        refreshRegion(m_regionToEdit);
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
  drawRegions(p);

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

void DeviceWidget::drawRegions(QPainter& p)
{
    QPen pen(m_regionColor);
    pen.setCosmetic(true);
    pen.setWidth(3);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    if (m_isSelectingNewRegion) {
        if (m_currentRegion->isClosed()) {
            p.drawRect(m_currentRegion->rect());
        }
    }

    for (const auto& [id, region]: m_regions) {
        if (m_regionToEdit && (region->id() == m_regionToEdit->id())) {
            continue;
        }
        p.drawRect(region->rect());
        highLightTilesInRegion(p, *region);
    }

    if (m_regionToEdit) {
        pen.setColor(m_editRegionColor);
        p.setPen(pen);
        p.drawRect(m_regionToEdit->rect());
        highLightTilesInRegion(p, *m_regionToEdit);

        p.setPen(Qt::NoPen);
        p.setBrush(m_editRegionTransparentColor);
        for (const auto& [role, rect]: m_regionToEdit->handlers) {
            p.drawRect(rect);
        }
    }
}

void DeviceWidget::highLightTilesInRegion(QPainter& p, const Region& region) const {
    if (!region.isValid()) {
        return;
    }

    for (const Tile::Index& index: region.tiles()) {
        p.drawRect(m_tiles.at(index).rect());
    }
}

void DeviceWidget::drawTileLabels(QPainter& p)
{
    p.setPen(m_textColor);

    for (const auto& [index, tile]: m_tiles) {
        if (!tile.isVisible(m_viewPort)) {
            continue;
        }
        p.drawText(tile.rect(), Qt::AlignCenter | Qt::AlignBottom, tile.label());
    }
}

QPointF DeviceWidget::toWorldCoord(const QPoint& screenCoord)
{
    return (QPointF(screenCoord) + m_panPixels) / m_scale;
}

void DeviceWidget::refreshRegion(const RegionPtr& region)
{
    region->buildHandles();

    std::unordered_set<Tile::Index> indexes;
    for (const auto& [index, tile]: m_tiles) {
        if (tile.isLocated(region->rect())) {
            indexes.insert(tile.index());
        }
    }

    region->setTiles(indexes);
}

} // namespace FOEDAG
