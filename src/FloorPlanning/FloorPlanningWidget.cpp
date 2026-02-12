#include "FloorPlanningWidget.h"

#include "SynthResourceHierarchyWidget.h"
#include "DeviceGridWidget.h"
#include "PartitionsListWidget.h"
#include "NewUniqueNameDialog.h"
#include "ErrorsListWidget.h"
#include "CheckableButton.h"
#include "Widgets/RoundProgressWidget.h"

#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QCheckBox>
#include <QSplitter>
#include <QKeyEvent>

namespace fp {
	
FloorPlanningWidget::FloorPlanningWidget(QWidget* parent)
    : QWidget(parent)
{
    m_busyOverlayWidget = new FOEDAG::RoundProgressWidget(32, this);
    const int m = FP_UI_MARGIN;

    m_synthResourcesWidget = new SynthResourceHierarchyWidget;
    m_synthResourcesWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_deviceWidget = new DeviceGridWidget;
    m_deviceWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // right pane
    QWidget* wRightPane = new QWidget;
    QVBoxLayout* rightPaneLayout = new QVBoxLayout(wRightPane);
    rightPaneLayout->setContentsMargins(m,m,m,m);
    rightPaneLayout->setSpacing(m);

    m_partitionsListWidget = new PartitionsListWidget;
    rightPaneLayout->addWidget(m_partitionsListWidget);
    connect(m_partitionsListWidget, &PartitionsListWidget::selectionChanged, m_deviceWidget, &DeviceGridWidget::onPartitionSelected);
    m_partitionsListWidget->setEnabled(false);

    int flags = SynthResourceHierarchyWidget::Flag::ShowOnlyCheckedItems | SynthResourceHierarchyWidget::Flag::HidePartitionsColumn;
    m_partitionResourcesWidget = new SynthResourceHierarchyWidget(flags);
    m_partitionResourcesWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_partitionResourcesWidget->setViewLabelTemplate(tr("Partition '%1' netlist:"));
    rightPaneLayout->addWidget(m_partitionResourcesWidget);

    m_errorsListWidget = new ErrorsListWidget;
    rightPaneLayout->addWidget(m_errorsListWidget);
    m_errorsListWidget->setEnabled(false);

    // m_synthResourcesWidget
    connect(m_deviceWidget, &DeviceGridWidget::partitionSelected,
            m_synthResourcesWidget, &SynthResourceHierarchyWidget::selectPartition);

    connect(m_deviceWidget, &DeviceGridWidget::unselectPartitionRequested,
            m_synthResourcesWidget, &SynthResourceHierarchyWidget::unselectPartition);

    // m_partitionResourcesWidget
    connect(m_deviceWidget, &DeviceGridWidget::unselectPartitionRequested,
            m_partitionResourcesWidget, &SynthResourceHierarchyWidget::unselectPartition);

    connect(m_deviceWidget, &DeviceGridWidget::partitionSelected,
            m_partitionResourcesWidget, &SynthResourceHierarchyWidget::selectPartition);

    // to FloorPlanningWidget hub
    connect(m_deviceWidget, &DeviceGridWidget::partitionsChanged,
            this, &FloorPlanningWidget::onPartitionsChanged);
    connect(m_synthResourcesWidget, &SynthResourceHierarchyWidget::partitionElementsChanged,
            m_deviceWidget, &DeviceGridWidget::onPartitionSelectedElementsChanged);
    connect(m_partitionResourcesWidget, &SynthResourceHierarchyWidget::partitionElementsChanged,
            m_deviceWidget, &DeviceGridWidget::onPartitionSelectedElementsChanged);

    // m_partitionsList
    connect(m_deviceWidget, &DeviceGridWidget::partitionSelected,
            m_partitionsListWidget, &PartitionsListWidget::onPartitionSelectedOutside);
    connect(m_partitionsListWidget, &PartitionsListWidget::partitionRenamed,
            m_deviceWidget, &DeviceGridWidget::onPartitionRenamed);
    connect(m_partitionsListWidget, &PartitionsListWidget::notify,
            this, &FloorPlanningWidget::onNotify);
    connect(m_deviceWidget, &DeviceGridWidget::unselectPartitionRequested,
            m_partitionsListWidget, &PartitionsListWidget::unselectPartition);

    // m_deviceWidget
    connect(m_deviceWidget, &DeviceGridWidget::notify,
            this, &FloorPlanningWidget::onNotify);

    connect(m_deviceWidget, &DeviceGridWidget::checkErrorsFinished,
            this, &FloorPlanningWidget::onCheckErrorsFinished);

    // toolBar
    QWidget* toolBarContainer = new QWidget;
    QHBoxLayout* toolBarLayout = new QHBoxLayout;
    toolBarContainer->setLayout(toolBarLayout);
    toolBarLayout->setContentsMargins(m,m,m,m);
    toolBarLayout->setSpacing(m);

    QPushButton* bnLoadQdc = new QPushButton(QIcon(":/load-action.png"), "");
    connect(bnLoadQdc, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::loadQdc);
    bnLoadQdc->setToolTip(tr("Load QDC"));

    m_bnSaveQdc = new QPushButton(QIcon(":/save-action.png"), "");
    connect(m_bnSaveQdc, &QPushButton::clicked, m_deviceWidget, [this]() {
        if (m_deviceWidget->saveQdc()) {
            m_bnSaveQdc->setEnabled(false);
            emit qdcFileSaved();
        } else {
            onNotify("Fail to save QDC", "");
        }
    });
    m_bnSaveQdc->setToolTip(tr("Save QDC"));
    m_bnSaveQdc->setEnabled(false);

    QPushButton* bnCreateNewPartition = new QPushButton(QIcon(":/add.png"), "");
    connect(bnCreateNewPartition, &QPushButton::clicked, this, &FloorPlanningWidget::createNewPartition);
    bnCreateNewPartition->setToolTip(tr("Create new partition"));

    m_bnRemoveSelectedPartition = new QPushButton(QIcon(":/minus.png"), "");
    connect(m_bnRemoveSelectedPartition, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::removeSelectedPartition);
    m_bnRemoveSelectedPartition->setToolTip(tr("Remove selected partition"));
    m_bnRemoveSelectedPartition->setEnabled(false);

    connect(m_deviceWidget, &DeviceGridWidget::createFirstPartitionRequested, this, &FloorPlanningWidget::createNewPartition);

    QCheckBox* bnScrollPartition = new QCheckBox("Scroll to partition");
    connect(bnScrollPartition, &QCheckBox::stateChanged, m_deviceWidget, &DeviceGridWidget::setScrollToPartitionWhenSelected);
    bnScrollPartition->setVisible(false);

    // move controls
    QPushButton* bnLeft = new QPushButton(QIcon(":/left-arrow.png"), "");
    QPushButton* bnRight = new QPushButton(QIcon(":/right-arrow.png"), "");
    QPushButton* bnDown = new QPushButton(QIcon(":/down-arrow.png"), "");
    QPushButton* bnUp = new QPushButton(QIcon(":/up-arrow.png"), "");

    bnLeft->setToolTip(tr("Pan view left"));
    bnRight->setToolTip(tr("Pan view right"));
    bnDown->setToolTip(tr("Pan view down"));
    bnUp->setToolTip(tr("Pan view up"));

    connect(bnLeft, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::moveViewLeft);
    connect(bnUp, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::moveViewUp);
    connect(bnRight, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::moveViewRight);
    connect(bnDown, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::moveViewDown);

            // zoom controls
    QPushButton* bnZoomIn = new QPushButton(QIcon(":/zoom-in.svg"), "");
    QPushButton* bnZoomOut = new QPushButton(QIcon(":/zoom-out.svg"), "");
    QPushButton* bnZoomFit = new QPushButton(QIcon(":/expand.png"), "");
    m_bnZoomInRegion = new CheckableButton(QIcon(":/zoom-in-area.png"));
    m_drawRegion = new CheckableButton(QIcon(":/selection.png"));
    m_drawRegion->setChecked(true);

    connect(bnZoomIn, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::zoomIn);
    connect(bnZoomOut, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::zoomOut);
    connect(bnZoomFit, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::zoomFit);
    connect(m_bnZoomInRegion, &QPushButton::clicked, this, [this](){
      m_drawRegion->setChecked(false);
      m_deviceWidget->activateZoomInRegionMode();
    });
    connect(m_drawRegion, &QPushButton::clicked, this, [this](){
      m_bnZoomInRegion->setChecked(false);
      m_deviceWidget->deactivateZoomInRegionMode();
    });

    bnZoomIn->setToolTip(tr("Zoom in"));
    bnZoomOut->setToolTip(tr("Zoom out"));
    bnZoomFit->setToolTip(tr("Fit view to device grid"));
    m_bnZoomInRegion->setToolTip(tr("Zoom to selected area"));
    m_drawRegion->setToolTip(tr("Draw new region"));

    m_bnRemoveAllPartitions = new QPushButton(QIcon(":/erase.png"), "");
    connect(m_bnRemoveAllPartitions, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::clearPartitions);
    m_bnRemoveAllPartitions->setToolTip(tr("Delete all partitions"));
    m_bnRemoveAllPartitions->setEnabled(false);

    const int spacing = 20;
    toolBarLayout->addWidget(bnLoadQdc);
    toolBarLayout->addWidget(m_bnSaveQdc);
    toolBarLayout->addStretch();
    toolBarLayout->addWidget(bnLeft);
    toolBarLayout->addWidget(bnRight);
    toolBarLayout->addWidget(bnDown);
    toolBarLayout->addWidget(bnUp);
    toolBarLayout->addSpacing(spacing);
    toolBarLayout->addWidget(bnZoomIn);
    toolBarLayout->addWidget(bnZoomOut);
    toolBarLayout->addWidget(bnZoomFit);
    toolBarLayout->addSpacing(spacing);
    toolBarLayout->addWidget(m_bnZoomInRegion);
    toolBarLayout->addWidget(m_drawRegion);
    toolBarLayout->addSpacing(spacing);
    if (bnScrollPartition->isVisible()) {
      toolBarLayout->addWidget(bnScrollPartition);
    }
    toolBarLayout->addStretch();
    toolBarLayout->addSpacing(spacing);
    toolBarLayout->addWidget(m_bnRemoveAllPartitions);
    toolBarLayout->addSpacing(spacing);
    toolBarLayout->addWidget(bnCreateNewPartition);
    toolBarLayout->addWidget(m_bnRemoveSelectedPartition);
    toolBarLayout->addSpacing(spacing);

    bnScrollPartition->setChecked(true);
    m_deviceWidget->setScrollToPartitionWhenSelected(bnScrollPartition->isChecked());

    // body layout
    QHBoxLayout* bodyLayout = new QHBoxLayout;
    bodyLayout->setContentsMargins(m,m,m,m);
    bodyLayout->setSpacing(m);

    QWidget* deviceWidgetContainer = new QWidget;
    QVBoxLayout* deviceWidgetContainerLayout = new QVBoxLayout;
    deviceWidgetContainerLayout->setContentsMargins(m,m,m,m);
    deviceWidgetContainerLayout->setSpacing(m);
    deviceWidgetContainer->setLayout(deviceWidgetContainerLayout);
    deviceWidgetContainerLayout->addWidget(toolBarContainer);
    deviceWidgetContainerLayout->addWidget(m_deviceWidget);

    QSplitter* splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false); // don't let panes disappear
    splitter->setHandleWidth(6);

