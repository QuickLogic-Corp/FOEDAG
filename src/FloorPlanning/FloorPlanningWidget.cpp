#include "FloorPlanningWidget.h"

#include "SynthResourceHierarchyWidget.h"
#include "DeviceWidget.h"
#include "SynthResourceExtractor.h"
#include "Utils/FileUtils.h"

#include <QHBoxLayout>
#include <filesystem>

namespace fp {
	
FloorPlanningWidget::FloorPlanningWidget(QWidget* parent)
    : QWidget(parent)
{
    m_synthResourcesWidget = new SynthResourceHierarchyWidget;
    m_synthResourcesWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_deviceWidget = new DeviceWidget;
    m_deviceWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QObject::connect(m_synthResourcesWidget, &SynthResourceHierarchyWidget::selectedElementsChanged,
                     m_deviceWidget, &DeviceWidget::onSelectedElementsChanged);

    QObject::connect(m_deviceWidget, &DeviceWidget::regionSelected,
                     m_synthResourcesWidget, &SynthResourceHierarchyWidget::onRegionSelected);

    QObject::connect(m_deviceWidget, &DeviceWidget::clearSelectionRequested,
                     m_synthResourcesWidget, &SynthResourceHierarchyWidget::clearSelectedElements);

    QObject::connect(m_deviceWidget, &DeviceWidget::regionsChanged,
                     m_synthResourcesWidget, &SynthResourceHierarchyWidget::onRegionsChanged);

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    layout->addWidget(m_synthResourcesWidget);
    layout->addWidget(m_deviceWidget);
}

void FloorPlanningWidget::loadPostSynthNetFile(const std::filesystem::path& postSynthNetFilePath) {
    if (postSynthNetFilePath.empty()) {
        std::set<std::string> elements = {"dut.prism.el00.sub001",
                                          "dut.prism.el00.sub002",
                                          "dut.prism.el01",
                                          "dut.prism.el02",
                                          "dut.tri.el0.sub2",
                                          "dut.tri.el1",
                                          "dut.tri.el2",
                                          "top"};

        m_synthResourcesWidget->build(elements);
    } else {
        SynthResourceExtractor resourceExtractor;
        resourceExtractor.parseNetFileContent(FOEDAG::FileUtils::GetFileContent(postSynthNetFilePath));
        const SynthResources& resources = resourceExtractor.resources();

        m_synthResourcesWidget->build(resources.atoms);
    }
}

void FloorPlanningWidget::setDeviceDescriptor(const DeviceDescriptorPtr& deviceDescriptor)
{
    m_deviceWidget->constructTiles(deviceDescriptor);
}

} // namespace fp
