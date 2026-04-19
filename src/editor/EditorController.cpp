// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "EditorController.h"
#include "services/ILayoutService.h"
#include "services/DBusLayoutService.h"
#include "services/ZoneManager.h"
#include "services/SnappingService.h"
#include "services/TemplateService.h"
#include "undo/UndoController.h"
#include "undo/commands/UpdateLayoutNameCommand.h"
#include "undo/commands/ChangeSelectionCommand.h"
#include "helpers/ZoneSerialization.h"
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorTiles/ScriptedAlgorithmLoader.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include "../common/layoutpreviewserialize.h"
#include "../core/constants.h"
#include "../core/geometryutils.h"
#include "../core/layoutworker/layoutcomputeservice.h"
#include "../core/logging.h"
#include "../core/utils.h"

#include <PhosphorLayoutApi/LayoutPreview.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/ZonesLayoutSource.h>

#include <QClipboard>
#include <QDBusConnection>
#include <QGuiApplication>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

namespace {
// Install the library-level screen-id resolver once per process so
// Layout::fromJson() can normalise legacy connector names ("DP-2") to
// EDID-based IDs ("LG:Model:Serial") during load. Uses QGuiApplication's
// screen list via Phosphor::Screens::ScreenIdentity::idForName.
void ensureScreenIdResolver()
{
    static const bool installed = [] {
        PhosphorZones::Layout::setScreenIdResolver([](const QString& name) -> QString {
            if (name.isEmpty() || !Phosphor::Screens::ScreenIdentity::isConnectorName(name))
                return name;
            return Phosphor::Screens::ScreenIdentity::idForName(name);
        });
        return true;
    }();
    (void)installed;
}
} // namespace

