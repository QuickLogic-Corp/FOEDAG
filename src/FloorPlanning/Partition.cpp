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
