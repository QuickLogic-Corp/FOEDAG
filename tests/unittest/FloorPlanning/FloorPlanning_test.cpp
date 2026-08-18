#include <FloorPlanning/DeviceGridDescriptor.h>

#include <FloorPlanning/DeviceGrid.h>
#include <FloorPlanning/QdcSerializer.h>
#include <FloorPlanning/RtlInstanceModel.h>
#include <FloorPlanning/Partition.h>

#include <QPoint>
#include <QTemporaryDir>

#include <fstream>
#include <iostream>

#include "gtest/gtest.h"

fp::DeviceGridDescriptorPtr genTestDescriptor()
{
    int deviceCols = 32;
    int deviceRows = 32;
    std::set<int> dspColumns = {7, 20};
    std::set<int> bramColumns = {13, 26};
    fp::DeviceGridDescriptorPtr descriptor = std::make_shared<fp::DeviceGridDescriptor>(deviceCols, deviceRows,
                                                                                dspColumns, bramColumns,
                                                                                QSize{1, 3}, QSize{1, 6});
    return descriptor;
}

std::string toString(const fp::HierarhyElements& elements)
{
    std::string result;
    for (const auto& element: elements.data()) {
        if (!result.empty()) {
            result += ",";
        }
        result += element.path;
        if (!element.isLeaf) {
            result += ".*";
        }
    }
    return result;
};

fp::HierarhyElements genTestElements()
{
    fp::HierarhyElements elements;
    elements.insert({fp::HierarhyElement{"dut.tri.el0.sub2", true},
                      fp::HierarhyElement{"dut.tri.el1", false},
                      fp::HierarhyElement{"dut.tri.el2", true},
                      fp::HierarhyElement{"top", false}});
    EXPECT_EQ("dut.tri.el0.sub2,dut.tri.el1.*,dut.tri.el2,top.*", toString(elements));
    return elements;
}

bool is_equal(const fp::Region& a, const fp::Region& b)
{
    if (a.id() != b.id()) {
        return false;
    }
    if (a.bottomLeftGridIndex() != b.bottomLeftGridIndex()) {
        return false;
    }
    if (a.bottomLeftTileIndex() != b.bottomLeftTileIndex()) {
        return false;
    }
    if (a.topRightGridIndex() != b.topRightGridIndex()) {
        return false;
    }
    // the rects could be slightly different, better to test comparison based on included tiles indexes
    // qInfo() << a.rect() << b.rect();
    // if (a.rect() != b.rect()) {
    //  return false;
    //}

    const std::unordered_map<fp::Tile::Index, fp::TilePtr>& at = a.tiles();
    const std::unordered_map<fp::Tile::Index, fp::TilePtr>& bt = b.tiles();
    bool isTilesIndexesSame =
        at.size() == bt.size() &&
        std::equal(at.begin(), at.end(),
                   bt.begin(),
                   [](auto& a, auto& b) {
                       return a.first == b.first;
                   });
    if (!isTilesIndexesSame) {
        return false;
    }
    return true;
}

bool is_equal(const fp::Partition& a, const fp::Partition& b)
{
    static auto regionsEqualByValue = [](const std::map<int, fp::RegionPtr>& a,
                                         const std::map<int, fp::RegionPtr>& b)->bool
    {
        if (a.size() != b.size()) {
            return false;
        }

        for (const auto& [keya, regiona]: a) {
            auto itb = b.find(keya);
            if (itb == b.end()) {
                return false;
            }

            const fp::RegionPtr& regionb = itb->second;
            if (!is_equal(*regiona, *regionb)) {
                return false;
            }
        }
        return true;
    };

    if (a.id() != b.id()) {
        return false;
    }
    if (a.name() != b.name()) {
        return false;
    }
    if (a.elements() != b.elements()) {
        return false;
    }
    if (!regionsEqualByValue(a.regions(), b.regions())) {
        return false;
    }
    return true;
}

std::optional<QPointF> findBottomLeftTilePoint(const fp::DeviceGrid& device, const fp::Tile::Index& idx)
{
    if (auto it = device.tiles().find(idx); it != device.tiles().end()) {
        const fp::TilePtr& tile = it->second;
        return tile->rect().bottomLeft();
    }
    if (auto it = device.tileFragments().find(idx); it != device.tileFragments().end()) {
        return device.bottomLeftPoint(idx);
    }

    return std::nullopt;
}

std::optional<QPointF> findTopRightTilePoint(const fp::DeviceGrid& device, const fp::Tile::Index& idx)
{
    if (auto it = device.tiles().find(idx); it != device.tiles().end()) {
        const fp::TilePtr& tile = it->second;
        return tile->rect().topRight();
    }
    if (auto it = device.tileFragments().find(idx); it != device.tileFragments().end()) {
        return device.topRightPoint(idx);
    }

    return std::nullopt;
}

