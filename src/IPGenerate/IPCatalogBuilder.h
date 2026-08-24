/*
Copyright 2021 The Foedag team

GPL License

Copyright (c) 2021 The Open-Source FPGA Foundation

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
#ifndef IPCATALOGBUILDER_H
#define IPCATALOGBUILDER_H

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "IPGenerate/IPCatalog.h"

namespace FOEDAG {
class Compiler;

class IPCatalogBuilder {
 public:
  IPCatalogBuilder(Compiler* compiler) : m_compiler(compiler) {}
  bool buildLiteXCatalog(IPCatalog* catalog,
                         const std::filesystem::path& litexIPgenPath,
                         bool namesOnly = false);

  virtual ~IPCatalogBuilder() {}

  bool buildLiteXIPFromGenerator(
      IPCatalog* catalog, const std::filesystem::path& pythonConverterScript);

  bool buildLiteXIPFromJson(IPCatalog* catalog,
                            const std::filesystem::path& pythonConverterScript,
                            const std::string& jsonStr,
                            const std::string& command = std::string{});

  // Reads the IP availability manifest that optionally sits beside an IP's
  // generator script: for Vendor/Library/Name/Version/<name>_gen.py that is
  // Version/ip_manifest.json. Never fails - a missing, unreadable, malformed
  // or unrecognised manifest yields the default IPAvailability (production,
  // no fabric requirement), with the problem recorded in manifestWarning and
  // reported once. An IP is never dropped from the catalog over its manifest.
  IPAvailability readIPManifest(
      const std::filesystem::path& pythonConverterScript);

  // Catalog name of the IP a generator script implements:
  // ".../axi_ram/V1_0/axi_ram_gen.py" -> "axi_ram_V1_0".
  static std::string ipNameFromGeneratorPath(
      const std::filesystem::path& pythonConverterScript);

 protected:
  bool buildLiteXIPFromGeneratorInternal(
      IPCatalog* catalog, const std::filesystem::path& pythonConverterScript);
  Compiler* m_compiler = nullptr;
  // "schema newer than we know" is a property of the catalog, not of each IP:
  // report it once per catalog walk instead of once per manifest.
  bool m_newerSchemaReported = false;
};

}  // namespace FOEDAG

#endif
