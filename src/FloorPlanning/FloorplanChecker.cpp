#include "FloorplanChecker.h"

#include "FloorplanningPaths.h"

#include "AtomSets.h"
#include "DeviceGridDescriptor.h"
#include "QdcSerializer.h"
#include "RtlInstanceModel.h"

#include <filesystem>

namespace fp {

bool FloorplanChecker::check(const std::filesystem::path& projectPath,
                             const std::string& projectName,
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

    // Partition ids are handed out from a static counter shared with the panel. Reset so a
    // batch check does not depend on whatever ran before it in the same process.
    Partition::resetIdGenerator();
    Region::resetIdGenerator();

    QdcSerializer serializer;
    serializer.load(device, qdcPath);
    if (device.partitions().empty()) {
        return false;  // a .qdc with no regions has nothing to check
    }

    // The .qdc carries RTL names only (REQ-002), so the elements arrive without atoms and
    // every required count would be zero -- nothing could ever look under-provisioned.
    // Attach them the same way the panel does, through the shared lookup.
    AtomNameMap atomNames;
    int atomsPerTile = Partition::atomsPerTile();
    const bool haveAtoms =
        loadAtomSets(floorplanningArtifactOrBare(projectPath, projectName, "atomsets.json"),
                     atomNames, atomsPerTile);
    if (haveAtoms) {
        Partition::setAtomsPerTile(atomsPerTile);
        device.setOwnAtomCounts(ownAtomCounts(atomNames));

        for (const auto& [id, partition] : device.partitions()) {
            const HierarhyElements elements = partition->elements();
            partition->clearElemenets();
            for (const HierarhyElement& element : elements) {
                partition->addElement(HierarhyElement{element.path, element.isLeaf,
                                                      atomNamesFor(atomNames, element.path)});
            }
        }
    }

    // Instances synthesis deleted entirely: a .qdc may still name one, and such a constraint
    // matches no atom.
    RtlInstanceModel rtlModel;
    if (rtlModel.loadInstances(
            floorplanningArtifact(projectPath, projectName, "instances.json"))) {
        rtlModel.mergeVerdicts(
            floorplanningArtifact(projectPath, projectName, "validation.json"));
        std::unordered_set<std::string> deleted;
        for (const RtlInstance& instance : rtlModel.instances()) {
            if (instance.status == "deleted") {
                deleted.insert(instance.path);
            }
        }
        device.setDeletedInstances(std::move(deleted));
    }

    // A region's tiles are computed lazily against its rect, and QdcSerializer restores
    // geometry but never tiles. Without this every partition reports zero available and the
    // whole floorplan looks under-provisioned. DeviceGridWidget::reportPartitionChanges()
    // does the same before it checks, for the same reason.
    device.refreshPartitions();

    issues = *device.checkIssues();
    return true;
}

}  // namespace fp
