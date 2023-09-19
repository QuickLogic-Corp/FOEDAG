#include "DefaultTaskReport.h"

#include "IDataReport.h"

#include "DataReportWriter.h"

#include <QDir>

#include <iostream>

namespace FOEDAG {
DefaultTaskReport::DefaultTaskReport(ITaskReport::DataReports &&dataReports,
                                     const QString &name)
    : m_dataReports{std::move(dataReports)}, m_name{name} {}

const ITaskReport::DataReports &DefaultTaskReport::getDataReports() const {
  return m_dataReports;
}

const QString &DefaultTaskReport::getName() const { return m_name; }

void DefaultTaskReport::saveToFile(const QString& root) const
{
  QDir location{root + "/task-reports"};
  if (!location.exists()) {
    if (!location.mkpath(".")) {
      std::cout << "cannot create folder" << location.absolutePath().toStdString() << std::endl;
    }
  }

  for (const std::unique_ptr<IDataReport>& dataReport: m_dataReports) {
    QString fileName{getName() + "_" + dataReport->getName() + ".rpt"};
    fileName = fileName.replace(" ", "");

    QString filePath{location.absolutePath() + "/" + fileName};
    
    DataReportWriter::write(filePath, *dataReport.get());
  }  
}

}  // namespace FOEDAG
