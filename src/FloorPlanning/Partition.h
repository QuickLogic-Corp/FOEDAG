#pragma once

#include "Region.h"
#include "HierarhyElement.h"

#include <QPoint>
#include <QRect>
#include <QLine>

#include <memory>

namespace fp {

class Partition {
    static int s_idGenerator;
public:
    static void resetIdGenerator() { s_idGenerator = 0; }

    Partition(const std::string& name = "") {
        m_name = name;
        m_id = s_idGenerator++;
        if (m_name.empty()) {
            m_name = "partition" + std::to_string(m_id);
        }
    }

    void setName(const std::string& name) { m_name = name; }

#ifdef USE_TESTS
    bool operator==(const Partition& rhs) const {
        if (id() != rhs.id()) {
            return false;
        }
        if (name() != rhs.name()) {
            return false;
        }
        if (*elements() != *rhs.elements()) {
            return false;
        }
        if (*regions() != rhs.regions()) {
            return false;
        }
        return true;
    }
#endif // USE_TESTS

    const std::string name() const { return m_name; }

    int id() const { return m_id; }

    void addRegion(const RegionPtr& region);
    bool removeRegion(const RegionPtr& region);

    void setElements(const HierarhyElementsPtr& elements);

    const HierarhyElementsPtr& elements() const { return m_elements; }
    const std::map<int, RegionPtr> regions() const { return m_regions; }

    std::unordered_set<std::string> collectOverlppedElements(const Partition&) const;

    const QRectF& rect() const { return m_rect; }

private:
    int m_id = -1;
    std::string m_name;

    QRectF m_rect;

    HierarhyElementsPtr m_elements;
    std::map<int, RegionPtr> m_regions;

    void updateRect();

};
using PartitionPtr = std::shared_ptr<Partition>;

}  // namespace fp
