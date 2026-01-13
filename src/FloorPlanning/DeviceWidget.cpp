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
#include "HierarhyElement.h"
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
 * - check all cases when region may contains overllaped tiles
 * - save/load qdc with mixed tiles types
 * - search multihierarhy
 * - fix selection
 * - post load, resize region works with some issue
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

void DeviceWidget::constuctTile(Tile::Type type, int col, int row)
{
    Tile::Index coord = toGridCoordIndex(type, Tile::Index{col, row});
    QSize size = m_device->elementSize(type);
    std::string label = getTileLabel(type, coord);
    label += "," + std::to_string(col) + ";" + std::to_string(row); // debug
    m_tiles[Tile::Index{col, row}] = Tile{type, col, row, size.width(), size.height(), QString::fromStdString(label)};
}

void DeviceWidget::constructTiles(const DeviceDescriptorPtr& device) {
    m_device = device;
    m_tiles.clear();

    bool step_on_bram = false;
    bool step_on_dsp = false;
    int stepCounter = 0;
    m_tiles.reserve(device->columns * device->rows);
    Tile::Index bottomLeftIndex;
    for (int col = 0; col < device->columns; ++col) {
        for (int row = 0; row < device->rows; ++row) {
            if (row == 0 || (row == device->rows -1) || col == 0 || (col == device->columns - 1)) {
                constuctTile(Tile::Type::Io, col, row);
            } else if (device->isBramColumn(col)) {
                if (!step_on_bram) {
                    constuctTile(Tile::Type::Bram, col, row);
                    bottomLeftIndex = Tile::Index(col, row);
                    step_on_bram = true;
                }
                if (step_on_bram) {
                    m_placeHolders[Tile::Index(col, row)] = bottomLeftIndex;
                    stepCounter++;
                    if (stepCounter == device->bramSize.height()) {
                        bottomLeftIndex.reset();
                        step_on_bram = false;
                        stepCounter= 0;
                    }
                }

            } else if (device->isDspColumn(col)) {
                if (!step_on_dsp) {
                    bottomLeftIndex = Tile::Index(col, row);
                    constuctTile(Tile::Type::Dsp, col, row);
                    step_on_dsp = true;
                }
                if (step_on_dsp) {
                    m_placeHolders[Tile::Index(col, row)] = bottomLeftIndex;
                    stepCounter++;
                    if (stepCounter == device->dspSize.height()) {
                        bottomLeftIndex.reset();
                        step_on_dsp = false;
                        stepCounter = 0;
                    }
                }
            } else {
                constuctTile(Tile::Type::Clb, col, row);
            }
        }
    }

    assert(!step_on_bram);
    assert(!step_on_dsp);

    update();
}

void DeviceWidget::onSelectedElementsChanged(const HierarhyElementsPtr& elements)
{
    m_selectedElements = elements;
    if (m_regionToEdit) {
        m_regionToEdit->setElements(elements);
        emit updateElementsSelectionRequested(m_regionToEdit->id(), m_regionToEdit->elements());
    }
}