bool test_partition(const fp::Tile::Index& bottomLeftIndex, const fp::Tile::Index& topRightIndex, const std::string& expectedRegionStr)
{
    std::cout << "testing " << expectedRegionStr << std::endl;
    fp::Partition::resetIdGenerator();
    fp::Region::resetIdGenerator();

    fp::DeviceGridDescriptorPtr descriptor = genTestDescriptor();

    fp::DeviceGrid device(descriptor);

    // create region
    std::optional<QPointF> bottomLeftPointOpt = findBottomLeftTilePoint(device, bottomLeftIndex);
    std::optional<QPointF> topRightPointOpt = findTopRightTilePoint(device, topRightIndex);

    fp::RegionPtr region = std::make_shared<fp::Region>(bottomLeftPointOpt.value(), topRightPointOpt.value());
    std::unordered_map<fp::Tile::Index, fp::TilePtr> tiles = device.findTiles(region->rect());
    region->setTiles(tiles);

    // create partition
    fp::PartitionPtr partition = std::make_shared<fp::Partition>("dummy");
    fp::HierarhyElements elements = genTestElements();
    for (const auto& element: elements) {
        partition->addElement(element);
    }
    partition->addRegion(region);

    device.addPartition(partition);

    fp::QdcSerializer qdc;
    qdc.save(device, expectedRegionStr+".qdc");

    fp::DeviceGrid newDevice(descriptor);

    std::vector<std::string> lines = qdc.readCommands(expectedRegionStr+".qdc");
    qdc.load(newDevice, lines);

    if(1 != lines.size()) {
        return false;
    }
    std::string expectedLine = "set_region " + toString(elements) + " " + expectedRegionStr + " " + partition->name();
    if (expectedLine != lines[0]) {
        return false;
    }
    if (1 != newDevice.partitions().size()) {
        return false;
    }

    // the data under pointer must be same but pointers must be different
    if (device.partitions().at(0) == newDevice.partitions().at(0)) {
        return false;
    }
    if (!is_equal(*device.partitions().at(0), *newDevice.partitions().at(0))) {
        return false;
    }

    return true;
}

TEST(FloorPlanning, saveLoadPartition)
{
    // clb
    EXPECT_TRUE(test_partition(fp::Tile::Index{2,4}, fp::Tile::Index{2,4}, "clb(2,4)"));
    // clb-clb
    EXPECT_TRUE(test_partition(fp::Tile::Index{2,2}, fp::Tile::Index{2,4}, "clb(2,2):clb(2,4)"));
    EXPECT_TRUE(test_partition(fp::Tile::Index{2,4}, fp::Tile::Index{4,4}, "clb(2,4):clb(4,4)"));
    // clb-dsp-clb
    EXPECT_TRUE(test_partition(fp::Tile::Index{6,2}, fp::Tile::Index{8,4}, "clb(6,2):clb(8,4)"));
    EXPECT_TRUE(test_partition(fp::Tile::Index{6,3}, fp::Tile::Index{9,5}, "clb(6,3):clb(9,5)"));
    // clb-bram-clb
    EXPECT_TRUE(test_partition(fp::Tile::Index{12,2}, fp::Tile::Index{14,8}, "clb(12,2):clb(14,8)"));
    EXPECT_TRUE(test_partition(fp::Tile::Index{12,3}, fp::Tile::Index{14,9}, "clb(12,3):clb(14,9)"));

    // dsp
    EXPECT_TRUE(test_partition(fp::Tile::Index{7,2}, fp::Tile::Index{7,2}, "dsp(7,2)"));
    // dsp-dsp
    EXPECT_TRUE(test_partition(fp::Tile::Index{7,2}, fp::Tile::Index{7,5}, "dsp(7,2):dsp(7,5)"));

    // bram
    EXPECT_TRUE(test_partition(fp::Tile::Index{13,2}, fp::Tile::Index{13,2}, "bram(13,2)"));
    // bram-bram
    EXPECT_TRUE(test_partition(fp::Tile::Index{13,2}, fp::Tile::Index{13,8}, "bram(13,2):bram(13,8)"));

    // dsp-clb
    EXPECT_TRUE(test_partition(fp::Tile::Index{7,2}, fp::Tile::Index{8,5}, "dsp(7,2):clb(8,5)"));

    // clb-dsp
    EXPECT_TRUE(test_partition(fp::Tile::Index{6,2}, fp::Tile::Index{7,2}, "clb(6,2):dsp(7,2)"));

    // bram-clb
    EXPECT_TRUE(test_partition(fp::Tile::Index{13,2}, fp::Tile::Index{14,7}, "bram(13,2):clb(14,7)"));

    // clb-bram
    EXPECT_TRUE(test_partition(fp::Tile::Index{12,2}, fp::Tile::Index{13,2}, "clb(12,2):bram(13,2)"));

    // dsp-bram
    EXPECT_TRUE(test_partition(fp::Tile::Index{7,2}, fp::Tile::Index{13,2}, "dsp(7,2):bram(13,2)"));

    // bram-dsp
    EXPECT_TRUE(test_partition(fp::Tile::Index{13,2}, fp::Tile::Index{20,5}, "bram(13,2):dsp(20,5)"));

    // non rectangular selection case, where clb(7,4) is part of dsp block
    EXPECT_TRUE(test_partition(fp::Tile::Index{7,4}, fp::Tile::Index{8,7}, "clb(7,4):clb(8,7)"));

    // special case when topright match to start of dsp (starts at dsp(6,26))
    EXPECT_TRUE(test_partition(fp::Tile::Index{2,23}, fp::Tile::Index{6,26}, "clb(2,23):clb(6,26)"));
}

