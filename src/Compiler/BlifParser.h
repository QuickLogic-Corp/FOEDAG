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
#pragma once

#include <filesystem>
#include <string>
#include <map>
#include <vector>
#include <memory>

namespace FOEDAG {

class HierNode {
public:
  HierNode(const std::string& name = "");
  ~HierNode();

  bool hasChildren() const { return !m_children.empty(); }

  const std::string& getName() const { return m_name; }
  const std::map<std::string, HierNode*>& getChildren() const { return m_children; }

  bool contains(const std::string& full_name);
  void insert(const std::string& full_name);

  void printTree();

private:
  std::string m_name;
  std::map<std::string, HierNode*> m_children;

  std::vector<std::string> splitHierarchy(const std::string& name) const;
  std::vector<HierNode*> findChildren(const std::string& wildCardPattern) const;
  HierNode* getChild(const std::string& name) const;
  HierNode* getOrCreateChild(const std::string& name);
  void printTreeRecursive(const HierNode* node, int depth = 0);
  std::string wildcardToRegex(const std::string& pattern) const;
  bool matchesWildcard(const std::string& text, const std::string& pattern) const;
  std::string popFirstSegment(std::string& str);
  std::string getFirstSegment(const std::string& str);
  bool containsRecursive(std::string& full_name);
  bool matches(const std::string& name, const std::string& pattern) const;
};

class BlifParser {
public:
  std::shared_ptr<HierNode> load(const std::filesystem::path& filepath);
  std::shared_ptr<HierNode> parseLines(const std::vector<std::string>& lines);
  bool isFileChanged(const std::filesystem::path& filepath) const;
  bool contains(const std::string& pattern);

  void printHierachy() {
    if (m_rootNodePtr) {
      m_rootNodePtr->printTree();
    }
  }

private:
  std::shared_ptr<HierNode> m_rootNodePtr;
};

void run_blif_test();

}  // namespace FOEDAG