bool DeviceWidget::trySelectRegionToEdit(const QPointF& worldCoord)
{
    if (m_regionToEdit) {
        resetEditRegion();
        return false;
    }

    for (const auto& [id, region]: m_regions) {
        if (region->rect().contains(worldCoord)) {
            m_regionToEdit = region;
            emit updateElementsSelectionRequested(region->id(), region->elements());
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

    m_currentRegion->accept(worldCoord, indexes, m_selectedElements);

    for (const auto& [id, region]: m_regions) {
        if (m_currentRegion->isOverllapedWith(*region)) {
            cancelRegion("Overllaped with other region");
            return;
        }
    }

    if (m_currentRegion->isValid()) {
        // selected pins are baked inside the m_currentRegion
        emit clearSelectionRequested();
        m_selectedElements.reset();
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
    auto resolvedElementName = [](const HierarhyElement& in)->std::string{
        std::string out{in.path};
        if (!in.isLeaf) {
            out += ".*";
        }
        return out;
    };
    constexpr std::string_view lineDelimeter = "\\\n";

    // preprocess to unite regions by same pin list
    std::map<std::string, std::string> data;
    for (const auto& [id, region]: m_regions) {
        if (!region->elements()) {
            continue;
        } if(region->elements()->empty()) {
            continue;
        }

        std::string elementsStr = "";
        int counter = 0;
        for (const HierarhyElement& element: *region->elements()) {
            elementsStr += resolvedElementName(element);
            counter++;
            if (counter < region->elements()->size()) {
                elementsStr += ",";
                elementsStr += lineDelimeter;
            }
        }

        Tile::Type bottomLeftType = tile(region->bottomLeftIndex()).type();
        Tile::Type topRightType = tile(region->topRightIndex()).type();
        Tile::Index bottomLeftIndexFixed = region->bottomLeftIndex();

        qInfo() << "region->bottomLeftIndex()=" << region->bottomLeftIndex().col << region->bottomLeftIndex().row;

        std::string bottomLeftStr = getTileLabel(bottomLeftType, toGridCoordIndex(bottomLeftType, bottomLeftIndexFixed));

        std::string topRightStr = getTileLabel(topRightType, toGridCoordIndex(topRightType, region->topRightIndex()));
        std::string regionStr{bottomLeftStr};
        if (topRightStr != bottomLeftStr) {
            regionStr += ":";
            regionStr += topRightStr;
        }
        if (data.find(elementsStr) != data.end()) {
            data[elementsStr] += "," + regionStr;
        } else {
            data[elementsStr] = regionStr;
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

    auto extractGridCoord = [](const std::string& idxStr, Tile::Type type)->std::optional<TileDescriptor> {
        std::vector<std::string> tokens = StringUtils::tokenize(idxStr, ",");
        if (tokens.size() == 2) {
            int col = std::stoi(tokens[0]);
            int row = std::stoi(tokens[1]);
            return TileDescriptor{Tile::Index(col, row), type};
        }
        return std::nullopt;
    };

    auto extractResolvedGridCoord = [&extractGridCoord](const std::string& data)->std::optional<TileDescriptor> {
        if (StringUtils::startsWith(data, "clb")) {
            std::string idxStr = StringUtils::extractWildcardSegment(data, "clb(*)");
            return extractGridCoord(idxStr, Tile::Type::Clb);
        } else if (StringUtils::startsWith(data, "bram")) {
            std::string idxStr = StringUtils::extractWildcardSegment(data, "bram(*)");
            return extractGridCoord(idxStr, Tile::Type::Bram);
        } else if (StringUtils::startsWith(data, "dsp")) {
            std::string idxStr = StringUtils::extractWildcardSegment(data, "dsp(*)");
            return extractGridCoord(idxStr, Tile::Type::Dsp);
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
                // extract elements
                std::vector<std::string> pathesDirty = StringUtils::tokenize(tokens[1], ",");
                HierarhyElementsPtr elements = std::make_shared<HierarhyElements>();
                for (std::string path: pathesDirty) {
                    if (StringUtils::endsWith(path, ".*")) {
                        StringUtils::removeSuffix(path, ".*");
                        elements->insert(HierarhyElement{path, false});
                    } else {
                        elements->insert(HierarhyElement{path, true});
                    }
                }
                // extract regions
                std::vector<std::string> regions = splitIgnoringParens(tokens[2], ',');
                for (const std::string& region: regions) {
                    std::vector<std::string> tokens = StringUtils::tokenize(region, ":");
                    if (tokens.size() == 1) {
                        // special case
                        std::optional<TileDescriptor> bottomLeftGridCoordTileDescriptorOpt = extractResolvedGridCoord(tokens[0]);
                        if (bottomLeftGridCoordTileDescriptorOpt) {
                            Tile::Index bottomLeftTileIndex = fromGridCoordIndex(bottomLeftGridCoordTileDescriptorOpt.value().type, bottomLeftGridCoordTileDescriptorOpt.value().index);
                            std::optional<QPointF> bottomLeftPointOpt = findBottomLeftPoint(bottomLeftTileIndex);
                            std::optional<QPointF> topRightPointOpt = findTopRightPoint(bottomLeftTileIndex);
                            if (bottomLeftPointOpt && topRightPointOpt) {
                                QPointF bottomLeftPoint = bottomLeftPointOpt.value() + 0.5*QPointF(-Tile::borderPx(), Tile::borderPx());
                                QPointF topRightPoint = topRightPointOpt.value()     + 0.5*QPointF(Tile::borderPx(), -Tile::borderPx());

                                RegionPtr region = std::make_shared<Region>(bottomLeftPoint);
                                std::unordered_set<Tile::Index> tiles = findTiles(QRectF(bottomLeftPoint, topRightPoint));
                                region->accept(topRightPoint, tiles, elements);
                                m_regions[region->id()] = region;
                            }
                        }

                    } else if (tokens.size() == 2) {
                        std::optional<TileDescriptor> bottomLeftGridCoordTileDescriptorOpt = extractResolvedGridCoord(tokens[0]);
                        std::optional<TileDescriptor> topRightGridCoordTileDescriptorOpt = extractResolvedGridCoord(tokens[1]);
                        if (bottomLeftGridCoordTileDescriptorOpt && topRightGridCoordTileDescriptorOpt) {
                            Tile::Index bottomLeftTileIndex = fromGridCoordIndex(bottomLeftGridCoordTileDescriptorOpt.value().type, bottomLeftGridCoordTileDescriptorOpt.value().index);
                            Tile::Index topRightTileIndex = fromGridCoordIndex(topRightGridCoordTileDescriptorOpt.value().type, topRightGridCoordTileDescriptorOpt.value().index);

                            std::optional<QPointF> bottomLeftPointOpt = findBottomLeftPoint(bottomLeftTileIndex);
                            std::optional<QPointF> topRightPointOpt = findTopRightPoint(topRightTileIndex);
                            if (bottomLeftPointOpt && topRightPointOpt) {
                                QPointF bottomLeftPoint = bottomLeftPointOpt.value() + 0.5*QPointF(-Tile::borderPx(), Tile::borderPx());
                                QPointF topRightPoint = topRightPointOpt.value()     + 0.5*QPointF(Tile::borderPx(), -Tile::borderPx());

                                RegionPtr region = std::make_shared<Region>(bottomLeftPoint);
                                std::unordered_set<Tile::Index> tiles = findTiles(QRectF(bottomLeftPoint, topRightPoint));
                                region->accept(topRightPoint, tiles, elements);
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
        emit regionsLoaded(m_regions);
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

    // draw selection
    if (m_isSelectingNewRegion) {
        if (m_currentRegion->isClosed()) {
            p.drawRect(m_currentRegion->rect());
        }
    }

    // draw regions
    for (const auto& [id, region]: m_regions) {
        if (m_regionToEdit && (region->id() == m_regionToEdit->id())) {
            continue;
        }
        p.drawRect(region->rect());
        highLightTilesInRegion(p, *region);
    }

    // region for edit
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

    // draw regions labels (id etc)
    QFont font;
    font.setPointSize(22);
    p.setFont(font);
    p.setPen(Qt::black);

    for (const auto& [id, region]: m_regions) {
        p.drawText(region->rect().center(), QString::number(region->id()));
    }
    //
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

std::unordered_set<Tile::Index> DeviceWidget::findTiles(const QRectF& rect, bool excludeIoTiles)
{
    std::unordered_set<Tile::Index> indexes;
    for (const auto& [index, tile]: m_tiles) {
        if (excludeIoTiles && (tile.type() == Tile::Type::Io)) {
            continue;
        }
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

std::string DeviceWidget::getTileLabel(Tile::Type type, const Tile::Index& index) const
{
    switch(type) {
    case Tile::Type::Clb:  return QString("clb(%1,%2)").arg(index.col).arg(index.row).toStdString(); break;
    case Tile::Type::Io:   return QString("io(%1,%2)").arg(index.col).arg(index.row).toStdString(); break;
    case Tile::Type::Bram: return QString("bram(%1,%2)").arg(index.col).arg(index.row).toStdString(); break;
    case Tile::Type::Dsp:  return QString("dsp(%1,%2)").arg(index.col).arg(index.row).toStdString(); break;
    case Tile::Type::Empty: return "";
    }
    return "";
}

Tile::Index DeviceWidget::toGridCoordIndex(Tile::Type type, const Tile::Index& index) const
{
    int col = index.col + 1;
    int row = m_device->rows - (index.row + tileUnitsHeight(type)) + 1;
    return Tile::Index{col, row};
}

Tile::Index DeviceWidget::fromGridCoordIndex(Tile::Type type, const Tile::Index& index) const
{
    int col = index.col - 1;
    int row = m_device->rows - (index.row + tileUnitsHeight(type)) + 1;
    return Tile::Index{col, row};
}

int DeviceWidget::tileUnitsHeight(Tile::Type type) const
{
    switch(type) {
    case Tile::Type::Io: return 1; break;
    case Tile::Type::Clb: return 1; break;
    case Tile::Type::Dsp: return m_device->dspSize.height(); break;
    case Tile::Type::Bram: return m_device->bramSize.height(); break;
    default: return 1;
    }
}

const Tile& DeviceWidget::tile(const Tile::Index& index) const
{
    Tile::Index bottomLeftIdx{index};
    if (auto it = m_placeHolders.find(index); it != m_placeHolders.end()) {
        bottomLeftIdx = it->second;
    }

    if (auto it = m_tiles.find(bottomLeftIdx); it != m_tiles.end()) {
        return it->second;
    }

    qCritical() << "tile wasn't found";
    return m_nullTile;
}

// unitests
// m_bottomLeftGridCoord = Tile::toGridCoord(bottomLeftIndex);
// m_topRightGridCoord   = Tile::toGridCoord(topRightIndex);

// // test
// Tile::Index bottomLeftIndexReversed = Tile::fromGridCoord(m_bottomLeftGridCoord);
// Tile::Index topRightIndexReversed = Tile::fromGridCoord(m_topRightGridCoord);

// assert(bottomLeftIndex.row == bottomLeftIndexReversed.row);
// assert(bottomLeftIndex.col == bottomLeftIndexReversed.col);
// assert(topRightIndex.row == topRightIndexReversed.row);
// assert(topRightIndex.col == topRightIndexReversed.col);
// //
//

} // namespace FOEDAG