// [aurora2#1725 stage P1] REQ-002: the .qdc carries RTL names, never post-synthesis ones.
//
// This is a regression guard, not a formality. Serializing resolved VPR atom names is what
// produced the defect the feature exists to fix: a .qdc holding 39 enumerated atoms for an
// instance with 523 in the netlist, under-constraining it by 13x and going stale on every
// resynthesis. Expansion belongs at emission time (REQ-003).
TEST(QdcSerializer, ResolvedVprNamesAreNeverWrittenToTheQdc)
{
    fp::Partition::resetIdGenerator();
    fp::Region::resetIdGenerator();

    fp::DeviceGridDescriptorPtr descriptor = genTestDescriptor();
    fp::DeviceGrid device(descriptor);

    std::optional<QPointF> bl = findBottomLeftTilePoint(device, fp::Tile::Index{2, 2});
    std::optional<QPointF> tr = findTopRightTilePoint(device, fp::Tile::Index{4, 4});
    ASSERT_TRUE(bl.has_value());
    ASSERT_TRUE(tr.has_value());

    fp::RegionPtr region = std::make_shared<fp::Region>(bl.value(), tr.value());
    region->setTiles(device.findTiles(region->rect()));

    fp::PartitionPtr partition = std::make_shared<fp::Partition>("p_rtl_only");
    partition->addRegion(region);

    // An element that HAS resolved VPR names -- the case that used to leak them into the file.
    partition->addElement(fp::HierarhyElement{
        "i_mul_24", /*isLeaf=*/false,
        {"i_mul_24.fract_o_adder_carry_sumout_cout[20]",
         "i_mul_24.count_sdffre_Q_1_D"}});
    device.addPartition(partition);

    fp::QdcSerializer qdc;
    const std::string content = qdc.serialize(device);

    EXPECT_NE(content.find("i_mul_24.*"), std::string::npos)
        << "the whole-instance RTL form must be written";
    EXPECT_EQ(content.find("fract_o_adder_carry_sumout_cout"), std::string::npos)
        << "a post-synthesis atom name reached the .qdc (REQ-002 violation)";
    EXPECT_EQ(content.find("count_sdffre_Q_1_D"), std::string::npos)
        << "a post-synthesis atom name reached the .qdc (REQ-002 violation)";
}

// ---------------------------------------------------------------------------
// [aurora2#1725 stage P0b/P1] RtlInstanceModel -- the RTL instance tree the panel shows.
// ---------------------------------------------------------------------------
namespace {

std::filesystem::path writeTempJson(const std::filesystem::path& dir,
                                    const std::string& name,
                                    const std::string& content)
{
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / name;
    std::ofstream stream(path);
    stream << content;
    return path;
}

const char* kInstancesJson = R"JSON({
  "top": "fpu_single",
  "instances": [
    { "path": "i_mul_24", "component": "mul_24", "module_raw": "mul_24_default(rtl)",
      "src": "fpu_single.vhd:225.2-225.10", "component_src": "mul_24.vhd:52.8-52.14",
      "parameters": {}, "last_status": "unknown" },
    { "path": "i_serial_mul", "component": "serial_mul", "module_raw": "serial_mul_default(rtl)",
      "src": "fpu_single.vhd:237.2-237.14", "component_src": "serial_mul.vhd:1.1-1.2",
      "parameters": {}, "last_status": "unknown" },
    { "path": "i_mul_24.i_inner", "component": "inner", "module_raw": "inner_default(rtl)",
      "src": "mul_24.vhd:70.2-70.9", "component_src": "inner.vhd:3.1-3.6",
      "parameters": {}, "last_status": "unknown" }
  ]
})JSON";

