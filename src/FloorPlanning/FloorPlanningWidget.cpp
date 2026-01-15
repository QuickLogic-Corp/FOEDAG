#include "FloorPlanningWidget.h"

#include "SynthResourceHierarchyWidget.h"
#include "DeviceGridWidget.h"

#include <QHBoxLayout>

namespace fp {
	
FloorPlanningWidget::FloorPlanningWidget(QWidget* parent)
    : QWidget(parent)
{
    m_synthResourcesWidget = new SynthResourceHierarchyWidget;
    m_synthResourcesWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_deviceWidget = new DeviceGridWidget;
    m_deviceWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QObject::connect(m_synthResourcesWidget, &SynthResourceHierarchyWidget::selectedElementsChanged,
                     m_deviceWidget, &DeviceGridWidget::onSelectedElementsChanged);

    QObject::connect(m_deviceWidget, &DeviceGridWidget::regionSelected,
                     m_synthResourcesWidget, &SynthResourceHierarchyWidget::onRegionSelected);

    QObject::connect(m_deviceWidget, &DeviceGridWidget::clearSelectionRequested,
                     m_synthResourcesWidget, &SynthResourceHierarchyWidget::clearSelectedElements);

    QObject::connect(m_deviceWidget, &DeviceGridWidget::regionsChanged,
                     m_synthResourcesWidget, &SynthResourceHierarchyWidget::onRegionsChanged);

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    layout->addWidget(m_synthResourcesWidget);
    layout->addWidget(m_deviceWidget);
}

void FloorPlanningWidget::loadNetList(const std::set<std::string>& elements)
{
    m_synthResourcesWidget->build(elements);
}

void FloorPlanningWidget::setDeviceGridDescriptor(const DeviceGridDescriptorPtr& descriptor)
{
    m_deviceWidget->constructTiles(descriptor);
}

} // namespace fp
