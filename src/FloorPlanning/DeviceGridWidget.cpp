#include "DeviceGridWidget.h"
#include "HierarhyElement.h"
#include "QdcSerializer.h"

#include <QElapsedTimer>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QDebug>

namespace fp {

DeviceGridWidget::DeviceGridWidget(QWidget* parent)
    : QWidget(parent)
{
  setAutoFillBackground(false);
  setMouseTracking(true);

  QHBoxLayout* layout = new QHBoxLayout;
  QWidget* container = new QWidget(this);
  container->setLayout(layout);

  QPushButton* bnClearSelections = new QPushButton("x");
  bnClearSelections->setFixedSize(20,20);
  QObject::connect(bnClearSelections, &QPushButton::clicked, this, &DeviceGridWidget::clearRegions);

  QPushButton* bnSaveQdc = new QPushButton("save qdc");
  QObject::connect(bnSaveQdc, &QPushButton::clicked, this, &DeviceGridWidget::saveQdc);

  QPushButton* bnLoadQdc = new QPushButton("load qdc");
  QObject::connect(bnLoadQdc, &QPushButton::clicked, this, &DeviceGridWidget::loadQdc);

  layout->addWidget(bnClearSelections);
  layout->addWidget(bnSaveQdc);
  layout->addWidget(bnLoadQdc);
  layout->addWidget(m_drawStat.label());
}

QSize DeviceGridWidget::sizeHint() const {
  return QSize(800, 600);
}

QSize DeviceGridWidget::minimumSizeHint() const {
  return QSize(200, 200);
}

void DeviceGridWidget::constructTiles(const DeviceGridDescriptorPtr& descriptor)
{
    m_device.constructTiles(descriptor);
    update();
}

void DeviceGridWidget::onSelectedElementsChanged(const HierarhyElementsPtr& elements)
{
    m_selectedElements = elements;
    if (m_regionToEdit) {
        m_regionToEdit->setElements(elements);
        emit regionSelected(m_regionToEdit);
    }
}

bool DeviceGridWidget::trySelectRegionToEdit(const QPointF& worldCoord)
{
    if (m_regionToEdit) {
        exitRegionEdit();
        return false;
    }

    if (RegionPtr region = m_device.findRegion(worldCoord); region) {
        m_regionToEdit = region;
        emit regionSelected(region);
        update();
        return true;
    } else {
        return false;
    }
}

void DeviceGridWidget::startNewRegion(const QPointF& worldCoord)
{
    m_isSelectingNewRegion = true;
    m_currentRegion = std::make_shared<Region>(worldCoord);
    update();
}

void DeviceGridWidget::stopRegion(const QPointF& worldCoord)
{
    m_isSelectingNewRegion = false;

    if (!m_currentRegion) {
        return;
    }

    std::unordered_set<Tile::Index> indexes = m_device.findTiles(m_currentRegion->rect());
    if (indexes.empty()) {
        cancelRegion();
        return;
    }

    m_currentRegion->accept(worldCoord, indexes, m_selectedElements);

    for (const auto& [id, region]: m_device.regions()) {
        if (m_currentRegion->isOverllapedWith(*region)) {
            cancelRegion("Overllaped with other region");
            return;
        }
    }

    if (m_currentRegion->isValid()) {
        // selected elements are baked inside the m_currentRegion
        emit clearSelectionRequested();
        m_selectedElements.reset();
        //

        m_device.addRegion(m_currentRegion);
        emit regionsChanged(m_device.regions());

        m_currentRegion.reset();
        update();
    }
}

void DeviceGridWidget::cancelRegion(const QString& msg)
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

void DeviceGridWidget::keyPressEvent(QKeyEvent* event)
{
    qInfo() << "todo: DeviceWidget::keyPressEvent";
}

void DeviceGridWidget::mousePressEvent(QMouseEvent* event)
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

void DeviceGridWidget::mouseReleaseEvent(QMouseEvent* event) {
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
            m_device.removeRegion(m_regionToEdit);
            update();
            exitRegionEdit();
        }
    }
}

