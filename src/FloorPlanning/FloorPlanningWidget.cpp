#include "FloorPlanningWidget.h"

#include "AtomSets.h"

#include "SynthResourceHierarchyWidget.h"
#include "DeviceGridWidget.h"
#include "PartitionsListWidget.h"
#include "IssuesListWidget.h"
#include "CheckableButton.h"
#include "Widgets/RoundProgressWidget.h"

#include <QAction>
#include <QButtonGroup>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QCheckBox>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStyle>
#include <QKeyEvent>

namespace fp {

namespace {

// [aurora2#1725] floorplanning.cfg keys. Grouped, so the file reads as a FloorPlanning
// section rather than QSettings' anonymous "[General]".
constexpr auto kCfgTreatWarningsAsErrors = "FloorPlanning/treat_warnings_as_errors";
constexpr auto kCfgShowAtomColumns = "FloorPlanning/show_atom_list_and_type_columns";

}  // namespace

FloorPlanningWidget::FloorPlanningWidget(const QString& projectName, QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(projectName + tr(" - Floor Planning"));
    const int indicatorSize = 32;
    m_busyOverlayWidget = new FOEDAG::RoundProgressWidget(indicatorSize, this);
    const int m = FP_UI_MARGIN;

    m_synthResourcesWidget = new SynthResourceHierarchyWidget;
    m_synthResourcesWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_deviceWidget = new DeviceGridWidget;
    m_deviceWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // right pane
    QWidget* wRightPane = new QWidget;
    QVBoxLayout* rightPaneLayout = new QVBoxLayout(wRightPane);
    rightPaneLayout->setContentsMargins(m,m,m,m);
    rightPaneLayout->setSpacing(m);

    m_partitionsListWidget = new PartitionsListWidget;
    rightPaneLayout->addWidget(m_partitionsListWidget);
    connect(m_partitionsListWidget, &PartitionsListWidget::selectionChanged, m_deviceWidget, &DeviceGridWidget::onPartitionSelected);

    // [aurora2#1725] The list owns creating and deleting partitions. A new one is created
    // unnamed: Partition's constructor assigns "partition<n>", and the list opens that cell
    // for editing, so the name is typed in place of a prompt shown before it exists.
    connect(m_partitionsListWidget, &PartitionsListWidget::newPartitionRequested,
            this, [this] { m_deviceWidget->createNewPartition(); });
    connect(m_partitionsListWidget, &PartitionsListWidget::partitionRemoveRequested,
            m_deviceWidget, &DeviceGridWidget::removePartitionById);

    int flags = SynthResourceHierarchyWidget::Flag::ShowOnlyCheckedItems | SynthResourceHierarchyWidget::Flag::HidePartitionsColumn;
    m_partitionResourcesWidget = new SynthResourceHierarchyWidget(flags);
    m_partitionResourcesWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_partitionResourcesWidget->setViewLabelTemplate(tr("Partition '%1' netlist:"));
    rightPaneLayout->addWidget(m_partitionResourcesWidget);

    m_issuesListWidget = new IssuesListWidget;
    rightPaneLayout->addWidget(m_issuesListWidget);

    // m_synthResourcesWidget
    connect(m_deviceWidget, &DeviceGridWidget::partitionSelected,
            m_synthResourcesWidget, &SynthResourceHierarchyWidget::selectPartition);

    connect(m_deviceWidget, &DeviceGridWidget::unselectPartitionRequested,
            m_synthResourcesWidget, &SynthResourceHierarchyWidget::unselectPartition);

    // m_partitionResourcesWidget
    connect(m_deviceWidget, &DeviceGridWidget::unselectPartitionRequested,
            m_partitionResourcesWidget, &SynthResourceHierarchyWidget::unselectPartition);

    connect(m_deviceWidget, &DeviceGridWidget::partitionSelected,
            m_partitionResourcesWidget, &SynthResourceHierarchyWidget::selectPartition);

    // to FloorPlanningWidget hub
    connect(m_deviceWidget, &DeviceGridWidget::partitionsChanged,
            this, &FloorPlanningWidget::onPartitionsChanged);
    connect(m_synthResourcesWidget, &SynthResourceHierarchyWidget::partitionElementsChanged,
            m_deviceWidget, &DeviceGridWidget::onPartitionSelectedElementsChanged);
    connect(m_partitionResourcesWidget, &SynthResourceHierarchyWidget::partitionElementsChanged,
            m_deviceWidget, &DeviceGridWidget::onPartitionSelectedElementsChanged);

    // m_partitionsList
    connect(m_deviceWidget, &DeviceGridWidget::partitionSelected,
            m_partitionsListWidget, &PartitionsListWidget::onPartitionSelectedOutside);
    connect(m_partitionsListWidget, &PartitionsListWidget::partitionRenamed,
            m_deviceWidget, &DeviceGridWidget::onPartitionRenamed);
    connect(m_partitionsListWidget, &PartitionsListWidget::notify,
            this, &FloorPlanningWidget::onNotify);
    connect(m_deviceWidget, &DeviceGridWidget::unselectPartitionRequested,
            m_partitionsListWidget, &PartitionsListWidget::unselectPartition);

    // m_deviceWidget
    connect(m_deviceWidget, &DeviceGridWidget::notify,
            this, &FloorPlanningWidget::onNotify);

    connect(m_deviceWidget, &DeviceGridWidget::checkIssuesFinished,
            this, &FloorPlanningWidget::onCheckIssuesFinished);

    // toolBar
    QWidget* toolBarContainer = new QWidget;
    QHBoxLayout* toolBarLayout = new QHBoxLayout;
    toolBarContainer->setLayout(toolBarLayout);
    toolBarLayout->setContentsMargins(m,m,m,m);
    toolBarLayout->setSpacing(m);

    QPushButton* bnLoadQdc = new QPushButton(QIcon(":/load-action.png"), "");
    connect(bnLoadQdc, &QPushButton::clicked, this, &FloorPlanningWidget::loadQdc);
    bnLoadQdc->setToolTip(tr("Load QDC"));

    m_bnSaveQdc = new QPushButton(QIcon(":/save-action.png"), "");
    connect(m_bnSaveQdc, &QPushButton::clicked, m_deviceWidget, [this]() { saveQdc(); });
    m_bnSaveQdc->setToolTip(tr("Save QDC (Ctrl+S)"));
    m_bnSaveQdc->setEnabled(false);

    // [aurora2#1725] Ctrl+S saves, same as the button. Scoped to this window (a QShortcut on
    // a widget defaults to Qt::WindowShortcut), so it cannot reach the main window's own
    // Ctrl+S while FloorPlanning has focus, nor fire when it does not.
    auto* saveShortcut = new QShortcut(QKeySequence::Save, this);
    connect(saveShortcut, &QShortcut::activated, this, [this]() { saveQdc(); });

    m_bnRemoveSelectedRegion = new QPushButton(QIcon(":/erase.png"), "");
    connect(m_bnRemoveSelectedRegion, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::removeSelected);
    // [aurora2#1725] Partitions are deleted from their own row in the partitions list now;
    // a selected region has no row of its own, so it is still removed from here.
    m_bnRemoveSelectedRegion->setToolTip(tr("Remove selected region"));
    m_bnRemoveSelectedRegion->setEnabled(false);

    // [aurora2#1725] Drawing a region with no partition yet: create one unnamed rather than
    // interrupting the gesture with a name prompt. It appears in the list, where its name can
    // be edited in place like any other.
    connect(m_deviceWidget, &DeviceGridWidget::createFirstPartitionRequested,
            this, [this] { m_deviceWidget->createNewPartition(); });

    QCheckBox* bnScrollPartition = new QCheckBox("Scroll to partition");
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    connect(bnScrollPartition, &QCheckBox::checkStateChanged, m_deviceWidget,
            [this](Qt::CheckState state) {
                m_deviceWidget->setScrollToPartitionWhenSelected(state != Qt::Unchecked);
            });
#else
    connect(bnScrollPartition, &QCheckBox::stateChanged, m_deviceWidget,
            &DeviceGridWidget::setScrollToPartitionWhenSelected);
#endif
    bnScrollPartition->setVisible(false);

    // move controls
    QPushButton* bnLeft = new QPushButton(QIcon(":/left-arrow.png"), "");
    QPushButton* bnRight = new QPushButton(QIcon(":/right-arrow.png"), "");
    QPushButton* bnDown = new QPushButton(QIcon(":/down-arrow.png"), "");
    QPushButton* bnUp = new QPushButton(QIcon(":/up-arrow.png"), "");

    bnLeft->setToolTip(tr("Pan view left"));
    bnRight->setToolTip(tr("Pan view right"));
    bnDown->setToolTip(tr("Pan view down"));
    bnUp->setToolTip(tr("Pan view up"));

    connect(bnLeft, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::moveViewLeft);
    connect(bnUp, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::moveViewUp);
    connect(bnRight, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::moveViewRight);
    connect(bnDown, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::moveViewDown);

    // zoom controls
    QPushButton* bnZoomIn = new QPushButton(QIcon(":/zoom-in.svg"), "");
    QPushButton* bnZoomOut = new QPushButton(QIcon(":/zoom-out.svg"), "");
    QPushButton* bnZoomFit = new QPushButton(QIcon(":/expand.png"), "");
    m_bnZoomInRegion = new CheckableButton(QIcon(":/zoom-in-area.png"));
    m_drawRegion = new CheckableButton(QIcon(":/selection.png"));
    m_drawRegion->setChecked(true);

    connect(bnZoomIn, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::zoomIn);
    connect(bnZoomOut, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::zoomOut);
    connect(bnZoomFit, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::zoomFit);

    // exclusive toggle
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);

