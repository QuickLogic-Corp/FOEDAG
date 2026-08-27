#include "FloorPlanningWidget.h"

#include "AtomSets.h"

#include "SynthResourceHierarchyWidget.h"
#include "DeviceGridWidget.h"
#include "PartitionsListWidget.h"
#include "IssuesListWidget.h"
#include "Widgets/RoundProgressWidget.h"

#include <QAction>
#include <QActionGroup>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QCheckBox>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QKeyEvent>

namespace fp {

namespace {

// [aurora2#1725] floorplanning.cfg keys. Grouped, so the file reads as a FloorPlanning
// section rather than QSettings' anonymous "[General]".
constexpr auto kCfgTreatWarningsAsErrors = "FloorPlanning/treat_warnings_as_errors";
constexpr auto kCfgShowAtomColumns = "FloorPlanning/show_atom_list_and_type_columns";

// [aurora2#1725] QToolBar's default icon size is the style's large one, noticeably bigger
// than the QPushButtons this toolbar used to hold. Match what was there.
constexpr int kToolBarIconPx = 16;

}  // namespace

FloorPlanningWidget::FloorPlanningWidget(const QString& projectName, QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(projectName + tr(" - Floor Planning"));
    const int indicatorSize = 32;
    m_busyOverlayWidget = new FOEDAG::RoundProgressWidget(indicatorSize, this);
    const int m = FP_UI_MARGIN;

    // [aurora2#1725] The left tree carries the "Selected RTL Resources" table; the partition
    // tree on the right does not -- what one partition costs is already its row in the
    // partitions table.
    m_synthResourcesWidget =
        new SynthResourceHierarchyWidget(SynthResourceHierarchyWidget::Flag::ShowSelectedResources);
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
    //
    // [aurora2#1725] A QToolBar of QActions, where this was a QWidget wrapping a QHBoxLayout
    // of QPushButtons. A layout's minimum width is the sum of everything in it, and that sum
    // -- measured at 628px -- became the device pane's floor inside the splitter: it is what
    // made the hierarchy pane undraggable on a 1366-wide screen and the whole panel too wide
    // for anything smaller. A toolbar moves what does not fit behind the standard extension
    // button instead, so the pane can be dragged as narrow as the user likes.
    //
    // QActions rather than the buttons added through QToolBar::addWidget(): a widget can only
    // live in one place at a time, so the extension menu cannot show one, and a button that
    // stopped fitting simply vanished -- at a 150px pane, 4 of 21 controls were visible and
    // the popup could offer none of the rest. Actions move into the popup properly. They also
    // carry a text, unused by the toolbar itself (icons only, as before) but shown as the
    // label in that popup.
    QToolBar* toolBar = new QToolBar;
    toolBar->setMovable(false);
    toolBar->setFloatable(false);
    toolBar->setContentsMargins(m,m,m,m);
    toolBar->setIconSize(QSize(kToolBarIconPx, kToolBarIconPx));

    // A toolbar has no addStretch(); an expanding blank widget is the equivalent. It is the
    // one thing here that is still a widget, which is harmless -- it has nothing to offer a
    // popup menu, and its minimum is zero so it never competes for space.
    auto addStretch = [toolBar]() {
        QWidget* stretch = new QWidget;
        stretch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        toolBar->addWidget(stretch);
    };

    QAction* actLoadQdc = new QAction(QIcon(":/load-action.png"), tr("Load QDC"), this);
    connect(actLoadQdc, &QAction::triggered, this, &FloorPlanningWidget::loadQdc);
    actLoadQdc->setToolTip(tr("Load QDC"));

    m_actSaveQdc = new QAction(QIcon(":/save-action.png"), tr("Save QDC"), this);
    connect(m_actSaveQdc, &QAction::triggered, this, [this]() { saveQdc(); });
    m_actSaveQdc->setToolTip(tr("Save QDC (Ctrl+S)"));
    m_actSaveQdc->setEnabled(false);

    // [aurora2#1725] Ctrl+S saves, same as the button. Scoped to this window (a QShortcut on
    // a widget defaults to Qt::WindowShortcut), so it cannot reach the main window's own
    // Ctrl+S while FloorPlanning has focus, nor fire when it does not.
    auto* saveShortcut = new QShortcut(QKeySequence::Save, this);
    connect(saveShortcut, &QShortcut::activated, this, [this]() { saveQdc(); });

    m_actRemoveSelectedRegion = new QAction(QIcon(":/erase.png"), tr("Remove selected region"), this);
    connect(m_actRemoveSelectedRegion, &QAction::triggered, m_deviceWidget, &DeviceGridWidget::removeSelected);
    // [aurora2#1725] Partitions are deleted from their own row in the partitions list now;
    // a selected region has no row of its own, so it is still removed from here.
    m_actRemoveSelectedRegion->setToolTip(tr("Remove selected region"));
    m_actRemoveSelectedRegion->setEnabled(false);

    // [aurora2#1725] Drawing a region with no partition yet: create one unnamed rather than
    // interrupting the gesture with a name prompt. It appears in the list, where its name can
    // be edited in place like any other.
    connect(m_deviceWidget, &DeviceGridWidget::createFirstPartitionRequested,
            this, [this] { m_deviceWidget->createNewPartition(); });

    // [aurora2#1725] Never shown -- setVisible(false) below, and it is not put on the toolbar.
    // It survives only because its checked state is what turns scroll-to-partition on.
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
    auto* actLeft = new QAction(QIcon(":/left-arrow.png"), tr("Pan left"), this);
    auto* actRight = new QAction(QIcon(":/right-arrow.png"), tr("Pan right"), this);
    auto* actDown = new QAction(QIcon(":/down-arrow.png"), tr("Pan down"), this);
    auto* actUp = new QAction(QIcon(":/up-arrow.png"), tr("Pan up"), this);

    actLeft->setToolTip(tr("Pan view left"));
    actRight->setToolTip(tr("Pan view right"));
    actDown->setToolTip(tr("Pan view down"));
    actUp->setToolTip(tr("Pan view up"));

    connect(actLeft, &QAction::triggered, m_deviceWidget, &DeviceGridWidget::moveViewLeft);
    connect(actUp, &QAction::triggered, m_deviceWidget, &DeviceGridWidget::moveViewUp);
    connect(actRight, &QAction::triggered, m_deviceWidget, &DeviceGridWidget::moveViewRight);
    connect(actDown, &QAction::triggered, m_deviceWidget, &DeviceGridWidget::moveViewDown);

    // zoom controls
    auto* actZoomIn = new QAction(QIcon(":/zoom-in.svg"), tr("Zoom in"), this);
    auto* actZoomOut = new QAction(QIcon(":/zoom-out.svg"), tr("Zoom out"), this);
    auto* actZoomFit = new QAction(QIcon(":/expand.png"), tr("Zoom to fit"), this);
    m_actZoomInRegion = new QAction(QIcon(":/zoom-in-area.png"), tr("Zoom to area"), this);
    m_actDrawRegion = new QAction(QIcon(":/selection.png"), tr("Draw region"), this);
    m_actZoomInRegion->setCheckable(true);
    m_actDrawRegion->setCheckable(true);
    m_actDrawRegion->setChecked(true);

    connect(actZoomIn, &QAction::triggered, m_deviceWidget, &DeviceGridWidget::zoomIn);
    connect(actZoomOut, &QAction::triggered, m_deviceWidget, &DeviceGridWidget::zoomOut);
    connect(actZoomFit, &QAction::triggered, m_deviceWidget, &DeviceGridWidget::zoomFit);

    // exclusive toggle -- a QActionGroup where this was a QButtonGroup, for the same reason
    // the buttons became actions.
    auto* group = new QActionGroup(this);
    group->setExclusive(true);
    group->addAction(m_actZoomInRegion);
    group->addAction(m_actDrawRegion);

    connect(m_actZoomInRegion, &QAction::toggled, this, [this](bool checked){
      if (checked) {
        m_deviceWidget->activateZoomInRegionMode();
      }
    });
    connect(m_actDrawRegion, &QAction::toggled, this, [this](bool checked){
      if (checked) {
        m_deviceWidget->deactivateZoomInRegionMode();
      }
    });

    actZoomIn->setToolTip(tr("Zoom in"));
    actZoomOut->setToolTip(tr("Zoom out"));
    actZoomFit->setToolTip(tr("Fit view to device grid"));
    m_actZoomInRegion->setToolTip(tr("Zoom to selected area"));
    m_actDrawRegion->setToolTip(tr("Draw new region"));

    connect(m_deviceWidget, &DeviceGridWidget::selectionChanged, this, [this](){
      // [aurora2#1725] Region selection only, not hasSelection(): the button is labelled
      // "Remove selected region", and a partition is deleted from its own row in the
      // partitions list. Enabling it for a selected partition offered a second, unlabelled
      // way to delete one.
      m_actRemoveSelectedRegion->setEnabled(m_deviceWidget->hasSelectedRegion());
      m_actPartitionColor->setEnabled(m_deviceWidget->hasSelectedPartition());
    });


    // color
    m_actPartitionColor =
        new QAction(QIcon(":/icons8-color-palette-48.png"), tr("Partition colour"), this);
    connect(m_actPartitionColor, &QAction::triggered, this, [this](){
      QColor color = QColorDialog::getColor(Qt::white, this, "Select color");
      m_deviceWidget->changeSelectedPartitionColor(color);
    });
    m_actPartitionColor->setToolTip(tr("Change selected partition color"));
    m_actPartitionColor->setEnabled(false);

    // [aurora2#1725] Options. A standard style icon rather than another PNG resource, and a
    // menu of checkable actions rather than a dialog, so flipping one costs a single click.
    // Both default off, and both are persisted in floorplanning.cfg by the handlers below.
    QAction* actOptions =
        new QAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView), tr("Options"), this);
    actOptions->setToolTip(tr("Options"));
    QMenu* optionsMenu = new QMenu(this);

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
    actOptions->setMenu(optionsMenu);

    // Separators where the old layout inserted fixed 20px gaps: they group the same way,
    // and unlike a gap they carry over into the extension menu when the group overflows.
    toolBar->addAction(actLoadQdc);
    toolBar->addAction(m_actSaveQdc);
    addStretch();
    toolBar->addAction(actLeft);
    toolBar->addAction(actRight);
    toolBar->addAction(actDown);
    toolBar->addAction(actUp);
    toolBar->addSeparator();
    toolBar->addAction(actZoomIn);
    toolBar->addAction(actZoomOut);
    toolBar->addAction(actZoomFit);
    toolBar->addSeparator();
    toolBar->addAction(m_actZoomInRegion);
    toolBar->addAction(m_actDrawRegion);
    toolBar->addSeparator();
    toolBar->addAction(m_actPartitionColor);
    addStretch();
    toolBar->addSeparator();
    toolBar->addAction(m_actRemoveSelectedRegion);
    toolBar->addSeparator();
    toolBar->addAction(actOptions);

    // The menu opens on the first click rather than on a press-and-hold, which is what the
    // QPushButton this replaced did.
    if (auto* optionsButton = qobject_cast<QToolButton*>(toolBar->widgetForAction(actOptions))) {
        optionsButton->setPopupMode(QToolButton::InstantPopup);
    }

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
    deviceWidgetContainerLayout->addWidget(toolBar);
    deviceWidgetContainerLayout->addWidget(m_deviceWidget);

    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->setChildrenCollapsible(false); // don't let panes disappear
    m_splitter->setHandleWidth(6);

    m_splitter->addWidget(m_synthResourcesWidget);
    m_splitter->addWidget(deviceWidgetContainer);
    m_splitter->addWidget(wRightPane);

    // [aurora2#1725] Growing the window keeps the three panes in a 1:2:1 relationship. The
    // opening widths are set separately, in applyInitialSplitterSizes(), because they need a
    // window width to divide and there is none yet here.
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setStretchFactor(2, 1);

    bodyLayout->addWidget(m_splitter);

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
    m_actSaveQdc->setEnabled(m_hasUnsavedChanges && m_deviceWidget->isSaveQdcAllowed());
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

