#include <FloorPlanning/DeviceGridDescriptor.h>

#include <FloorPlanning/DeviceGrid.h>
#include <FloorPlanning/QdcSerializer.h>
#include <FloorPlanning/Partition.h>
#include <FloorPlanning/SynthResourceExtractor.h>
#include <FloorPlanning/PostSynthVerilogResourceExtractor.h>
#include <FloorPlanning/PostSynthVerilogNameBridge.h>

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

// ═══════════════════════════════════════════════════════════════════════
// PostSynthVerilogResourceExtractor — parses cell instantiations (real leaf-
// cell identity in synthesized output), not wire/input/output declarations.
// ═══════════════════════════════════════════════════════════════════════

TEST(PostSynthVerilogResourceExtractor, PlainInstanceName)
{
    const std::string v =
        "module top(clk, a, y);\n"
        "  input clk;\n"
        "  input [3:0] a;\n"
        "  output [3:0] y;\n"
        "  sdffre my_ff (.C(clk), .D(a[3]), .Q(y[3]));\n"
        "endmodule\n";

    fp::PostSynthVerilogResourceExtractor ex;
    ASSERT_TRUE(ex.parseAtomNamesFromVerilogFileContent(v));
    EXPECT_EQ(ex.elements().count("my_ff"), 1u);
    EXPECT_EQ(ex.elements().size(), 1u);
}

TEST(PostSynthVerilogResourceExtractor, EscapedInstanceNameWithDots)
{
    const std::string v =
        "module top(clk);\n"
        "  input clk;\n"
        "  sdffre \\u_sub.u_leaf.q_ff  (\n"
        "    .C(clk)\n"
        "  );\n"
        "endmodule\n";

    fp::PostSynthVerilogResourceExtractor ex;
    ASSERT_TRUE(ex.parseAtomNamesFromVerilogFileContent(v));
    EXPECT_EQ(ex.elements().count("u_sub.u_leaf.q_ff"), 1u);
}

TEST(PostSynthVerilogResourceExtractor, BusInstanceGroupYosysSuffixed)
{
    // One RTL register split into per-bit primitives by techmap, disambiguated
    // with a Yosys-style _Q/_Q_1/_Q_2/... suffix -- each is a distinct instance.
    const std::string v =
        "module top(clk, a, y);\n"
        "  input clk;\n"
        "  input [3:0] a;\n"
        "  output [3:0] y;\n"
        "  sdffre \\u_leaf.q_sdffre_Q  (.C(clk), .D(a[3]), .Q(y[3]));\n"
        "  sdffre \\u_leaf.q_sdffre_Q_1  (.C(clk), .D(a[2]), .Q(y[2]));\n"
        "  sdffre \\u_leaf.q_sdffre_Q_2  (.C(clk), .D(a[1]), .Q(y[1]));\n"
        "  sdffre \\u_leaf.q_sdffre_Q_3  (.C(clk), .D(a[0]), .Q(y[0]));\n"
        "endmodule\n";

    fp::PostSynthVerilogResourceExtractor ex;
    ASSERT_TRUE(ex.parseAtomNamesFromVerilogFileContent(v));
    EXPECT_EQ(ex.elements().size(), 4u);
    EXPECT_EQ(ex.elements().count("u_leaf.q_sdffre_Q"), 1u);
    EXPECT_EQ(ex.elements().count("u_leaf.q_sdffre_Q_1"), 1u);
    EXPECT_EQ(ex.elements().count("u_leaf.q_sdffre_Q_2"), 1u);
    EXPECT_EQ(ex.elements().count("u_leaf.q_sdffre_Q_3"), 1u);
}

TEST(PostSynthVerilogResourceExtractor, PortConnectionBracketsNotPartOfName)
{
    // Bracket/index syntax inside port-connection expressions (.D(a[3])) must
    // not be mistaken for part of the instance name.
    const std::string v =
        "module top(clk, a, y);\n"
        "  input clk;\n"
        "  input [3:0] a;\n"
        "  output [3:0] y;\n"
        "  sdffre my_ff (.C(clk), .D(a[3]), .Q(y[3]));\n"
        "endmodule\n";

    fp::PostSynthVerilogResourceExtractor ex;
    ASSERT_TRUE(ex.parseAtomNamesFromVerilogFileContent(v));
    EXPECT_EQ(ex.elements().count("my_ff"), 1u);
    EXPECT_EQ(ex.elements().count("a"), 0u);
    EXPECT_EQ(ex.elements().count("a[3"), 0u);
}

