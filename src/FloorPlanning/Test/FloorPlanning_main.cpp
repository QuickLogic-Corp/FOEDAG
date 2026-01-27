#include <QApplication>

#include "DeviceGridDescriptor.h"
#include "FloorPlanningWidget.h"

std::set<std::string> genTestElements()
{
  std::set<std::string> elements = {"dut.prism.el00.sub001",
                                    "dut.prism.el00.sub002",
                                    "dut.prism.el01",
                                    "dut.prism.el02",
                                    "dut.tri.el0.sub2",
                                    "dut.tri.el1",
                                    "dut.tri.el2",
                                    "top"};

  return elements;
}

fp::DeviceGridDescriptorPtr genTestDeviceDescriptor()
{
  const int columns = 32;
  const int rows = 32;
  const std::set<int> dspColumns{6,19};
  const std::set<int> bramColumns{12,25};
  const QSize dspSize{1, 3};
  const QSize bramSize{1, 6};

  fp::DeviceGridDescriptorPtr descriptor = std::make_shared<fp::DeviceGridDescriptor>(columns,
                                                                                      rows,
                                                                                      dspColumns,
                                                                                      bramColumns,
                                                                                      dspSize,
                                                                                      bramSize);
  return descriptor;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    fp::FloorPlanningWidget w;

    std::set<std::string> elements = genTestElements();
    w.loadNetList(elements);

    fp::DeviceGridDescriptorPtr descriptor = genTestDeviceDescriptor();

    if (descriptor->hasError()) {
        qCritical() << descriptor->error();
        return 1;
    }
    w.setDeviceGridDescriptor(descriptor);
    w.setQdcFilePath("floorplanning.qdc", true);

    w.show();

    return app.exec();
}