const char* kValidationJson = R"JSON({
  "netlist_sha256": "abc123",
  "instances": {
    "i_mul_24":     { "verdict": "complete", "atom_count": 536, "checks": {} },
    "i_serial_mul": { "verdict": "deleted", "atom_count": 0,
                      "reason": "no atoms in the netlist; synthesis reported 'Deleting now unused module serial_mul_default'" },
    "i_mul_24.i_inner": { "verdict": "partial", "atom_count": 12,
                          "warnings": ["check 3 not run: [6] unavailable",
                                       "check 2b: 1 atom(s) where prefix and scopename disagree"] }
  }
})JSON";

}  // namespace

TEST(RtlInstanceModel, MissingFileFailsWithAnActionableMessage)
{
    // The panel must say "run synthesis" rather than showing an empty tree or a VPR error.
    fp::RtlInstanceModel model;
    EXPECT_FALSE(model.loadInstances("definitely/not/here/instances.json"));
    EXPECT_NE(model.error().find("SYNTHESIS"), std::string::npos);
    EXPECT_TRUE(model.empty());
}

TEST(RtlInstanceModel, MalformedJsonIsRejectedNotSilentlyEmpty)
{
    const auto dir = std::filesystem::temp_directory_path() / "fp_rtlmodel_bad";
    const auto path = writeTempJson(dir, "instances.json", "{ this is not json ");
    fp::RtlInstanceModel model;
    EXPECT_FALSE(model.loadInstances(path));
    EXPECT_NE(model.error().find("valid JSON"), std::string::npos);
}

TEST(RtlInstanceModel, LoadsInstancesAndNestsThemByDottedPath)
{
    const auto dir = std::filesystem::temp_directory_path() / "fp_rtlmodel_ok";
    const auto path = writeTempJson(dir, "instances.json", kInstancesJson);

    fp::RtlInstanceModel model;
    ASSERT_TRUE(model.loadInstances(path)) << model.error();
    EXPECT_EQ(model.top(), "fpu_single");
    EXPECT_EQ(model.size(), 3u);

    // i_mul_24.i_inner is a child of i_mul_24, so the parent is not a leaf.
    const fp::RtlInstance* parent = model.find("i_mul_24");
    ASSERT_NE(parent, nullptr);
    EXPECT_FALSE(parent->isLeaf);
    ASSERT_EQ(parent->children.size(), 1u);
    EXPECT_EQ(parent->children.front(), "i_mul_24.i_inner");
    EXPECT_EQ(parent->component, "mul_24");
    EXPECT_EQ(parent->src, "fpu_single.vhd:225.2-225.10");

    // Only the two top-level instances are roots; the nested one is not.
    const std::vector<std::string> roots = model.roots();
    ASSERT_EQ(roots.size(), 2u);
    EXPECT_EQ(roots[0], "i_mul_24");
    EXPECT_EQ(roots[1], "i_serial_mul");
}

TEST(RtlInstanceModel, StatusIsUnknownUntilVerdictsAreMerged)
{
    // Absence of a verdict must never read as usable.
    const auto dir = std::filesystem::temp_directory_path() / "fp_rtlmodel_nostatus";
    const auto path = writeTempJson(dir, "instances.json", kInstancesJson);
    fp::RtlInstanceModel model;
    ASSERT_TRUE(model.loadInstances(path));
    for (const fp::RtlInstance& instance : model.instances()) {
        EXPECT_EQ(instance.status, "unknown");
        EXPECT_FALSE(instance.constrainable());
    }
}

TEST(RtlInstanceModel, MergedVerdictsDriveWhatThePanelCanShow)
{
    const auto dir = std::filesystem::temp_directory_path() / "fp_rtlmodel_verdicts";
    const auto instances = writeTempJson(dir, "instances.json", kInstancesJson);
    const auto verdicts = writeTempJson(dir, "validation.json", kValidationJson);

    fp::RtlInstanceModel model;
    ASSERT_TRUE(model.loadInstances(instances)) << model.error();
    ASSERT_TRUE(model.mergeVerdicts(verdicts)) << model.error();

    const fp::RtlInstance* complete = model.find("i_mul_24");
    ASSERT_NE(complete, nullptr);
    EXPECT_EQ(complete->status, "complete");
    EXPECT_EQ(complete->atomCount, 536);
    EXPECT_TRUE(complete->constrainable());

    // A deleted instance stays in the tree so it can be greyed out, and carries the cause.
    const fp::RtlInstance* deleted = model.find("i_serial_mul");
    ASSERT_NE(deleted, nullptr);
    EXPECT_TRUE(deleted->deleted());
    EXPECT_FALSE(deleted->constrainable());
    EXPECT_NE(deleted->statusReason.find("Deleting now unused module"), std::string::npos);

    // A partial instance is still constrainable, but the reason shown must be the real check
    // failure, not the "check 3 not run" bookkeeping note.
    const fp::RtlInstance* partial = model.find("i_mul_24.i_inner");
    ASSERT_NE(partial, nullptr);
    EXPECT_TRUE(partial->partial());
    EXPECT_TRUE(partial->constrainable());
    EXPECT_NE(partial->statusReason.find("scopename"), std::string::npos);
    EXPECT_EQ(partial->statusReason.find("not run"), std::string::npos);
}

