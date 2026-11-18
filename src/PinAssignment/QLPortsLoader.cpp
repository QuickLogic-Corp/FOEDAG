#include "QLPortsLoader.h"
#include "IODirection.h"

#include <QFile>
#include <QSet>
#include <QRegularExpression>
#include <QStringView>
#include <algorithm>

namespace FOEDAG {

namespace {

// Natural order, so count[2] comes before count[10]: a run of digits compares
// by its value, anything else as text.
bool naturalLess(const QString &a, const QString &b) {
  qsizetype i = 0;
  qsizetype j = 0;
  while (i < a.size() && j < b.size()) {
    if (!a.at(i).isDigit() || !b.at(j).isDigit()) {
      if (a.at(i) != b.at(j)) return a.at(i) < b.at(j);
      ++i;
      ++j;
      continue;
    }
    auto number = [](const QString &s, qsizetype &k) {
      const qsizetype start = k;
      while (k < s.size() && s.at(k).isDigit()) ++k;
      QStringView digits = QStringView{s}.mid(start, k - start);
      while (digits.size() > 1 && digits.front() == u'0') digits = digits.mid(1);
      return digits;
    };
    const QStringView x = number(a, i);
    const QStringView y = number(b, j);
    if (x.size() != y.size()) return x.size() < y.size();
    if (x != y) return x < y;
  }
  return a.size() - i < b.size() - j;
}

}  // namespace

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

    // all ".subckt ? C=?" occurs after ".inputs" and ".outputs", so it's safe to extract it here
    static QRegularExpression clockPattern(R"(\.subckt\s+\w+.*?\s+C=([\w\[\]]+))");
    QRegularExpressionMatch match = clockPattern.match(line);
    if (match.hasMatch()) {
      QString clock = match.captured(1);
      clocks.insert(clock);
    }
  }

  // The netlist lists bus bits in no useful order (count[0], count[3], ...).
  std::sort(inputs.begin(), inputs.end(), naturalLess);
  std::sort(outputs.begin(), outputs.end(), naturalLess);
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
