#include "DeviceGridWidget.h"

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
    : QWidget(parent)
    , m_moveAnimation(this)
{
  setAutoFillBackground(false);
  setMouseTracking(true);
  connect(&m_moveAnimation, &PointAnimation::pointChanged, this, &DeviceGridWidget::setWorldCenter);
}

bool DeviceGridWidget::isSaveQdcAllowed() const
{
  if (m_device.partitions().empty()) {
    return false;
  }
  if (m_device.hasErrors()) {
    return false;
  }
  return true;
}

void DeviceGridWidget::onPartitionSelected(int partitionId)
{
    PartitionPtr partition = m_device.partition(partitionId);
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
    PartitionPtr partition = m_device.partition(partitionId);
    if (partition) {
        partition->setName(newName.toStdString());
        reportPartitionChanges();
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
        reportPartitionChanges();
    } else {
        qCritical() << "cannot apply elemenets, because partition is not selected";
    }
}

bool DeviceGridWidget::trySelect(const QPointF& worldCoord)
{
    m_selectedRegion.reset();

    auto trySelectHelper = [this](const QPointF& worldCoord)->std::pair<RegionPtr, std::optional<Region::HandlerRole>> {
        RegionPtr candidate;
        for (const auto& [partitionId, partition]: m_device.partitions()) {
            for (const auto& [regionId, region]: partition->regions()) {
                if (region->rect().contains(worldCoord)) {
                  if (std::optional<Region::HandlerRole> roleOpt = region->checkHandlerClick(worldCoord)) {
                    // if user click on area which contains several regions,
                    // the one who has intersection with handler will have more priority
                    return std::make_pair(region, roleOpt);
                  } else {
                    candidate = region;
                  }
                }
            }
        }
        return std::make_pair(candidate, std::nullopt);
    };

    // check focused region handlers
    auto [region, roleOpt] = trySelectHelper(worldCoord);
    if (region) {
      selectRegion(region);
      if (roleOpt) {
        if (roleOpt == Region::HandlerRole::REMOVE) {
          if (m_device.removeRegion(region)) {
            reportPartitionChanges();
            update();
          }
        } else {
          m_regionEditRoleOpt = roleOpt;
        }
        return true;
      }
    }
    return (region != nullptr);
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
        reportPartitionChanges();
        m_newRegion.reset();
        update();
    }
}

void DeviceGridWidget::selectRegion(const RegionPtr& region)
{
    unselectPartition(/*silent*/true);
    m_selectedRegion = region;
    emit selectionChanged();
    update();
}

void DeviceGridWidget::selectPartition(PartitionPtr partition)
{
    if (m_selectedPartition != partition) {
        unselectRegion(/*silent*/true);
        m_selectedPartition = partition;
        emit partitionSelected(partition);
        emit selectionChanged();
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
    switch (event->key()) {
    case Qt::Key_Up: {
      moveViewUp();
      break;
    }
    case Qt::Key_Down: {
      moveViewDown();
      break;
    }
    case Qt::Key_Left: {
      moveViewLeft();
      break;
    }
    case Qt::Key_Right: {
      moveViewRight();
      break;
    }    
    default: {
      QWidget::keyPressEvent(event);
      return;
    }
    }

    event->accept();
}

void DeviceGridWidget::moveViewUp()
{
  m_panPixels += QPointF(0, -moveStep);
  update();
}

void DeviceGridWidget::moveViewDown()
{
  m_panPixels += QPointF(0, moveStep);
  update();
}

void DeviceGridWidget::moveViewLeft()
{
  m_panPixels += QPointF(-moveStep, 0);
  update();
}

void DeviceGridWidget::moveViewRight()
{
  m_panPixels += QPointF(moveStep, 0);
  update();
}

void DeviceGridWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus(); // needed for keyPressEvent

    const QPointF mousePos = QPointF(event->pos());
    
    const QPointF worldCoord{screenToWorldCoord(mousePos)};
    switch(event->button()) {
    case Qt::LeftButton: {
        if (m_isZoomInRegionModeActive) {
          m_selectionBottomLeftOpt = worldCoord;
        } else {
          if (!trySelect(worldCoord)) {
              startNewRegionSelection(worldCoord);
          }
        }
        break;
    }
    case Qt::MiddleButton:
    case Qt::RightButton: {
        startPanning(mousePos);
        break;
    }
    default: break;
    }

    m_isMousePressed = true;
}