    splitter->addWidget(m_synthResourcesWidget);
    splitter->addWidget(deviceWidgetContainer);
    splitter->addWidget(wRightPane);

    splitter->setSizes({1, 2, 1});    // initial proportions (relative)

    bodyLayout->addWidget(splitter);

    // main layout
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(m,m,m,m);
    mainLayout->setSpacing(m);

    mainLayout->addLayout(bodyLayout);
}

FloorPlanningWidget::~FloorPlanningWidget()
{
    //qDebug() << "~FloorPlanningWidget";
}

void FloorPlanningWidget::setQdcFilePath(const std::filesystem::path& path, bool load)
{
    m_deviceWidget->setQdcFilePath(path);
    if (load) {
        m_deviceWidget->loadQdc();
    }
}

void FloorPlanningWidget::onPartitionsChanged(const std::map<int, PartitionPtr>& partitions)
{
    m_synthResourcesWidget->onPartitionsChanged(partitions);
    m_partitionResourcesWidget->onPartitionsChanged(partitions);
    m_partitionsListWidget->onPartitionsChanged(partitions);

    const bool partitionsNotEmpty = !partitions.empty();
    m_partitionsListWidget->setEnabled(partitionsNotEmpty);
    m_bnRemoveSelectedPartition->setEnabled(partitionsNotEmpty);
    if (!partitionsNotEmpty) {
        m_bnSaveQdc->setEnabled(false);
    }
    m_bnRemoveAllPartitions->setEnabled(partitionsNotEmpty);
}

