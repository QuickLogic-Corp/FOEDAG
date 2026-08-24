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
    const QColor m_overlappedConflictingTileColor{255, 50, 50, 150}; // transparent-red
    const QColor m_overlappedNonConflictingTileColor{255, 255, 50, 150}; // transparent-yellow
    const int m_tileLineBaseWidth = 3;

    double adaptiveTileLineWidthF() const {
        return std::clamp(m_tileLineBaseWidth * m_scale, 0.5, double(2*m_tileLineBaseWidth));
    }

public:
    explicit DeviceGridWidget(QWidget* parent = nullptr);
    virtual ~DeviceGridWidget()=default;

    bool isSaveQdcAllowed() const;

    bool hasSelection() const { return (m_selectedPartition != nullptr) || (m_selectedRegion != nullptr); }
    void constructTiles(const DeviceGridDescriptorPtr& descriptor);
    void onPartitionSelectedElementsChanged(PartitionPtr partition);
    void onPartitionSelected(int);
    void onPartitionRenamed(int partitionId, QString newName);

    void setScrollToPartitionWhenSelected(bool flag) { m_isScrollToPartitionWhenSelected = flag; }

    // [aurora2#1725] Per-instance count of directly-owned atoms, for DeviceGrid's
    // unconstrained-own-logic warning. See FloorPlanningWidget::setAtomNames().
    void setOwnAtomCounts(std::map<std::string, int> ownAtomCounts) {
        m_device.setOwnAtomCounts(std::move(ownAtomCounts));
    }

    // [aurora2#1725 stage P4] Instances graded "deleted", for DeviceGrid's report on a .qdc
    // that still constrains one. See FloorPlanningWidget::setInstanceVerdicts().
    void setDeletedInstances(std::unordered_set<std::string> deletedInstances) {
        m_device.setDeletedInstances(std::move(deletedInstances));
    }

    // [aurora2#1725] Options menu. Re-checks straight away, so the issues list and the Save
    // QDC button reflect the new severity without waiting for the next edit.
    void setTreatWarningsAsErrors(bool treatWarningsAsErrors) {
        m_device.setTreatWarningsAsErrors(treatWarningsAsErrors);
        checkIssues();
    }

    void clearPartitions();
    void createNewPartition(const std::string& partitionName = "");
    void removeSelected();
    // [aurora2#1725] Remove one partition by id, whatever is selected. The partitions list
    // deletes a row directly, which is not necessarily the row the grid has selected.
    void removePartitionById(int id);
    void unselect();
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

    bool hasSelectedPartition() const { return m_selectedPartition != nullptr; }
    void changeSelectedPartitionColor(const QColor& color) {
      if (m_selectedPartition) {
        m_selectedPartition->setColor(color);
      }
    }

signals:
    void checkIssuesFinished(DeviceGrid::IssuesPtr issues);
    void createFirstPartitionRequested();
    void unselectPartitionRequested();
    void partitionSelected(const PartitionPtr&);
    void partitionsChanged(const std::map<int, PartitionPtr>&);
    void notify(QString, QString);
    void selectionChanged();

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
    void drawRegion(QPainter& p, const RegionPtr& region, const QColor& color);
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

    PartitionPtr m_selectedPartition;
    RegionPtr m_selectedRegion;
    std::optional<Region::HandlerRole> m_regionEditRoleOpt;
    bool trySelect(const QPointF& worldCoord);

    void startNewRegionSelection(const QPointF& worldCoord);
    void stopRegionSelection(const QPointF& worldCoord);
    void cancelRegionCreation(const QString& msg = "");
    void selectPartition(PartitionPtr partition);
    void selectRegion(const RegionPtr& region);
    // selection

    void reportPartitionChanges();

    QPointF screenToWorldCoord(const QPointF&) const;
    QPointF worldToScreenCoord(const QPointF&) const;
    void scrollToPartition(const PartitionPtr&);
    void startMoveAnimation(const QPointF&);
    QPointF viewPortCenter() const;
    QPointF currentWorldCenter() const;
    void setWorldCenter(const QPointF&);

    void checkIssues();

    void zoom(const QPointF& refPoint, double delta);
    void zoomInRect(QRectF&);

    QColor colorFromIndex(int index) const;

    void unselectPartition(bool silent = false);
    void unselectRegion(bool silent = false);

    void removeSelectedPartition();
    void removeSelectedRegion();
};

}  // namespace fp
