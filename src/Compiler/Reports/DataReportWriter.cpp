#include "DataReportWriter.h"

#include "IDataReport.h"

#include <fstream>
#include <iostream>
#include <iomanip>

namespace FOEDAG {

bool DataReportWriter::write(const QString& filePath, const IDataReport& dataReport)
{
  bool status = false;

  std::cout << "~~~ attempt to write to" << filePath.toStdString() << std::endl;
  std::ofstream file(filePath.toStdString());
  if (file.is_open()) {
    std::vector<ColumnFormatting> columnsFormat = detectColumnFormatting(dataReport);
    write(file, dataReport, columnsFormat);
    status = true;
  } else {
    std::cout << "~~~ [ERROR] cannot open" << filePath.toStdString() << std::endl;
    status = false;
  }

  file.close();

  return status;
}

std::vector<DataReportWriter::ColumnFormatting> DataReportWriter::detectColumnFormatting(const IDataReport& dataReport) 
{
  std::vector<ColumnFormatting> columnsFormat;
  QList<QString> header;
  int counter = 0;
  for (const ReportColumn& column: dataReport.getColumns()) {
    header << column.m_name;
    columnsFormat.push_back(ColumnFormatting{static_cast<bool>(column.m_alignment & Qt::AlignLeft), column.m_name.size()});
    counter++;
  }

  for (const QList<QString>& row: dataReport.getData()) {
    for (int i=0; i<header.size(); ++i) {
      if (i < row.size()) { // in some cases (seen in histogram), for some row we have number of cell is less than in header
        if (row[i].size() > columnsFormat[i].size) {
          columnsFormat[i].size = row[i].size();
        }
      }
    }
  }

  return columnsFormat;
}

void DataReportWriter::write(std::ofstream& file, const IDataReport& dataReport, const std::vector<ColumnFormatting>& columnsFormatting)
{
    auto writeRowHelperFn = [](std::ofstream& file, const QList<QString>& row, std::vector<ColumnFormatting> columnsFormatting){
      for (int i=0; i<row.size(); ++i) {
        QString cell = row[i];
        const ColumnFormatting& columnFormatting = columnsFormatting[i];

        int columnSize = columnFormatting.size + DataReportWriter::COLUMN_MARGIN;

        if (columnFormatting.alignLeft) {
          file << std::setw(columnSize) << std::left << cell.toStdString();
        } else {
          int padding = columnSize - cell.length();
          int leftPadding = padding / 2;
          int rightPadding = padding - leftPadding;
            
          file << std::setw(leftPadding) << ""  << std::left << cell.toStdString() << std::setw(rightPadding) << "";
        }
      }
      file << "\n";
    };

    QList<QString> columns;
    for (const ReportColumn& column: dataReport.getColumns()) {
      columns << column.m_name;
    }
    writeRowHelperFn(file, columns, columnsFormatting);
    for (const QList<QString>& lineData: dataReport.getData()) {
      writeRowHelperFn(file, lineData, columnsFormatting);
    }
}

}  // namespace FOEDAG
