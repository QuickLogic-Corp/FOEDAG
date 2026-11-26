#include "FloorplanChecker.h"

#include "FloorplanningPaths.h"

#include "AtomSets.h"
#include "DeviceGridDescriptor.h"
#include "QdcSerializer.h"
#include "RtlInstanceModel.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fp {

namespace {

// The top model's .inputs and .outputs: the first .model of a BLIF is the top.
void readBlifPorts(const std::filesystem::path& blif, std::set<std::string>& inputs,
                   std::set<std::string>& outputs)
{
    std::ifstream in(blif);
    std::string line;
    int models = 0;
    while (std::getline(in, line)) {
        while (!line.empty() && line.back() == '\\') {
            line.pop_back();
            std::string next;
            if (!std::getline(in, next)) {
                break;
            }
            line += " " + next;
        }
        std::istringstream words(line);
        std::string command;
        words >> command;
        if (command == ".model" && ++models > 1) {
            return;
        }
        std::set<std::string>* ports =
            command == ".inputs" ? &inputs : command == ".outputs" ? &outputs : nullptr;
        for (std::string port; ports && words >> port;) {
            ports->insert(port);
        }
    }
}

}  // namespace

bool FloorplanChecker::check(const std::filesystem::path& projectPath,
                             const std::string& projectName,
                             const std::string& topModule,
                             const std::filesystem::path& qdcPath,
                             const std::filesystem::path& deviceLayoutFile,
                             DeviceGrid::Issues& issues,
                             std::string& error)
{
    issues.clear();

    if (!std::filesystem::exists(qdcPath)) {
        return false;  // nothing constrained; not a failure
    }
    if (deviceLayoutFile.empty() || !std::filesystem::exists(deviceLayoutFile)) {
        return false;  // no resolved layout yet (e.g. AUTO/RESOURCES before Packing); not a failure
    }

    DeviceGridDescriptorPtr descriptor =
        std::make_shared<DeviceGridDescriptor>(deviceLayoutFile);
    if (descriptor->hasError()) {
        error = descriptor->error().toStdString();
        return false;
    }

    DeviceGrid device(descriptor);
    FloorplanContext& context = *device.context();

    QdcSerializer serializer;
    serializer.load(device, qdcPath);
    if (device.partitions().empty()) {
        return false;  // a .qdc with no regions has nothing to check
    }

    // The .qdc carries RTL names only (REQ-002), so the elements arrive without atoms and
    // every required count would be zero. Attach them from the owner table, as the panel
    // does; without one nothing can be sized, which is an error, not "no issues" (F4).
    OwnerTable table;
    if (!loadOwnerTable(floorplanningArtifactOrBare(projectPath, projectName, "atomsets.json"),
                        table, error)) {
        return false;
    }
    context.atomTypes = table.atomTypes;
    context.top = table.top;
    loadDspTiles(postSynthDebugJson(projectPath, topModule), context.dspTiles);
    device.setOwnAtomCounts(table.ownAtomCounts);
    for (const auto& [id, partition] : device.partitions()) {
        const HierarhyElements elements = partition->elements();
        partition->clearElemenets();
        for (const HierarhyElement& element : elements) {
            partition->addElement(HierarhyElement{element.path, element.isLeaf,
                                                  atomNamesFor(table.atomNames, element.path)});
        }
    }

    // Instances synthesis deleted entirely: a .qdc may still name one, and such a constraint
    // matches no atom.
    RtlInstanceModel rtlModel;
    rtlModel.load(table);
    rtlModel.mergeVerdicts(floorplanningArtifact(projectPath, projectName, "validation.json"));
    std::unordered_set<std::string> deleted;
    std::set<std::string> paths;
    for (const RtlInstance& instance : rtlModel.instances()) {
        paths.insert(instance.path);
        if (instance.status == "deleted") {
            deleted.insert(instance.path);
        }
    }
    device.setDeletedInstances(std::move(deleted));
    device.setRtlInstances(std::move(paths));
    std::set<std::string> inputs;
    std::set<std::string> outputs;
    readBlifPorts(projectPath / (projectName + "_post_synth.blif"), inputs, outputs);
    device.setPorts(std::move(inputs), std::move(outputs));

    // A region's tiles are computed lazily against its rect, and QdcSerializer restores
    // geometry but never tiles. Without this every partition reports zero available and the
    // whole floorplan looks under-provisioned. DeviceGridWidget::reportPartitionChanges()
    // does the same before it checks, for the same reason.
    device.refreshPartitions();

    issues = *device.checkIssues();
    return true;
}

}  // namespace fp
