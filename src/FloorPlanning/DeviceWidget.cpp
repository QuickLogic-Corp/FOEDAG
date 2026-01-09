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
#include "Utils/FileUtils.h"
#include "Utils/StringUtils.h"

#include <QElapsedTimer>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QDebug>

/*
 * TODO:
 * - io tiles shoould be exluded from region based assignment
 * - check all cases when region may contains overllaped tiles
 */
namespace FOEDAG {

DeviceWidget::DeviceWidget(QWidget* parent)
    : QWidget(parent)
{
  setAutoFillBackground(false);
  setMouseTracking(true);

  QHBoxLayout* layout = new QHBoxLayout;
  QWidget* container = new QWidget(this);
  container->setLayout(layout);

  QPushButton* bnClearSelections = new QPushButton("x");
  bnClearSelections->setFixedSize(20,20);
  QObject::connect(bnClearSelections, &QPushButton::clicked, this, &DeviceWidget::clearRegions);

  QPushButton* bnSaveQdc = new QPushButton("save qdc");
  QObject::connect(bnSaveQdc, &QPushButton::clicked, this, &DeviceWidget::saveQdc);

  QPushButton* bnLoadQdc = new QPushButton("load qdc");
  QObject::connect(bnLoadQdc, &QPushButton::clicked, this, &DeviceWidget::loadQdc);

  layout->addWidget(bnClearSelections);
  layout->addWidget(bnSaveQdc);
  layout->addWidget(bnLoadQdc);
  layout->addWidget(m_drawStat.label());
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

void DeviceWidget::onSelectedElementsChanged(const std::set<std::string>& pins)
{
    m_selectedPins = pins;
    if (m_regionToEdit) {
        m_regionToEdit->setPins(pins);
        emit updateElementsSelectionRequested(m_regionToEdit->id(), m_regionToEdit->pins());
    }
}

bool DeviceWidget::trySelectRegionToEdit(const QPointF& worldCoord)
{
    if (m_regionToEdit) {
        resetEditRegion();
    }

    for (const auto& [id, region]: m_regions) {
        if (region->rect().contains(worldCoord)) {
            m_regionToEdit = region;
            emit updateElementsSelectionRequested(region->id(), region->pins());
            update();
            return true;
        }
    }
    return false;
}

void DeviceWidget::startNewRegion(const QPointF& worldCoord)
{
    m_isSelectingNewRegion = true;
    m_currentRegion = std::make_shared<Region>(worldCoord);
    update();
}

void DeviceWidget::stopRegion(const QPointF& worldCoord)
{
    m_isSelectingNewRegion = false;

    if (!m_currentRegion) {
        return;
    }

    std::unordered_set<Tile::Index> indexes = findTiles(m_currentRegion->rect());
    if (indexes.empty()) {
        cancelRegion();
        return;
    }

    m_currentRegion->accept(worldCoord, indexes, m_selectedPins);

    for (const auto& [id, region]: m_regions) {
        if (m_currentRegion->isOverllapedWith(*region)) {
            cancelRegion("Overllaped with other region");
            return;
        }
    }

    if (m_currentRegion->isValid()) {
        // selected pins are baked inside the m_currentRegion
        emit clearSelectionRequested();
        m_selectedPins.clear();
        //

        m_regions[m_currentRegion->id()] = m_currentRegion;
        m_currentRegion.reset();
        update();
    }
}

void DeviceWidget::cancelRegion(const QString& msg)
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

        startNewRegion(worldCoord);
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
        stopRegion(worldCoord);
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

void DeviceWidget::saveQdc()
{
    auto correctedPinName = [](const std::string& in)->std::string{
        std::string out{in};
        out += ".*";
        return out;
    };
    constexpr std::string_view lineDelimeter = "\\\n";

    // preprocess to unite regions by same pin list
    std::map<std::string, std::string> data;
    for (const auto& [id, region]: m_regions) {
        if (region->pins().empty()) {
            continue;
        }

        std::string pinsStr = "";
        int counter = 0;
        for (const std::string& pin: region->pins()) {
            pinsStr += correctedPinName(pin);
            counter++;
            if (counter < region->pins().size()) {
                pinsStr += ",";
                pinsStr += lineDelimeter;
            }
        }

        std::string regionStr = region->bottomLeftGridCoord().toClbString();
        regionStr += ":";
        regionStr += region->topRightGridCoord().toClbString();
        if (data.find(pinsStr) != data.end()) {
            data[pinsStr] += "," + regionStr;
        } else {
            data[pinsStr] = regionStr;
        }
    }
    // end preprocess

    std::string content;
    for (const auto& [pinsStr, regionsStr]: data) {
        std::string line = "set_region ";
        line += lineDelimeter;

        line += pinsStr;

        line += " ";
        line += lineDelimeter;

        line += regionsStr;

        line += "\n\n";

        content += line;
    }

    FileUtils::WriteToFile(m_qdcFilepath, content);
}

void DeviceWidget::loadQdc()
{
    resetEditRegion();
    clearRegions();

    std::string content = FileUtils::GetFileContent(m_qdcFilepath);

    constexpr std::string_view lineDelimeter = "\\\n";

    content = StringUtils::replaceAll(content, "  ", " ");
    content = StringUtils::replaceAll(content, lineDelimeter, "");
    //qInfo() << QString::fromStdString(content);

    auto extractGridCoord = [](const std::string& idxStr)->std::optional<Tile::Index> {
        std::vector<std::string> tokens = StringUtils::tokenize(idxStr, ",");
        if (tokens.size() == 2) {
            int col = std::stoi(tokens[0]);
            int row = std::stoi(tokens[1]);
            return Tile::Index(col, row);
        }
        return std::nullopt;
    };

    auto extractResolvedGridCoord = [&extractGridCoord](const std::string& data)->std::optional<Tile::Index> {
        if (StringUtils::startsWith(data, "clb")) {
            std::string idxStr = StringUtils::extractWildcardSegment(data, "clb(*)");
            return extractGridCoord(idxStr);
        } else if (StringUtils::startsWith(data, "bram")) {
            assert(false && "TODO");
        } else if (StringUtils::startsWith(data, "dsp")) {
            assert(false && "TODO");
        }
        return std::nullopt;
    };

    auto splitIgnoringParens = [](const std::string& s, char delimiter)->std::vector<std::string> {
        std::vector<std::string> result;
        std::string current;

        int depth = 0;

        for (char c : s) {
            if (c == '(') {
                depth++;
                current += c;
            } else if (c == ')') {
                depth--;
                current += c;
            } else if (c == delimiter && depth == 0) {
                result.push_back(current);
                current.clear();
            } else {
                current += c;
            }
        }

        if (!current.empty()) {
            result.push_back(current);
        }

        return result;
    };

    std::vector<std::string> lines = StringUtils::tokenize(content, "\n\n");
    for (const std::string& line: lines) {
        if (StringUtils::startsWith(line, "set_region")) {
            std::vector<std::string> tokens = StringUtils::tokenize(line, " ");
            if (tokens.size() == 3) {
                // extract pins
                std::vector<std::string> pinsDirty = StringUtils::tokenize(tokens[1], ",");
                std::set<std::string> pins;
                for (std::string pin: pinsDirty) {
                    StringUtils::removeSuffix(pin, ".*");
                    pins.insert(pin);
                }
                // extract regions
                std::vector<std::string> regions = splitIgnoringParens(tokens[2], ',');
                for (const std::string& region: regions) {
                    std::vector<std::string> tokens = StringUtils::tokenize(region, ":");
                    if (tokens.size() == 1) {
                        assert(false && "TODO");
                        // special case
                    } else if (tokens.size() == 2) {
                        std::optional<Tile::Index> bottomLeftGridCoordOpt = extractResolvedGridCoord(tokens[0]);
                        std::optional<Tile::Index> topRightGridCoordOpt = extractResolvedGridCoord(tokens[1]);
                        if (bottomLeftGridCoordOpt && topRightGridCoordOpt) {
                            Tile::Index bottomLeftTileIndex = Tile::fromGridCoord(bottomLeftGridCoordOpt.value());
                            Tile::Index topRightTileIndex = Tile::fromGridCoord(topRightGridCoordOpt.value());

                            std::optional<QPointF> bottomLeftPointOpt = findBottomLeftPoint(bottomLeftTileIndex);
                            std::optional<QPointF> topRightPointOpt = findTopRightPoint(topRightTileIndex);
                            if (bottomLeftPointOpt && topRightPointOpt) {
                                QPointF bottomLeftPoint = bottomLeftPointOpt.value() + 0.5*QPointF(-Tile::borderPx(), Tile::borderPx());
                                QPointF topRightPoint = topRightPointOpt.value()     + 0.5*QPointF(Tile::borderPx(), -Tile::borderPx());

                                RegionPtr region = std::make_shared<Region>(bottomLeftPoint);
                                std::unordered_set<Tile::Index> tiles = findTiles(QRectF(bottomLeftPoint, topRightPoint));
                                region->accept(topRightPoint, tiles, pins);
                                m_regions[region->id()] = region;
                            } else {
                                qCritical() << "syntax error for regions" << QString::fromStdString(region) << "coudn't extract start and end points";
                            }
                        } else {
                            qCritical() << "syntax error for regions" << QString::fromStdString(region) << "coudn't extract bottomLeft or topRight indexes";
                        }
                    } else {
                        qCritical() << "syntax error for regions" << QString::fromStdString(region);
                    }
                }
            }
        }
    }

    if (!m_regions.empty()) {
        update();
    }
}

void DeviceWidget::paintEvent(QPaintEvent* event) 
{
  QElapsedTimer t;
  t.start();

  Q_UNUSED(event);

  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::TextAntialiasing, true);

  p.translate(-m_panPixels);
  p.scale(m_scale, m_scale);

  m_viewPort = QRectF(
      m_panPixels.x() / m_scale,
      m_panPixels.y() / m_scale,
      width()     / m_scale,
      height()    / m_scale
      );

  drawTilesBatched(p);
  drawRegions(p);

  const bool isDrawingText = true;
  if (isDrawingText) {
    drawTileLabels(p);
  }

  m_drawStat.setDrawTimeMs(t.elapsed());
}

void DeviceWidget::drawBackground(QPainter& p)
{
  p.fillRect(rect(), m_backgroundColor);
}

void DeviceWidget::drawTilesBatched(QPainter& p)
{
    QVector<QRectF> clbRects, ioRects, bramRects, dspRects;
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

    m_drawStat.setDrawableTilesNum(clbRects.size() + ioRects.size() + bramRects.size() + dspRects.size());
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
    QFont font;
    font.setPointSize(4);
    p.setFont(font);

    for (const auto& [index, tile]: m_tiles) {
        if (!tile.isVisible(m_viewPort)) {
            continue;
        }
        p.drawText(tile.rect(), Qt::AlignCenter | Qt::AlignCenter, tile.label());
    }
}

QPointF DeviceWidget::toWorldCoord(const QPoint& screenCoord)
{
    return (QPointF(screenCoord) + m_panPixels) / m_scale;
}

void DeviceWidget::refreshRegion(const RegionPtr& region)
{
    region->buildHandles();
    region->setTiles(findTiles(region->rect()));
}

std::unordered_set<Tile::Index> DeviceWidget::findTiles(const QRectF& rect)
{
    std::unordered_set<Tile::Index> indexes;
    for (const auto& [index, tile]: m_tiles) {
        if (tile.isLocated(rect)) {
            indexes.insert(tile.index());
        }
    }
    return indexes;
}

std::optional<QPointF> DeviceWidget::findBottomLeftPoint(const Tile::Index& idx) const
{
    auto it = m_tiles.find(idx);
    if (it != m_tiles.end()) {
        const Tile& t = it->second;
        return t.rect().bottomLeft();
    }

    return std::nullopt;
}

std::optional<QPointF> DeviceWidget::findTopRightPoint(const Tile::Index& idx) const
{
    auto it = m_tiles.find(idx);
    if (it != m_tiles.end()) {
        const Tile& t = it->second;
        return t.rect().topRight();
    }

    return std::nullopt;
}

} // namespace FOEDAG