TEST(RtlInstanceModel, DeletedInstancesAreNotOfferedAsConstraintTargets)
{
    const auto dir = std::filesystem::temp_directory_path() / "fp_rtlmodel_elements";
    const auto instances = writeTempJson(dir, "instances.json", kInstancesJson);
    const auto verdicts = writeTempJson(dir, "validation.json", kValidationJson);

    fp::RtlInstanceModel model;
    ASSERT_TRUE(model.loadInstances(instances));
    ASSERT_TRUE(model.mergeVerdicts(verdicts));

    const fp::HierarhyElements elements = model.toHierarhyElements();
    EXPECT_TRUE(elements.contains("i_mul_24"));
    EXPECT_TRUE(elements.contains("i_mul_24.i_inner"));
    EXPECT_FALSE(elements.contains("i_serial_mul"))
        << "an instance with no atoms cannot be constrained";

    // And no element carries post-synthesis names (REQ-002).
    for (const fp::HierarhyElement& element : elements) {
        EXPECT_TRUE(element.vprNames.empty());
    }
}

TEST(RtlInstanceModel, VerdictsForUnknownInstancesAreIgnoredNotFatal)
{
    // Two files from different elaborations should not crash the panel.
    const auto dir = std::filesystem::temp_directory_path() / "fp_rtlmodel_mismatch";
    const auto instances = writeTempJson(dir, "instances.json", kInstancesJson);
    const auto verdicts = writeTempJson(
        dir, "validation.json",
        R"({"instances": {"i_does_not_exist": {"verdict": "complete", "atom_count": 1}}})");

    fp::RtlInstanceModel model;
    ASSERT_TRUE(model.loadInstances(instances));
    EXPECT_TRUE(model.mergeVerdicts(verdicts));
    EXPECT_EQ(model.find("i_mul_24")->status, "unknown");
}

// [aurora2#1725 stage P7] design_resources.json -> Partition sizing.
//
// The whole point of the tier-1 path is the window BEFORE synthesis, where no atoms exist
// and the panel previously sized every partition at 0. These pin the two things that must
// not regress: that an estimate is used when there is nothing else, and that it is never
// mixed into or mistaken for measured atom counts.

namespace {

fp::DesignResources makeResources(int tier, std::map<std::string, fp::DesignResourceEntry> instances)
{
    fp::DesignResources resources;
    resources.tier = tier;
    resources.tierName = (tier == 1) ? "estimate from elaboration (pre-synthesis)"
                                     : "exact primitives (post-synthesis)";
    resources.instances = std::move(instances);
    return resources;
}

fp::DesignResourceEntry entry(int clbEst, int dsp, int bram)
{
    fp::DesignResourceEntry e;
    e.clbEst = clbEst;
    e.dsp = dsp;
    e.bram = bram;
    return e;
}

}  // namespace

TEST(PartitionResources, PreSynthesisEstimateSizesAPartitionWithNoAtoms)
{
    // Before synthesis there is no atomsets.json, so every element's vprNames is empty and
    // the atom-based tally has nothing to count. Without the tier-1 fallback this partition
    // reports 0 clb for a design that needs 40 tiles.
    fp::Partition::setDesignResources(makeResources(1, {{"i_mul_24", entry(40, 4, 0)}}));

    fp::Partition partition("p");
    partition.addElement(fp::HierarhyElement{"i_mul_24", /*isLeaf*/ true});

    EXPECT_EQ(partition.clbRequiredCount(), 40);
    EXPECT_EQ(partition.dspRequiredCount(), 4);
    EXPECT_TRUE(partition.isEstimated());

    fp::Partition::setDesignResources(fp::DesignResources{});
}