TEST(PostSynthVerilogResourceExtractor, MidPathBracketFromGenerateArray)
{
    // A generate-array index lands in the MIDDLE of the escaped instance
    // name, not just as a trailing bus-bit suffix. Per the escaped-identifier
    // rule this is one token, delimited only by whitespace -- must not be cut
    // short at the first '['.
    const std::string v =
        "module top(clk, a0, y0);\n"
        "  input clk;\n"
        "  input [3:0] a0;\n"
        "  output [3:0] y0;\n"
        "  sdffre \\gen_lane[0].u_lane.u_leaf.q_sdffre_Q  (\n"
        "    .C(clk),\n"
        "    .D(a0[3]),\n"
        "    .Q(y0[3])\n"
        "  );\n"
        "endmodule\n";

    fp::PostSynthVerilogResourceExtractor ex;
    ASSERT_TRUE(ex.parseAtomNamesFromVerilogFileContent(v));
    EXPECT_EQ(ex.elements().count("gen_lane[0].u_lane.u_leaf.q_sdffre_Q"), 1u);
}

TEST(PostSynthVerilogResourceExtractor, MultiLineParameterizedInstantiation)
{
    const std::string v =
        "module top(a, y);\n"
        "  input [7:0] a;\n"
        "  output [7:0] y;\n"
        "  QL_DSPV2 #(\n"
        "    .WIDTH(8),\n"
        "    .MODE(\"MULT\")\n"
        "  ) \\dsp.inst0  (\n"
        "    .A(a),\n"
        "    .Y(y)\n"
        "  );\n"
        "endmodule\n";

    fp::PostSynthVerilogResourceExtractor ex;
    ASSERT_TRUE(ex.parseAtomNamesFromVerilogFileContent(v));
    EXPECT_EQ(ex.elements().count("dsp.inst0"), 1u);
    EXPECT_EQ(ex.elements().size(), 1u);
}

TEST(PostSynthVerilogResourceExtractor, AttributeLinesSkipped)
{
    const std::string v =
        "module top(clk, a, y);\n"
        "  input clk;\n"
        "  input [3:0] a;\n"
        "  output [3:0] y;\n"
        "  (* module_not_derived = 32'd1 *)\n"
        "  (* src = \"leaf.v:6.3-6.32|internal.v:1.1-1.1\" *)\n"
        "  sdffre \\u_sub.u_leaf.q_sdffre_Q  (\n"
        "    .C(clk),\n"
        "    .D(a[3]),\n"
        "    .Q(y[3])\n"
        "  );\n"
        "endmodule\n";

    fp::PostSynthVerilogResourceExtractor ex;
    ASSERT_TRUE(ex.parseAtomNamesFromVerilogFileContent(v));
    EXPECT_EQ(ex.elements().size(), 1u);
    EXPECT_EQ(ex.elements().count("u_sub.u_leaf.q_sdffre_Q"), 1u);
}

TEST(PostSynthVerilogResourceExtractor, DeepHierarchyPath)
{
    const std::string v =
        "module top(a, y);\n"
        "  input a;\n"
        "  output y;\n"
        "  lut6 \\u_top.u_mid.u_leaf.cellname  (.I0(a), .O(y));\n"
        "endmodule\n";

    fp::PostSynthVerilogResourceExtractor ex;
    ASSERT_TRUE(ex.parseAtomNamesFromVerilogFileContent(v));
    EXPECT_EQ(ex.elements().count("u_top.u_mid.u_leaf.cellname"), 1u);
}

TEST(PostSynthVerilogResourceExtractor, AnonymousCellsExcluded)
{
    const std::string v =
        "module top(a, y);\n"
        "  input [3:0] a;\n"
        "  output [3:0] y;\n"
        "  $abc$1234$auto$blah.cc:5678:some_pass$99 (\n"
        "    .A(a[0]),\n"
        "    .Y(y[0])\n"
        "  );\n"
        "  lut6 plain_inst (.I0(a[1]), .O(y[1]));\n"
        "endmodule\n";

    fp::PostSynthVerilogResourceExtractor ex;
    ASSERT_TRUE(ex.parseAtomNamesFromVerilogFileContent(v));
    EXPECT_EQ(ex.elements().count("plain_inst"), 1u);
    EXPECT_EQ(ex.elements().size(), 1u);  // the $abc$.../$auto$... cell must not appear
}

