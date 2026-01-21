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

namespace fp {
	
FloorPlanningWidget::FloorPlanningWidget(QWidget* parent)
    : QWidget(parent)
{
    // m_lbStatus = new QLabel;

    m_synthResourcesWidget = new SynthResourceHierarchyWidget;
    m_synthResourcesWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_deviceWidget = new DeviceGridWidget;
    m_deviceWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // right pane
    QVBoxLayout* rightPaneLayout = new QVBoxLayout;
    rightPaneLayout->setContentsMargins(0,0,0,0);

    rightPaneLayout->addWidget(new QLabel("Partitions:"));

    m_partitionsListWidget = new PartitionsListWidget;
    rightPaneLayout->addWidget(m_partitionsListWidget);
    connect(m_partitionsListWidget, &PartitionsListWidget::selectionChanged, m_deviceWidget, &DeviceGridWidget::onPartitionSelected);

    int flags = SynthResourceHierarchyWidget::Flag::ShowOnlyCheckedItems | SynthResourceHierarchyWidget::Flag::HidePartitionsColumn;
    m_partitionResourcesWidget = new SynthResourceHierarchyWidget(flags);
    m_partitionResourcesWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    rightPaneLayout->addWidget(m_partitionResourcesWidget);

    m_errorsListWidget = new ErrorsListWidget;
    rightPaneLayout->addWidget(m_errorsListWidget);
    m_errorsListWidget->setEnabled(false);

    // m_synthResourcesWidget
    connect(m_deviceWidget, &DeviceGridWidget::partitionSelected,
            m_synthResourcesWidget, &SynthResourceHierarchyWidget::onPartitionSelected);

    connect(m_deviceWidget, &DeviceGridWidget::clearSelectionRequested,
            m_synthResourcesWidget, &SynthResourceHierarchyWidget::clearSelectedElements);

    // m_partitionResourcesWidget
    connect(m_deviceWidget, &DeviceGridWidget::clearSelectionRequested,
            m_partitionResourcesWidget, &SynthResourceHierarchyWidget::clearSelectedElements);

    connect(m_deviceWidget, &DeviceGridWidget::partitionSelected,
            m_partitionResourcesWidget, &SynthResourceHierarchyWidget::onPartitionSelected);

    // to FloorPlanningWidget hub
    connect(m_deviceWidget, &DeviceGridWidget::partitionsChanged,
            this, &FloorPlanningWidget::onPartitionsChanged);
    connect(m_synthResourcesWidget, &SynthResourceHierarchyWidget::selectedElementsChanged,
            m_deviceWidget, &DeviceGridWidget::onSelectedElementsChanged);
    connect(m_partitionResourcesWidget, &SynthResourceHierarchyWidget::selectedElementsChanged,
            m_deviceWidget, &DeviceGridWidget::onSelectedElementsChanged);

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
    toolBarLayout->setContentsMargins(0,0,0,0);
    toolBarLayout->setSpacing(0);

    m_bnSaveQdc = new QPushButton(QIcon(":/save-action.png"), "");
    connect(m_bnSaveQdc, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::saveQdc);
    m_bnSaveQdc->setToolTip(tr("Save QDC"));

    QPushButton* bnLoadQdc = new QPushButton(QIcon(":/load-action.png"), "");
    connect(bnLoadQdc, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::loadQdc);
    bnLoadQdc->setToolTip(tr("Load QDC"));

    QPushButton* bnDeletePartitions = new QPushButton(QIcon(":/erase.png"), "");
    connect(bnDeletePartitions, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::clearPartitions);
    bnDeletePartitions->setToolTip(tr("Delete all partitions"));

    QPushButton* bnCreateNewPartition = new QPushButton(QIcon(":/add.png"), "");
    connect(bnCreateNewPartition, &QPushButton::clicked, this, &FloorPlanningWidget::createNewPartition);
    bnCreateNewPartition->setToolTip(tr("Create new partition"));

    QPushButton* bnRemovePartition = new QPushButton(QIcon(":/minus.png"), "");
    connect(bnRemovePartition, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::removeSelectedPartition);
    bnRemovePartition->setToolTip(tr("Remove selected partition"));

    connect(m_deviceWidget, &DeviceGridWidget::createFirstPartitionRequested, this, &FloorPlanningWidget::createNewPartition);

    QCheckBox* bnScrollPartition = new QCheckBox("scroll to partition");
    connect(bnScrollPartition, &QCheckBox::stateChanged, m_deviceWidget, &DeviceGridWidget::setScrollToPartitionWhenSelected);
    bnScrollPartition->setVisible(false);

    const int spacing = 20;
    toolBarLayout->addWidget(m_bnSaveQdc);
    toolBarLayout->addWidget(bnLoadQdc);
    toolBarLayout->addSpacing(spacing);
    toolBarLayout->addWidget(bnDeletePartitions);
    toolBarLayout->addSpacing(spacing);
    toolBarLayout->addWidget(bnCreateNewPartition);
    toolBarLayout->addWidget(bnRemovePartition);
    if (bnScrollPartition->isVisible()) {
        toolBarLayout->addWidget(bnScrollPartition);
    }
    toolBarLayout->addStretch();

    bnScrollPartition->setChecked(true);
    m_deviceWidget->setScrollToPartitionWhenSelected(bnScrollPartition->isChecked());

    // body layout
    QHBoxLayout* bodyLayout = new QHBoxLayout;
    bodyLayout->setContentsMargins(0,0,0,0);
    bodyLayout->setSpacing(0);

    bodyLayout->addLayout(toolBarLayout);
    bodyLayout->addWidget(m_synthResourcesWidget);
    bodyLayout->addWidget(m_deviceWidget, 1);
    bodyLayout->addLayout(rightPaneLayout);

    // main layout
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0,0,0,0);

    mainLayout->addLayout(bodyLayout);
    // mainLayout->addWidget(m_lbStatus);
}

void FloorPlanningWidget::onPartitionsChanged(const std::map<int, PartitionPtr>& partitions)
{
    m_synthResourcesWidget->onPartitionsChanged(partitions);
    m_partitionResourcesWidget->onPartitionsChanged(partitions);
    m_partitionsListWidget->onPartitionsChanged(partitions);
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
