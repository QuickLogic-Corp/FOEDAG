#include <QApplication>

#include "DeviceGridDescriptor.h"
#include "FloorPlanningWidget.h"

#include "SynthResourceExtractor.h"

#include <filesystem>
#include <fstream>
#include <random>

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
  // Minimal device_layout.json: a 30x30 core (-> 32x32 grid) with the DSP/BRAM
  // layout this test used to hard-code. dsp_cols/bram_cols are 1-based core
  // columns (grid column minus the IO border); already resolved by
  // QLDeviceLayoutInfo, so there is no CUSTOM-vs-flat spelling to choose here.
  const std::string deviceLayoutJson = R"({
    "array_x": 30,
    "array_y": 30,
    "dsp_size": "1x3",
    "bram_size": "1x6",
    "dsp_cols": "5,18",
    "bram_cols": "11,24"
})";

  // A fixed name in the shared temp dir can collide with a leftover file
  // from another user/run; a random suffix keeps this path ours alone.
  static std::mt19937_64 rng(std::random_device{}());
  const std::filesystem::path layoutPath =
      std::filesystem::temp_directory_path() /
      ("fp_test_device_layout_" + std::to_string(rng()) + ".json");

  bool wroteOk = false;
  {
    std::ofstream out(layoutPath);
    out << deviceLayoutJson;
    wroteOk = out.good();
  }
  if (!wroteOk) {
    qCritical() << "failed to write test device_layout.json to"
                << QString::fromStdString(layoutPath.string());
    return nullptr;
  }

  fp::DeviceGridDescriptorPtr descriptor =
      std::make_shared<fp::DeviceGridDescriptor>(layoutPath);

  std::filesystem::remove(layoutPath);

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

    if (!descriptor) {
        return 1;
    }
    if (descriptor->hasError()) {
        qCritical() << descriptor->error();
        return 1;
    }
    w.setDeviceGridDescriptor(descriptor);
    w.setQdcFilePath("floorplanning.qdc", true);

    w.show();

    return app.exec();
}
