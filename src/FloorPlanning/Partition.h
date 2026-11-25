#pragma once

#include "Region.h"
#include "HierarhyElement.h"
#include "AtomSets.h"
#include "FloorplanContext.h"

#include <QPoint>
#include <QRect>
#include <QLine>
#include <QColor>

#include <memory>

namespace fp {

class Partition {
public:
    explicit Partition(FloorplanContextPtr context, const std::string& name = "");

    const FloorplanContext& context() const { return *m_context; }

    void setName(const std::string& name) { m_name = name; }

    // [aurora2#1725] A trailing comment from the .qdc line this partition came from, e.g.
    // "set_region i_a clb(2,4) p1  # keep near the DSP column". It annotates THIS constraint,
    // so it is carried here and written back on the same line rather than being hoisted to
    // the top of the file with the standalone comments, where it would lose its referent.
    void setComment(const std::string& comment) { m_comment = comment; }
    const std::string& comment() const { return m_comment; }

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
        m_dspTileIds.clear();
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
            const auto typeIt = m_context->atomTypes.find(atomName);
            const std::string realType =
                (typeIt != m_context->atomTypes.end()) ? typeIt->second : "";
            const QString type = classifyAtomType(atomName, realType);
            if (type == "dsp") {
                if (const auto tile = m_context->dspTiles.find(atomName);
                    tile != m_context->dspTiles.end()) {
                    if (m_dspTileIds.insert(tile->second).second) ++m_dspRequiredCount;
                } else if (countsAsDspTile(realType)) ++m_dspRequiredCount;
            } else if (type == "bram") ++m_bramRequiredCount;
            else ++m_clbAtomCount;
        }
    }

    // [aurora2#1725 stage P7] What design_resources.json contributes to this partition.
    //
    // Only the tier-2 measurement: the clb/dsp/bram the panel shows are tallied from this
    // partition's own atoms, which are exact post-synthesis and need nothing from this file.
    //
    // Computed on demand rather than accumulated in addElement(), because the entries NEST
    // -- atomsets.json reports a parent's figures as already including its children's.
    // Adding them incrementally would count a partition holding both a parent and a child
    // twice; only the maximal elements may contribute.
    struct ResourceContribution {
        int clbActual = 0;     // tier-2 measured tiles
        bool hasActual = false;
        bool actualShared = false;   // at least one contributor shares clusters
        bool actualPartial = false;  // some contributor had no measurement
    };

    ResourceContribution resourceContribution() const {
        ResourceContribution out;
        const DesignResources& designResources = m_context->designResources;
        if (!designResources.valid() || m_elements.empty()) {
            return out;
        }
        std::set<std::string> paths;
        for (const HierarhyElement& element : m_elements) {
            if (!isElementPattern(element.path)) {
                paths.insert(element.path);
                continue;
            }
            bool matched = false;
            for (const auto& [instance, entry] : designResources.instances) {
                if (elementPatternCovers(element.path, instance)) {
                    paths.insert(instance);
                    matched = true;
                }
            }
            out.actualPartial = out.actualPartial || !matched;
        }
        for (const std::string& path : paths) {
            if (hasAncestorIn(path, paths)) {
                continue;  // already accounted for by the ancestor, since entries nest
            }
            const auto it = designResources.instances.find(path);
            if (it == designResources.instances.end()) {
                out.actualPartial = true;
                continue;
            }
            if (it->second.hasClbActual) {
                out.clbActual += it->second.clbActual;
                out.hasActual = true;
                out.actualShared = out.actualShared || it->second.clbActualShared;
            } else {
                out.actualPartial = true;
            }
        }
        return out;
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
    // The estimate contribution is already in tiles (design_resources.json's clb_est
    // carries the A.13.2b safety margin), so it is added after the atom->tile division,
    // not before it.
    int clbRequiredCount() const {
        return (m_clbAtomCount + m_context->atomsPerTile - 1) / m_context->atomsPerTile;
    }
    int dspRequiredCount() const { return m_dspRequiredCount; }
    int bramRequiredCount() const { return m_bramRequiredCount; }

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
    FloorplanContextPtr m_context;
    int m_id = -1;
    std::string m_name;
    std::string m_comment;
    QColor m_color;
    QColor m_colorTransparent;

    QRectF m_rect;

    HierarhyElements m_elements;
    std::map<int, RegionPtr> m_regions;
    int m_clbAtomCount = 0;
    int m_dspRequiredCount = 0;
    std::set<std::string> m_dspTileIds;
    int m_bramRequiredCount = 0;

    void updateRect();
    QColor colorFromIndex(int index) const;

    // Whether any proper ancestor of `path` is also an element of this partition.
    static bool hasAncestorIn(const std::string& path, const std::set<std::string>& paths) {
        std::string::size_type dot = path.find('.');
        while (dot != std::string::npos) {
            if (paths.count(path.substr(0, dot))) return true;
            dot = path.find('.', dot + 1);
        }
        return false;
    }

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
