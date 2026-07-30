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
#include "Utils/FileUtils.h"

#include <sstream>
#include <fstream>
#include <iostream>
#include <regex>
#include <cassert>
#include <unordered_set>

namespace FOEDAG {

BlifNode::BlifNode(const std::string& name): m_name{name}
{
}

BlifNode::~BlifNode()
{
    for (auto& [name, child]: m_children) {
        delete child;
    }
}

std::vector<std::string> BlifNode::findMatchingNames(const std::string& pattern)
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

void BlifNode::expandRecursive(const std::vector<std::string>& parts, std::size_t index,
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

void BlifNode::collectAllLeafPaths(const std::string& path, std::vector<std::string>& names) const {
    if (isLeaf()) {
        names.push_back(path);
        return;
    }

    for (const auto& [name, child] : m_children) {
        std::string fullPath = path.empty() ? name : path + "." + name;
        child->collectAllLeafPaths(fullPath, names);
    }
}

void BlifNode::insert(const std::string& fullName)
{
    auto parts = splitHierarchy(fullName);
    BlifNode* node = this;
    for (const std::string& part: parts) {
        node = node->getOrCreateChild(part);
    }
}

void BlifNode::printTree()
{
    printTreeRecursive(this);
}

std::vector<std::string> BlifNode::splitHierarchy(const std::string& name) const
{
    std::vector<std::string> parts;
    std::stringstream ss(name);
    std::string part;
    while (std::getline(ss, part, '.')) {
        parts.push_back(part);
    }
    return parts;
}

std::vector<BlifNode*> BlifNode::findChildren(const std::string& wildCardPattern) const
{
    std::vector<BlifNode*> result;

    for (const auto& [name, child]: m_children) {
        if (matches(child->getName(), wildCardPattern)) {
            result.push_back(child);
        }
    }

    return result;
}

BlifNode* BlifNode::getOrCreateChild(const std::string& name)
{
    if (!m_children.count(name)) {
        m_children[name] = new BlifNode{name};
    }
    return m_children[name];
}

void BlifNode::printTreeRecursive(const BlifNode* node, int depth)
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

std::string BlifNode::wildcardToRegex(const std::string& pattern)
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

bool BlifNode::containsPath(const std::vector<std::string>& parts, std::size_t index) const {
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
    for (BlifNode* child : findChildren(next)) {
        if (child->containsPath(parts, index + 1)) {
            return true;
        }
    }

    return false;
}

bool BlifNode::matches(const std::string& name, const std::string& pattern) const
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

std::shared_ptr<BlifNode> BlifParser::load(const std::filesystem::path& filepath)
{
    if (!std::filesystem::exists(filepath)) {
        return nullptr;
    }

    if (filepath.extension() != ".blif") {
        return nullptr;
    }

    if (!m_rootNodePtr || isFileChanged(filepath)) {
        std::vector<std::string> lines = FileUtils::GetFileContentLines(filepath);

        m_rootNodePtr = parseLines(lines);
        m_lastWriteTime = std::filesystem::last_write_time(filepath);
    }    

    return m_rootNodePtr;
}

std::shared_ptr<BlifNode> BlifParser::loadFromNames(const std::set<std::string>& names)
{
    m_rootNodePtr = std::make_shared<BlifNode>();
    for (const std::string& name : names) {
        m_rootNodePtr->insert(name);
    }
    return m_rootNodePtr;
}

std::shared_ptr<BlifNode> BlifParser::parseLines(const std::vector<std::string>& lines)
{
    m_rootNodePtr = std::make_shared<BlifNode>();

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

}  // namespace FOEDAG