    group->addButton(m_bnZoomInRegion);
    group->addButton(m_drawRegion);
    //

    connect(m_bnZoomInRegion, &QPushButton::toggled, this, [this](bool checked){
      if (checked) {
        m_deviceWidget->activateZoomInRegionMode();
      }
    });
    connect(m_drawRegion, &QPushButton::toggled, this, [this](bool checked){
      if (checked) {
        m_deviceWidget->deactivateZoomInRegionMode();
      }
    });

    bnZoomIn->setToolTip(tr("Zoom in"));
    bnZoomOut->setToolTip(tr("Zoom out"));
    bnZoomFit->setToolTip(tr("Fit view to device grid"));
    m_bnZoomInRegion->setToolTip(tr("Zoom to selected area"));
    m_drawRegion->setToolTip(tr("Draw new region"));

    connect(m_deviceWidget, &DeviceGridWidget::selectionChanged, this, [this](){
      // [aurora2#1725] Region selection only, not hasSelection(): the button is labelled
      // "Remove selected region", and a partition is deleted from its own row in the
      // partitions list. Enabling it for a selected partition offered a second, unlabelled
      // way to delete one.
      m_bnRemoveSelectedRegion->setEnabled(m_deviceWidget->hasSelectedRegion());
      m_bnPartitionColor->setEnabled(m_deviceWidget->hasSelectedPartition());
    });


