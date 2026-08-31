#include <FloorPlanning/DeviceGridDescriptor.h>

#include <FloorPlanning/DeviceGrid.h>
#include <FloorPlanning/QdcSerializer.h>
#include <FloorPlanning/Partition.h>
#include <FloorPlanning/SynthResourceExtractor.h>

#include <QPoint>

#include <filesystem>
#include <fstream>
#include <iostream>

#include "gtest/gtest.h"

fp::DeviceGridDescriptorPtr genTestDescriptor()
{
    // Minimal device config.json: a 30x30 core (-> 32x32 grid) with DSP columns
    // at grid {7,20} and BRAM columns at grid {13,26}. A device variant is a
    // layout, so the geometry sits in DEVICE_TYPE_SETTINGS.CUSTOM;
    // DSP_COLS/BRAM_COLS are 1-based core columns (grid column minus the IO
    // border).
    const std::string configJson = R"({
    "DSP_SIZE": "1x3",
    "BRAM_SIZE": "1x6",
    "DEVICE_TYPE": "CUSTOM",
    "DEVICE_TYPE_SETTINGS": {
        "LAYOUT_MODE": "AUTO",
        "CUSTOM": {
            "ARRAY_X": "30",
            "ARRAY_Y": "30",
            "DSP_COLS": "6,19",
            "BRAM_COLS": "12,25"
        }
    }
})";

    const std::filesystem::path configPath =
        std::filesystem::temp_directory_path() / "fp_unittest_device_config.json";

    {
        std::ofstream out(configPath);
        out << configJson;
    }

    fp::DeviceGridDescriptorPtr descriptor =
        std::make_shared<fp::DeviceGridDescriptor>(configPath);

    std::filesystem::remove(configPath);

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

// Atom-netlist echo blif: ".subckt" carries the model name, and the real
// instance/atom name is in the preceding "# Subckt <N>: <name>" comment. The
// extracted atom names come from those comments, and model names (adder_carry,
// dffre) must NOT leak in.
TEST(SynthResourceExtractor, BlifSubcktNames)
{
    const std::string blif =
        "#Atom netlist generated by VPR\n"
        ".names $false\n"
        "\n"
        ".names count[0] count_$lut_A_1_Y[0]\n"
        "0 1\n"
        "\n"
        "# Subckt 25: count_adder_carry_p_cout[10]\n"
        ".subckt adder_carry \\\n"
        "    cin=count_adder_carry_p_cout[9] \\\n"
        "    p=count[9] \\\n"
        "    cout=count_adder_carry_p_cout[10]\n"
        "\n"
        "# Subckt 39: count[15]\n"
        ".subckt dffre \\\n"
        "    D=count_dffre_Q_D[15] \\\n"
        "    Q=count[15] \\\n"
        "    C=clk\n";

    fp::SynthResourceExtractor extractor;
    ASSERT_TRUE(extractor.parseAtomNamesFromBlifFileContent(blif));
    const auto& e = extractor.elements();

    // instance names taken from the "# Subckt N:" comment
    EXPECT_EQ(e.count("count_adder_carry_p_cout[10]"), 1u);
    EXPECT_EQ(e.count("count[15]"), 1u);
    // .names output (last token)
    EXPECT_EQ(e.count("$false"), 1u);
    EXPECT_EQ(e.count("count_$lut_A_1_Y[0]"), 1u);
    // model names must not be collected
    EXPECT_EQ(e.count("adder_carry"), 0u);
    EXPECT_EQ(e.count("dffre"), 0u);
}

// Fallback: a blif WITHOUT the "# Subckt <N>:" echo comments (hand-written,
// Yosys/ABC output, older VPR). With no comment to supply the instance name,
// the token right after ".subckt" (the model/instance name) is used instead, so
// atoms are not silently lost.
TEST(SynthResourceExtractor, BlifSubcktNamesFallbackNoComment)
{
    const std::string blif =
        ".names out_net\n"
        "\n"
        ".subckt my_adder \\\n"
        "    a=n1 \\\n"
        "    b=n2 \\\n"
        "    y=sum\n"
        "\n"
        ".subckt my_dff \\\n"
        "    D=d0 \\\n"
        "    Q=q0\n";

    fp::SynthResourceExtractor extractor;
    ASSERT_TRUE(extractor.parseAtomNamesFromBlifFileContent(blif));
    const auto& e = extractor.elements();

    // no comment present -> token right after ".subckt" is taken
    EXPECT_EQ(e.count("my_adder"), 1u);
    EXPECT_EQ(e.count("my_dff"), 1u);
    // .names output (last token)
    EXPECT_EQ(e.count("out_net"), 1u);
    // connection nets must not be collected from .subckt lines
    EXPECT_EQ(e.count("n1"), 0u);
    EXPECT_EQ(e.count("sum"), 0u);
}