EditorController::EditorController(QObject* parent)
    : QObject((ensureScreenIdResolver(), parent))
    , m_layoutService(new DBusLayoutService(this))
    , m_zoneManager(new ZoneManager(this))
    , m_snappingService(new SnappingService(this))
    , m_templateService(new TemplateService(this))
    , m_undoController(new UndoController(this))
    , m_localAlgorithmRegistry(std::make_unique<PhosphorTiles::AlgorithmRegistry>(nullptr))
    , m_localLayoutManager(std::make_unique<LayoutManager>(nullptr))
{
    // Auto-discovery pattern: every linked provider library has
    // already registered a builder via static-init. The editor just
    // publishes the registries it owns into the FactoryContext and
    // calls buildFromRegistered. Adding a new engine library doesn't
    // require editing this file unless the engine demands a service
    // the editor doesn't already publish.
    PhosphorLayout::FactoryContext factoryCtx;
    factoryCtx.set<PhosphorZones::IZoneLayoutRegistry>(m_localLayoutManager.get());
    factoryCtx.set<PhosphorTiles::ITileAlgorithmRegistry>(m_localAlgorithmRegistry.get());
    m_localSources.buildFromRegistered(factoryCtx);

    // Discover + register user-authored scripted algorithms in the editor-
    // owned AlgorithmRegistry so standalone editor launches (daemon down)
    // still surface them in layout pickers. The loader also sets up a
    // QFileSystemWatcher so hot-edits roll through automatically.
    auto* scriptLoader = new PhosphorTiles::ScriptedAlgorithmLoader(QString(ScriptedAlgorithmSubdir),
                                                                    m_localAlgorithmRegistry.get(), this);
    scriptLoader->scanAndRegister();
    connect(scriptLoader, &PhosphorTiles::ScriptedAlgorithmLoader::algorithmsChanged, this,
            &EditorController::reloadLocalLayouts);

    // Populate the daemon-independent layout source from disk on startup
    // so localLayoutPreviews() returns a populated list immediately. The
    // LayoutManager installs a QFileSystemWatcher so subsequent disk
    // changes (daemon writes, settings creates, hand edits) auto-reload.
    m_localLayoutManager->loadLayouts();
    // Recompute zone geometry for fixed-geometry layouts so ZonesLayoutSource
    // emits non-empty zones + a real referenceAspectRatio — see the matching
    // comment in SettingsController.
    recalcLocalLayouts();
    // Recompute geometry on every layouts-changed. ZonesLayoutSource
    // self-wires to the registry's unified ILayoutSourceRegistry::
    // contentsChanged — no manual bridge required. Editor has no
    // downstream consumer of ILayoutSource::contentsChanged, so slot
    // ordering against recalcLocalLayouts is not load-bearing here;
    // consumers always query availableLayouts() directly after an
    // interactive edit, by which point recalcLocalLayouts has run.
    connect(m_localLayoutManager.get(), &LayoutManager::layoutsChanged, this, &EditorController::recalcLocalLayouts);

    // Subscribe to the daemon's layout-change D-Bus signals and force
    // a local-source reload when any fire. Belt-and-suspenders alongside
    // the QFileSystemWatcher: Qt's QFSW has known misses on cross-process
    // atomic-rename writes (the daemon writes via QSaveFile, which
    // creates a new inode the watcher may not bind to in time). Tying
    // the local reload to the daemon's signal stream guarantees the
    // editor's preview surface stays in sync with the daemon's view
    // regardless of which file-event path fires first. When the daemon
    // isn't running, none of these signals fire — the QFSW path covers
    // single-process editor + manual hand-edits.
    //
    // Debounce the 5 signals through a 50 ms single-shot timer so a
    // typical editor save (layoutChanged + layoutListChanged back-to-back)
    // only triggers one reloadLocalLayouts() pass. Mirrors the
    // SettingsController::m_layoutLoadTimer pattern.
    m_layoutReloadTimer.setSingleShot(true);
    m_layoutReloadTimer.setInterval(50);
    connect(&m_layoutReloadTimer, &QTimer::timeout, this, &EditorController::reloadLocalLayouts);

    auto bus = QDBusConnection::sessionBus();
    const QString svc = QString::fromLatin1(DBus::ServiceName);
    const QString path = QString::fromLatin1(DBus::ObjectPath);
    const QString iface = QString::fromLatin1(DBus::Interface::LayoutManager);
    for (const auto& sig :
         {QStringLiteral("layoutCreated"), QStringLiteral("layoutDeleted"), QStringLiteral("layoutChanged"),
          QStringLiteral("layoutListChanged"), QStringLiteral("layoutPropertyChanged")}) {
        bus.connect(svc, path, iface, sig, &m_layoutReloadTimer, SLOT(start()));
    }

    // Connect service signals
    connect(m_layoutService, &ILayoutService::errorOccurred, this, [this](const QString& error) {
        Q_EMIT layoutLoadFailed(error);
        Q_EMIT layoutSaveFailed(error);
    });

    connect(m_zoneManager, &ZoneManager::zonesChanged, this, [this]() {
        // Check if selected zones still exist after zones changed
        // This handles cases where restoreZones() or clearAllZones() removes selected zones
        if (!m_selectedZoneIds.isEmpty() && m_zoneManager) {
            QStringList validZoneIds;
            for (const QString& zoneId : m_selectedZoneIds) {
                QVariantMap zone = m_zoneManager->getZoneById(zoneId);
                if (!zone.isEmpty()) {
                    validZoneIds.append(zoneId);
                }
            }
            if (validZoneIds != m_selectedZoneIds) {
                m_selectedZoneIds = validZoneIds;
                QString newSelectedId = validZoneIds.isEmpty() ? QString() : validZoneIds.first();
                if (m_selectedZoneId != newSelectedId) {
                    m_selectedZoneId = newSelectedId;
                    Q_EMIT selectedZoneIdChanged();
                }
                Q_EMIT selectedZoneIdsChanged();
            }
        }
        ++m_zonesVersion;
        Q_EMIT zonesChanged();
    });
    connect(m_zoneManager, &ZoneManager::zoneAdded, this, &EditorController::zoneAdded);
    connect(m_zoneManager, &ZoneManager::zoneRemoved, this, [this](const QString& zoneId) {
        // Remove zone from selection if it was selected
        if (m_selectedZoneIds.contains(zoneId)) {
            m_selectedZoneIds.removeAll(zoneId);
            syncSelectionSignals();
        }
        Q_EMIT zoneRemoved(zoneId);
    });
    connect(m_zoneManager, &ZoneManager::zoneGeometryChanged, this, &EditorController::zoneGeometryChanged);
    connect(m_zoneManager, &ZoneManager::zoneNameChanged, this, &EditorController::zoneNameChanged);
    connect(m_zoneManager, &ZoneManager::zoneNumberChanged, this, &EditorController::zoneNumberChanged);
    connect(m_zoneManager, &ZoneManager::zoneColorChanged, this, &EditorController::zoneColorChanged);
    connect(m_zoneManager, &ZoneManager::zonesModified, this, &EditorController::markUnsaved);

    // Initialize ZoneManager with default screen size (updated when target screen is set)
    m_zoneManager->setReferenceScreenSize(targetScreenSize());

    connect(m_snappingService, &SnappingService::gridSnappingEnabledChanged, this,
            &EditorController::gridSnappingEnabledChanged);
    connect(m_snappingService, &SnappingService::edgeSnappingEnabledChanged, this,
            &EditorController::edgeSnappingEnabledChanged);
    connect(m_snappingService, &SnappingService::snapIntervalXChanged, this, &EditorController::snapIntervalXChanged);
    connect(m_snappingService, &SnappingService::snapIntervalYChanged, this, &EditorController::snapIntervalYChanged);
    connect(m_snappingService, &SnappingService::snapIntervalChanged, this,
            &EditorController::snapIntervalChanged); // For backward compatibility

    // Connect to clipboard changes for reactive canPaste updates
    QClipboard* clipboard = QGuiApplication::clipboard();
    connect(clipboard, &QClipboard::dataChanged, this, &EditorController::onClipboardChanged);

    // Initialize canPaste state
    m_canPaste = canPaste();

    // Load editor settings from KConfig
    loadEditorSettings();
}