    // color
    m_bnPartitionColor = new QPushButton(QIcon(":/icons8-color-palette-48.png"), "");
    connect(m_bnPartitionColor, &QPushButton::clicked, this, [this](){
      QColor color = QColorDialog::getColor(Qt::white, this, "Select color");
      m_deviceWidget->changeSelectedPartitionColor(color);
    });
    m_bnPartitionColor->setToolTip(tr("Change selected partition color"));
    m_bnPartitionColor->setEnabled(false);

    // [aurora2#1725] Options. A standard style icon rather than another PNG resource, and a
    // menu of checkable actions rather than a dialog, so flipping one costs a single click.
    // Both default off, and both are persisted in floorplanning.cfg by the handlers below.
    QPushButton* bnOptions = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogDetailedView), "");
    bnOptions->setToolTip(tr("Options"));
    QMenu* optionsMenu = new QMenu(bnOptions);

    m_actTreatWarningsAsErrors = optionsMenu->addAction(tr("Treat warnings as errors"));
    m_actTreatWarningsAsErrors->setCheckable(true);
    m_actTreatWarningsAsErrors->setToolTip(
        tr("Count every advisory as an error, which stops the QDC being\n"
           "saved while any remains."));
    connect(m_actTreatWarningsAsErrors, &QAction::toggled, this, [this](bool on) {
        // Re-checks and so refreshes the issues list and the Save QDC button.
        m_deviceWidget->setTreatWarningsAsErrors(on);
        saveOptions();
    });

    m_actShowAtomColumns = optionsMenu->addAction(tr("Show Atom List and Type columns"));
    m_actShowAtomColumns->setCheckable(true);
    m_actShowAtomColumns->setToolTip(
        tr("Show each instance's netlist atom names and their clb/dsp/bram type."));
    connect(m_actShowAtomColumns, &QAction::toggled, this, [this](bool on) {
        m_synthResourcesWidget->setAtomColumnsVisible(on);
        m_partitionResourcesWidget->setAtomColumnsVisible(on);
        saveOptions();
    });

    optionsMenu->setToolTipsVisible(true);
    bnOptions->setMenu(optionsMenu);

    const int spacing = 20;
    toolBarLayout->addWidget(bnLoadQdc);
    toolBarLayout->addWidget(m_bnSaveQdc);
    toolBarLayout->addStretch();
    toolBarLayout->addWidget(bnLeft);
    toolBarLayout->addWidget(bnRight);
    toolBarLayout->addWidget(bnDown);
    toolBarLayout->addWidget(bnUp);
    toolBarLayout->addSpacing(spacing);
    toolBarLayout->addWidget(bnZoomIn);
    toolBarLayout->addWidget(bnZoomOut);
    toolBarLayout->addWidget(bnZoomFit);
    toolBarLayout->addSpacing(spacing);
    toolBarLayout->addWidget(m_bnZoomInRegion);
    toolBarLayout->addWidget(m_drawRegion);
    toolBarLayout->addSpacing(spacing);
    toolBarLayout->addWidget(m_bnPartitionColor);
    toolBarLayout->addSpacing(spacing);
    if (bnScrollPartition->isVisible()) {
      toolBarLayout->addWidget(bnScrollPartition);
    }
    toolBarLayout->addStretch();
    toolBarLayout->addSpacing(spacing);
    toolBarLayout->addSpacing(spacing);
    toolBarLayout->addWidget(m_bnRemoveSelectedRegion);
    toolBarLayout->addSpacing(spacing);
    toolBarLayout->addWidget(bnOptions);

    bnScrollPartition->setChecked(true);
    m_deviceWidget->setScrollToPartitionWhenSelected(bnScrollPartition->isChecked());

    // body layout
    QHBoxLayout* bodyLayout = new QHBoxLayout;
    bodyLayout->setContentsMargins(m,m,m,m);
    bodyLayout->setSpacing(m);

    QWidget* deviceWidgetContainer = new QWidget;
    QVBoxLayout* deviceWidgetContainerLayout = new QVBoxLayout;
    deviceWidgetContainerLayout->setContentsMargins(m,m,m,m);
    deviceWidgetContainerLayout->setSpacing(m);
    deviceWidgetContainer->setLayout(deviceWidgetContainerLayout);
    deviceWidgetContainerLayout->addWidget(toolBarContainer);
    deviceWidgetContainerLayout->addWidget(m_deviceWidget);

    QSplitter* splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false); // don't let panes disappear
    splitter->setHandleWidth(6);

    splitter->addWidget(m_synthResourcesWidget);
    splitter->addWidget(deviceWidgetContainer);
    splitter->addWidget(wRightPane);

    splitter->setSizes({1, 2, 1});    // initial proportions (relative)

    bodyLayout->addWidget(splitter);

    // main layout
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(m,m,m,m);
    mainLayout->setSpacing(m);

    mainLayout->addLayout(bodyLayout);

    m_busyOverlayWidget->setVisible(true);
}

