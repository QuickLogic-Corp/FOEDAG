#include "FloorPlanningWidget.h"

#include "SynthResourceHierarchyWidget.h"
#include "DeviceGridWidget.h"
#include "PartitionsListWidget.h"
#include "NewUniqueNameDialog.h"
#include "ErrorsListWidget.h"

#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QCheckBox>
#include <QSplitter>

namespace fp {
	
FloorPlanningWidget::FloorPlanningWidget(QWidget* parent)
    : QWidget(parent)
{
    const int m = FP_MARGIN;

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
            m_synthResourcesWidget, &SynthResourceHierarchyWidget::bindPartition);

    connect(m_deviceWidget, &DeviceGridWidget::clearPartitionSelectionRequested,
            m_synthResourcesWidget, &SynthResourceHierarchyWidget::unbindPartition);

    // m_partitionResourcesWidget
    connect(m_deviceWidget, &DeviceGridWidget::clearPartitionSelectionRequested,
            m_partitionResourcesWidget, &SynthResourceHierarchyWidget::unbindPartition);

    connect(m_deviceWidget, &DeviceGridWidget::partitionSelected,
            m_partitionResourcesWidget, &SynthResourceHierarchyWidget::bindPartition);

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

    // m_deviceWidget
    connect(m_deviceWidget, &DeviceGridWidget::notify,
            this, &FloorPlanningWidget::onNotify);

    connect(m_deviceWidget, &DeviceGridWidget::checkErrorsFinished,
            this, &FloorPlanningWidget::onCheckErrorsFinished);

    // toolBar
    QVBoxLayout* toolBarLayout = new QVBoxLayout;
    toolBarLayout->setContentsMargins(m,m,m,m);
    toolBarLayout->setSpacing(m);

    QPushButton* bnLoadQdc = new QPushButton(QIcon(":/images/load-action.png"), "");
    connect(bnLoadQdc, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::loadQdc);
    bnLoadQdc->setToolTip(tr("Load QDC"));

    m_bnSaveQdc = new QPushButton(QIcon(":/images/save-action.png"), "");
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

    m_bnDeletePartitions = new QPushButton(QIcon(":/images/erase.png"), "");
    connect(m_bnDeletePartitions, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::clearPartitions);
    m_bnDeletePartitions->setToolTip(tr("Delete all partitions"));
    m_bnDeletePartitions->setEnabled(false);

    QPushButton* bnCreateNewPartition = new QPushButton(QIcon(":/images/add.png"), "");
    connect(bnCreateNewPartition, &QPushButton::clicked, this, &FloorPlanningWidget::createNewPartition);
    bnCreateNewPartition->setToolTip(tr("Create new partition"));

    m_bnRemovePartition = new QPushButton(QIcon(":/images/minus.png"), "");
    connect(m_bnRemovePartition, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::removeSelectedPartition);
    m_bnRemovePartition->setToolTip(tr("Remove selected partition"));
    m_bnRemovePartition->setEnabled(false);

    connect(m_deviceWidget, &DeviceGridWidget::createFirstPartitionRequested, this, &FloorPlanningWidget::createNewPartition);

    QCheckBox* bnScrollPartition = new QCheckBox("scroll to partition");
    connect(bnScrollPartition, &QCheckBox::stateChanged, m_deviceWidget, &DeviceGridWidget::setScrollToPartitionWhenSelected);
    bnScrollPartition->setVisible(false);

    const int spacing = 20;
    toolBarLayout->addWidget(bnLoadQdc);
    toolBarLayout->addWidget(m_bnSaveQdc);
    toolBarLayout->addSpacing(spacing);
    toolBarLayout->addWidget(m_bnDeletePartitions);
    toolBarLayout->addSpacing(spacing);
    toolBarLayout->addWidget(bnCreateNewPartition);
    toolBarLayout->addWidget(m_bnRemovePartition);
    if (bnScrollPartition->isVisible()) {
        toolBarLayout->addWidget(bnScrollPartition);
    }
    toolBarLayout->addStretch();

    bnScrollPartition->setChecked(true);
    m_deviceWidget->setScrollToPartitionWhenSelected(bnScrollPartition->isChecked());

    // body layout
    QHBoxLayout* bodyLayout = new QHBoxLayout;
    bodyLayout->setContentsMargins(m,m,m,m);
    bodyLayout->setSpacing(m);

    bodyLayout->addLayout(toolBarLayout);

    QSplitter* splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false); // don't let panes disappear
    splitter->setHandleWidth(6);

    splitter->addWidget(m_synthResourcesWidget);
    splitter->addWidget(m_deviceWidget);
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
    m_bnRemovePartition->setEnabled(partitionsNotEmpty);
    m_bnDeletePartitions->setEnabled(partitionsNotEmpty);
    if (!partitionsNotEmpty) {
        m_bnSaveQdc->setEnabled(false);
    }
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

} // namespace fp
