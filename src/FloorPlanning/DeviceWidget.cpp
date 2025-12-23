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
#include <QtMath>

#include <QDebug>

namespace FOEDAG {
	
DeviceWidget::DeviceWidget(QWidget* parent)
    : QWidget(parent)
{
  setAutoFillBackground(false);
  setMouseTracking(true);
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
                m_tiles.emplace_back(Tile{Tile::Type::Io, col, row, 1, 1});
            } else if (device->bramColumns.find(col) != device->bramColumns.end()) {
                if (!step_on_bram) {
                    m_tiles.emplace_back(Tile{Tile::Type::Bram, col, row, device->bramSize.width(), device->bramSize.height()});
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
                    m_tiles.emplace_back(Tile{Tile::Type::Dsp, col, row, device->dspSize.width(), device->dspSize.height()});
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
                m_tiles.emplace_back(Tile{Tile::Type::Clb, col, row, 1, 1});
            }
        }
    }

    assert(!step_on_bram);
    assert(!step_on_dsp);

    update();
}

void DeviceWidget::setSelected(int x, int y) {
  m_selected = {x, y};
  update();
}

void DeviceWidget::keyPressEvent(QKeyEvent* event)
{
  qInfo() << "todo: DeviceWidget::keyPressEvent";
}

void DeviceWidget::mousePressEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton) {
    // screen to tile-pixel space
    // const double s = m_pxPerTile;
    // const double tilePxX = event->pos().x() + m_panPx.x();
    // const double tilePxY = event->pos().y() + m_panPx.y();

    // int tx = int(tilePxX / s);
    // int ty = int(tilePxY / s);

    // if (tx < 0 || ty < 0 || tx >= m_device->columns || ty >= m_device->rows) {
    //   return;
    // }

    // setSelected(tx, ty);
  } else if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
    m_panning = true;
    m_lastMouse = event->pos();
    setCursor(Qt::ClosedHandCursor);
  }
}

void DeviceWidget::mouseMoveEvent(QMouseEvent* event)
{
  if (!m_panning) {
    return;
  }

  QPointF delta = event->pos() - m_lastMouse;
  m_lastMouse = event->pos();

  m_panPx -= delta;
  update();
}

void DeviceWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
    m_panning = false;
    unsetCursor();
  }
}

void DeviceWidget::wheelEvent(QWheelEvent* event)
{
  const double factor = (event->angleDelta().y() > 0) ? 1.15 : 1.0 / 1.15;

  QPointF mouse = event->pos();
  QPointF before = (mouse + m_panPx) / m_scale;

  m_scale = qBound(0.2, m_scale * factor, 10.0);

  QPointF after = before * m_scale;
  m_panPx = after - mouse;

  update();
}

void DeviceWidget::paintEvent(QPaintEvent* event) 
{
  ScopedTimer st("DeviceWidget::paintEvent");

  Q_UNUSED(event);

  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::TextAntialiasing, true);

  p.save();

  p.translate(-m_panPx);
  p.scale(m_scale, m_scale);

  m_viewPort = QRectF(
      m_panPx.x() / m_scale,
      m_panPx.y() / m_scale,
      width()     / m_scale,
      height()    / m_scale
      );

  // ---- Draw tile fills (optional) ----
  // Only draw labels if tiles are large enough
  // const bool drawText = (s >= 22.0);
  const bool isDrawingText = true;

  drawTilesBatched(p);
  drawGrid(p);
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

    for (const Tile& tile: m_tiles) {
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

void DeviceWidget::drawGrid(QPainter& p)
{
    //   QPen pen(QColor(55, 55, 62));
    //   pen.setCosmetic(true);
    //   p.setPen(pen);

    //   // Vertical lines
    //   for (int x = x0; x <= x1; ++x) {
    //     const double X = screenX(x * s);
    //     p.drawLine(QPointF(X, screenY(y0 * s)), QPointF(X, screenY(y1 * s)));
    //   }
    //   // Horizontal lines
    //   for (int y = y0; y <= y1; ++y) {
    //     const double Y = screenY(y * s);
    //     p.drawLine(QPointF(screenX(x0 * s), Y), QPointF(screenX(x1 * s), Y));
    //   }
}

void DeviceWidget::drawSelectedTile(QPainter& p)
{
    // // ---- Selected tile ----
    // if (m_selected.x() >= 0 && m_selected.y() >= 0 &&
    //     m_selected.x() < m_device->columns && m_selected.y() < m_device->rows) {

        //   QRectF r(screenX(m_selected.x() * s), screenY(m_selected.y() * s), s, s);

    //   QPen pen(QColor(255, 220, 120));
    //   pen.setCosmetic(true);
    //   pen.setWidth(3);
    //   p.setPen(pen);
    //   p.setBrush(Qt::NoBrush);
    //   p.drawRect(r.adjusted(1, 1, -1, -1));
    // }
}

void DeviceWidget::drawTileLabels(QPainter& p)
{
    p.setPen(m_textColor);

    for (const Tile& tile: m_tiles) {
        if (!tile.isVisible(m_viewPort)) {
            continue;
        }
        p.drawText(tile.rect().adjusted(2, 1, -2, -1), Qt::AlignCenter | Qt::AlignBottom, tile.label());
    }
}

} // namespace FOEDAG
