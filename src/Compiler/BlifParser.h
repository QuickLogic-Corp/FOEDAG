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

class BlifNode {
public:
  BlifNode(const std::string& name = "");
  ~BlifNode();

  bool isLeaf() const { return m_children.empty(); }

  const std::string& getName() const { return m_name; }

  std::vector<std::string> findMatchingNames(const std::string& pattern);

  void insert(const std::string& fullName);

  void printTree();

private:
  std::string m_name;
  std::map<std::string, BlifNode*> m_children;

  std::vector<std::string> splitHierarchy(const std::string& name) const;
  std::vector<BlifNode*> findChildren(const std::string& wildCardPattern) const;
  BlifNode* getOrCreateChild(const std::string& name);
  static std::string wildcardToRegex(const std::string& pattern);
  bool containsPath(const std::vector<std::string>& parts, std::size_t index = 0) const;
  void expandRecursive(const std::vector<std::string>& parts, std::size_t index,
                                 const std::string& path, std::vector<std::string>& names) const;
  void collectAllLeafPaths(const std::string& path, std::vector<std::string>& names) const;
  bool matches(const std::string& name, const std::string& pattern) const;

  const std::map<std::string, BlifNode*>& getChildren() const { return m_children; }

  // debug hierarchy
  void printTreeRecursive(const BlifNode* node, int depth = 0);
};

class BlifParser {
public:
  std::vector<std::string> findMatchingNames(const std::string& pattern);

  std::shared_ptr<BlifNode> load(const std::filesystem::path& filepath);
  std::shared_ptr<BlifNode> parseLines(const std::vector<std::string>& lines);
  bool isFileChanged(const std::filesystem::path& filepath) const;

  void printHierachy() {
    if (m_rootNodePtr) {
      m_rootNodePtr->printTree();
    }
  }

private:
  std::shared_ptr<BlifNode> m_rootNodePtr;
  std::filesystem::file_time_type m_lastWriteTime;
};

#ifdef TEST_BLIFLOADER
void run_blif_test();
#endif

}  // namespace FOEDAG
