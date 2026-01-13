/*
Copyright 2022 The Foedag team

GPL License

Copyright (c) 2022 The Open-Source FPGA Foundation

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "FloorPlanningWidget.h"

#include "SynthResourceHierarchyWidget.h"
#include "DeviceWidget.h"
#include "SynthResourceExtractor.h"
#include "Utils/FileUtils.h"

#include <QHBoxLayout>
#include <filesystem>

namespace FOEDAG {
	
FloorPlanningWidget::FloorPlanningWidget(QWidget* parent)
    : QWidget(parent)
{
    m_synthResourcesWidget = new SynthResourceHierarchyWidget;
    m_synthResourcesWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_deviceWidget = new DeviceWidget;
    m_deviceWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QObject::connect(m_synthResourcesWidget, &SynthResourceHierarchyWidget::selectedElementsChanged,
                     m_deviceWidget, &DeviceWidget::onSelectedElementsChanged);

    QObject::connect(m_deviceWidget, &DeviceWidget::updateElementsSelectionRequested,
                     m_synthResourcesWidget, &SynthResourceHierarchyWidget::setSelectedElements);

    QObject::connect(m_deviceWidget, &DeviceWidget::clearSelectionRequested,
                     m_synthResourcesWidget, &SynthResourceHierarchyWidget::clearSelectedElements);

    QObject::connect(m_deviceWidget, &DeviceWidget::regionsLoaded,
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
        resourceExtractor.parseNetFileContent(FileUtils::GetFileContent(postSynthNetFilePath));
        const SynthResources& resources = resourceExtractor.resources();
        //resources.print();

        m_synthResourcesWidget->build(resources.atoms);
    }
}

void FloorPlanningWidget::setDeviceDescriptor(const DeviceDescriptorPtr& deviceDescriptor)
{
    m_deviceWidget->constructTiles(deviceDescriptor);
}

} // namespace FOEDAG