void FloorPlanningWidget::onCheckErrorsFinished(std::unordered_set<std::string> errors)
{
    if (errors.empty()) {
        m_bnSaveQdc->setEnabled(true);
        m_errorsListWidget->clear();
    } else {
        m_bnSaveQdc->setEnabled(false);
        m_errorsListWidget->setErrors(errors);
    }
}

void FloorPlanningWidget::loadNetList(const std::set<std::string>& elements)
{
    m_synthResourcesWidget->build(elements);
    m_partitionResourcesWidget->build(elements);
    m_busyOverlayWidget->hide();
}

void FloorPlanningWidget::setDeviceGridDescriptor(const DeviceGridDescriptorPtr& descriptor)
{
    m_deviceWidget->constructTiles(descriptor);
}

void FloorPlanningWidget::createNewPartition()
{
    if (!m_newPartitionNameDialog) {
        m_newPartitionNameDialog = new NewUniqueNameDialog("Enter unique partition name", this);
    }
    m_newPartitionNameDialog->setExistedNames(m_deviceWidget->existedPartitionNames());
    if (m_newPartitionNameDialog->exec()) {
        std::string name = m_newPartitionNameDialog->name();
        if (!name.empty()) {
            m_deviceWidget->createNewPartition(name);
        }
    }
}

void FloorPlanningWidget::onNotify(QString title, QString msg)
{
    QMessageBox::warning(this, title, msg);
}

void FloorPlanningWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    m_busyOverlayWidget->show();
}

void FloorPlanningWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_busyOverlayWidget->resize(event->size());
}

void FloorPlanningWidget::closeEvent(QCloseEvent* event)
{
  emit closed();
  QWidget::closeEvent(event);
}

void FloorPlanningWidget::keyPressEvent(QKeyEvent* event)
{
  switch (event->key()) {
  case Qt::Key_Delete: {
    m_deviceWidget->removeSelectedPartition();
    event->accept();
    break;
  }
  case Qt::Key_Escape: {
    m_deviceWidget->unselectPartition();
    event->accept();
    break;
  } default: {
    QWidget::keyPressEvent(event);
  }
  }
}

} // namespace fp