FloorPlanningWidget::~FloorPlanningWidget()
{
    //qDebug() << "~FloorPlanningWidget";
}

void FloorPlanningWidget::setQdcFilePath(const std::filesystem::path& path, bool load)
{
    m_deviceWidget->setQdcFilePath(path);

    // [aurora2#1725] floorplanning.cfg sits beside the .qdc, where every other floorplanning
    // artifact lives, and so is per project -- "treat warnings as errors" in particular is a
    // decision about one floorplan, not a global taste. This is also the first point at which
    // the directory is known, which is why the options load here rather than in the ctor.
    m_optionsFilePath = path.parent_path() / "floorplanning.cfg";
    loadOptions();

    if (load) {
        loadQdc();   // clears the unsaved-changes flag the load itself would set
    } else {
        m_hasUnsavedChanges = false;
        updateSaveQdcButtonEnability();
    }
}

void FloorPlanningWidget::loadOptions()
{
    QSettings cfg(QString::fromStdString(m_optionsFilePath.string()), QSettings::IniFormat);
    const bool treatWarningsAsErrors = cfg.value(kCfgTreatWarningsAsErrors, false).toBool();
    const bool showAtomColumns = cfg.value(kCfgShowAtomColumns, false).toBool();

    // Set the actions without firing toggled(): those handlers write the file back, and a
    // freshly read value has nothing to save. The effects are applied explicitly instead,
    // which also covers the case where the stored value equals the default and setChecked()
    // would emit nothing at all.
    {
        QSignalBlocker blockWarnings(m_actTreatWarningsAsErrors);
        QSignalBlocker blockColumns(m_actShowAtomColumns);
        m_actTreatWarningsAsErrors->setChecked(treatWarningsAsErrors);
        m_actShowAtomColumns->setChecked(showAtomColumns);
    }

    m_deviceWidget->setTreatWarningsAsErrors(treatWarningsAsErrors);
    m_synthResourcesWidget->setAtomColumnsVisible(showAtomColumns);
    m_partitionResourcesWidget->setAtomColumnsVisible(showAtomColumns);
}

