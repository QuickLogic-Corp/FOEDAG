#pragma once

#include <QWidget>

#include "DeviceWidget.h" // needed for DeviceDescriptorPtr

#include <filesystem>

namespace fp {

class SynthResourceHierarchyWidget;
class DeviceWidget;

class FloorPlanningWidget : public QWidget {
public:
    explicit FloorPlanningWidget(QWidget* parent = nullptr);
    void loadPostSynthNetFile(const std::filesystem::path& postSynthNetFilePath);
    void setDeviceDescriptor(const DeviceDescriptorPtr& deviceDescriptor);

private:
    SynthResourceHierarchyWidget* m_synthResourcesWidget{nullptr};
    DeviceWidget* m_deviceWidget{nullptr};
};

}  // namespace fp