void DeviceGridWidget::mouseReleaseEvent(QMouseEvent* event) {
    QPointF mousePos = QPointF(event->pos());

    const QPointF worldCoord{screenToWorldCoord(mousePos)};
    switch(event->button()) {
    case Qt::LeftButton: {
        if (m_isZoomInRegionModeActive && m_selectionBottomLeftOpt && m_selectionTopRightOpt) {
          m_selectionTopRightOpt = worldCoord;
          QRectF rect(m_selectionBottomLeftOpt.value(), m_selectionTopRightOpt.value());
          zoomInRect(rect);

          m_selectionBottomLeftOpt.reset();
          m_selectionTopRightOpt.reset();

          update();
        } else {
          if (m_newRegion) {
            stopRegionSelection(worldCoord);
          }
          // even if there is no m_newRegion, we still could move existed regions, so we need refresh
          m_device.alignRegions();
          checkErrors();
          update();

        }
        break;
    }
    case Qt::MiddleButton:
    case Qt::RightButton: {
        stopPanning();
        break;
    }
    default: break;
    }
    m_isMousePressed = false;
}

void DeviceGridWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF mousePos = QPointF(event->pos());

    const QPointF worldCoord = screenToWorldCoord(mousePos);

    if (m_isPanning) {
        QPointF delta = mousePos - m_lastMousePos;
        m_lastMousePos = mousePos;

        m_panPixels -= delta;
        update();
        return;
    }
    if (m_isZoomInRegionModeActive) {
      m_selectionTopRightOpt = worldCoord;
      update();
      return;
    }
    if (m_newRegion) {
        m_newRegion->setStopPos(worldCoord);
        update();
        return;
    }
    if (m_isMousePressed && m_selectedRegion && m_regionEditRoleOpt) {
        switch (m_regionEditRoleOpt.value()) {
        case Region::HandlerRole::BL: m_selectedRegion->setBottomLeft(worldCoord); break;
        case Region::HandlerRole::BR: m_selectedRegion->setBottomRight(worldCoord); break;
        case Region::HandlerRole::TR: m_selectedRegion->setTopRight(worldCoord); break;
        case Region::HandlerRole::TL: m_selectedRegion->setTopLeft(worldCoord); break;
        case Region::HandlerRole::MOVE: m_selectedRegion->moveCenter(worldCoord); break;
        default: break;
        }

        PartitionPtr partition = m_device.partition(m_selectedRegion->partitionId());
        m_device.refreshPartition(partition);
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

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
  QPointF cursorPos = event->position();
#else
  QPointF cursorPos = event->pos();
#endif

  zoom(cursorPos, delta);
}

void DeviceGridWidget::zoom(const QPointF& refPoint, double delta)
{
  if (delta == 0.0) {
    return;
  }

  const double zoomFactor = std::pow(1.15, delta);

  QPointF before = (refPoint + m_panPixels) / m_scale;

  m_scale = std::clamp(m_scale * zoomFactor, scaleMin, scaleMax);

  QPointF after = before * m_scale;
  m_panPixels = after - refPoint;

  update();
}

void DeviceGridWidget::zoomIn()
{
  zoom(viewPortCenter(), zoomStep);
}

void DeviceGridWidget::zoomOut()
{
  zoom(viewPortCenter(), -zoomStep);
}

void DeviceGridWidget::zoomFit()
{
  QRectF rect = m_device.rect();
  zoomInRect(rect);
}

void DeviceGridWidget::zoomInRect(QRectF& area)
{
  area = area.normalized();
  const QSizeF deviceSize = area.size();
  if ((deviceSize.width() <= 0.0) || (deviceSize.height() <= 0.0)) {
    return;
  }

  const QRect viewRect = contentsRect();

  const double wFactor = viewRect.width()  / deviceSize.width();
  const double hFactor = viewRect.height() / deviceSize.height();

  m_scale = std::min(wFactor, hFactor);

  const QSizeF scaledDeviceSize = deviceSize * m_scale;

  QPointF targetTopLeft = QPointF(viewRect.center()) - 0.5*QPointF(scaledDeviceSize.width(), scaledDeviceSize.height());
  m_panPixels = m_scale * area.topLeft() - targetTopLeft;

  update();
}

