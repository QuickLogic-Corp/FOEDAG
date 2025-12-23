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

#pragma once

#include <QWidget>

#include "DeviceWidget.h" // needed for DeviceDescriptorPtr

#include <filesystem>

namespace FOEDAG {

class SynthResourceHierarchyWidget;
class DeviceWidget;

class FloorPlanningWidget : public QWidget {
public:
    explicit FloorPlanningWidget(QWidget* parent = nullptr);
    void setPostSynthNetFile(const std::filesystem::path& path);
    void setDeviceDescriptor(const DeviceDescriptorPtr& deviceDescriptor);
private:
    SynthResourceHierarchyWidget* m_synthResourcesWidget{nullptr};
    DeviceWidget* m_deviceWidget{nullptr};
};

}  // namespace FOEDAG