TEST(PartitionResources, MeasuredAtomsAreNeverMarkedAsEstimated)
{
    // An element that HAS atoms is counted from them, exactly, and the estimate is not
    // consulted at all -- even though a tier-1 file is loaded and names this instance.
    fp::Partition::setDesignResources(makeResources(1, {{"i_mul_24", entry(40, 4, 0)}}));
    fp::Partition::setAtomsPerTile(14);

    fp::Partition partition("p");
    partition.addElement(fp::HierarhyElement{
        "i_mul_24", /*isLeaf*/ true,
        std::set<std::string>{"i_mul_24.a0", "i_mul_24.a1", "i_mul_24.a2"}});

    EXPECT_EQ(partition.clbRequiredCount(), 1);  // 3 atoms / 14 per tile, rounded up
    EXPECT_EQ(partition.dspRequiredCount(), 0);
    EXPECT_FALSE(partition.isEstimated());

    fp::Partition::setDesignResources(fp::DesignResources{});
}

TEST(PartitionResources, PostSynthesisTiersNeverFeedThePerPartitionTally)
{
    // Tier 2/3 entries NEST -- an ancestor's counts already include its descendants' -- so
    // adding them to a per-partition tally would double-count a partition holding both a
    // parent and a child. Only tier 1, whose entries are flat, may be used this way.
    fp::Partition::setDesignResources(makeResources(2, {{"i_mul_24", entry(40, 4, 0)}}));

    fp::Partition partition("p");
    partition.addElement(fp::HierarhyElement{"i_mul_24", /*isLeaf*/ true});

    EXPECT_EQ(partition.clbRequiredCount(), 0);
    EXPECT_EQ(partition.dspRequiredCount(), 0);
    EXPECT_FALSE(partition.isEstimated());

    fp::Partition::setDesignResources(fp::DesignResources{});
}

TEST(PartitionResources, AnInstanceWithNoEstimateContributesNothing)
{
    // A path deeper than the flat tier-1 map goes has no entry. It must contribute 0 rather
    // than fall back to some other instance's figures.
    fp::Partition::setDesignResources(makeResources(1, {{"i_mul_24", entry(40, 4, 0)}}));

    fp::Partition partition("p");
    partition.addElement(fp::HierarhyElement{"i_mul_24.inner", /*isLeaf*/ true});

    EXPECT_EQ(partition.clbRequiredCount(), 0);
    EXPECT_FALSE(partition.isEstimated());

    fp::Partition::setDesignResources(fp::DesignResources{});
}

TEST(PartitionResources, NoDesignResourcesFileLeavesTheOldBehaviourUntouched)
{
    // A project that has never been compiled, or a device whose template has no P2/P3
    // blocks, must behave exactly as it did before this stage existed.
    fp::Partition::setDesignResources(fp::DesignResources{});

    fp::Partition partition("p");
    partition.addElement(fp::HierarhyElement{"i_mul_24", /*isLeaf*/ true});

    EXPECT_EQ(partition.clbRequiredCount(), 0);
    EXPECT_FALSE(partition.isEstimated());
    EXPECT_FALSE(fp::Partition::designResources().valid());
}

TEST(PartitionResources, ClearingElementsAlsoClearsTheEstimate)
{
    // clearElemenets() is what a reload goes through; a stale estimate surviving it would
    // be added a second time when the elements come back.
    fp::Partition::setDesignResources(makeResources(1, {{"i_mul_24", entry(40, 4, 0)}}));

    fp::Partition partition("p");
    partition.addElement(fp::HierarhyElement{"i_mul_24", /*isLeaf*/ true});
    ASSERT_EQ(partition.clbRequiredCount(), 40);

    partition.clearElemenets();
    EXPECT_EQ(partition.clbRequiredCount(), 0);
    EXPECT_FALSE(partition.isEstimated());

    partition.addElement(fp::HierarhyElement{"i_mul_24", /*isLeaf*/ true});
    EXPECT_EQ(partition.clbRequiredCount(), 40);  // not 80

    fp::Partition::setDesignResources(fp::DesignResources{});
}

// [aurora2#1725 stage P7] tier-1 nesting and tier-3 measured tiles.

namespace {

fp::DesignResourceEntry entryWithActual(int clbEst, int clbActual, bool shared = false)
{
    fp::DesignResourceEntry e;
    e.clbEst = clbEst;
    e.clbActual = clbActual;
    e.hasClbActual = true;
    e.clbActualShared = shared;
    return e;
}

}  // namespace

TEST(PartitionResources, NestedTier1EstimatesAreNotCountedTwice)
{
    // The tier-1 estimator now walks the whole instance tree, so a parent's figures already
    // include its children's. A partition holding both must count the parent only.
    fp::Partition::setDesignResources(makeResources(1, {
        {"dut", entry(100, 4, 0)},
        {"dut.a", entry(40, 2, 0)},
    }));

    fp::Partition partition("p");
    partition.addElement(fp::HierarhyElement{"dut", true});
    partition.addElement(fp::HierarhyElement{"dut.a", true});

    EXPECT_EQ(partition.clbRequiredCount(), 100);  // not 140
    EXPECT_EQ(partition.dspRequiredCount(), 4);    // not 6

    fp::Partition::setDesignResources(fp::DesignResources{});
}

