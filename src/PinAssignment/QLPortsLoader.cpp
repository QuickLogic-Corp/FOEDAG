#include "QLPortsLoader.h"
#include "IODirection.h"

#include <QFile>
#include <QSet>
#include <QRegularExpression>

namespace FOEDAG {

QLPortsLoader::QLPortsLoader(PortsModel *model, QObject *parent)
    : PortsLoader(model, parent) {}

std::pair<bool, QString> QLPortsLoader::load(const QString& filePath) {
  if (!filePath.endsWith(".blif")) {
    return std::make_pair(false, QString("QLPortsLoader: %1 is not a blif format").arg(filePath));
  }
  QFile file{filePath};
  if (!file.open(QFile::ReadOnly)) {
    return std::make_pair(false, QString("QLPortsLoader: Unable to open %1 file for reading").arg(filePath));
  }

  QList<QString> inputs;
  QList<QString> outputs;
  QSet<QString> clocks;

  while (!file.atEnd()) {
    QString line = file.readLine();
    if (line.startsWith(".inputs ")) {
      line = line.replace(".inputs ", "").trimmed();
      inputs = line.split(" ");
    }
    if (line.startsWith(".outputs ")) {
      line = line.replace(".outputs ", "").trimmed();
      outputs = line.split(" ");
    }

    // all ".subckt dffre" layes occur after ".inputs" and ".outputs", so it's safe to extract it here
    static QRegularExpression clockPattern(R"(\.subckt\s+dffre\s+C=([\w\[\]]+))");
    QRegularExpressionMatch match = clockPattern.match(line);
    if (match.hasMatch()) {
      QString clock = match.captured(1);
      clocks.insert(clock);
    }
  }

  IOPortGroup group;
  for (const QString& input: inputs) {
    if (!clocks.contains(input)) {
      addPort(group, input, IODirection::INPUT);
    }
  }
  for (const QString& output: outputs) {
    if (!clocks.contains(output)) {
      addPort(group, output, IODirection::OUTPUT);
    }
  }
  m_model->clear();
  m_model->append(group);
  m_model->initListModel();

  return std::make_pair(true, QString());
}

void QLPortsLoader::addPort(IOPortGroup& group, const QString& portName, const QString& portDirection)
{
  const int msb = 0; // not used
  const int lsb = 0; // not used

  IOPort ioport{portName,
          QString(portDirection),
          QString(),
          QString("LOGIC"),
          QString("Msb: %1, lsb: %2")
              .arg(QString::number(msb), QString::number(lsb)),
          (msb != lsb),
          {}};

  group.ports.append(ioport);
}

}  // namespace FOEDAG
