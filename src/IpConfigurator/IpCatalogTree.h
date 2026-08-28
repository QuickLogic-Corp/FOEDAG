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
#pragma once

#include <QList>
#include <QTreeWidget>
#include <filesystem>

namespace FOEDAG {

class IpCatalogTree : public QTreeWidget {
  Q_OBJECT

 public:
  explicit IpCatalogTree(QWidget* parent = nullptr);
  void refresh();
  static void loadIps(const std::vector<std::filesystem::path>& paths);

 signals:
  void ipReady();
  void openIpSettings();

 private slots:
  void itemSelectionHasChanged();

 private:
  // One row of the catalog as reported by `ip_catalog -all -format json`.
  // The reason travels with the name so an IP the device cannot take can be
  // shown greyed out and still explain itself, instead of just disappearing.
  struct IpEntry {
    QString name;
    QString state;  // "production" | "preview" | "unavailable"
    QString reason;
    bool available{true};
    // False when the IP's fabric requirement could not be read; the IP is
    // still usable but the gate never ran, and the tooltip says so.
    bool verified{true};
    bool operator==(const IpEntry& other) const {
      return name == other.name && state == other.state &&
             reason == other.reason && available == other.available &&
             verified == other.verified;
    }
  };
  QList<IpEntry> prevIpCatalogResults;

  QList<IpEntry> getAvailableIPs(const std::vector<std::filesystem::path>& paths);
};

}  // namespace FOEDAG
