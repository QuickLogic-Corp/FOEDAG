#pragma once

#include <string>
#include <set>

#include <QDebug>

namespace fp {

class HierarhyElement {
public:
    HierarhyElement(const std::string& path): path(path) {}
    HierarhyElement(const std::string& path, bool isLeaf): path(path), isLeaf(isLeaf) {}
    HierarhyElement(const std::string& path, bool isLeaf, std::set<std::string> vprNames)
        : path(path), isLeaf(isLeaf), vprNames(std::move(vprNames)) {}

    bool operator==(const HierarhyElement& rhs) const {
        return ((path == rhs.path) && (isLeaf == rhs.isLeaf));
    }

    std::string path;          // user RTL name
    bool isLeaf = true;
    std::set<std::string> vprNames;  // resolved VPR atom names

    bool operator<(const HierarhyElement& rhs) const { return path < rhs.path; }
};

class HierarhyElements {
public:
    bool empty() const { return m_elements.empty(); }
    bool operator==(const HierarhyElements& rhs) const {
        if (size() != rhs.size()) {
            return false;
        }
        return m_elements == rhs.m_elements;
    }

    const std::set<HierarhyElement>& data() const { return m_elements; }

    std::size_t size() const { return m_elements.size(); }

    void clear() { m_elements.clear(); }

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

private:
    std::set<HierarhyElement> m_elements;
};

}  // namespace fp
