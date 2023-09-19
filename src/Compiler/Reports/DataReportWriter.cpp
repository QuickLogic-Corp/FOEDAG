#include "DataReportWriter.h"

#include "IDataReport.h"

#include <fstream>
#include <iostream>
#include <iomanip>

namespace FOEDAG {

bool DataReportWriter::write(const QString& filePath, const IDataReport& dataReport)
{
  bool status = false;

  std::ofstream file(filePath.toStdString());
  if (file.is_open()) {
    std::vector<ColumnFormatting> columnsFormat = detectColumnFormatting(dataReport);
    write(file, dataReport, columnsFormat);
    status = true;
  } else {
    std::cout << "[ERROR] cannot open " << filePath.toStdString() << " for writing" << std::endl;
    status = false;
  }

  file.close();

  return status;
}

std::vector<DataReportWriter::ColumnFormatting> DataReportWriter::detectColumnFormatting(const IDataReport& dataReport) 
{
  std::vector<ColumnFormatting> columnsFormatting;
  QList<QString> header;
  for (const ReportColumn& column: dataReport.getColumns()) {
    header << column.m_name;
    columnsFormatting.push_back(ColumnFormatting{static_cast<bool>(column.m_alignment & Qt::AlignLeft), column.m_name.size()});
  }

  for (int i=0; i<header.size(); ++i) {
    for (const QList<QString>& row: dataReport.getData()) {
      if ((i < row.size()) && (i < static_cast<int>(columnsFormatting.size()))) { // in some cases (seen in histogram), for some row we have number of cell is less than in header
        QString currentCell{row[i]};
        if (columnsFormatting[i].maxSize < currentCell.size()) {
          columnsFormatting[i].maxSize = currentCell.size();
        }
      }
    }
  }

  return columnsFormatting;
}

void DataReportWriter::write(std::ofstream& file, const IDataReport& dataReport, const std::vector<ColumnFormatting>& columnsFormatting)
{
    auto writeRowHelperFn = [](std::ofstream& file, const QList<QString>& row, std::vector<ColumnFormatting> columnsFormatting){
      ColumnFormatting defaultColumnFormatting{false, 100};
      ColumnFormatting& columnFormatting = defaultColumnFormatting;
      for (int i=0; i<row.size(); ++i) {
        QString cell = row[i];
        if (i < static_cast<int>(columnsFormatting.size())) {
          columnFormatting = columnsFormatting[i];
        } else {
          columnFormatting = defaultColumnFormatting;
        }

        int columnSize = columnFormatting.maxSize + DataReportWriter::COLUMN_MARGIN;

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
