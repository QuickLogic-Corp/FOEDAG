#pragma once

#include <string>
#include <set>
#include <memory>

#include <QDebug>

namespace fp {

class HierarhyElement {
public:
    HierarhyElement(const std::string& path): path(path) {}
    HierarhyElement(const std::string& path, bool isLeaf): path(path), isLeaf(isLeaf) {}

#ifdef USE_TESTS
    bool operator==(const HierarhyElement& rhs) const {
        return ((path == rhs.path) && (isLeaf == rhs.isLeaf));
    }
#endif // USE_TESTS
    std::string path;
    bool isLeaf = true;

    bool operator<(const HierarhyElement& rhs) const { return path < rhs.path; }
};

class HierarhyElements {
public:
#ifdef USE_TESTS
    bool operator==(const HierarhyElements& rhs) const {
        if (size() != rhs.size()) {
            return false;
        }
        return m_elements == rhs.m_elements;
    }

    std::string toString() const {
        std::string result;
        for (const auto& element: m_elements) {
            if (!result.empty()) {
                result += ",";
            }
            result += element.path;
            if (!element.isLeaf) {
                result += ".*";
            }
        }
        return result;
    }
#endif // USE_TESTS

    bool empty() const { return m_elements.empty(); }

    std::size_t size() const { return m_elements.size(); }

    bool contains(const std::string& path) const {
        return m_elements.find(HierarhyElement{path}) != m_elements.end();
    }

    std::set<HierarhyElement>::const_iterator begin() const { return m_elements.begin(); }
    std::set<HierarhyElement>::const_iterator end()   const { return m_elements.end(); }

    void insert(const std::set<HierarhyElement>& elements) {
        m_elements.insert(elements.begin(), elements.end());
    }
    void insert(const HierarhyElement& element) {
        m_elements.insert(element);
    }
    void print(const std::string& label) {
        qDebug() << "~~~" << QString::fromStdString(label);
        for (const HierarhyElement& element: m_elements) {
            qDebug() << QString::fromStdString(element.path) << element.isLeaf;
        }
        qDebug() << "\n";
    }

private:
    std::set<HierarhyElement> m_elements;
};
using HierarhyElementsPtr = std::shared_ptr<HierarhyElements>;


}  // namespace fp
