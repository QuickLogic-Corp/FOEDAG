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

#include <string>
#include <set>

#include <QDebug>

class QDomElement;

namespace FOEDAG {

enum BlockType {
  UNKNOWN, CLB, BRAM, DSP
};

struct SynthResources {
  std::set<std::string> clbs;
  std::set<std::string> brams;
  std::set<std::string> dsps;

  void add(BlockType type, const std::string& name) {
    switch(type) {
      case BlockType::CLB: clbs.insert(name); break;
      case BlockType::BRAM: brams.insert(name); break;
      case BlockType::DSP: dsps.insert(name); break;
      default: break;
    }
  }

  // debug
  void print() const {
    qDebug() << "clbs:";
    for (const std::string& clb: clbs) {
      qDebug() << "  " << clb.c_str();
    }

    qDebug() << "brams:";
    for (const std::string& bram: brams) {
      qDebug() << "  " << bram.c_str();
    }
    
    qDebug() << "dsps:";
    for (const std::string& dsp: dsps) {
      qDebug() << "  " << dsp.c_str();
    }
  }
  // debug
};

class SynthResourceExtractor {
public:
  const SynthResources& resources() const { return m_resources; }
  void parseNetFileContent(const std::string&);

private:
  SynthResources m_resources;
  BlockType determineBlockType(const QDomElement&) const;
};

}  // namespace FOEDAG