void FloorPlanningWidget::saveOptions() const
{
    if (m_optionsFilePath.empty()) {
        return;   // no project directory known yet, so nowhere to write
    }

    QSettings cfg(QString::fromStdString(m_optionsFilePath.string()), QSettings::IniFormat);
    cfg.setValue(kCfgTreatWarningsAsErrors, m_actTreatWarningsAsErrors->isChecked());
    cfg.setValue(kCfgShowAtomColumns, m_actShowAtomColumns->isChecked());
    cfg.sync();
}

void FloorPlanningWidget::onPartitionsChanged(const std::map<int, PartitionPtr>& partitions)
{
    m_synthResourcesWidget->onPartitionsChanged(partitions);
    m_partitionResourcesWidget->onPartitionsChanged(partitions);
    m_partitionsListWidget->onPartitionsChanged(partitions);

    // The list stays enabled even when empty -- its "+" row is how the first partition is
    // created, and disabling the widget would disable that too.


    // Any partition change is an edit worth saving -- except the ones loadQdc() and
    // setQdcFilePath() cause, which clear the flag again once the load has finished.
    m_hasUnsavedChanges = true;
    updateSaveQdcButtonEnability();
}

void FloorPlanningWidget::onCheckIssuesFinished(DeviceGrid::IssuesPtr issues)
{
    if (issues->isEmpty()) {
        m_issuesListWidget->clear();
    } else {
        m_issuesListWidget->setIssues(issues->errors, issues->warnings);
    }
    updateSaveQdcButtonEnability();
}

