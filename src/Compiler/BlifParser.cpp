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

namespace FOEDAG {

HierNode::HierNode(const std::string& name): m_name{name} 
{
    //std::cout << "HierNode(" << name << ")" << std::endl;
}

HierNode::~HierNode() 
{
    //std::cout << "~HierNode(" << m_name << ")" << std::endl;
    for (auto& [name, child]: m_children) {
        delete child;
    }
}

bool HierNode::contains(const std::string& full_name) 
{
    bool has_match = false;
    for (const auto& [name, node]: getChildren()) {
        std::string copy_full_name = full_name;
        if (node->containsRecursive(copy_full_name)) {
            has_match = true;
        }
    }
    return has_match;
}

void HierNode::insert(const std::string& full_name) 
{
    auto parts = splitHierarchy(full_name);
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

HierNode* HierNode::getChild(const std::string& name) const 
{
    if (auto it = m_children.find(name); it != m_children.end()) {
        return it->second;
    }

    return nullptr;
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
    if (node->hasChildren()) {
        std::cout << indent << node->getName() << "/" << std::endl;
    } else {
        std::cout << indent << node->getName() << std::endl;
    }
    for (const auto& [name, child] : node->getChildren()) {
        printTreeRecursive(child, depth + 1);
    }
}

std::string HierNode::wildcardToRegex(const std::string& pattern) const 
{
    std::string regex_pattern;
    regex_pattern += "^";
    for (char c : pattern) {
        switch (c) {
        case '*': regex_pattern += ".*"; break;
        case '.': regex_pattern += "\\."; break;
        case '^': regex_pattern += "\\^"; break;
        case '$': regex_pattern += "\\$"; break;
        case '|': regex_pattern += "\\|"; break;
        case '(': regex_pattern += "\\("; break;
        case ')': regex_pattern += "\\)"; break;
        case '[': regex_pattern += "\\["; break;
        case ']': regex_pattern += "\\]"; break;
        case '{': regex_pattern += "\\{"; break;
        case '}': regex_pattern += "\\}"; break;
        case '?': regex_pattern += "\\?"; break;
        case '+': regex_pattern += "\\+"; break;
        case '\\': regex_pattern += "\\\\"; break;
        default: regex_pattern += c; break;
        }
    }
    regex_pattern += "$";
    return regex_pattern;
}

bool HierNode::matchesWildcard(const std::string& text, const std::string& pattern) const 
{
    std::regex regex(wildcardToRegex(pattern));
    return std::regex_match(text, regex);
}

std::string HierNode::popFirstSegment(std::string& str) 
{
    std::string firstSegment;
    size_t pos = str.find('.');
    if (pos == std::string::npos) {
        firstSegment = str;
        str.clear();
    } else {
        firstSegment = str.substr(0, pos);
        str = str.substr(pos + 1);
    }
    return firstSegment;
}

std::string HierNode::getFirstSegment(const std::string& str) 
{
    std::string firstSegment;
    size_t pos = str.find('.');
    if (pos == std::string::npos) {
        firstSegment = str;
    } else {
        firstSegment = str.substr(0, pos);
    }
    return firstSegment;
}

bool HierNode::containsRecursive(std::string& full_name) 
{
    std::string segment = popFirstSegment(full_name);

    if (!matches(getName(), segment)) {
        return false;
    }

    if (full_name.empty()) {
        return true;
    }

    std::string next_segment = getFirstSegment(full_name);
    std::vector<HierNode*> children = findChildren(next_segment);

    for (HierNode* child : children) {
        std::string full_name_copy = full_name;
        if (child->containsRecursive(full_name_copy)) {
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
        return matchesWildcard(name, pattern);
    } else {
        return (name == pattern);
    }
}

bool BlifParser::isFileChanged(const std::filesystem::path& filepath) const
{
    /* TODO: check md5sum of file content read previously with the current attempt */
    return true;
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
                        std::string full_name = token.substr(eq + 1);
                        m_rootNodePtr->insert(full_name);
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


// test
std::vector<std::string> getFakeBlifLines()
{
    std::vector<std::string> lines = {
        ".subckt sdffre C=clk D=do_sdffre_Q_D E=$true Q=do R=$true",
        ".names ld dut.u0.u0.d[6] dut.u0.w[0][30] dut.u0.r0.out[30] dut.key[126] dut.u0.w[0]_sdffre_Q_1_D"
    };
    return lines;
}

void expect_equal(bool expected, bool actual)
{
    assert(actual == expected);
}

void run_blif_test()
{
    std::shared_ptr<HierNode> rootNode = BlifParser().parseLines(getFakeBlifLines());
    if (rootNode) {
        rootNode->printTree();
    }

    expect_equal(true, rootNode->contains("dut.u0.u0.d[6]"));
    expect_equal(true, rootNode->contains("dut.u0.u0.*"));
    expect_equal(true, rootNode->contains("dut.*.u0.d[6]"));
    expect_equal(true, rootNode->contains("dut.u*.u*.d[6]"));
    expect_equal(true, rootNode->contains("dut.u*.u*.d[*]"));
    expect_equal(true, rootNode->contains("dut.*"));

    expect_equal(false, rootNode->contains("dut.u0.u0.d[7]"));
    expect_equal(false, rootNode->contains("dut.u.u0.d[6]"));
    expect_equal(false, rootNode->contains("du.u0.u0.d[6]"));
}
//

}  // namespace FOEDAG
