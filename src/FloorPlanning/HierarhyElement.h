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

#include <string>
#include <set>
#include <memory>

#include <QDebug>

namespace FOEDAG {

class HierarhyElement {
public:
    HierarhyElement(const std::string& path): path(path) {}
    HierarhyElement(const std::string& path, bool isLeaf): path(path), isLeaf(isLeaf) {}

    std::string path;
    bool isLeaf = true;

    bool operator<(const HierarhyElement& rhs) const { return path < rhs.path; }
};

class HierarhyElements {
public:
    bool empty() const { return m_elements.empty(); }

    std::size_t size() const { return m_elements.size(); }

    bool contains(const std::string& path) const {
        return m_elements.find(HierarhyElement{path}) != m_elements.end();
    }

    std::set<HierarhyElement>::const_iterator begin() const { return m_elements.begin(); }
    std::set<HierarhyElement>::const_iterator end()   const { return m_elements.end(); }

    void insert(const HierarhyElement& elemenet) {
        m_elements.insert(elemenet);
    }
    void print(const std::string& label) {
        qInfo() << "~~~" << QString::fromStdString(label);
        for (const HierarhyElement& element: m_elements) {
            qInfo() << QString::fromStdString(element.path) << element.isLeaf;
        }
        qInfo() << "\n";
    }

private:
    std::set<HierarhyElement> m_elements;
};
using HierarhyElementsPtr = std::shared_ptr<HierarhyElements>;


}  // namespace FOEDAG
