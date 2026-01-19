#pragma once

#include <QWidget>

#include "DeviceGridDescriptor.h"

namespace fp {

class SynthResourceHierarchyWidget;
class DeviceGridWidget;
class RegionsListWidget;

class FloorPlanningWidget : public QWidget {
    Q_OBJECT

public:
    explicit FloorPlanningWidget(QWidget* parent = nullptr);
    void loadNetList(const std::set<std::string>& elements);
    void setDeviceGridDescriptor(const DeviceGridDescriptorPtr& deviceDescriptor);

private:
    SynthResourceHierarchyWidget* m_synthResourcesWidget{nullptr};
    SynthResourceHierarchyWidget* m_regionResourcesWidget{nullptr};
    DeviceGridWidget* m_deviceWidget{nullptr};
    RegionsListWidget* m_regionsList{nullptr};
};

}  // namespace fp