TEST(PostSynthVerilogResourceExtractor, RealSynthQuicklogicOutputFixture)
{
    // Modeled directly on real synth_quicklogic output (Yosys 0.55, verified
    // against an actual run) for a small hierarchical design with a
    // generate/genvar loop instantiating two "lane" submodules, each
    // containing one 4-bit register -- a regression fixture for the exact
    // syntax shape this extractor must handle end to end, not a synthetic
    // minimal case.
    const std::string v =
        "module top(clk, a0, a1, y0, y1);\n"
        "  input [3:0] a0;\n"
        "  wire [3:0] a0;\n"
        "  input [3:0] a1;\n"
        "  wire [3:0] a1;\n"
        "  input clk;\n"
        "  wire clk;\n"
        "  output [3:0] y0;\n"
        "  wire [3:0] y0;\n"
        "  output [3:0] y1;\n"
        "  wire [3:0] y1;\n"
        "  (* module_not_derived = 32'h00000001 *)\n"
        "  (* src = \"leaf.v:6.3-6.32|/opt/yosys/qlf_k6n10f/ffs_map.v:178.12-178.65\" *)\n"
        "  sdffre \\gen_lane[0].u_lane.u_leaf.q_sdffre_Q  (\n"
        "    .C(clk),\n"
        "    .D(a0[3]),\n"
        "    .E(1'h1),\n"
        "    .Q(y0[3]),\n"
        "    .R(1'h1)\n"
        "  );\n"
        "  (* module_not_derived = 32'h00000001 *)\n"
        "  (* src = \"leaf.v:6.3-6.32|/opt/yosys/qlf_k6n10f/ffs_map.v:178.12-178.65\" *)\n"
        "  sdffre \\gen_lane[0].u_lane.u_leaf.q_sdffre_Q_1  (\n"
        "    .C(clk),\n"
        "    .D(a0[2]),\n"
        "    .E(1'h1),\n"
        "    .Q(y0[2]),\n"
        "    .R(1'h1)\n"
        "  );\n"
        "  (* module_not_derived = 32'h00000001 *)\n"
        "  (* src = \"leaf.v:6.3-6.32|/opt/yosys/qlf_k6n10f/ffs_map.v:178.12-178.65\" *)\n"
        "  sdffre \\gen_lane[0].u_lane.u_leaf.q_sdffre_Q_2  (\n"
        "    .C(clk),\n"
        "    .D(a0[1]),\n"
        "    .E(1'h1),\n"
        "    .Q(y0[1]),\n"
        "    .R(1'h1)\n"
        "  );\n"
        "  (* module_not_derived = 32'h00000001 *)\n"
        "  (* src = \"leaf.v:6.3-6.32|/opt/yosys/qlf_k6n10f/ffs_map.v:178.12-178.65\" *)\n"
        "  sdffre \\gen_lane[0].u_lane.u_leaf.q_sdffre_Q_3  (\n"
        "    .C(clk),\n"
        "    .D(a0[0]),\n"
        "    .E(1'h1),\n"
        "    .Q(y0[0]),\n"
        "    .R(1'h1)\n"
        "  );\n"
        "  (* module_not_derived = 32'h00000001 *)\n"
        "  (* src = \"leaf.v:6.3-6.32|/opt/yosys/qlf_k6n10f/ffs_map.v:178.12-178.65\" *)\n"
        "  sdffre \\gen_lane[1].u_lane.u_leaf.q_sdffre_Q  (\n"
        "    .C(clk),\n"
        "    .D(a1[3]),\n"
        "    .E(1'h1),\n"
        "    .Q(y1[3]),\n"
        "    .R(1'h1)\n"
        "  );\n"
        "  (* module_not_derived = 32'h00000001 *)\n"
        "  (* src = \"leaf.v:6.3-6.32|/opt/yosys/qlf_k6n10f/ffs_map.v:178.12-178.65\" *)\n"
        "  sdffre \\gen_lane[1].u_lane.u_leaf.q_sdffre_Q_1  (\n"
        "    .C(clk),\n"
        "    .D(a1[2]),\n"
        "    .E(1'h1),\n"
        "    .Q(y1[2]),\n"
        "    .R(1'h1)\n"
        "  );\n"
        "  (* module_not_derived = 32'h00000001 *)\n"
        "  (* src = \"leaf.v:6.3-6.32|/opt/yosys/qlf_k6n10f/ffs_map.v:178.12-178.65\" *)\n"
        "  sdffre \\gen_lane[1].u_lane.u_leaf.q_sdffre_Q_2  (\n"
        "    .C(clk),\n"
        "    .D(a1[1]),\n"
        "    .E(1'h1),\n"
        "    .Q(y1[1]),\n"
        "    .R(1'h1)\n"
        "  );\n"
        "  (* module_not_derived = 32'h00000001 *)\n"
        "  (* src = \"leaf.v:6.3-6.32|/opt/yosys/qlf_k6n10f/ffs_map.v:178.12-178.65\" *)\n"
        "  sdffre \\gen_lane[1].u_lane.u_leaf.q_sdffre_Q_3  (\n"
        "    .C(clk),\n"
        "    .D(a1[0]),\n"
        "    .E(1'h1),\n"
        "    .Q(y1[0]),\n"
        "    .R(1'h1)\n"
        "  );\n"
        "endmodule\n";

    fp::PostSynthVerilogResourceExtractor ex;
    ASSERT_TRUE(ex.parseAtomNamesFromVerilogFileContent(v));
    EXPECT_EQ(ex.elements().size(), 8u);
    for (int lane = 0; lane < 2; ++lane) {
        EXPECT_EQ(ex.elements().count("gen_lane[" + std::to_string(lane) + "].u_lane.u_leaf.q_sdffre_Q"), 1u);
        EXPECT_EQ(ex.elements().count("gen_lane[" + std::to_string(lane) + "].u_lane.u_leaf.q_sdffre_Q_1"), 1u);
        EXPECT_EQ(ex.elements().count("gen_lane[" + std::to_string(lane) + "].u_lane.u_leaf.q_sdffre_Q_2"), 1u);
        EXPECT_EQ(ex.elements().count("gen_lane[" + std::to_string(lane) + "].u_lane.u_leaf.q_sdffre_Q_3"), 1u);
    }
}

