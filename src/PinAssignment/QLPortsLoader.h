#pragma once

#include "PortsLoader.h"

#include <QSet>

namespace FOEDAG {

class QLPortsLoader : public PortsLoader {
 public:
  QLPortsLoader(PortsModel *model, const QSet<QString>& clocks, QObject *parent = nullptr);
  std::pair<bool, QString> load(const QString &file) override final;

private:
  QSet<QString> m_clocks;

  void addPort(IOPortGroup& group, const QString& portName, const QString& portDirection);
};

}  // namespace FOEDAG
