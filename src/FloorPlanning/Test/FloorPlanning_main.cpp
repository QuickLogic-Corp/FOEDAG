#include <QApplication>

#include "DeviceGridDescriptor.h"
#include "FloorPlanningWidget.h"

#include "SynthResourceExtractor.h"

#include <filesystem>
#include <fstream>

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
  // Minimal device config.json: a 30x30 core (-> 32x32 grid) with the DSP/BRAM
  // layout this test used to hard-code. DSP_COLS/BRAM_COLS are 1-based core
  // columns (grid column minus the IO border).
  const std::string configJson = R"({
    "DEVICE_SIZE": "30x30",
    "DSP_SIZE": "1x3",
    "BRAM_SIZE": "1x6",
    "DSP_COLS": "5,18",
    "BRAM_COLS": "11,24"
})";

  const std::filesystem::path configPath =
      std::filesystem::temp_directory_path() / "fp_test_device_config.json";

  {
    std::ofstream out(configPath);
    out << configJson;
  }

  fp::DeviceGridDescriptorPtr descriptor =
      std::make_shared<fp::DeviceGridDescriptor>(configPath);

  std::filesystem::remove(configPath);

  return descriptor;
}

int main(int argc, char** argv) {
    Q_INIT_RESOURCE(floorplanning_resource);
    QApplication app(argc, argv);

    fp::FloorPlanningWidget w("Demo");

    std::set<std::string> elements = genTestElements();
    // debug
    // fp::SynthResourceExtractor extractor;
    // extractor.parseAtomNamesFromBlifFile("/home/work/aurora_projects/counter_16bit/atom_netlist.cleaned.echo.blif");
    // const std::set<std::string>& elements = extractor.elements();
    // debug

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
