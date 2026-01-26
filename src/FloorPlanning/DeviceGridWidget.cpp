#include "DeviceGridWidget.h"
#include "HierarhyElement.h"

#include <QElapsedTimer>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QDebug>

#include <cmath>

namespace fp {

DeviceGridWidget::DeviceGridWidget(QWidget* parent)
    : QWidget(parent),
    m_moveAnimation(this)
{
  setAutoFillBackground(false);
  setMouseTracking(true);

#ifdef SHOW_DRAWING_STAT
  QHBoxLayout* layout = new QHBoxLayout;
  QWidget* container = new QWidget(this);
  container->setLayout(layout);

  layout->addWidget(m_drawStat.label());
#endif // SHOW_DRAWING_STAT

  connect(&m_moveAnimation, &PointAnimation::pointChanged, this, &DeviceGridWidget::setWorldCenter);
}

void DeviceGridWidget::onPartitionSelected(int partitionId)
{
    PartitionPtr partition = m_device.findPartition(partitionId);
    if (partition) {
        selectPartition(partition);
        if (m_isScrollToPartitionWhenSelected) {
            scrollToPartition(partition);
        }
        update();
    }
}

void DeviceGridWidget::onPartitionRenamed(int partitionId, QString newName)
{
    PartitionPtr partition = m_device.findPartition(partitionId);
    if (partition) {
        partition->setName(newName.toStdString());
        checkErrors();
        emit partitionsChanged(m_device.partitions());
        update();
    }
}

void DeviceGridWidget::scrollToPartition(const PartitionPtr& partition)
{
    if (partition) {
        if (!partition->regions().empty()) {
            m_moveAnimation.start(currentWorldCenter(), partition->rect().center());
            update();
        }
    }
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

void DeviceGridWidget::onPartitionSelectedElementsChanged(PartitionPtr partition)
{
    if (m_selectedPartition) {
        emit partitionSelected(m_selectedPartition); // needs to update partition netlist widget
        emit partitionsChanged(m_device.partitions());
    } else {
        qCritical() << "cannot apply elemenets, because partition is not selected";
    }

    checkErrors();
}

bool DeviceGridWidget::trySelect(const QPointF& worldCoord)
{
    auto trySelectHelper = [this](const QPointF& worldCoord)->bool {
        for (const auto& [partitionId, partition]: m_device.partitions()) {
            for (const auto& [regionId, region]: partition->regions()) {
                if (region->rect().contains(worldCoord)) {
                    m_selectedRegion = region;
                    selectPartition(partition);
                    update();
                    return true;
                }
            }
        }
        return false;
    };

    bool selectionResult = trySelectHelper(worldCoord);
    if (selectionResult) {
        if (m_selectedPartition && m_selectedRegion) {
            std::optional<Region::HandlerRole> roleOpt = m_selectedRegion->checkHandlerClick(worldCoord);
            if (roleOpt) {
                if (roleOpt == Region::HandlerRole::REMOVE) {
                    m_selectedPartition->removeRegion(m_selectedRegion);
                    checkErrors();
                    emit partitionsChanged(m_device.partitions());
                    update();
                } else {
                    m_regionEditRoleOpt = roleOpt;
                }
                return true;
            }
        }
    }
    return selectionResult;
}

void DeviceGridWidget::startNewRegionSelection(const QPointF& worldCoord)
{
    if (!m_selectedPartition) {
        emit createFirstPartitionRequested();
        return;
    }
    m_newRegion = std::make_shared<Region>(worldCoord);
    update();
}

void DeviceGridWidget::stopRegionSelection(const QPointF& worldCoord)
{
    if (!m_selectedPartition) {
        return;
    }

    if (!m_newRegion) {
        return;
    }

    std::unordered_map<Tile::Index, TilePtr> tiles = m_device.findTiles(m_newRegion->rect());
    if (tiles.empty()) {
        cancelRegionCreation();
        return;
    }

    m_newRegion->accept(worldCoord, tiles);

    if (m_newRegion->isValid()) {
        m_device.alignRegion(m_newRegion);
        m_selectedPartition->addRegion(m_newRegion);
        checkErrors();
        emit partitionsChanged(m_device.partitions());

        m_newRegion.reset();
        update();
    }
}

void DeviceGridWidget::selectPartition(PartitionPtr partition)
{
    if (m_selectedPartition != partition) {
        m_selectedPartition = partition;
        emit partitionSelected(partition);
        update();
    }
}

void DeviceGridWidget::cancelRegionCreation(const QString& msg)
{
    if (!m_newRegion) {
        return;
    }
    if (!msg.isEmpty()) {
        emit notify("Drawing new region was declined", msg);
    }

    m_newRegion.reset();
    update();
}

void DeviceGridWidget::keyPressEvent(QKeyEvent* event)
{
    const double speed = 50.0;
    switch (event->key()) {
    case Qt::Key_Up: {
        m_panPixels += QPointF(0, -speed);
        break;
    }
    case Qt::Key_Down: {
        m_panPixels += QPointF(0, speed);
        break;
    }
    case Qt::Key_Left: {
        m_panPixels += QPointF(-speed, 0);
        break;
    }
    case Qt::Key_Right: {
        m_panPixels += QPointF(speed, 0);
        break;
    }
    case Qt::Key_Delete: {
        if (m_selectedPartition) {
            removeSelectedPartition();
        }
        break;
    }
    case Qt::Key_Escape: {
        if (m_selectedPartition) {
            exitPartitionSelect();
        }
        break;
    }
    default: {
        QWidget::keyPressEvent(event);
        return;
    }
    }

    update();
    event->accept();
}

void DeviceGridWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus(); // needed for keyPressEvent
    m_isMousePressed = true;

    const QPointF worldCoord{screenToWorldCoord(event->pos())};
    switch(event->button()) {
    case Qt::LeftButton: {
        if (!trySelect(worldCoord)) {
            startNewRegionSelection(worldCoord);
        }
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
    const QPointF worldCoord{screenToWorldCoord(event->pos())};
    switch(event->button()) {
    case Qt::LeftButton: {
        stopRegionSelection(worldCoord);
        m_device.alignRegions();
        checkErrors();
        update();
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

void DeviceGridWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF worldCoord = screenToWorldCoord(event->pos());

    if (m_isPanning) {
        QPointF delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();

        m_panPixels -= delta;
        update();
        return;
    }
    if (m_newRegion) {
        m_newRegion->setStopPos(worldCoord);
        update();
        return;
    }
    if (m_isMousePressed && m_selectedPartition && m_selectedRegion && m_regionEditRoleOpt) {
        switch (m_regionEditRoleOpt.value()) {
        case Region::HandlerRole::BL: m_selectedRegion->setBottomLeft(worldCoord); break;
        case Region::HandlerRole::BR: m_selectedRegion->setBottomRight(worldCoord); break;
        case Region::HandlerRole::TR: m_selectedRegion->setTopRight(worldCoord); break;
        case Region::HandlerRole::TL: m_selectedRegion->setTopLeft(worldCoord); break;
        case Region::HandlerRole::MOVE: m_selectedRegion->moveCenter(worldCoord); break;
        default: break;
        }

        m_device.refreshPartition(m_selectedPartition);
        update();
    }
}

void DeviceGridWidget::wheelEvent(QWheelEvent* event)
{
    double delta = 0.0;

    if (!event->angleDelta().isNull()) {
        delta = event->angleDelta().y() / 120.0;
    } else if (!event->pixelDelta().isNull()) {
        delta = event->pixelDelta().y() / 100.0;
    }

    if (delta == 0.0) {
        return;
    }

    const double zoomFactor = std::pow(1.15, delta);

    QPointF mouse = event->pos();
    QPointF before = (mouse + m_panPixels) / m_scale;

    m_scale = std::clamp(m_scale * zoomFactor, scaleMin, scaleMax);

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

std::unordered_set<std::string> DeviceGridWidget::existedPartitionNames() const
{
    std::unordered_set<std::string> names;
    for (const auto& [id, partition]: m_device.partitions()) {
        names.insert(partition->name());
    }
    return names;
}

void DeviceGridWidget::setQdcFilePath(const std::filesystem::path& path)
{
    m_qdcSerializer.setFilePath(path);
}

bool DeviceGridWidget::saveQdc()
{
    return m_qdcSerializer.save(m_device);
}

void DeviceGridWidget::loadQdc()
{
    exitPartitionSelect();
    m_qdcSerializer.load(m_device);
    checkErrors();
    emit partitionsChanged(m_device.partitions());
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

  const bool isDrawingTilesText = m_scale > 0.5;
  if (isDrawingTilesText) {
    drawTileLabels(p);
  }

  drawPartitions(p);
#ifdef SHOW_DRAWING_STAT
  m_drawStat.setDrawTimeMs(t.elapsed());
#endif
}

void DeviceGridWidget::drawBackground(QPainter& p)
{
  p.fillRect(rect(), m_backgroundColor);
}

void DeviceGridWidget::drawTilesBatched(QPainter& p)
{
    QVector<QRectF> clbRects, ioRects, bramRects, dspRects;
    for (const auto& [index, tile]: m_device.tiles()) {
        if (!tile->isVisible()) {
            continue;
        }
        switch (tile->type()) {
        case Tile::Type::Clb:  clbRects.append(tile->rect()); break;
        case Tile::Type::Io:   ioRects.append(tile->rect()); break;
        case Tile::Type::Bram: bramRects.append(tile->rect()); break;
        case Tile::Type::Dsp:  dspRects.append(tile->rect()); break;
        default: break;
        }
    }

#ifdef SHOW_DRAWING_STAT
    m_drawStat.setDrawableTilesNum(clbRects.size() + ioRects.size() + bramRects.size() + dspRects.size());
#endif
    p.setPen(Qt::NoPen);

    p.setBrush(Tile::color(Tile::Type::Clb));
    p.drawRects(clbRects);
    p.setBrush(Tile::color(Tile::Type::Io));
    p.drawRects(ioRects);
    p.setBrush(Tile::color(Tile::Type::Bram));
    p.drawRects(bramRects);
    p.setBrush(Tile::color(Tile::Type::Dsp));
    p.drawRects(dspRects);

    // draw overlapped tiles
    if (!m_device.overlappedIndexes().empty()) {
        QVector<QRectF> rects;
        rects.reserve(m_device.overlappedIndexes().size());
        for (const Tile::Index& index: m_device.overlappedIndexes()) {
            const TilePtr& tile = m_device.tile(index);
            rects.append(tile->rect());
        }
        p.setBrush(m_overlappedTileColor);
        p.drawRects(rects);
    }
}

void DeviceGridWidget::drawPartitions(QPainter& p)
{
    QPen pen(m_partitionColor);
    pen.setCosmetic(true);
    pen.setWidthF(adaptiveTileLineWidthF());
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    // draw selection
    if (m_newRegion) {
        if (m_newRegion->isClosed()) {
            p.drawRect(m_newRegion->rect());
        }
    }

    // draw partitions
    for (const auto& [partitionId, partition]: m_device.partitions()) {
        if (m_selectedPartition && (partition->id() == m_selectedPartition->id())) {
            continue;
        }
        for (const auto& [regionId, region]: partition->regions()) {
            p.drawRect(region->rect());
            highLightTilesInRegion(p, *region);
        }
    }

    // selected partition
    if (m_selectedPartition) {
        for (const auto& [regionId, region]: m_selectedPartition->regions()) {
            region->rebuildHandles(1.0/m_scale);
            p.setBrush(Qt::NoBrush);
            pen.setColor(m_editPartitionColor);
            p.setPen(pen);
            p.drawRect(region->rect());
            highLightTilesInRegion(p, *region);

            p.setPen(Qt::NoPen);

            for (const auto& [role, rect]: region->handles) {
                if (role == Region::HandlerRole::REMOVE) {
                    p.setBrush(m_removeHandlerColor);
                } else {
                    p.setBrush(m_editPartitionTransparentColor);
                }
                p.drawRect(rect);
            }
        }
    }

    // draw partition labels (id etc)
    const int padding = 2;
    const int radius = 4;
    const int flags = Qt::AlignHCenter | Qt::AlignBottom;

    const int selectedPartitionId = m_selectedPartition? m_selectedPartition->id() : -1;

    QFont font;
    const double minFontSize = 4.0;
    const double baseFontSize = 12.0;
    const double shadowOffsetMin = 0.5;
    const double shadowOffsetMax = 5.0;

    const double shadowOffset = std::clamp(1.0/m_scale, shadowOffsetMin, shadowOffsetMax);
    font.setPointSize(std::max(minFontSize, baseFontSize / m_scale));
    p.setFont(font);
    for (const auto& [id, partition]: m_device.partitions()) {
        QString text = QString::fromStdString(partition->name());
        for (const auto& [regionId, region]: partition->regions()) {
            const QRectF rect = region->rect().translated(0, -10);

            // pass 0: draw text (to determine rendered text rectangle stored in textBoundRect)
            if (partition->id() == selectedPartitionId) {
                p.setPen(Qt::black);
            } else {
                p.setPen(Qt::white);
            }
            p.setBrush(Qt::NoBrush);
            QRectF textBoundRect;
            p.drawText(rect, flags, text, &textBoundRect);

            // pass 1: draw background
            p.setPen(Qt::NoPen);
            if (partition->id() == selectedPartitionId) {
                p.setBrush(m_editPartitionTransparentColor);
            } else {
                p.setBrush(m_partitionTransparentColor);
            }
            p.drawRoundedRect(textBoundRect.adjusted(-padding, -padding, padding, padding), radius, radius);

            // pass 2: draw text (second time on the top of background)
            if (partition->id() == selectedPartitionId) {
                p.setPen(Qt::white);
            } else {
                p.setPen(Qt::black);
            }
            p.setBrush(Qt::NoBrush);
            p.drawText(rect.translated(shadowOffset, -shadowOffset), flags, text);

            // Note: in pass 0 and pass 2 text are drawn in opposite colors and shifted with some offset in order to imitate font shadow, for better readability
        }
    }
    //
}

void DeviceGridWidget::highLightTilesInRegion(QPainter& p, const Region& region) const {
    if (!region.isValid()) {
        return;
    }

    for (const auto& [index, tile]: region.tiles()) {
        p.drawRect(tile->rect());
    }
}

void DeviceGridWidget::drawTileLabels(QPainter& p)
{
    p.setPen(m_textColor);
    QFont font;
    font.setPointSize(4);
    p.setFont(font);

    for (const auto& [index, tile]: m_device.tiles()) {
        if (!tile->isVisible()) {
            continue;
        }
        p.drawText(tile->rect(), Qt::AlignCenter | Qt::AlignCenter, tile->name());
    }
}

QPointF DeviceGridWidget::screenToWorldCoord(const QPoint& screenCoord) const
{
    return (QPointF(screenCoord) + m_panPixels) / m_scale;
}

QPointF DeviceGridWidget::worldToScreenCoord(const QPointF& worldCoord) const
{
    return worldCoord * m_scale - m_panPixels;
}

QPointF DeviceGridWidget::currentWorldCenter() const
{
    const QPointF screenCenter(0.5 * width(), 0.5 * height());
    return (screenCenter + m_panPixels) / m_scale;
}

void DeviceGridWidget::setWorldCenter(const QPointF& worldCenter)
{
    const QPointF screenCenter(0.5 * width(), 0.5 * height());
    m_panPixels = worldCenter * m_scale - screenCenter;
    update();
}

void DeviceGridWidget::createNewPartition(const std::string& partitionName)
{
    PartitionPtr partition = std::make_shared<Partition>(partitionName);
    m_device.addPartition(partition);
    checkErrors();
    emit partitionsChanged(m_device.partitions());
    selectPartition(partition); // automatically select
}

void DeviceGridWidget::removeSelectedPartition()
{
    if (m_selectedPartition) {
        m_device.removePartition(m_selectedPartition);
        exitPartitionSelect();
        checkErrors();
        emit partitionsChanged(m_device.partitions());
    }
}

void DeviceGridWidget::clearPartitions() {
    m_device.clearPartitions();
    m_selectedRegion.reset();
    m_newRegion.reset();
    exitPartitionSelect();
    checkErrors();
    emit partitionsChanged(m_device.partitions());
    update();
}

void DeviceGridWidget::exitPartitionSelect() {
    m_selectedPartition.reset();
    m_regionEditRoleOpt.reset();
    emit clearPartitionSelectionRequested();
    update();
}

void DeviceGridWidget::checkErrors()
{
    std::unordered_set<std::string> errors = m_device.collectErrors();
    emit checkErrorsFinished(errors);
}

} // namespace fp
