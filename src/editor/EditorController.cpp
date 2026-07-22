// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "EditorController.h"

#include "../config/configbackends.h"
#include "../config/configdefaults.h"
#include "services/ILayoutService.h"
#include "services/DBusLayoutService.h"
#include "services/ZoneManager.h"
#include "services/SnappingService.h"
#include "services/TemplateService.h"
#include "undo/UndoController.h"
#include "undo/commands/UpdateLayoutNameCommand.h"
#include "undo/commands/ChangeSelectionCommand.h"
#include "helpers/ZoneSerialization.h"
#include <PhosphorRules/RuleStore.h>
#include <PhosphorRules/RuleStoreWatcher.h>
#include "core/utils/geometryutils.h"
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorZones/LayoutComputeService.h>
#include "core/platform/logging.h"
#include "core/utils/utils.h"
#include "../shaderpreview/shaderpreviewcontroller.h"

#include <PhosphorZones/Layout.h>

#include <QClipboard>
#include <QDBusConnection>
#include <QGuiApplication>
#include <PhosphorScreens/ScreenIdentity.h>

#include "../common/screenidresolver.h"

namespace PlasmaZones {

EditorController::EditorController(QObject* parent)
    : QObject(parent)
    , m_layoutService(new DBusLayoutService(this))
    , m_zoneManager(new ZoneManager(this))
    , m_snappingService(new SnappingService(this))
    , m_templateService(new TemplateService(this))
    , m_undoController(new UndoController(this))
    , m_localRuleStore(std::make_unique<PhosphorRules::RuleStore>(ConfigDefaults::rulesFilePath()))
    , m_localRuleStoreWatcher(std::make_unique<PhosphorRules::RuleStoreWatcher>(*m_localRuleStore))
    , m_localLayoutManager(std::make_unique<PhosphorZones::LayoutRegistry>(m_localRuleStore.get(),
                                                                           QStringLiteral("plasmazones/layouts")))
{
    // Install the library-level screen-id resolver before any layout load
    // runs. First call initialises the static; subsequent constructions
    // in the same process reuse it. Moved out of the ctor-initializer
    // comma-operator trick so the intent is obvious at a glance —
    // matches the daemon's handling.
    ensureScreenIdResolver();

    // The shared zone-shader preview feed. EditorController is its backend
    // (IShaderPreviewBackend — D-Bus shader metadata + the live edited layout);
    // the QML-facing preview methods delegate to it. Forward the audio-spectrum
    // change so existing editor QML bindings on `audioSpectrum` keep firing.
    m_shaderPreview = new ShaderPreviewController(this, this);
    connect(m_shaderPreview, &ShaderPreviewController::audioSpectrumChanged, this,
            &EditorController::audioSpectrumChanged);
    connect(m_shaderPreview, &ShaderPreviewController::shaderPresetSaveFailed, this,
            &EditorController::shaderPresetSaveFailed);
    connect(m_shaderPreview, &ShaderPreviewController::shaderPresetLoadFailed, this,
            &EditorController::shaderPresetLoadFailed);

    // Begin watching rules.json for external writes. The editor has no
    // D-Bus rules-reload path, so without this its m_localRuleStore would serve
    // the snapshot scanned at launch — the assignment cascade would ignore rule
    // edits the daemon (or settings app) makes while the editor is open. The
    // store's idempotent load() means a self-write or no-op change emits nothing.
    m_localRuleStoreWatcher->start();

    // Wire the layoutsChanged → recalcLocalLayouts connection BEFORE the
    // initial loadLayouts() so the load below is routed through it.
    connect(m_localLayoutManager.get(), &PhosphorZones::LayoutRegistry::layoutsChanged, this,
            &EditorController::recalcLocalLayouts);

    // Populate the in-process registry from disk on startup so loadLayout()'s
    // instant-open fast path can resolve a layout by id without the daemon.
    // PhosphorZones::LayoutRegistry does NOT watch the layout directory: an
    // explicit loadLayouts() call is the only thing that rescans disk. This
    // one covers startup, and the D-Bus subscriptions wired below cover every
    // later change. Do not drop either as redundant.
    m_localLayoutManager->loadLayouts();
    // Recompute zone geometry for fixed-geometry layouts so a loaded layout
    // carries real zone rects — see the matching
    // comment in SettingsController. The connect above also fires
    // recalcLocalLayouts on the first loadLayouts() emission, but the
    // explicit call here covers the single-shot path where loadLayouts()
    // returns no-op (e.g. empty layouts dir, recalc still has work to do
    // against any pre-existing in-memory state).
    recalcLocalLayouts();

    // Subscribe to the daemon's layout-change D-Bus signals and force
    // a local-source reload when any fire. Nothing watches the layout
    // directory, so this subscription is the ONLY thing that refreshes the
    // editor's view of another process's writes — it is not a backup for a
    // file watcher. Dropping it strands the preview surface on whatever disk
    // held at startup.
    //
    // With the daemon down none of these fire. The editor's own edits go
    // through its in-process registry and are already in memory, so the gap is
    // narrow: a hand-edit to a layout file made while the editor is open and
    // the daemon is down is picked up on the next editor start.
    //
    // Debounce the 5 signals through a 50 ms single-shot timer so a
    // typical editor save (layoutChanged + layoutListChanged back-to-back)
    // only triggers one reloadLocalLayouts() pass. Mirrors the
    // SettingsController::m_layoutLoadTimer pattern.
    m_layoutReloadTimer.setSingleShot(true);
    m_layoutReloadTimer.setInterval(50);
    connect(&m_layoutReloadTimer, &QTimer::timeout, this, &EditorController::reloadLocalLayouts);

    // Same coalescing shape for editor-settings writes. 250 ms: long enough to
    // swallow a slider drag's mouse-move burst, short enough that the write
    // still feels immediate once the user lets go.
    m_editorSettingsSaveTimer.setSingleShot(true);
    m_editorSettingsSaveTimer.setInterval(250);
    connect(&m_editorSettingsSaveTimer, &QTimer::timeout, this, &EditorController::flushEditorSettings);

    auto bus = QDBusConnection::sessionBus();
    const QString svc = QString(PhosphorProtocol::Service::Name);
    const QString path = QString(PhosphorProtocol::Service::ObjectPath);
    const QString iface = QString(PhosphorProtocol::Service::Interface::LayoutRegistry);
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
    // Flush a queued settings write rather than queueing another: the timer
    // will never fire again from here, so a still-pending edit (the user closed
    // the editor within the debounce window of their last change) would be lost.
    if (m_editorSettingsSaveTimer.isActive()) {
        m_editorSettingsSaveTimer.stop();
        flushEditorSettingsBlocking();
    }

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

void EditorController::reloadLocalLayouts()
{
    // Slot wired to the daemon's layout-mutation signals — see ctor for
    // the connect block + rationale. loadLayouts() does no diffing: it
    // re-reads and rebuilds the whole catalogue every call, which is what
    // makes it a reload. The layout set is O(10s) of small JSON files and
    // this only fires on a daemon layout signal, so the re-scan is not on
    // any hot path.
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
        PhosphorZones::LayoutComputeService::recalculateSync(
            layout, GeometryUtils::effectiveScreenGeometry(nullptr, layout, primary));
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