TEST(PartitionResources, SiblingsOnDifferentBranchesStillAddUp)
{
    // Only ancestors collapse -- two unrelated instances are both maximal.
    fp::Partition::setDesignResources(makeResources(1, {
        {"dut.a", entry(40, 1, 0)},
        {"dut.b", entry(25, 2, 0)},
    }));

    fp::Partition partition("p");
    partition.addElement(fp::HierarhyElement{"dut.a", true});
    partition.addElement(fp::HierarhyElement{"dut.b", true});

    EXPECT_EQ(partition.clbRequiredCount(), 65);
    EXPECT_EQ(partition.dspRequiredCount(), 3);

    fp::Partition::setDesignResources(fp::DesignResources{});
}

TEST(PartitionResources, MeasuredTilesAreReportedSeparatelyFromTheEstimate)
{
    // clb_actual is a measurement and clb_est a projection; they must not be conflated.
    fp::Partition::setDesignResources(makeResources(3, {{"i_a", entryWithActual(40, 37)}}));

    fp::Partition partition("p");
    partition.addElement(fp::HierarhyElement{"i_a", true});

    const auto contribution = partition.resourceContribution();
    EXPECT_TRUE(contribution.hasActual);
    EXPECT_EQ(contribution.clbActual, 37);
    EXPECT_FALSE(contribution.actualShared);
    // Tier 3 is not an estimate, so the required column must not be marked with "~".
    EXPECT_FALSE(partition.isEstimated());

    fp::Partition::setDesignResources(fp::DesignResources{});
}

TEST(PartitionResources, SharedClustersMakeTheMeasuredTotalAnUpperBound)
{
    // A cluster holding atoms from two branches is counted for both, so the sum over-counts
    // and the UI must not present it as exact.
    fp::Partition::setDesignResources(makeResources(3, {
        {"i_a", entryWithActual(10, 8, /*shared*/ true)},
    }));

    fp::Partition partition("p");
    partition.addElement(fp::HierarhyElement{"i_a", true});

    EXPECT_TRUE(partition.resourceContribution().actualShared);

    fp::Partition::setDesignResources(fp::DesignResources{});
}

TEST(PartitionResources, AnElementWithNoMeasurementMarksTheTotalIncomplete)
{
    // Silently summing what IS measured would understate the partition without saying so.
    fp::Partition::setDesignResources(makeResources(3, {{"i_a", entryWithActual(10, 8)}}));

    fp::Partition partition("p");
    partition.addElement(fp::HierarhyElement{"i_a", true});
    partition.addElement(fp::HierarhyElement{"i_unknown", true});

    const auto contribution = partition.resourceContribution();
    EXPECT_TRUE(contribution.hasActual);
    EXPECT_TRUE(contribution.actualPartial);

    fp::Partition::setDesignResources(fp::DesignResources{});
}

TEST(PartitionResources, NoPlacementMeansNoMeasuredTiles)
{
    // Tiers 1 and 2 have no clb_actual at all; the column must stay empty rather than 0.
    fp::Partition::setDesignResources(makeResources(2, {{"i_a", entry(10, 0, 0)}}));

    fp::Partition partition("p");
    partition.addElement(fp::HierarhyElement{"i_a", true});

    EXPECT_FALSE(partition.resourceContribution().hasActual);

    fp::Partition::setDesignResources(fp::DesignResources{});
}

// [aurora2#1725] QdcSerializer round-trip safety. This file persists the user's floorplan,
// so a defect here loses work rather than merely reporting the wrong number.

