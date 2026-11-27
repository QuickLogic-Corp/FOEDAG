#include "PinAssignment/PortsView.h"

#include "PinAssignment/IODirection.h"
#include "PinAssignment/PackagePinsModel.h"
#include "PinAssignment/PinsBaseModel.h"
#include "PinAssignment/PortsModel.h"
#include "gtest/gtest.h"

#include <QCoreApplication>
#include <QHeaderView>

using namespace FOEDAG;

TEST(PortsView, DirColumnFitsItsText) {
  PortsModel ports;
  IOPort bus{"count", IODirection::OUTPUT, {}, {}, {}, true, {}};
  bus.ports.append(IOPort{"count[0]", IODirection::OUTPUT, {}, {}, {}, false, {}});
  ports.append(IOPortGroup{{}, {IOPort{"reset", IODirection::INPUT, {}, {}, {}, false, {}}, bus}});
  ports.initListModel();
  PackagePinsModel pins;
  PinsBaseModel base;
  base.setPortsModel(&ports);
  base.setPackagePinModel(&pins);
  pins.setBaseModel(&base);

  PortsView view{&base};
  view.show();
  view.expandAll();
  QCoreApplication::processEvents();
  const int width = view.columnWidth(1);
  view.header()->setSectionResizeMode(1, QHeaderView::Interactive);
  view.resizeColumnToContents(1);
  EXPECT_EQ(width, view.columnWidth(1));
}
