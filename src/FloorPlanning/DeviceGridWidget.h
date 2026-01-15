#pragma once

#include "DeviceGrid.h"
#include "Tile.h"
#include "Region.h"
#include "HierarhyElement.h"

#include <QWidget>
#include <QPoint>
#include <QLabel>

class QPaintEvent;

namespace fp {

class DeviceGridWidget final : public QWidget {
    Q_OBJECT

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
public:
    explicit DeviceGridWidget(QWidget* parent = nullptr);
    virtual ~DeviceGridWidget()=default;

    void constructTiles(const DeviceGridDescriptorPtr& descriptor);
    void onSelectedElementsChanged(const HierarhyElementsPtr& elemenets);

signals:
    void clearSelectionRequested();
    void regionSelected(const RegionPtr&);
    void regionsChanged(const std::map<int, RegionPtr>&);

protected:
    QSize sizeHint() const override final;
    QSize minimumSizeHint() const override final;
    void keyPressEvent(QKeyEvent*) override final;
    void mousePressEvent(QMouseEvent*) override final;
    void mouseReleaseEvent(QMouseEvent*) override final;
    void mouseMoveEvent(QMouseEvent*) override final;
    void wheelEvent(QWheelEvent* event) override final;
    void paintEvent(QPaintEvent*) override final;

    void saveQdc();
    void loadQdc();

private:
    DeviceGrid m_device;

    HierarhyElementsPtr m_selectedElements;

    QColor m_backgroundColor{25, 25, 28};
    QColor m_transparentColor{0, 0, 0, 0};
    QColor m_textColor{30, 30, 35};
    QColor m_regionColor{50, 160, 50}; // green
    QColor m_editRegionColor{50, 50, 160}; // blue
    QColor m_editRegionTransparentColor{50, 50, 160, 150}; // blue

    double m_scale = 1.0f;

    bool m_isMousePressed = false;

    DrawStat m_drawStat;

    QRectF m_viewPort;

    void drawBackground(QPainter& p);
    void drawTilesBatched(QPainter& p);
    void drawRegions(QPainter& p);
    void drawTileLabels(QPainter& p);
    void highLightTilesInRegion(QPainter& p, const Region& area) const;

    // panning
    bool m_isPanning{false};
    QPointF m_lastMousePos;
    QPointF m_panPixels{0.0, 0.0};

    void startPanning(const QPoint& pos);
    void stopPanning();
    // panning

    // selection
    bool m_isSelectingNewRegion{false};
    RegionPtr m_currentRegion;

    RegionPtr m_regionToEdit;
    std::optional<Region::HandlerRole> m_editRoleOpt;
    bool trySelectRegionToEdit(const QPointF& worldCoord);

    void clearRegions() {
        m_device.clearRegions();
        m_currentRegion.reset();
        exitRegionEdit();
        update();
    }
    void exitRegionEdit() {
        emit clearSelectionRequested();
        emit regionsChanged(m_device.regions());
        m_selectedElements.reset();
        m_regionToEdit.reset();
        m_editRoleOpt.reset();
        update();
    }
    void startNewRegion(const QPointF& worldCoord);
    void stopRegion(const QPointF& selection);
    void cancelRegion(const QString& msg = "");
    // selection

    QPointF toWorldCoord(const QPoint&);
};

}  // namespace fp
