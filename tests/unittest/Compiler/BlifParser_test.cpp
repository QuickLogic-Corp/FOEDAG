/*
Copyright 2022 The Foedag team

GPL License

Copyright (c) 2022 The Open-Source FPGA Foundation

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Compiler/BlifParser.h"

#include "gtest/gtest.h"

#include <unordered_set>
#include <algorithm>

//#define DEBUG_BLIF_PARSER

using namespace FOEDAG;

namespace {

std::vector<std::string> getFakeBlifLines()
{
    std::vector<std::string> lines = {
        ".subckt sdffre C=clk D=do_sdffre_Q_D E=$true Q=do R=$true",
        ".names $some d.l.l[0] d.l.l[1] d.z.l dut.u0.z0.d[4] dut.u0.z0.d[5] dut.u1.z1.d[4] dut.u2.z2.d[4]",
        ".names ld dut.u0.z0.d[6] dut.u0.w[0][30] dut.u0.w[0][31] dut.u0.w[0][32] dut.u0.w[1][30] dut.u0.w[1][31] dut.u0.r0.out[30] dut.key[126] dut.u0.w[0]_sdffre_Q_1_D"
    };
    return lines;
}

void expect_equal(bool expected, bool actual)
{
    assert(actual == expected);
}


std::vector<std::string> diff(const std::vector<std::string>& expected, const std::vector<std::string>& actual) {
    std::unordered_set<std::string> actualSet(actual.begin(), actual.end());
    std::vector<std::string> missing;

    for (const auto& expectedItem : expected) {
        if (std::find(actual.begin(), actual.end(), expectedItem) == actual.end()) {
            missing.push_back(expectedItem);
        }
    }
    return missing;
}

void print_vector(const std::string& label, const std::vector<std::string>& v)
{
 #ifdef DEBUG_BLIF_PARSER
    std::cout << label << ": ";
    for (const std::string& e: v) {
        std::cout << e << ",";
    }
    std::cout << std::endl;
#endif
}

void expect_equal(const std::vector<std::string>& expected, const std::vector<std::string>& actual)
{
    auto missing = diff(expected, actual);
    if (!missing.empty()) {
        print_vector("missing elements", missing);
    }
    EXPECT_EQ(expected, actual);
}

void find(const std::string& pattern, const std::shared_ptr<BlifNode>& node, const std::vector<std::string>& expected)
{
    auto found = node->findMatchingNames(pattern);
    print_vector(pattern, found);
    expect_equal(expected, found);
}

} // namespace


TEST(BlifParser, load)
{
    std::shared_ptr<BlifNode> rootNode = BlifParser().parseLines(getFakeBlifLines());
    if (rootNode) {
#ifdef DEBUG_BLIF_PARSER
        rootNode->printTree();
#endif
    }

    find("dut.u*.z*.d[*]", rootNode, {"dut.u0.z0.d[4]", "dut.u0.z0.d[5]", "dut.u0.z0.d[6]", "dut.u1.z1.d[4]", "dut.u2.z2.d[4]"});
    find("dut.u0.z0.*", rootNode, {"dut.u0.z0.d[4]", "dut.u0.z0.d[5]", "dut.u0.z0.d[6]"});
    find("dut.u0.z0.d[6]", rootNode, {"dut.u0.z0.d[6]"});
    find("clk", rootNode, {"clk"});
    find("$some", rootNode, {"$some"});
    find("dut.u0.w*", rootNode, {"dut.u0.w[0][30]", "dut.u0.w[0][31]", "dut.u0.w[0][32]", "dut.u0.w[0]_sdffre_Q_1_D", "dut.u0.w[1][30]", "dut.u0.w[1][31]"});
    find("dut.u0.w[0][*]", rootNode, {"dut.u0.w[0][30]", "dut.u0.w[0][31]", "dut.u0.w[0][32]"});
    find("dut.u0.w[1][*]", rootNode, {"dut.u0.w[1][30]", "dut.u0.w[1][31]"});
    find("dut.u0.w[*][*]", rootNode, {"dut.u0.w[0][30]", "dut.u0.w[0][31]", "dut.u0.w[0][32]", "dut.u0.w[1][30]", "dut.u0.w[1][31]"});
    find("dut.u0.w[*][30]", rootNode, {"dut.u0.w[0][30]", "dut.u0.w[1][30]"});
    find("dut.u0.*", rootNode, {"dut.u0.r0.out[30]","dut.u0.w[0][30]","dut.u0.w[0][31]","dut.u0.w[0][32]","dut.u0.w[0]_sdffre_Q_1_D","dut.u0.w[1][30]","dut.u0.w[1][31]","dut.u0.z0.d[4]","dut.u0.z0.d[5]","dut.u0.z0.d[6]"});
    find("d.*", rootNode, {"d.l.l[0]","d.l.l[1]","d.z.l"});
    find("dut.u*", rootNode, {"dut.u0.r0.out[30]","dut.u0.w[0][30]","dut.u0.w[0][31]","dut.u0.w[0][32]","dut.u0.w[0]_sdffre_Q_1_D","dut.u0.w[1][30]","dut.u0.w[1][31]","dut.u0.z0.d[4]","dut.u0.z0.d[5]","dut.u0.z0.d[6]","dut.u1.z1.d[4]","dut.u2.z2.d[4]"});

    // attempt to find absent node
    find("dut.u0.z0.d[7]", rootNode, {});
    find("dut.u.z0.d[6]", rootNode, {});
    find("du.u0.z0.d[6]", rootNode, {});

    auto found = rootNode->findMatchingNames("dut.*");
    print_vector("dut.*", found);

    found = rootNode->findMatchingNames("*");
    print_vector("*", found);
}