void FloorPlanningWidget::updateSaveQdcButtonEnability()
{
    // [aurora2#1725] Enabled only when there is something to save. Loading a project, or
    // loading a .qdc, reaches here through onPartitionsChanged() like any edit does, so
    // without the flag the button came up enabled on a floorplan identical to the file on
    // disk -- offering to save what was just read.
    m_bnSaveQdc->setEnabled(m_hasUnsavedChanges && m_deviceWidget->isSaveQdcAllowed());
}

void FloorPlanningWidget::loadQdc()
{
    m_deviceWidget->loadQdc();
    // loadQdc() emits partitionsChanged as it repopulates, which marks the floorplan dirty;
    // it is not, it is exactly what the file says. Clear the flag afterwards, not before.
    m_hasUnsavedChanges = false;
    updateSaveQdcButtonEnability();
}

void FloorPlanningWidget::loadNetList(const NaturalStringSet& elements)
{
    m_synthResourcesWidget->build(elements);
    m_partitionResourcesWidget->build(elements);
    m_busyOverlayWidget->hide();

    // [aurora2#1725] Report the atom-mapping gaps once, into the compiler log. Both trees
    // compute the same tally, so this reads it from one of them -- logging inside the widget
    // said everything twice. It used to go to stderr, one line per instance, which on fft256
    // meant 161 lines nobody could act on: stage P4's verdicts now show those instances greyed
    // out in the tree, reading "(deleted)" with synthesis's reason in the tooltip.
    //
    // The count is still worth recording, and individually the leaves NO verdict accounts
    // for: those mean atomsets.json and validation.json disagree about the same netlist,
    // which is a real problem rather than ordinary constant folding. Capped, with the number
    // dropped stated, so a systematic mismatch cannot masquerade as a short list.
    const auto& report = m_synthResourcesWidget->atomMappingReport();
    if (report.total() > 0) {
        emit logMessage(tr("Floor Planning: %1 RTL instance(s) have no atoms in the netlist: "
                           "%2 graded 'deleted' by synthesis validation (shown greyed out), "
                           "%3 unaccounted for")
                            .arg(report.total())
                            .arg(report.explainedByVerdict)
                            .arg(report.unexplained.size()));

        constexpr std::size_t kMaxListed = 10;
        for (std::size_t i = 0; (i < report.unexplained.size()) && (i < kMaxListed); ++i) {
            emit logMessage(tr("Floor Planning: no atoms and no verdict for '%1'")
                                .arg(QString::fromStdString(report.unexplained[i])));
        }
        if (report.unexplained.size() > kMaxListed) {
            emit logMessage(tr("Floor Planning: ... and %1 more instance(s) with no atoms and "
                               "no verdict, not listed")
                                .arg(report.unexplained.size() - kMaxListed));
        }
    }
}

void FloorPlanningWidget::setAtomNames(std::map<std::string, std::vector<std::string>, NaturalLess> atomNames)
{
    // [aurora2#1725] Atoms belonging to an instance DIRECTLY, i.e. not to one of its
    // sub-instances. DeviceGrid warns when a partition takes an instance's sub-instances
    // and leaves this logic unconstrained. Shared with the batch checker (REQ-004): the
    // rule for what counts as "own" is one decision, so it lives in one place.
    m_deviceWidget->setOwnAtomCounts(fp::ownAtomCounts(atomNames));

    m_synthResourcesWidget->setAtomNames(atomNames);
    m_partitionResourcesWidget->setAtomNames(std::move(atomNames));
}