void DeviceGridWidget::mouseMoveEvent(QMouseEvent* event)
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

        m_device.refreshRegion(m_regionToEdit);
        update();
    }
}

void DeviceGridWidget::wheelEvent(QWheelEvent* event)
{
    const double factor = (event->angleDelta().y() > 0) ? 1.15 : 1.0 / 1.15;

    QPointF mouse = event->pos();
    QPointF before = (mouse + m_panPixels) / m_scale;

    m_scale = qBound(0.2, m_scale * factor, 10.0);

    QPointF after = before * m_scale;
    m_panPixels = after - mouse;

    update();
}

void DeviceGridWidget::startPanning(const QPoint& pos)
{
    m_isPanning = true;
    m_lastMousePos = pos;
    setCursor(Qt::ClosedHandCursor);
}

void DeviceGridWidget::stopPanning()
{
    m_isPanning = false;
    unsetCursor();
}

void DeviceGridWidget::saveQdc()
{
    QdcSerializer qdc;
    qdc.save(m_device);
}

void DeviceGridWidget::loadQdc()
{
    exitRegionEdit();
    QdcSerializer qdc;
    qdc.load(m_device);
    emit regionsChanged(m_device.regions());
    update();
}

void DeviceGridWidget::paintEvent(QPaintEvent* event)
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

  m_device.markVisibleTiles(m_viewPort);

  drawTilesBatched(p);
  drawRegions(p);

  const bool isDrawingTilesText = m_scale > 0.5;
  if (isDrawingTilesText) {
    drawTileLabels(p);
  }

  m_drawStat.setDrawTimeMs(t.elapsed());
}

void DeviceGridWidget::drawBackground(QPainter& p)
{
  p.fillRect(rect(), m_backgroundColor);
}

void DeviceGridWidget::drawTilesBatched(QPainter& p)
{
    QVector<QRectF> clbRects, ioRects, bramRects, dspRects;
    for (const auto& [index, tile]: m_device.tiles()) {
        if (!tile.isVisible()) {
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

void DeviceGridWidget::drawRegions(QPainter& p)
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
    for (const auto& [id, region]: m_device.regions()) {
        if (m_regionToEdit && (region->id() == m_regionToEdit->id())) {
            continue;
        }
        p.drawRect(region->rect());
        highLightTilesInRegion(p, *region);
    }

    // region for edit
    if (m_regionToEdit) {
        m_regionToEdit->rebuildHandles(1.0/m_scale);
        pen.setColor(m_editRegionColor);
        p.setPen(pen);
        p.drawRect(m_regionToEdit->rect());
        highLightTilesInRegion(p, *m_regionToEdit);

        p.setPen(Qt::NoPen);
        p.setBrush(m_editRegionTransparentColor);
        for (const auto& [role, rect]: m_regionToEdit->handles) {
            p.drawRect(rect);
        }
    }

    // draw regions labels (id etc)
    QFont font;
    font.setPointSize(18/m_scale);
    p.setFont(font);
    p.setPen(Qt::black);

    for (const auto& [id, region]: m_device.regions()) {
        QString text(QString::number(region->id()));
        p.drawText(region->rect(), Qt::AlignCenter, text);
    }
    //
}

void DeviceGridWidget::highLightTilesInRegion(QPainter& p, const Region& region) const {
    if (!region.isValid()) {
        return;
    }

    for (const Tile::Index& index: region.tiles()) {
        p.drawRect(m_device.tile(index).rect());
    }
}

void DeviceGridWidget::drawTileLabels(QPainter& p)
{
    p.setPen(m_textColor);
    QFont font;
    font.setPointSize(4);
    p.setFont(font);

    for (const auto& [index, tile]: m_device.tiles()) {
        if (!tile.isVisible()) {
            continue;
        }
        p.drawText(tile.rect(), Qt::AlignCenter | Qt::AlignCenter, tile.name());
    }
}

QPointF DeviceGridWidget::toWorldCoord(const QPoint& screenCoord)
{
    return (QPointF(screenCoord) + m_panPixels) / m_scale;
}

} // namespace fp
