#pragma once

#include <string>
#include <filesystem>
#include <set>

namespace fp {

class SynthResourceExtractor {
public:
  bool loadAtomNamesFromNetFile(const std::filesystem::path&);
  bool loadAtomNamesFromBlifFile(const std::filesystem::path&);
  bool loadAtomNamesFromTxtFile(const std::filesystem::path&);

  bool parseAtomNamesFromNetFileContent(const std::string&);
  bool parseAtomNamesFromBlifFileContent(const std::string&);
  bool parseAtomNamesFromTxtFileContent(const std::string&);

  std::string error() const { return m_error; }

  const std::set<std::string>& elements() const { return m_elements; }

private:
  std::string m_error;
  std::set<std::string> m_elements;
};

}  // namespace fp
