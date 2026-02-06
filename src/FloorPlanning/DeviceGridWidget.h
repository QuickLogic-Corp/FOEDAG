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
    const QColor m_partitionTransparentColor{50, 160, 50, 150}; // transparent-green
    const QColor m_editPartitionColor{50, 50, 160}; // blue
    const QColor m_editPartitionTransparentColor{50, 50, 160, 150}; // transparent-blue
    const QColor m_removeHandlerColor{160, 50, 50, 150}; // transparent-red
    const QColor m_overlappedTileColor{160, 50, 50, 150}; // transparent-red
    const int m_tileLineBaseWidth = 3;

    double adaptiveTileLineWidthF() const {
        return std::clamp(m_tileLineBaseWidth * m_scale, 0.5, double(2*m_tileLineBaseWidth));
    }

#ifdef SHOW_DRAWING_STAT
    struct DrawStat {
    public:
        DrawStat() {
            m_label = new QLabel;
            m_label->setMinimumWidth(200);
        }
        void setLabel(QLabel* label) { m_label = label; }
        void setDrawableTilesNum(int num) {
            m_drawableTilesNum = num;
            updateLabel();
        }
        void setDrawTimeMs(long long ms) {
            m_drawTimeMs = ms;
            updateLabel();
        }

        QLabel* label() { return m_label; }

    private:
        int m_drawableTilesNum = -1;
        long long m_drawTimeMs = -1;
        QLabel* m_label{nullptr};

        void updateLabel() {
            m_label->setText(QString("visible %1 tiles, took %2 ms").arg(m_drawableTilesNum).arg(m_drawTimeMs));
        }
    };
#endif // SHOW_DRAWING_STAT
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
    std::unordered_set<std::string> existedPartitionNames() const;

    void setQdcFilePath(const std::filesystem::path& path);
    bool saveQdc();
    void loadQdc();

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
    void resizeEvent(QResizeEvent*) override final;
    void showEvent(QShowEvent*) override final;

private:
    QWidget* m_toolBar{nullptr};

    DeviceGrid m_device;
    QdcSerializer m_qdcSerializer;

    bool m_isScrollToPartitionWhenSelected = false;

    double m_scale = 1.0f;

    bool m_isMousePressed = false;

#ifdef SHOW_DRAWING_STAT
    DrawStat m_drawStat;
#endif

    QRectF m_viewPort;

    void drawBackground(QPainter& p);
    void drawTilesBatched(QPainter& p);
    void drawPartitions(QPainter& p);
    void drawTileLabels(QPainter& p);
    void highLightTilesInRegion(QPainter& p, const Region& region) const;

    // panning
    PointAnimation m_moveAnimation;
    bool m_isPanning{false};
    QPointF m_lastMousePos;
    QPointF m_panPixels{0.0, 0.0};

    void startPanning(const QPointF& pos);
    void stopPanning();
    // panning

    // selection
    RegionPtr m_newRegion;
    RegionPtr m_selectedRegion;

    PartitionPtr m_selectedPartition;
    std::optional<Region::HandlerRole> m_regionEditRoleOpt;
    bool trySelect(const QPointF& worldCoord);

    void unselectPartition();
    void startNewRegionSelection(const QPointF& worldCoord);
    void stopRegionSelection(const QPointF& worldCoord);
    void cancelRegionCreation(const QString& msg = "");
    void selectPartition(PartitionPtr partition);
    // selection

    QPointF screenToWorldCoord(const QPointF&) const;
    QPointF worldToScreenCoord(const QPointF&) const;
    void scrollToPartition(const PartitionPtr&);
    void startMoveAnimation(const QPointF&);
    QPointF viewPortCenter() const;
    QPointF currentWorldCenter() const;
    void setWorldCenter(const QPointF&);

    void checkErrors();
    void updateToolBarPosition();

    void moveViewUp();
    void moveViewDown();
    void moveViewLeft();
    void moveViewRight();

    void zoom(const QPointF& refPoint, double delta);
    void zoomIn();
    void zoomOut();
    void zoomFit();
};

}  // namespace fp
