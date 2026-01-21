#include "Partition.h"

#include <QDebug>

namespace fp {

int Partition::s_idGenerator = 0;

void Partition::setElements(const HierarhyElementsPtr& elemenets)
{
#ifdef DEBUG_SELECTION_ELEMENTS
    if (elemenets) {
        elemenets->print(QString("Partition(%1)::setElements").arg(m_id).toStdString());
    }
#endif // DEBUG_SELECTION_ELEMENTS
    m_elements = elemenets;
}

std::unordered_set<std::string> Partition::collectOverlppedElements(const Partition& partition) const
{
    if (!partition.elements() || !m_elements) {
        return {};
    }
    std::unordered_set<std::string> elements;
    for (const HierarhyElement& element: *partition.elements()) {
        if (m_elements->contains(element.path)) {
            elements.insert(element.path);
        }
    }
    for (const HierarhyElement& element: *m_elements) {
        if (partition.elements()->contains(element.path)) {
            elements.insert(element.path);
        }
    }
    return elements;
}

void Partition::addRegion(const RegionPtr& region)
{
    m_regions[region->id()] = region;
    updateRect();
}

bool Partition::removeRegion(const RegionPtr& region) {
    auto it = m_regions.find(region->id());
    if (it != m_regions.end()) {
        m_regions.erase(it);
        return true;
    }
    return false;
}

void Partition::updateRect()
{
    m_rect = QRectF();
    bool isFirst = true;
    for (const auto& [id, region]: m_regions) {
        if (isFirst) {
            m_rect = region->rect();
            isFirst = false;
        } else {
            m_rect = m_rect.united(region->rect());
        }
    }
}

} // namespace fp