// ═══════════════════════════════════════════════════════════════════════
// PostSynthVerilogNameBridge::loadRtlSources — generate/genvar support
// ═══════════════════════════════════════════════════════════════════════

namespace {

// loadRtlSources() takes real file paths; write RTL content to a temp file
// under a fresh, auto-cleaned temporary directory.
std::filesystem::path writeTempRtl(QTemporaryDir& dir, const std::string& name, const std::string& content)
{
    const std::filesystem::path path = std::filesystem::path(dir.path().toStdString()) / name;
    std::ofstream out(path);
    out << content;
    out.close();
    return path;
}

}  // namespace

TEST(PostSynthVerilogNameBridge, GenerateForLoopNamedBlockPerIterationPaths)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const auto path = writeTempRtl(dir, "top.v",
        "module leaf(input clk, input [3:0] d, output reg [3:0] q);\n"
        "  always @(posedge clk) q <= d;\n"
        "endmodule\n"
        "module lane(input clk, input [3:0] a, output [3:0] y);\n"
        "  leaf u_leaf(.clk(clk), .d(a), .q(y));\n"
        "endmodule\n"
        "module top(input clk, input [3:0] a0, input [3:0] a1, output [3:0] y0, output [3:0] y1);\n"
        "  genvar i;\n"
        "  generate\n"
        "    for (i = 0; i < 2; i = i + 1) begin : gen_lane\n"
        "      lane u_lane (.clk(clk), .a(a0), .y(y0));\n"
        "    end\n"
        "  endgenerate\n"
        "endmodule\n");

    fp::PostSynthVerilogNameBridge bridge;
    ASSERT_TRUE(bridge.loadRtlSources({path}));
    const auto paths = bridge.instPaths();

    // Without generate support, only "u_lane"/"u_lane.u_leaf" would appear
    // once each -- the fix must produce one entry per elaborated iteration.
    EXPECT_TRUE(paths.count("gen_lane[0].u_lane"));
    EXPECT_TRUE(paths.count("gen_lane[0].u_lane.u_leaf"));
    EXPECT_TRUE(paths.count("gen_lane[1].u_lane"));
    EXPECT_TRUE(paths.count("gen_lane[1].u_lane.u_leaf"));
    EXPECT_FALSE(paths.count("u_lane"));
}

