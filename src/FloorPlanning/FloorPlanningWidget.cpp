#include "FloorPlanningWidget.h"

#include "SynthResourceHierarchyWidget.h"
#include "DeviceGridWidget.h"
#include "RegionsListWidget.h"

#include <QHBoxLayout>

namespace fp {
	
FloorPlanningWidget::FloorPlanningWidget(QWidget* parent)
    : QWidget(parent)
{
    m_synthResourcesWidget = new SynthResourceHierarchyWidget;
    m_synthResourcesWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_deviceWidget = new DeviceGridWidget;
    m_deviceWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // right pane
    QVBoxLayout* rightPaneLayout = new QVBoxLayout;
    int flags = SynthResourceHierarchyWidget::Flag::ShowOnlyCheckedItems | SynthResourceHierarchyWidget::Flag::HideRegionsColumn;
    m_regionResourcesWidget = new SynthResourceHierarchyWidget(flags);
    m_regionResourcesWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    rightPaneLayout->addWidget(m_regionResourcesWidget);

    rightPaneLayout->addWidget(new QLabel("Regions:"));

    m_regionsList = new RegionsListWidget;
    rightPaneLayout->addWidget(m_regionsList);
    connect(m_regionsList, &RegionsListWidget::selectionChanged, m_deviceWidget, &DeviceGridWidget::onRegionSelected);

    // m_synthResourcesWidget
    connect(m_synthResourcesWidget, &SynthResourceHierarchyWidget::selectedElementsChanged,
            m_deviceWidget, &DeviceGridWidget::onSelectedElementsChanged);

    connect(m_deviceWidget, &DeviceGridWidget::regionSelected,
            m_synthResourcesWidget, &SynthResourceHierarchyWidget::onRegionSelected);

    connect(m_deviceWidget, &DeviceGridWidget::clearSelectionRequested,
            m_synthResourcesWidget, &SynthResourceHierarchyWidget::clearSelectedElements);

    connect(m_deviceWidget, &DeviceGridWidget::regionsChanged,
            m_synthResourcesWidget, &SynthResourceHierarchyWidget::onRegionsChanged);

    // m_regionResourcesWidget
    connect(m_regionResourcesWidget, &SynthResourceHierarchyWidget::selectedElementsChanged,
            m_deviceWidget, &DeviceGridWidget::onSelectedElementsChanged);

    connect(m_deviceWidget, &DeviceGridWidget::clearSelectionRequested,
            m_regionResourcesWidget, &SynthResourceHierarchyWidget::clearSelectedElements);

    connect(m_deviceWidget, &DeviceGridWidget::regionsChanged,
            m_regionResourcesWidget, &SynthResourceHierarchyWidget::onRegionsChanged);

    connect(m_deviceWidget, &DeviceGridWidget::regionSelected,
            m_regionResourcesWidget, &SynthResourceHierarchyWidget::onRegionSelected);

    // m_regionsList
    connect(m_deviceWidget, &DeviceGridWidget::regionSelected, m_regionsList, &RegionsListWidget::onRegionSelectedOutside);
    connect(m_deviceWidget, &DeviceGridWidget::regionsChanged, m_regionsList, &RegionsListWidget::onRegionsChanged);

    // main layout
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    layout->addWidget(m_synthResourcesWidget);
    layout->addWidget(m_deviceWidget);
    layout->addLayout(rightPaneLayout);
}

void FloorPlanningWidget::loadNetList(const std::set<std::string>& elements)
{
    m_synthResourcesWidget->build(elements);
    m_regionResourcesWidget->build(elements);
}

void FloorPlanningWidget::setDeviceGridDescriptor(const DeviceGridDescriptorPtr& descriptor)
{
    m_deviceWidget->constructTiles(descriptor);
}

} // namespace fp
