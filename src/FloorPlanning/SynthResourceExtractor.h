#pragma once

#include <string>
#include <set>

namespace fp {

enum BlockType {
  UNKNOWN, FLE, CLB, BRAM, DSP
};

class SynthResourceExtractor {
public:
  bool parseNetFileContent(const std::string&);
  std::string error() const { return m_error; }

  const std::set<std::string>& elements() const { return m_elements; }

private:
  std::string m_error;
  std::set<std::string> m_elements;
};

}  // namespace fp
