#pragma once

#include "Region.h"
#include "HierarhyElement.h"

#include <QPoint>
#include <QRect>
#include <QLine>
#include <QColor>

#include <memory>

namespace fp {

class Partition {
    static int s_idGenerator;
public:
    static void resetIdGenerator() { s_idGenerator = 0; }

    Partition(const std::string& name = "");

    void setName(const std::string& name) { m_name = name; }

    const std::string name() const { return m_name; }

    void setColor(const QColor& color);

    const QColor& color() const { return m_color; }
    const QColor& colorTransparent() const { return m_colorTransparent; }

    int id() const { return m_id; }

    void addRegion(const RegionPtr& region);
    bool removeRegion(const RegionPtr& region);

    void clearElemenets() { m_elements.clear(); m_clbCount = m_dspCount = m_bramCount = 0; }
    void addElement(const HierarhyElement& element) {
        m_elements.insert(element);
        // [aurora2#1725] Running clb/dsp/bram counts, kept alongside the element set
        // rather than recomputed by every UI consumer (view label today; more are
        // coming) so they all read the same numbers without re-deriving them.
        for (const std::string& atomName : element.vprNames) {
            const QString type = classifyAtomType(atomName);
            if (type == "dsp") ++m_dspCount;
            else if (type == "bram") ++m_bramCount;
            else ++m_clbCount;
        }
    }

    const HierarhyElements& elements() const { return m_elements; }
    const std::map<int, RegionPtr> regions() const { return m_regions; }

    int clbCount() const { return m_clbCount; }
    int dspCount() const { return m_dspCount; }
    int bramCount() const { return m_bramCount; }

    std::unordered_set<std::string> collectOverlappedElements(const Partition&) const;

    const QRectF& rect() const { return m_rect; }

private:
    int m_id = -1;
    std::string m_name;
    QColor m_color;
    QColor m_colorTransparent;

    QRectF m_rect;

    HierarhyElements m_elements;
    std::map<int, RegionPtr> m_regions;
    int m_clbCount = 0;
    int m_dspCount = 0;
    int m_bramCount = 0;

    void updateRect();
    QColor colorFromIndex(int index) const;

};
using PartitionPtr = std::shared_ptr<Partition>;

}  // namespace fp
