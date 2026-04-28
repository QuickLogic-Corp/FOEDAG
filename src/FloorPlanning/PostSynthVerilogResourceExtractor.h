#pragma once

#include <string>
#include <filesystem>
#include <set>

namespace fp {

class PostSynthVerilogResourceExtractor {
public:
  bool loadAtomNamesFromVerilogFile(const std::filesystem::path&);

  bool parseAtomNamesFromVerilogFileContent(const std::string&);

  std::string error() const { return m_error; }

  const std::set<std::string>& elements() const { return m_elements; }

  bool dumpElementsToFile(const std::filesystem::path&) const;

private:
  std::string m_error;
  std::set<std::string> m_elements;
};

}  // namespace fp
