#include "Partition.h"

#include <cmath>

#include <QDebug>

namespace fp {

int Partition::s_idGenerator = 0;

// atomsets.json's own default (spec A.13.3). Overridden by setAtomsPerTile() once the
// project's atomsets.json has been read.
int Partition::s_atomsPerTile = 14;

Partition::Partition(const std::string& name): m_name(name) {
  m_id = s_idGenerator++;
  setColor(colorFromIndex(m_id));
  if (m_name.empty()) {
    m_name = "partition" + std::to_string(m_id);
  }
}

void Partition::setColor(const QColor& color)
{
  m_color = color;
  m_colorTransparent = m_color;
  m_colorTransparent.setAlphaF(0.4);
}

std::unordered_set<std::string> Partition::collectOverlappedElements(const Partition& partition) const
{
    // [aurora2#1725] An element covers its whole subtree, so "the same element in two
    // partitions" is not only an identical path: an instance here and something nested
    // inside it there claim the same atoms just as surely. Exact-path comparison alone
    // missed that, which is how partition1 could hold dut.instPerm20009 while partition2
    // held dut.instPerm20009.in_sw_0_0 with nothing reported -- and VPR cannot honour it,
    // a cluster sitting in one region only. The nested path is the one named, since that
    // is the narrower claim and the one to drop to resolve it.
    auto collectNested = [](const HierarhyElements& inner, const HierarhyElements& outer,
                            std::unordered_set<std::string>& into) {
        for (const HierarhyElement& element: inner) {
            std::size_t dot = element.path.rfind('.');
            while (dot != std::string::npos) {
                const std::string ancestor = element.path.substr(0, dot);
                if (outer.contains(ancestor)) {
                    into.insert(element.path);
                    break;
                }
                dot = ancestor.rfind('.');
            }
        }
    };

    std::unordered_set<std::string> elements;
    for (const HierarhyElement& element: partition.elements()) {
        if (m_elements.contains(element.path)) {
            elements.insert(element.path);
        }
    }
    for (const HierarhyElement& element: m_elements) {
        if (partition.elements().contains(element.path)) {
            elements.insert(element.path);
        }
    }
    collectNested(partition.elements(), m_elements, elements);
    collectNested(m_elements, partition.elements(), elements);
    return elements;
}

void Partition::addRegion(const RegionPtr& region)
{
    m_regions[region->id()] = region;
    region->setPartitionId(id());
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

QColor Partition::colorFromIndex(int index) const
{
  const double goldenAngle = 137.508;
  int hue = int(std::fmod(index * goldenAngle, 360.0));
  return QColor::fromHsv(hue, 180, 140);
}

} // namespace fp