// [aurora2#1725] Opening widths for the three panes: a quarter, a half, a quarter, with the
// partitions pane widened to fit its columns if a quarter is not enough (that pane is the one
// whose rightmost columns used to fall outside it).
//
// This used to be splitter->setSizes({1,2,1}) at construction, read as a ratio. It never was
// one: QSplitter treats those as pixel widths, clamps each to the pane's minimum, and shares
// what is left by weight -- so the layout that appeared was really the old minimums (628 for
// the device pane, 306 for the partitions pane) plus the remainder. Removing those minimums
// to make the panes draggable therefore also removed what was, by accident, holding the
// proportions up, and the device pane opened at a fifth of the window. Hence a real division
// of a real width, done once, on the first resize that carries one.
void FloorPlanningWidget::applyInitialSplitterSizes()
{
    const int total = m_splitter->width();
    if (total <= 0) {
        return;  // not laid out yet; the next resize will carry a width
    }
    m_splitterSized = true;

    const int quarter = total / 4;
    int right = qMax(quarter, m_splitter->widget(2)->sizeHint().width());
    right = qMin(right, total / 2);  // never at the expense of the grid being the smallest pane
    const int left = quarter;
    m_splitter->setSizes({left, total - left - right, right});
}

void FloorPlanningWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_busyOverlayWidget->resize(event->size());
    if (!m_splitterSized) {
        applyInitialSplitterSizes();
    }
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
