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

#include "BlifParser.h"

#include <sstream>
#include <fstream>
#include <iostream>
#include <regex>
#include <cassert>
#include <unordered_set>

namespace FOEDAG {

HierNode::HierNode(const std::string& name): m_name{name} 
{
}

HierNode::~HierNode() 
{
    for (auto& [name, child]: m_children) {
        delete child;
    }
}

std::vector<std::string> HierNode::findMatchingNames(const std::string& pattern)
{
    std::vector<std::string> names;
    std::vector<std::string> parts = splitHierarchy(pattern);

    if (parts.empty()) {
        return names;
    }

    const std::string& first = parts[0];
    for (const auto& [name, child]: m_children) {
        if (matches(name, first)) {
            std::string path = name;
            if (parts.size() == 1 && (parts[0] == "*")) {
                collectAllLeafPaths("", names);
                break;
            } else {
                child->expandRecursive(parts, 1, path, names);
            }
        }
    }

    return names;
}

void HierNode::expandRecursive(const std::vector<std::string>& parts, std::size_t index,
                               const std::string& path, std::vector<std::string>& names) const
{
    if (index >= parts.size()) {
        if (isLeaf()) { // this is needed to capture the root leaf nodes
            names.push_back(path);
        }
        return;
    }

    const std::string& pattern = parts[index];

    for (const auto& [name, child]: m_children) {
        if (!matches(name, pattern)) {
            continue;
        }

        std::string fullPath = path.empty()? name: path + "." + name;

        if (index == parts.size() - 1) {
            // handle last segment of pattern, by collecting all leafs below
            child->collectAllLeafPaths(fullPath, names);
        } else {
            // continue recursion
            child->expandRecursive(parts, index + 1, fullPath, names);
        }
    }
}

void HierNode::collectAllLeafPaths(const std::string& path, std::vector<std::string>& names) const {
    if (isLeaf()) {
        names.push_back(path);
        return;
    }

    for (const auto& [name, child] : m_children) {
        std::string fullPath = path.empty() ? name : path + "." + name;
        child->collectAllLeafPaths(fullPath, names);
    }
}

void HierNode::insert(const std::string& fullName)
{
    auto parts = splitHierarchy(fullName);
    HierNode* node = this;
    for (const std::string& part: parts) {
        node = node->getOrCreateChild(part);
    }
}

void HierNode::printTree() 
{
    printTreeRecursive(this);
}

std::vector<std::string> HierNode::splitHierarchy(const std::string& name) const 
{
    std::vector<std::string> parts;
    std::stringstream ss(name);
    std::string part;
    while (std::getline(ss, part, '.')) {
        parts.push_back(part);
    }
    return parts;
}

std::vector<HierNode*> HierNode::findChildren(const std::string& wildCardPattern) const
{
    std::vector<HierNode*> result;

    for (const auto& [name, child]: m_children) {
        if (matches(child->getName(), wildCardPattern)) {
            result.push_back(child);
        }
    }

    return result;
}

HierNode* HierNode::getOrCreateChild(const std::string& name) 
{
    if (!m_children.count(name)) {
        m_children[name] = new HierNode{name};
    }
    return m_children[name];
}

void HierNode::printTreeRecursive(const HierNode* node, int depth) 
{
    std::string indent(depth * 2, ' ');
    if (node->isLeaf()) {
        std::cout << indent << node->getName() << std::endl;
    } else {
        std::cout << indent << node->getName() << "/" << std::endl;
    }
    for (const auto& [name, child]: node->getChildren()) {
        printTreeRecursive(child, depth + 1);
    }
}

std::string HierNode::wildcardToRegex(const std::string& pattern)
{
    std::string regexPattern;
    regexPattern += "^";
    for (char c : pattern) {
        switch (c) {
        case '*': regexPattern += ".*"; break;
        case '.': regexPattern += "\\."; break;
        case '^': regexPattern += "\\^"; break;
        case '$': regexPattern += "\\$"; break;
        case '|': regexPattern += "\\|"; break;
        case '(': regexPattern += "\\("; break;
        case ')': regexPattern += "\\)"; break;
        case '[': regexPattern += "\\["; break;
        case ']': regexPattern += "\\]"; break;
        case '{': regexPattern += "\\{"; break;
        case '}': regexPattern += "\\}"; break;
        case '?': regexPattern += "\\?"; break;
        case '+': regexPattern += "\\+"; break;
        case '\\': regexPattern += "\\\\"; break;
        default: regexPattern += c; break;
        }
    }
    regexPattern += "$";
    return regexPattern;
}

bool HierNode::containsPath(const std::vector<std::string>& parts, std::size_t index) const {
    if (index >= parts.size()) {
        return false;
    }

    const std::string& current = parts[index];
    if (!matches(getName(), current)) {
        return false;
    }

    if (index == parts.size() - 1) {
        return true;  // full match
    }

    const std::string& next = parts[index + 1];
    for (HierNode* child : findChildren(next)) {
        if (child->containsPath(parts, index + 1)) {
            return true;
        }
    }

    return false;
}

bool HierNode::matches(const std::string& name, const std::string& pattern) const 
{
    if (pattern == "*") {
        return true;
    } else if (pattern.find("*") != std::string::npos) {
        std::regex regex(wildcardToRegex(pattern));
        return std::regex_match(name, regex);
    } else {
        return (name == pattern);
    }
}

std::vector<std::string> BlifParser::findMatchingNames(const std::string& pattern)
{
    if (m_rootNodePtr) {
        return m_rootNodePtr->findMatchingNames(pattern);
    }
    return {};
}

bool BlifParser::isFileChanged(const std::filesystem::path& filepath) const
{
    return m_lastWriteTime != std::filesystem::last_write_time(filepath);
}

std::shared_ptr<HierNode> BlifParser::load(const std::filesystem::path& filepath) 
{
    if (!std::filesystem::exists(filepath)) {
        return nullptr;
    }

    if (filepath.extension() != ".blif") {
        return nullptr;
    }

    std::ifstream in(filepath);
    if (!in) {
        return nullptr;
    }

    if (!m_rootNodePtr || isFileChanged(filepath)) {
        std::vector<std::string> lines;
        for (std::string line; std::getline(in, line); ) {
            lines.push_back(std::move(line));
        }
        m_rootNodePtr = parseLines(lines);
        m_lastWriteTime = std::filesystem::last_write_time(filepath);
    }    

    return m_rootNodePtr;
}

std::shared_ptr<HierNode> BlifParser::parseLines(const std::vector<std::string>& lines) 
{
    m_rootNodePtr = std::make_shared<HierNode>();

    for (const std::string& line: lines) {
        if (line.empty()) continue;
        if (line[0] == '.') {
            std::istringstream iss(line);
            std::string token;
            iss >> token;
            if (token == ".subckt") {
                while (iss >> token) {
                    auto eq = token.find('=');
                    if (eq != std::string::npos) {
                        std::string fullName = token.substr(eq + 1);
                        m_rootNodePtr->insert(fullName);
                    }
                }
            } else if (token == ".names") {
                while (iss >> token) {
                    m_rootNodePtr->insert(token);
                }
            }
        }
    }

    return m_rootNodePtr;
}

#ifdef TEST_BLIFLOADER

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
    std::cout << label << ": ";
    for (const std::string& e: v) {
        std::cout << e << ",";
    }
    std::cout << std::endl;
}

void expect_equal(const std::vector<std::string>& expected, const std::vector<std::string>& actual)
{
    auto missing = diff(expected, actual);
    if (!missing.empty()) {
        print_vector("missing elements", missing);
    }
    assert(missing.empty() && (expected.size() == actual.size()));
}

void find(const std::string& pattern, const std::shared_ptr<HierNode>& node, const std::vector<std::string>& expected)
{
    auto found = node->findMatchingNames(pattern);
    print_vector(pattern, found);
    expect_equal(expected, found);
}

} // namespace

void run_blif_test()
{
    std::shared_ptr<HierNode> rootNode = BlifParser().parseLines(getFakeBlifLines());
    if (rootNode) {
        rootNode->printTree();
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

#endif 

}  // namespace FOEDAG
