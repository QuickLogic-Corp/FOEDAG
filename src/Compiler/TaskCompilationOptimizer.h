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

#include <memory>
#include <filesystem>
#include <set>

#include <iostream> // debug

namespace FOEDAG {

enum class Stage{
  Synthesis,
  Packing,
  Placement,
  Routing,
  TimingAnalysis,
  Power,
  BitstreamGeneration
};

std::string to_string(Stage id) {
  switch(id) {
  case Stage::Synthesis: return "Synthesis";
  case Stage::Packing: return "Packing";
  case Stage::Placement: return "Placement";
  case Stage::Routing: return "Routing";
  case Stage::TimingAnalysis: return "TimingAnalysis";
  case Stage::Power: return "Power";
  case Stage::BitstreamGeneration: return "BitstreamGeneration";
  }
  return "";
}

class Node;
using NodePtr = std::shared_ptr<Node>;

using TripleKey = std::tuple<std::string, std::string, std::string>;

class Node {
public:
  Node(Stage id): m_id(id) {
    std::cout << "create Node:" << to_string(m_id) << std::endl;
  }
  ~Node() {
    std::cout << "delete Node:" << to_string(m_id) << std::endl;
  }
 
  void append(const NodePtr& next) { 
    if (m_next) {
      m_next->append(next);
    } else {
      m_next = next; 
    }
  }

  void addFilePath(const std::filesystem::path& filePath) {
    m_filePathes.insert(filePath);
  }

  void addStringParameter(const TripleKey& key) {
    m_stringParameters.insert(key);
  }

  void addLongDoubleParameter(const TripleKey& key) {
    m_longDoubleParameters.insert(key);
  }

private:
  Stage m_id;
  std::set<std::filesystem::path> m_filePathes;
  std::set<TripleKey> m_stringParameters;
  std::set<TripleKey> m_longDoubleParameters;

  NodePtr m_next;
};

class Graph {
public:
  Graph() {
    NodePtr synthesis = std::make_shared<Node>(Stage::Synthesis);
    NodePtr packing = std::make_shared<Node>(Stage::Packing);
    NodePtr placement = std::make_shared<Node>(Stage::Placement);
    NodePtr routing = std::make_shared<Node>(Stage::Routing);
    NodePtr analysis = std::make_shared<Node>(Stage::TimingAnalysis);
    NodePtr power = std::make_shared<Node>(Stage::Power);
    NodePtr bitstream = std::make_shared<Node>(Stage::BitstreamGeneration);

    // device
    synthesis->addStringParameter(std::tuple("general", "device", "family"));
    synthesis->addStringParameter(std::tuple("general", "device", "foundry"));
    synthesis->addStringParameter(std::tuple("general", "device", "node"));
    synthesis->addStringParameter(std::tuple("general", "device", "devicename"));
    synthesis->addStringParameter(std::tuple("general", "device", "voltage_threshold"));
    synthesis->addStringParameter(std::tuple("general", "device", "p_v_t_corner"));
    synthesis->addStringParameter(std::tuple("general", "device", "layout"));
    // device

    // synth tool
    synthesis->addStringParameter(std::tuple("synplify", "general", "mode"));
    //synthesis->addFilePath(QLSettingsManager::getSDCFilePath());
    //synthesis->addFilePath(QLDeviceManager::getInstance()->deviceYosysScriptFile());

    synthesis->addStringParameter(std::tuple("general", "options", "verific"));

    // what to do with design files? // for (const auto& lang_file : ProjManager()->DesignFiles()) {
    // maybe we just should check the ProjManager on changes?



    m_root = synthesis;
    m_root->append(packing);
    m_root->append(placement);
    m_root->append(routing);
    m_root->append(analysis);
    m_root->append(power);
    m_root->append(bitstream);
  }
private:
  NodePtr m_root;
};

void graph_test() {
  Graph graph;
}

}  // namespace FOEDAG