void DeviceGridWidget::startPanning(const QPointF& pos)
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
    unselect();
    m_qdcSerializer.load(m_device);
    emit unselectPartitionRequested();
    reportPartitionChanges();
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
  drawCurrentSelection(p);

  // debug
  // QPen pen;
  // pen.setWidthF(5.0);
  // pen.setColor(Qt::red);
  // p.setPen(pen);
  // p.setBrush(Qt::NoBrush);
  // p.drawRect(m_device.rect());
  // debug
}

void DeviceGridWidget::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);
  zoomFit();
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

    p.setPen(Qt::NoPen);

    p.setBrush(Tile::color(Tile::Type::Clb));
    p.drawRects(clbRects);
    p.setBrush(Tile::color(Tile::Type::Io));
    p.drawRects(ioRects);
    p.setBrush(Tile::color(Tile::Type::Bram));
    p.drawRects(bramRects);
    p.setBrush(Tile::color(Tile::Type::Dsp));
    p.drawRects(dspRects);

    // draw conflicting overlapped tiles
    if (!m_device.overlappedConflictingIndexes().empty()) {
        QVector<QRectF> rects;
        rects.reserve(m_device.overlappedConflictingIndexes().size());
        for (const Tile::Index& index: m_device.overlappedConflictingIndexes()) {
            const TilePtr& tile = m_device.tile(index);
            rects.append(tile->rect());
        }
        p.setBrush(m_overlappedConflictingTileColor);
        p.drawRects(rects);
    }

    // draw overlapped non conflicting tiles
    if (!m_device.overlappedNonConflictingIndexes().empty()) {
      QVector<QRectF> rects;
      rects.reserve(m_device.overlappedNonConflictingIndexes().size());
      for (const Tile::Index& index: m_device.overlappedNonConflictingIndexes()) {
        const TilePtr& tile = m_device.tile(index);
        rects.append(tile->rect());
      }
      p.setBrush(m_overlappedNonConflictingTileColor);
      p.drawRects(rects);
    }
}

void DeviceGridWidget::drawCurrentSelection(QPainter& p)
{
  QPen pen(m_selectionFrameColor);
  pen.setCosmetic(true);
  pen.setWidthF(adaptiveTileLineWidthF());
  p.setPen(pen);
  p.setBrush(m_selectionBgColor);

  if (m_isZoomInRegionModeActive) {
    if (m_isMousePressed && m_selectionBottomLeftOpt && m_selectionTopRightOpt) {
      QRectF selectionRect(m_selectionBottomLeftOpt.value(), m_selectionTopRightOpt.value());
      selectionRect = selectionRect.normalized();
      p.drawRect(selectionRect);
    }
  } else {
    if (m_newRegion) {
      if (m_newRegion->isClosed()) {
        p.drawRect(m_newRegion->rect());
      }
    }
  }
}

