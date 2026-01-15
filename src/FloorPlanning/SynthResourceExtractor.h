#pragma once

#include <string>
#include <set>

#include <QDebug>

class QDomElement;

namespace fp {

enum BlockType {
  UNKNOWN, FLE, CLB, BRAM, DSP
};

struct SynthResources {
  std::set<std::string> atoms;

  void add(const std::string& name) {
      atoms.insert(name);
  }

  // debug
  void print() const {
    qDebug() << "clbs:";
    for (const std::string& atom: atoms) {
      qDebug() << "  " << atom.c_str();
    }
  }
  // debug
};

class SynthResourceExtractor {
public:
  bool parseNetFileContent(const std::string&);
  QString error() const { return m_error; }

  const SynthResources& resources() const { return m_resources; }

private:
  QString m_error;
  SynthResources m_resources;
};

}  // namespace fp