namespace {

std::filesystem::path writeQdc(const std::string& dirName, const std::string& content)
{
    const auto dir = std::filesystem::temp_directory_path() / dirName;
    std::filesystem::create_directories(dir);
    const auto path = dir / "fp.qdc";
    std::ofstream out(path);
    out << content;
    out.close();
    return path;
}

std::string readAll(const std::filesystem::path& path)
{
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

TEST(QdcSerializer, CommentsSurviveASaveRoundTrip)
{
    // readCommands() stripped a comment to an empty string and skipped it, so it never
    // reached load(); m_reservedContent is filled only from the lines load() receives, so
    // every comment was destroyed by the next save. The testcase .qdc files carry their
    // entire rationale in exactly such headers.
    const auto qdc = writeQdc("fp_qdc_comments",
                              "# why this floorplan exists\n"
                              "# second line of the rationale\n"
                              "\n"
                              "set_region i_a clb(2,4) p1\n");

    fp::DeviceGrid device(genTestDescriptor());
    fp::QdcSerializer serializer;
    serializer.load(device, qdc);
    ASSERT_TRUE(serializer.save(device, qdc));

    const std::string content = readAll(qdc);
    EXPECT_NE(content.find("# why this floorplan exists"), std::string::npos);
    EXPECT_NE(content.find("# second line of the rationale"), std::string::npos);
    // ...and the command itself is still there.
    EXPECT_NE(content.find("set_region"), std::string::npos);
}

TEST(QdcSerializer, MalformedCoordinatesAreASyntaxErrorNotACrash)
{
    // The .qdc is user-editable, so std::stoi's input is untrusted. Nothing in the load path
    // catches, and load() runs from a Qt slot, so a typo took the whole IDE down.
    const auto qdc = writeQdc("fp_qdc_badcoord", "set_region i_a clb(abc,2) p1\n");

    fp::DeviceGrid device(genTestDescriptor());
    fp::QdcSerializer serializer;
    EXPECT_NO_THROW(serializer.load(device, qdc));
}

TEST(QdcSerializer, OutOfRangeCoordinatesAreASyntaxErrorNotACrash)
{
    // stoi throws out_of_range as readily as invalid_argument.
    const auto qdc = writeQdc("fp_qdc_bigcoord",
                              "set_region i_a clb(999999999999999999999,2) p1\n");

    fp::DeviceGrid device(genTestDescriptor());
    fp::QdcSerializer serializer;
    EXPECT_NO_THROW(serializer.load(device, qdc));
}

TEST(QdcSerializer, ATrailingCommentDoesNotBreakTheCommandOnTheSameLine)
{
    const auto qdc = writeQdc("fp_qdc_trailing", "set_region i_a clb(2,4) p1  # note\n");

    fp::DeviceGrid device(genTestDescriptor());
    fp::QdcSerializer serializer;
    serializer.load(device, qdc);
    ASSERT_EQ(device.partitions().size(), 1u);
    EXPECT_EQ(device.partitions().at(0)->name(), "p1");
}

TEST(QdcSerializer, AnUnparsableRegionIsPreservedRatherThanDiscardedOnSave)
{
    // The partition used to be added to the device before its regions were parsed, so a bad
    // coordinate left it holding elements and no region. serialize() skips only partitions
    // with no ELEMENTS, so the next save rewrote it without the region and the constraint
    // was gone -- a typo in a hand-edited .qdc silently destroyed what it was written in.
    const auto qdc = writeQdc("fp_qdc_badregion", "set_region i_a clb(abc,2) p1\n");

    fp::DeviceGrid device(genTestDescriptor());
    fp::QdcSerializer serializer;
    serializer.load(device, qdc);

    // Not half-loaded: no partition rather than one that would round-trip without a region.
    EXPECT_TRUE(device.partitions().empty());

    ASSERT_TRUE(serializer.save(device, qdc));
    const std::string content = readAll(qdc);
    EXPECT_NE(content.find("i_a"), std::string::npos);
    EXPECT_NE(content.find("clb(abc,2)"), std::string::npos);
    EXPECT_NE(content.find("p1"), std::string::npos);
}

TEST(QdcSerializer, OneBadRegionDoesNotCostThePartitionItsGoodOnes)
{
    // A partition may hold several regions. Restoring the ones that parsed and dropping the
    // rest would write back a SMALLER partition than the user drew, which is the same silent
    // loss in a less obvious form.
    const auto qdc = writeQdc("fp_qdc_mixedregion",
                              "set_region i_a clb(2,4):clb(4,4),clb(oops,9) p1\n");

    fp::DeviceGrid device(genTestDescriptor());
    fp::QdcSerializer serializer;
    serializer.load(device, qdc);
    EXPECT_TRUE(device.partitions().empty());

    ASSERT_TRUE(serializer.save(device, qdc));
    const std::string content = readAll(qdc);
    EXPECT_NE(content.find("clb(2,4):clb(4,4)"), std::string::npos) << "the good region must survive";
    EXPECT_NE(content.find("clb(oops,9)"), std::string::npos) << "so must the one to be fixed";
}

TEST(QdcSerializer, AValidRegionStillLoadsNormally)
{
    // Guard against the fix over-reaching: well-formed input must be unaffected.
    const auto qdc = writeQdc("fp_qdc_goodregion", "set_region i_a clb(2,4):clb(4,4) p1\n");

    fp::DeviceGrid device(genTestDescriptor());
    fp::QdcSerializer serializer;
    serializer.load(device, qdc);

    ASSERT_EQ(device.partitions().size(), 1u);
    const auto& partition = device.partitions().at(0);
    EXPECT_EQ(partition->name(), "p1");
    EXPECT_EQ(partition->elements().size(), 1u);
    EXPECT_FALSE(partition->regions().empty());
}