void DeviceGridWidget::drawPartitions(QPainter& p)
{
    QPen pen(m_partitionColor);
    pen.setCosmetic(true);
    pen.setWidthF(adaptiveTileLineWidthF());
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    // draw partitions (excluding selected)
    for (const auto& [partitionId, partition]: m_device.partitions()) {
        if (m_selectedPartition && (partition->id() == m_selectedPartition->id())) {
            continue;
        }
        pen.setColor(partition->color());
        p.setPen(pen);
        for (const auto& [regionId, region]: partition->regions()) {
            p.drawRect(region->rect());
            highLightTilesInRegion(p, *region);
        }
    }

    // selected partition
    if (m_selectedPartition) {
        for (const auto& [regionId, region]: m_selectedPartition->regions()) {
          drawRegion(p, region, m_editPartitionColor);
        }
    }

    if (m_selectedRegion) {
      drawRegion(p, m_selectedRegion, m_editPartitionColor);
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
    for (const auto& [partitionId, partition]: m_device.partitions()) {
        QString text = QString::fromStdString(partition->name());
        for (const auto& [regionId, region]: partition->regions()) {
            const QRectF rect = region->rect().translated(0, -10);

            // pass 0: draw text (to determine rendered text rectangle stored in textBoundRect)
            if (partitionId == selectedPartitionId) {
                p.setPen(Qt::black);
            } else {
                p.setPen(Qt::white);
            }
            p.setBrush(Qt::NoBrush);
            QRectF textBoundRect;
            p.drawText(rect, flags, text, &textBoundRect);

            // pass 1: draw background
            p.setPen(Qt::NoPen);
            const bool isRegionSelected = (partitionId == selectedPartitionId) ||
                                    (m_selectedRegion && (m_selectedRegion->id() == regionId) && (partitionId == region->partitionId()));
            if (isRegionSelected) {
                p.setBrush(m_editPartitionTransparentColor);
            } else {
                p.setBrush(partition->colorTransparent());
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

void DeviceGridWidget::drawRegion(QPainter& p, const RegionPtr& region, const QColor& color)
{
  region->rebuildHandles(1.0/m_scale);
  p.setBrush(Qt::NoBrush);

  QPen pen(color);
  pen.setCosmetic(true);
  pen.setWidthF(adaptiveTileLineWidthF());

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

QPointF DeviceGridWidget::screenToWorldCoord(const QPointF& screenCoord) const
{
    return (screenCoord + m_panPixels) / m_scale;
}

QPointF DeviceGridWidget::worldToScreenCoord(const QPointF& worldCoord) const
{
    return worldCoord * m_scale - m_panPixels;
}

QPointF DeviceGridWidget::viewPortCenter() const
{
  return 0.5f*QPointF(width(), height());
}

QPointF DeviceGridWidget::currentWorldCenter() const
{
  return (viewPortCenter() + m_panPixels) / m_scale;
}

void DeviceGridWidget::setWorldCenter(const QPointF& worldCenter)
{
  m_panPixels = worldCenter * m_scale - viewPortCenter();
  update();
}

void DeviceGridWidget::createNewPartition(const std::string& partitionName)
{
    PartitionPtr partition = std::make_shared<Partition>(partitionName);
    m_device.addPartition(partition);
    reportPartitionChanges();
    selectPartition(partition); // automatically select
}

void DeviceGridWidget::removeSelected()
{
  if (m_selectedPartition) {
    removeSelectedPartition();
  }
  if (m_selectedRegion) {
    removeSelectedRegion();
  }
}

void DeviceGridWidget::removeSelectedPartition()
{
    if (m_selectedPartition) {
        m_device.removePartition(m_selectedPartition);
        unselect();
        reportPartitionChanges();
        update();
    }
}

void DeviceGridWidget::removeSelectedRegion()
{
  if (m_selectedRegion) {
    m_device.removeRegion(m_selectedRegion);
    unselect();
    reportPartitionChanges();
    update();
  }
}

void DeviceGridWidget::clearPartitions() {
    m_device.clearPartitions();
    m_selectedRegion.reset();
    m_newRegion.reset();
    unselect();
    reportPartitionChanges();
    update();
}

void DeviceGridWidget::unselect()
{
  if (m_selectedPartition) {
    unselectPartition();
  }
  if (m_selectedRegion || m_regionEditRoleOpt) {
    unselectRegion();
  }
}

void DeviceGridWidget::unselectPartition(bool silent) {
  m_selectedPartition.reset();
  if (!silent) {
    emit unselectPartitionRequested();
    emit selectionChanged();
  }
  update();
}

void DeviceGridWidget::unselectRegion(bool silent)
{
  m_selectedRegion.reset();
  m_regionEditRoleOpt.reset();
  if (!silent) {
    emit selectionChanged();
  }
  update();
}

void DeviceGridWidget::checkErrors()
{
    std::unordered_set<std::string> errors = m_device.checkErrors();
    emit checkErrorsFinished(errors);
}

void DeviceGridWidget::reportPartitionChanges()
{
  checkErrors();
  emit partitionsChanged(m_device.partitions());
}

} // namespace fp
