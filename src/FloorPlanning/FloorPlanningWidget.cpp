#include "FloorPlanningWidget.h"

#include "SynthResourceHierarchyWidget.h"
#include "DeviceGridWidget.h"
#include "PartitionsListWidget.h"
#include "NewUniqueNameDialog.h"
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
    m_partitionsListWidget->setEnabled(false);

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
    connect(bnLoadQdc, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::loadQdc);
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

    QPushButton* bnCreateNewPartition = new QPushButton(QIcon(":/add.png"), "");
    connect(bnCreateNewPartition, &QPushButton::clicked, this, &FloorPlanningWidget::createNewPartition);
    bnCreateNewPartition->setToolTip(tr("Create new partition"));

    m_bnRemoveSelectedPartition = new QPushButton(QIcon(":/erase.png"), "");
    connect(m_bnRemoveSelectedPartition, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::removeSelected);
    m_bnRemoveSelectedPartition->setToolTip(tr("Remove selected partition or region"));
    m_bnRemoveSelectedPartition->setEnabled(false);

    connect(m_deviceWidget, &DeviceGridWidget::createFirstPartitionRequested, this, &FloorPlanningWidget::createNewPartition);

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

    m_bnRemoveAllPartitions = new QPushButton(QIcon(":/delete-10402_32.png"), "");
    //

    //
    connect(m_bnRemoveAllPartitions, &QPushButton::clicked, m_deviceWidget, &DeviceGridWidget::clearPartitions);
    connect(m_deviceWidget, &DeviceGridWidget::selectionChanged, this, [this](){
      m_bnRemoveSelectedPartition->setEnabled(m_deviceWidget->hasSelection());
      m_bnPartitionColor->setEnabled(m_deviceWidget->hasSelectedPartition());
    });

    m_bnRemoveAllPartitions->setToolTip(tr("Delete all partitions"));
    m_bnRemoveAllPartitions->setEnabled(false);

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
        tr("Count every advisory as an error, which stops the QDC being saved while any remains."));
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
    toolBarLayout->addWidget(m_bnRemoveAllPartitions);
    toolBarLayout->addSpacing(spacing);
    toolBarLayout->addWidget(bnCreateNewPartition);
    toolBarLayout->addWidget(m_bnRemoveSelectedPartition);
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
        m_deviceWidget->loadQdc();
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

    const bool partitionsNotEmpty = !partitions.empty();
    m_partitionsListWidget->setEnabled(partitionsNotEmpty);

    m_bnRemoveAllPartitions->setEnabled(partitionsNotEmpty);
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
    m_bnSaveQdc->setEnabled(m_deviceWidget->isSaveQdcAllowed());
}

void FloorPlanningWidget::loadNetList(const NaturalStringSet& elements)
{
    m_synthResourcesWidget->build(elements);
    m_partitionResourcesWidget->build(elements);
    m_busyOverlayWidget->hide();
}

void FloorPlanningWidget::setAtomNames(std::map<std::string, std::vector<std::string>, NaturalLess> atomNames)
{
    // [aurora2#1725] Atoms belonging to an instance DIRECTLY, i.e. not to one of its
    // sub-instances. An entry's atom list is subtree-inclusive, so an atom is the
    // instance's own exactly when what follows "<path>." holds no further dot -- the
    // "dut.i.n0_$lut_Y" case as opposed to "dut.i.sub.n0_$lut_Y". DeviceGrid warns when a
    // partition takes an instance's sub-instances and leaves this logic unconstrained.
    std::map<std::string, int> ownAtomCounts;
    for (const auto& [path, atoms]: atomNames) {
        const std::size_t tail = path.size() + 1;
        int own = 0;
        for (const std::string& atom: atoms) {
            if ((atom.size() > tail) && (atom.find('.', tail) == std::string::npos)) {
                ++own;
            }
        }
        ownAtomCounts[path] = own;
    }
    m_deviceWidget->setOwnAtomCounts(std::move(ownAtomCounts));

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

void FloorPlanningWidget::setDeviceGridDescriptor(const DeviceGridDescriptorPtr& descriptor)
{
    m_deviceWidget->constructTiles(descriptor);
}

void FloorPlanningWidget::createNewPartition()
{
    if (!m_newPartitionNameDialog) {
        m_newPartitionNameDialog = new NewUniqueNameDialog("Enter unique partition name", this);
    }
    m_newPartitionNameDialog->setExistedNames(m_deviceWidget->existedPartitionNames());
    if (m_newPartitionNameDialog->exec()) {
        std::string name = m_newPartitionNameDialog->name();
        if (!name.empty()) {
            m_deviceWidget->createNewPartition(name);
        }
    }
}

void FloorPlanningWidget::onNotify(QString title, QString msg)
{
    QMessageBox::warning(this, title, msg);
}

void FloorPlanningWidget::saveQdc()
{
    // [aurora2#1725] The button and Ctrl+S go through here, so the two cannot drift apart.
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
        m_bnSaveQdc->setEnabled(false);
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