void FloorPlanningWidget::setInstanceVerdicts(std::map<std::string, InstanceVerdict> verdicts)
{
    // The grid only needs to know which instances are dead, to flag a .qdc that still
    // constrains one -- see DeviceGrid::checkIssues().
    std::unordered_set<std::string> deleted;
    for (const auto& [path, graded]: verdicts) {
        if (graded.verdict == "deleted") {
            deleted.insert(path);
        }
    }
    m_deviceWidget->setDeletedInstances(std::move(deleted));

    m_synthResourcesWidget->setInstanceVerdicts(verdicts);
    m_partitionResourcesWidget->setInstanceVerdicts(std::move(verdicts));
}

void FloorPlanningWidget::setPlacementVerdicts(std::map<std::string, InstancePlacement> placements)
{
    m_synthResourcesWidget->setPlacementVerdicts(placements);
    m_partitionResourcesWidget->setPlacementVerdicts(std::move(placements));
}

void FloorPlanningWidget::setDesignResources(DesignResources resources)
{
    // An invalid set means "this project has no design_resources.json". It must still be
    // stored: Partition holds these statically, so an early return here would leave the
    // PREVIOUS project's figures in place and size this project's partitions from them --
    // silently, and marked as though they described this design.
    if (!resources.valid()) {
        Partition::setDesignResources(DesignResources{});
        return;
    }

    // A.13.5: both tiers share a shape, so this line is the only thing telling a user
    // whether the clb/dsp/bram they are sizing against were counted from the netlist or
    // measured from the placement. Logged to the compiler log for the same reason the
    // no-atom report is -- a GUI user never sees the terminal.
    //
    // tierName carries the whole predicate -- "counted from the post-synthesis netlist" --
    // so the level number is left out. It ranks the two sources for code that has to pick
    // the better one; to a user reading a log it is a number with no scale attached.
    emit logMessage(tr("Floor Planning: resource figures are %1, for %2 instance(s).")
                        .arg(QString::fromStdString(resources.tierName))
                        .arg(static_cast<int>(resources.instances.size())));

    Partition::setDesignResources(std::move(resources));
}

void FloorPlanningWidget::setDeviceGridDescriptor(const DeviceGridDescriptorPtr& descriptor)
{
    m_deviceWidget->constructTiles(descriptor);
}

void FloorPlanningWidget::onNotify(QString title, QString msg)
{
    QMessageBox::warning(this, title, msg);
}

void FloorPlanningWidget::saveQdc()
{
    // [aurora2#1725] The button and Ctrl+S go through here, so the two cannot drift apart.
    // Nothing changed means nothing to write: rewriting an identical .qdc would still fire
    // qdcFileSaved(), and that invalidates the compiled stages downstream.
    if (!m_hasUnsavedChanges) {
        return;
    }

    // The button is disabled while the floorplan has errors, and the shortcut has no such
    // visual state, so it says why rather than doing nothing: a keystroke that silently
    // achieves nothing reads as a broken shortcut.
    if (!m_deviceWidget->isSaveQdcAllowed()) {
        onNotify(tr("Cannot save QDC"),
                 tr("The floorplan has errors. Resolve the errors listed in Issues, or turn off "
                    "\"Treat warnings as errors\" in Options if that is what promoted them."));
        return;
    }

    if (m_deviceWidget->saveQdc()) {
        m_hasUnsavedChanges = false;
        updateSaveQdcButtonEnability();
        emit qdcFileSaved();
    } else {
        onNotify(tr("Fail to save QDC"), "");
    }
}

void FloorPlanningWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_busyOverlayWidget->resize(event->size());
}

void FloorPlanningWidget::closeEvent(QCloseEvent* event)
{
  emit closed();
  QWidget::closeEvent(event);
}

void FloorPlanningWidget::keyPressEvent(QKeyEvent* event)
{
  switch (event->key()) {
  case Qt::Key_Delete: {
    m_deviceWidget->removeSelected();
    event->accept();
    break;
  }
  case Qt::Key_Escape: {
    m_deviceWidget->unselect();
    event->accept();
    break;
  } default: {
    QWidget::keyPressEvent(event);
  }
  }
}

} // namespace fp
