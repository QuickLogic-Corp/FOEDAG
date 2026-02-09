#pragma once

#include "DeviceGrid.h"
#include "Tile.h"
#include "Partition.h"
#include "PointAnimation.h"
#include "QdcSerializer.h"

#include <QWidget>
#include <QPoint>
#include <QLabel>

class QPaintEvent;

namespace fp {

class DeviceGridWidget final : public QWidget {
    Q_OBJECT

    const double moveStep = 50.0;
    const double zoomStep = 2.0;
    const double scaleMin = 0.05;
    const double scaleMax = 10.0;

    const QColor m_backgroundColor{25, 25, 28};
    const QColor m_transparentColor{0, 0, 0, 0};
    const QColor m_textColor{30, 30, 35};
    const QColor m_partitionColor{50, 160, 50}; // green
    const QColor m_selectionFrameColor{50, 160, 50}; // green
    const QColor m_selectionBgColor{50, 160, 50, 50}; // green
    const QColor m_partitionTransparentColor{50, 160, 50, 150}; // transparent-green
    const QColor m_editPartitionColor{50, 50, 160}; // blue
    const QColor m_editPartitionTransparentColor{50, 50, 160, 150}; // transparent-blue
    const QColor m_removeHandlerColor{160, 50, 50, 150}; // transparent-red
    const QColor m_overlappedTileColor{160, 50, 50, 150}; // transparent-red
    const int m_tileLineBaseWidth = 3;

    double adaptiveTileLineWidthF() const {
        return std::clamp(m_tileLineBaseWidth * m_scale, 0.5, double(2*m_tileLineBaseWidth));
    }

public:
    explicit DeviceGridWidget(QWidget* parent = nullptr);
    virtual ~DeviceGridWidget()=default;

    void constructTiles(const DeviceGridDescriptorPtr& descriptor);
    void onPartitionSelectedElementsChanged(PartitionPtr partition);
    void onPartitionSelected(int);
    void onPartitionRenamed(int partitionId, QString newName);

    void setScrollToPartitionWhenSelected(bool flag) { m_isScrollToPartitionWhenSelected = flag; }

    void clearPartitions();
    void createNewPartition(const std::string& partitionName = "");
    void removeSelectedPartition();
    void unselectPartition();
    std::unordered_set<std::string> existedPartitionNames() const;

    void setQdcFilePath(const std::filesystem::path& path);
    bool saveQdc();
    void loadQdc();

    void moveViewUp();
    void moveViewDown();
    void moveViewLeft();
    void moveViewRight();

    void zoomIn();
    void zoomOut();
    void zoomFit();

    void activateZoomInRegionMode() { m_isZoomInRegionModeActive = true; }
    void deactivateZoomInRegionMode() { m_isZoomInRegionModeActive = false; }

signals:
    void checkErrorsFinished(std::unordered_set<std::string> errors);
    void createFirstPartitionRequested();
    void unselectPartitionRequested();
    void partitionSelected(const PartitionPtr&);
    void partitionsChanged(const std::map<int, PartitionPtr>&);
    void notify(QString, QString);

protected:
    QSize sizeHint() const override final;
    QSize minimumSizeHint() const override final;
    void keyPressEvent(QKeyEvent*) override final;
    void mousePressEvent(QMouseEvent*) override final;
    void mouseReleaseEvent(QMouseEvent*) override final;
    void mouseMoveEvent(QMouseEvent*) override final;
    void wheelEvent(QWheelEvent*) override final;
    void paintEvent(QPaintEvent*) override final;
    void showEvent(QShowEvent*) override final;

private:
    DeviceGrid m_device;
    QdcSerializer m_qdcSerializer;

    bool m_isScrollToPartitionWhenSelected = false;

    double m_scale = 1.0f;

    bool m_isMousePressed = false;

    QRectF m_viewPort;

    void drawBackground(QPainter& p);
    void drawTilesBatched(QPainter& p);
    void drawPartitions(QPainter& p);
    void drawCurrentSelection(QPainter& p);
    void drawTileLabels(QPainter& p);
    void highLightTilesInRegion(QPainter& p, const Region& region) const;

    bool m_isZoomInRegionModeActive = false;

    // panning
    PointAnimation m_moveAnimation;
    bool m_isPanning{false};
    QPointF m_lastMousePos;
    QPointF m_panPixels{0.0, 0.0};

    void startPanning(const QPointF& pos);
    void stopPanning();
    // panning

    // selection
    std::optional<QPointF> m_selectionBottomLeftOpt;
    std::optional<QPointF> m_selectionTopRightOpt;
    RegionPtr m_newRegion;
    RegionPtr m_selectedRegion;

    PartitionPtr m_selectedPartition;
    std::optional<Region::HandlerRole> m_regionEditRoleOpt;
    bool trySelect(const QPointF& worldCoord);

    void startNewRegionSelection(const QPointF& worldCoord);
    void stopRegionSelection(const QPointF& worldCoord);
    void cancelRegionCreation(const QString& msg = "");
    void selectPartition(PartitionPtr partition);
    // selection

    void reportPartitionChanges();

    QPointF screenToWorldCoord(const QPointF&) const;
    QPointF worldToScreenCoord(const QPointF&) const;
    void scrollToPartition(const PartitionPtr&);
    void startMoveAnimation(const QPointF&);
    QPointF viewPortCenter() const;
    QPointF currentWorldCenter() const;
    void setWorldCenter(const QPointF&);

    void checkErrors();

    void zoom(const QPointF& refPoint, double delta);
    void zoomInRect(QRectF&);
};

}  // namespace fp
