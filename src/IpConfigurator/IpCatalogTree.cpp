/*
Copyright 2022 The Foedag team

GPL License

Copyright (c) 2022 The Open-Source FPGA Foundation

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "IpConfigurator/IpCatalogTree.h"

#include <QTreeWidgetItem>

#include "IPGenerate/IPCatalog.h"
#include "IPGenerate/IPCatalogBuilder.h"
#include "MainWindow/Session.h"
#include "nlohmann_json/json.hpp"

#include <QDebug>

extern FOEDAG::Session* GlobalSession;

using namespace FOEDAG;

bool tclCmdExists(const QString& cmdName) {
  bool exists = false;
  int ok = TCL_ERROR;
  // If [info commands cmdName] returns nothing, the command doesn't exist
  QString cmd = QString("expr {[llength [info commands %1]] > 0}").arg(cmdName);
  // result == "0": Command doesn't exist
  // result == "1": Command does exist
  auto result = GlobalSession->TclInterp()->evalCmd(cmd.toStdString(), &ok);

  if (ok == TCL_OK && result != "0") {
    exists = true;
  }

  return exists;
}

IpCatalogTree::IpCatalogTree(QWidget* parent /*nullptr*/)
    : QTreeWidget(parent) {
  this->setHeaderLabel("Available IPs");
  refresh();
  connect(this, &QTreeWidget::itemSelectionChanged, this,
          &IpCatalogTree::itemSelectionHasChanged);
  connect(this, &QTreeWidget::itemDoubleClicked, this,
          &IpCatalogTree::openIpSettings);
}

void IpCatalogTree::refresh() {
  // The installed catalog plus the project-local user catalog
  // (<project>/IP_Catalog, empty when no project is open); loadIps skips
  // paths that do not exist. A name found in both roots is a load error —
  // IP names are unique across catalogs, matching the Tcl-side rule.
  IPGenerator* generator = GlobalSession->GetCompiler()->GetIPGenerator();
  std::filesystem::path IpCatalogPath = generator->DefaultIPCatalogPath();
  std::filesystem::path UserCatalogPath = generator->ProjectUserCatalogPath();
  std::vector<std::filesystem::path> IpPaths{IpCatalogPath, UserCatalogPath};

  const QList<IpEntry> ips = getAvailableIPs(IpPaths);

  // If available IPs have changed
  if (ips != prevIpCatalogResults) {
    this->clear();
    // Add a tree entry for each IP name
    for (const auto& ip : std::as_const(ips)) {
      QTreeWidgetItem* item = new QTreeWidgetItem();
      item->setText(0, ip.name);
      // An IP the current device cannot take stays visible but unusable, and
      // carries the reason in its tooltip. Hiding it would leave the user with
      // no way to find out why the IP they expected is not there.
      if (!ip.available) item->setDisabled(true);
      if (!ip.reason.isEmpty()) item->setToolTip(0, ip.reason);
      this->addTopLevelItem(item);
    }
    sortItems(0, Qt::SortOrder::AscendingOrder);
    prevIpCatalogResults = ips;
  }
}

QList<IpCatalogTree::IpEntry> IpCatalogTree::getAvailableIPs(
    const std::vector<std::filesystem::path>& paths) {
  QList<IpEntry> ips;

  // Load IPs
  loadIps(paths);

  if (!tclCmdExists("ip_catalog")) {
    qCritical() << "cmd ip_catalog is not registered";
    return ips;
  }

  // `-all -format json` is the only catalog surface that carries availability
  // and its reason; the plain listing is names only.
  const std::string result =
      GlobalSession->TclInterp()->evalCmd("ip_catalog -all -format json");
  try {
    const auto records = nlohmann::ordered_json::parse(result);
    for (const auto& record : records) {
      IpEntry entry;
      entry.name = QString::fromStdString(record.value("name", std::string{}));
      entry.state =
          QString::fromStdString(record.value("state", std::string{}));
      entry.reason =
          QString::fromStdString(record.value("reason", std::string{}));
      entry.available = record.value("available", true);
      entry.verified = record.value("requirement_verified", true);
      if (!entry.name.isEmpty()) ips.push_back(entry);
    }
  } catch (const std::exception& e) {
    qCritical() << "ip_catalog -all -format json is not parsable:" << e.what();
  }

  return ips;
}

void IpCatalogTree::loadIps(const std::vector<std::filesystem::path>& paths) {
  for (const auto& path : paths) {
    if (std::filesystem::exists(path)) {
      GlobalSession->GetCompiler()->BuildLiteXIPCatalog(path.lexically_normal(),
                                                        true);
    }
  }
}

void IpCatalogTree::itemSelectionHasChanged() {
  auto selected = selectedItems();
  if (!selected.isEmpty()) {
    auto ip = selected.first()->text(0);
    auto def =
        GlobalSession->GetCompiler()->GetIPGenerator()->Catalog()->Definition(
            ip.toStdString());
    if (def && !def->Valid()) {
      IPCatalogBuilder builder{GlobalSession->GetCompiler()};
      GlobalSession->GetCompiler()->GetIPGenerator()->shareContext();
      builder.buildLiteXIPFromGenerator(
          GlobalSession->GetCompiler()->GetIPGenerator()->Catalog(),
          def->FilePath());
    }
  }
  emit ipReady();
}