TEST(PostSynthVerilogNameBridge, NestedGenerateBlocksComposePrefixes)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const auto path = writeTempRtl(dir, "top.v",
        "module leaf(input clk, output q);\n"
        "endmodule\n"
        "module top(input clk);\n"
        "  genvar i, j;\n"
        "  generate\n"
        "    for (i = 0; i < 2; i = i + 1) begin : outer\n"
        "      for (j = 0; j < 2; j = j + 1) begin : inner\n"
        "        leaf u_leaf(.clk(clk));\n"
        "      end\n"
        "    end\n"
        "  endgenerate\n"
        "endmodule\n");

    fp::PostSynthVerilogNameBridge bridge;
    ASSERT_TRUE(bridge.loadRtlSources({path}));
    const auto paths = bridge.instPaths();

    for (int oi = 0; oi < 2; ++oi)
        for (int ii = 0; ii < 2; ++ii)
            EXPECT_TRUE(paths.count("outer[" + std::to_string(oi) + "].inner[" + std::to_string(ii) + "].u_leaf"));
}

TEST(PostSynthVerilogNameBridge, GenerateBlockWithNoInstantiationsDoesNotCrash)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const auto path = writeTempRtl(dir, "top.v",
        "module top(input clk, output reg [3:0] q);\n"
        "  genvar i;\n"
        "  generate\n"
        "    for (i = 0; i < 4; i = i + 1) begin : gen_bit\n"
        "      // no instantiation here -- purely combinational, nothing to record\n"
        "    end\n"
        "  endgenerate\n"
        "endmodule\n");

    fp::PostSynthVerilogNameBridge bridge;
    EXPECT_TRUE(bridge.loadRtlSources({path}));
    for (int i = 0; i < 4; ++i)
        EXPECT_FALSE(bridge.instPaths().count("gen_bit[" + std::to_string(i) + "]"));
}

TEST(PostSynthVerilogNameBridge, AnonymousGenerateBlockGetsAPlaceholderLabel)
{
    // NOT a claim about real Yosys naming -- that convention is unverified
    // (see requirements.md's open risks). This only pins down today's
    // placeholder behavior (a distinct, counter-based label per anonymous
    // block) so a future change to it is a deliberate, visible diff here,
    // not a silent regression.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const auto path = writeTempRtl(dir, "top.v",
        "module leaf(input clk, output q);\n"
        "endmodule\n"
        "module top(input clk);\n"
        "  genvar i;\n"
        "  generate\n"
        "    for (i = 0; i < 2; i = i + 1) begin\n"
        "      leaf u_leaf(.clk(clk));\n"
        "    end\n"
        "  endgenerate\n"
        "endmodule\n");

    fp::PostSynthVerilogNameBridge bridge;
    ASSERT_TRUE(bridge.loadRtlSources({path}));
    const auto paths = bridge.instPaths();

    int labeledEntries = 0;
    for (const auto& p : paths)
        if (p.find(".u_leaf") != std::string::npos) ++labeledEntries;
    EXPECT_EQ(labeledEntries, 2);  // one per elaborated iteration, each under its own label
}

TEST(PostSynthVerilogNameBridge, PlainInstantiationNoRegressionWithoutGenerate)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const auto path = writeTempRtl(dir, "top.v",
        "module leaf(input clk, input [3:0] d, output reg [3:0] q);\n"
        "  always @(posedge clk) q <= d;\n"
        "endmodule\n"
        "module top(input clk, input [3:0] a, output [3:0] y);\n"
        "  leaf u_leaf(.clk(clk), .d(a), .q(y));\n"
        "endmodule\n");

    fp::PostSynthVerilogNameBridge bridge;
    ASSERT_TRUE(bridge.loadRtlSources({path}));
    EXPECT_TRUE(bridge.instPaths().count("u_leaf"));
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
