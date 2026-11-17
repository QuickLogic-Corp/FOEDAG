#include "PinAssignment/PcfObserver.h"

#include "PinAssignment/LocationCollisionDetector.h"
#include "PinAssignment/PackagePinsModel.h"
#include "PinAssignment/PinsBaseModel.h"
#include "PinAssignment/PortsModel.h"
#include "gtest/gtest.h"

#include <QFile>
#include <QStringListModel>
#include <QTemporaryDir>

using namespace FOEDAG;

TEST(PcfObserver, RechecksWhenPortsChange) {
  QTemporaryDir dir;
  const QString pcf = dir.filePath("pins.pcf");
  QFile file{pcf};
  ASSERT_TRUE(file.open(QFile::WriteOnly));
  file.write("set_io reset P1\n");
  file.close();

  PortsModel ports;
  PackagePinsModel pins;
  PinsBaseModel base;
  base.setPortsModel(&ports);
  base.setPackagePinModel(&pins);
  base.setCollisionDetector(
      std::make_shared<LocationCollisionDetector>(QMap<QString, QString>{}));
  pins.setBaseModel(&base);
  pins.listModel()->setStringList({"", "P1"});
  ports.listModel()->setStringList({"", "reset"});

  PcfObserver observer{nullptr, pcf, &ports, &pins};
  observer.lineFrames();
  EXPECT_FALSE(observer.hasErrors());

  ports.listModel()->setStringList({"", "rst_n"});
  observer.lineFrames();
  ASSERT_TRUE(observer.hasErrors());
  EXPECT_EQ(observer.errors().first().first(), "Bad port 'reset'");
}
