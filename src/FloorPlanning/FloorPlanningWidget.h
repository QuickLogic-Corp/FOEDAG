#pragma once

#include <QWidget>

#include "DeviceGridDescriptor.h"

namespace fp {

class SynthResourceHierarchyWidget;
class DeviceGridWidget;

class FloorPlanningWidget : public QWidget {
public:
    explicit FloorPlanningWidget(QWidget* parent = nullptr);
    void loadNetList(const std::set<std::string>& elements);
    void setDeviceGridDescriptor(const DeviceGridDescriptorPtr& deviceDescriptor);

private:
    SynthResourceHierarchyWidget* m_synthResourcesWidget{nullptr};
    DeviceGridWidget* m_deviceWidget{nullptr};
};

}  // namespace fp
