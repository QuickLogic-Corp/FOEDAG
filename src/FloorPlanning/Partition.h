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
    static int s_atomsPerTile;
public:
    static void resetIdGenerator() { s_idGenerator = 0; }

    // [aurora2#1725] atoms_per_tile from atomsets.json -- the packing density hint of
    // spec A.13.3, the same divisor its est_tiles field uses. Needed because clb atoms
    // and clb tiles are different units (see clbRequiredCount()). Static, like
    // s_idGenerator: it describes the netlist every partition is measured against, not
    // one partition. Left at the A.13.3 default when atomsets.json doesn't say.
    static void setAtomsPerTile(int atomsPerTile) {
        s_atomsPerTile = (atomsPerTile > 0) ? atomsPerTile : 1;
    }
    static int atomsPerTile() { return s_atomsPerTile; }

    Partition(const std::string& name = "");

    void setName(const std::string& name) { m_name = name; }

    const std::string name() const { return m_name; }

    void setColor(const QColor& color);

    const QColor& color() const { return m_color; }
    const QColor& colorTransparent() const { return m_colorTransparent; }

    int id() const { return m_id; }

    void addRegion(const RegionPtr& region);
    bool removeRegion(const RegionPtr& region);

    void clearElemenets() {
        m_elements.clear();
        m_clbAtomCount = m_dspRequiredCount = m_bramRequiredCount = 0;
    }
    void addElement(const HierarhyElement& element) {
        m_elements.insert(element);
        // [aurora2#1725] Running clb/dsp/bram counts, kept alongside the element set
        // rather than recomputed by every UI consumer (view label today; more are
        // coming) so they all read the same numbers without re-deriving them.
        // "Required" because these come from the atoms assigned to the partition --
        // what the design needs -- as opposed to *Available*Count() below, which is
        // what the partition's regions physically have room for.
        for (const std::string& atomName : element.vprNames) {
            const QString type = classifyAtomType(atomName);
            if (type == "dsp") ++m_dspRequiredCount;
            else if (type == "bram") ++m_bramRequiredCount;
            else ++m_clbAtomCount;
        }
    }

    const HierarhyElements& elements() const { return m_elements; }
    const std::map<int, RegionPtr> regions() const { return m_regions; }

    // [aurora2#1725] Every *RequiredCount() is in TILES, the unit *AvailableCount() below
    // reports, so the two can be compared (PartitionsListWidget's columns, DeviceGrid's
    // under-provisioned check). A dsp/bram atom IS one tile, but clb atoms are luts and
    // flops that pack many-to-a-tile, so that count has to be divided down: fft256's whole
    // "dut" is 19029 clb atoms, which is 1360 tiles at 14 atoms/tile, not 19029 of them.
    // A sizing hint, deliberately conservative -- the packer fits that design in 799 clb --
    // but in the same unit as what a region actually offers, which the raw atom count is not.
    int clbRequiredCount() const {
        return (m_clbAtomCount + s_atomsPerTile - 1) / s_atomsPerTile;
    }
    int dspRequiredCount() const { return m_dspRequiredCount; }
    int bramRequiredCount() const { return m_bramRequiredCount; }

    // Raw atom count behind clbRequiredCount(), for anything reporting atoms rather than
    // the tiles they pack into.
    int clbAtomCount() const { return m_clbAtomCount; }

    // [aurora2#1725] Tile counts by type across every region of this partition --
    // what's physically available, as opposed to *Required*Count() above. Computed
    // on demand rather than cached: a region's tiles can change after addRegion()
    // (resize/move), and Partition isn't notified of that, so a cached count would
    // go stale.
    int clbAvailableCount() const { return availableTileCount(Tile::Type::Clb); }
    int dspAvailableCount() const { return availableTileCount(Tile::Type::Dsp); }
    int bramAvailableCount() const { return availableTileCount(Tile::Type::Bram); }

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
    int m_clbAtomCount = 0;
    int m_dspRequiredCount = 0;
    int m_bramRequiredCount = 0;

    void updateRect();
    QColor colorFromIndex(int index) const;

    int availableTileCount(Tile::Type type) const {
        int count = 0;
        for (const auto& [id, region] : m_regions) {
            for (const auto& [index, tile] : region->tiles()) {
                if (tile && tile->type() == type) {
                    ++count;
                }
            }
        }
        return count;
    }

};
using PartitionPtr = std::shared_ptr<Partition>;

}  // namespace fp