EditorController::~EditorController()
{
    // Save editor settings to KConfig
    saveEditorSettings();

    // Services are QObjects with this as parent, so they'll be deleted automatically.
}

// Preview mode
bool EditorController::previewMode() const
{
    return m_previewMode;
}

void EditorController::setPreviewMode(bool preview)
{
    if (m_previewMode == preview) {
        return;
    }
    m_previewMode = preview;
    Q_EMIT previewModeChanged();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Property getters
// ═══════════════════════════════════════════════════════════════════════════════

QString EditorController::layoutId() const
{
    return m_layoutId;
}
QString EditorController::layoutName() const
{
    return m_layoutName;
}
QVariantList EditorController::zones() const
{
    return m_zoneManager ? m_zoneManager->zones() : QVariantList();
}

// ── Daemon-independent layout previews (PhosphorZones::ILayoutSource) ───────
// Same shape as SettingsController's localLayoutPreviews — both processes
// route through the shared toVariantMap so QML preview-rendering
// code stays identical across the two consumers.

QVariantList EditorController::localLayoutPreviews() const
{
    QVariantList list;
    if (!m_localSources.composite()) {
        return list;
    }
    const auto previews = m_localSources.composite()->availableLayouts();
    list.reserve(previews.size());
    for (const auto& preview : previews) {
        list.append(toVariantMap(preview));
    }
    return list;
}

QVariantMap EditorController::localLayoutPreview(const QString& id, int windowCount)
{
    if (id.isEmpty() || !m_localSources.composite()) {
        return {};
    }
    const auto preview = m_localSources.composite()->previewAt(id, windowCount);
    if (preview.id.isEmpty()) {
        return {};
    }
    return toVariantMap(preview);
}

void EditorController::reloadLocalLayouts()
{
    // Slot wired to the daemon's layout-mutation signals — see ctor for
    // the connect block + rationale. Cheap on no-op (LayoutManager
    // diff-checks file mtimes / hashes internally).
    if (m_localLayoutManager) {
        m_localLayoutManager->loadLayouts();
    }
}

void EditorController::recalcLocalLayouts()
{
    if (!m_localLayoutManager) {
        return;
    }
    QScreen* primary = Utils::primaryScreen();
    if (!primary) {
        return;
    }
    for (PhosphorZones::Layout* layout : m_localLayoutManager->layouts()) {
        if (!layout) {
            continue;
        }
        // Editor is a separate process without a daemon ScreenManager — pass
        // nullptr and accept the Qt-availableGeometry fallback for previews.
        LayoutComputeService::recalculateSync(layout, GeometryUtils::effectiveScreenGeometry(nullptr, layout, primary));
    }
}
QString EditorController::selectedZoneId() const
{
    return m_selectedZoneId;
}
QStringList EditorController::selectedZoneIds() const
{
    return m_selectedZoneIds;
}
int EditorController::selectionCount() const
{
    return m_selectedZoneIds.count();
}
bool EditorController::hasMultipleSelection() const
{
    return m_selectedZoneIds.count() > 1;
}
bool EditorController::hasUnsavedChanges() const
{
    return m_hasUnsavedChanges;
}
bool EditorController::isNewLayout() const
{
    return m_isNewLayout;
}
bool EditorController::gridSnappingEnabled() const
{
    return m_snappingService->gridSnappingEnabled();
}
bool EditorController::edgeSnappingEnabled() const
{
    return m_snappingService->edgeSnappingEnabled();
}
qreal EditorController::snapIntervalX() const
{
    return m_snappingService->snapIntervalX();
}
qreal EditorController::snapIntervalY() const
{
    return m_snappingService->snapIntervalY();
}
qreal EditorController::snapInterval() const
{
    return snapIntervalX();
} // Backward compatibility
bool EditorController::gridOverlayVisible() const
{
    return m_gridOverlayVisible;
}
QString EditorController::editorDuplicateShortcut() const
{
    return m_editorDuplicateShortcut;
}
QString EditorController::editorSplitHorizontalShortcut() const
{
    return m_editorSplitHorizontalShortcut;
}
QString EditorController::editorSplitVerticalShortcut() const
{
    return m_editorSplitVerticalShortcut;
}
QString EditorController::editorFillShortcut() const
{
    return m_editorFillShortcut;
}
int EditorController::snapOverrideModifier() const
{
    return m_snapOverrideModifier;
}
bool EditorController::fillOnDropEnabled() const
{
    return m_fillOnDropEnabled;
}
int EditorController::fillOnDropModifier() const
{
    return m_fillOnDropModifier;
}
QString EditorController::targetScreen() const
{
    return m_targetScreen;
}

UndoController* EditorController::undoController() const
{
    return m_undoController;
}
bool EditorController::canPaste() const
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    return ZoneSerialization::isValidClipboardFormat(clipboard->text());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Property setters
// ═══════════════════════════════════════════════════════════════════════════════

void EditorController::setLayoutName(const QString& name)
{
    if (m_layoutName != name) {
        if (!m_undoController) {
            qCWarning(lcEditor) << "setLayoutName: undo controller is null";
            return;
        }

        QString oldName = m_layoutName;

        // Create and push command
        auto* command = new UpdateLayoutNameCommand(QPointer<EditorController>(this), oldName, name, QString());
        m_undoController->push(command);
        markUnsaved();
    }
}

void EditorController::setLayoutNameDirect(const QString& name)
{
    if (m_layoutName != name) {
        m_layoutName = name;
        Q_EMIT layoutNameChanged();
    }
}

void EditorController::setSelectedZoneId(const QString& zoneId)
{
    if (m_selectedZoneId != zoneId) {
        m_selectedZoneId = zoneId;
        // Sync with multi-selection: single selection = list with one item
        m_selectedZoneIds.clear();
        if (!zoneId.isEmpty()) {
            m_selectedZoneIds.append(zoneId);
        }
        Q_EMIT selectedZoneIdChanged();
        Q_EMIT selectedZoneIdsChanged();
    }
}

void EditorController::setSelectedZoneIds(const QStringList& zoneIds)
{
    if (m_selectedZoneIds != zoneIds) {
        QStringList oldSelection = m_selectedZoneIds;

        // Apply the change
        setSelectedZoneIdsDirect(zoneIds);

        // Push undo command (if undo controller available)
        if (m_undoController) {
            auto* command = new ChangeSelectionCommand(QPointer<EditorController>(this), oldSelection, zoneIds);
            m_undoController->push(command);
        }
    }
}

void EditorController::setSelectedZoneIdsDirect(const QStringList& zoneIds)
{
    if (m_selectedZoneIds != zoneIds) {
        m_selectedZoneIds = zoneIds;
        // Sync with single-selection for backward compatibility
        QString newSelectedId = zoneIds.isEmpty() ? QString() : zoneIds.first();
        if (m_selectedZoneId != newSelectedId) {
            m_selectedZoneId = newSelectedId;
            Q_EMIT selectedZoneIdChanged();
        }
        Q_EMIT selectedZoneIdsChanged();
    }
}

void EditorController::setGridSnappingEnabled(bool enabled)
{
    m_snappingService->setGridSnappingEnabled(enabled);
    saveEditorSettings();
}

void EditorController::setGridOverlayVisible(bool visible)
{
    if (m_gridOverlayVisible != visible) {
        m_gridOverlayVisible = visible;
        Q_EMIT gridOverlayVisibleChanged();
    }
}

void EditorController::setEdgeSnappingEnabled(bool enabled)
{
    m_snappingService->setEdgeSnappingEnabled(enabled);
    saveEditorSettings();
}

void EditorController::setSnapIntervalX(qreal interval)
{
    m_snappingService->setSnapIntervalX(interval);
    saveEditorSettings();
}

void EditorController::setSnapIntervalY(qreal interval)
{
    m_snappingService->setSnapIntervalY(interval);
    saveEditorSettings();
}

void EditorController::setSnapInterval(qreal interval)
{
    // Backward compatibility: set both X and Y to the same value
    setSnapIntervalX(interval);
    setSnapIntervalY(interval);
}

void EditorController::setSnapOverrideModifier(int modifier)
{
    if (m_snapOverrideModifier != modifier) {
        m_snapOverrideModifier = modifier;
        Q_EMIT snapOverrideModifierChanged();
        saveEditorSettings();
    }
}

void EditorController::setFillOnDropEnabled(bool enabled)
{
    if (m_fillOnDropEnabled != enabled) {
        m_fillOnDropEnabled = enabled;
        Q_EMIT fillOnDropEnabledChanged();
        saveEditorSettings();
    }
}

void EditorController::setFillOnDropModifier(int modifier)
{
    if (m_fillOnDropModifier != modifier) {
        m_fillOnDropModifier = modifier;
        Q_EMIT fillOnDropModifierChanged();
        saveEditorSettings();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Snapping delegation
// ═══════════════════════════════════════════════════════════════════════════════

QVariantMap EditorController::snapGeometry(qreal x, qreal y, qreal width, qreal height, const QString& excludeZoneId)
{
    if (!m_snappingService || !m_zoneManager) {
        // Fallback: return unsnapped geometry
        QVariantMap result;
        result[::PhosphorZones::ZoneJsonKeys::X] = x;
        result[::PhosphorZones::ZoneJsonKeys::Y] = y;
        result[::PhosphorZones::ZoneJsonKeys::Width] = width;
        result[::PhosphorZones::ZoneJsonKeys::Height] = height;
        return result;
    }

    QVariantList allZones = m_zoneManager->zones();
    return m_snappingService->snapGeometry(x, y, width, height, allZones, excludeZoneId);
}

QVariantMap EditorController::snapGeometrySelective(qreal x, qreal y, qreal width, qreal height,
                                                    const QString& excludeZoneId, bool snapLeft, bool snapRight,
                                                    bool snapTop, bool snapBottom)
{
    if (!m_snappingService || !m_zoneManager) {
        // Fallback: return unsnapped geometry
        QVariantMap result;
        result[::PhosphorZones::ZoneJsonKeys::X] = x;
        result[::PhosphorZones::ZoneJsonKeys::Y] = y;
        result[::PhosphorZones::ZoneJsonKeys::Width] = width;
        result[::PhosphorZones::ZoneJsonKeys::Height] = height;
        return result;
    }

    QVariantList allZones = m_zoneManager->zones();
    return m_snappingService->snapGeometrySelective(x, y, width, height, allZones, excludeZoneId, snapLeft, snapRight,
                                                    snapTop, snapBottom);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Internal helpers
// ═══════════════════════════════════════════════════════════════════════════════

void EditorController::markUnsaved()
{
    if (!m_hasUnsavedChanges) {
        m_hasUnsavedChanges = true;
        Q_EMIT hasUnsavedChangesChanged();
    }
}

bool EditorController::servicesReady(const char* operation) const
{
    if (!m_undoController || !m_zoneManager) {
        qCWarning(lcEditor) << "Cannot" << operation << "- undo controller or zone manager is null";
        return false;
    }
    return true;
}

} // namespace PlasmaZones
