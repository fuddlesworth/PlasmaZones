// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Central controller for the standalone settings application.
// Manages page navigation, layout CRUD (via D-Bus), screen info,
// editor config, and owns the shared Settings instance.

#pragma once

#include "../config/settings.h"
#include "../config/updatechecker.h"
#include "../common/daemoncontroller.h"
#include "screenhelper.h"
#include "../core/constants.h"
#include "../core/enums.h"
#include "../core/modifierutils.h"

#include <QObject>
#include <QString>
#include <QDBusConnection>
#include <QVariantList>
#include <QTimer>
#include <memory>
#include <optional>

namespace PlasmaZones {

class SettingsController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString activePage READ activePage WRITE setActivePage NOTIFY activePageChanged)
    Q_PROPERTY(bool needsSave READ needsSave NOTIFY needsSaveChanged)
    Q_PROPERTY(bool daemonRunning READ daemonRunning NOTIFY daemonRunningChanged)
    Q_PROPERTY(Settings* settings READ settings CONSTANT)
    Q_PROPERTY(DaemonController* daemonController READ daemonController CONSTANT)
    Q_PROPERTY(UpdateChecker* updateChecker READ updateChecker CONSTANT)
    Q_PROPERTY(QString dismissedUpdateVersion READ dismissedUpdateVersion WRITE setDismissedUpdateVersion NOTIFY
                   dismissedUpdateVersionChanged)

    // Layout management
    Q_PROPERTY(QVariantList layouts READ layouts NOTIFY layoutsChanged)

    // Screen management
    Q_PROPERTY(QVariantList screens READ screens NOTIFY screensChanged)

    // Editor settings
    Q_PROPERTY(QString editorDuplicateShortcut READ editorDuplicateShortcut WRITE setEditorDuplicateShortcut NOTIFY
                   editorDuplicateShortcutChanged)
    Q_PROPERTY(QString editorSplitHorizontalShortcut READ editorSplitHorizontalShortcut WRITE
                   setEditorSplitHorizontalShortcut NOTIFY editorSplitHorizontalShortcutChanged)
    Q_PROPERTY(QString editorSplitVerticalShortcut READ editorSplitVerticalShortcut WRITE setEditorSplitVerticalShortcut
                   NOTIFY editorSplitVerticalShortcutChanged)
    Q_PROPERTY(
        QString editorFillShortcut READ editorFillShortcut WRITE setEditorFillShortcut NOTIFY editorFillShortcutChanged)
    Q_PROPERTY(bool editorGridSnappingEnabled READ editorGridSnappingEnabled WRITE setEditorGridSnappingEnabled NOTIFY
                   editorGridSnappingEnabledChanged)
    Q_PROPERTY(bool editorEdgeSnappingEnabled READ editorEdgeSnappingEnabled WRITE setEditorEdgeSnappingEnabled NOTIFY
                   editorEdgeSnappingEnabledChanged)
    Q_PROPERTY(qreal editorSnapIntervalX READ editorSnapIntervalX WRITE setEditorSnapIntervalX NOTIFY
                   editorSnapIntervalXChanged)
    Q_PROPERTY(int editorSnapOverrideModifier READ editorSnapOverrideModifier WRITE setEditorSnapOverrideModifier NOTIFY
                   editorSnapOverrideModifierChanged)
    Q_PROPERTY(bool fillOnDropEnabled READ fillOnDropEnabled WRITE setFillOnDropEnabled NOTIFY fillOnDropEnabledChanged)
    Q_PROPERTY(
        int fillOnDropModifier READ fillOnDropModifier WRITE setFillOnDropModifier NOTIFY fillOnDropModifierChanged)
    Q_PROPERTY(qreal editorSnapIntervalY READ editorSnapIntervalY WRITE setEditorSnapIntervalY NOTIFY
                   editorSnapIntervalYChanged)

    // Trigger configuration
    Q_PROPERTY(bool alwaysActivateOnDrag READ alwaysActivateOnDrag WRITE setAlwaysActivateOnDrag NOTIFY
                   alwaysActivateOnDragChanged)
    Q_PROPERTY(QVariantList dragActivationTriggers READ dragActivationTriggers WRITE setDragActivationTriggers NOTIFY
                   dragActivationTriggersChanged)
    Q_PROPERTY(QVariantList defaultDragActivationTriggers READ defaultDragActivationTriggers CONSTANT)
    Q_PROPERTY(
        QVariantList zoneSpanTriggers READ zoneSpanTriggers WRITE setZoneSpanTriggers NOTIFY zoneSpanTriggersChanged)
    Q_PROPERTY(QVariantList defaultZoneSpanTriggers READ defaultZoneSpanTriggers CONSTANT)
    Q_PROPERTY(QVariantList snapAssistTriggers READ snapAssistTriggers WRITE setSnapAssistTriggers NOTIFY
                   snapAssistTriggersChanged)
    Q_PROPERTY(QVariantList defaultSnapAssistTriggers READ defaultSnapAssistTriggers CONSTANT)

    // Cava detection
    Q_PROPERTY(bool cavaAvailable READ cavaAvailable CONSTANT)

public:
    explicit SettingsController(QObject* parent = nullptr);
    ~SettingsController() override = default;

    QString activePage() const
    {
        return m_activePage;
    }
    void setActivePage(const QString& page);

    bool needsSave() const
    {
        return m_needsSave;
    }
    bool daemonRunning() const
    {
        return m_daemonController.isRunning();
    }

    Settings* settings()
    {
        return &m_settings;
    }
    DaemonController* daemonController()
    {
        return &m_daemonController;
    }
    UpdateChecker* updateChecker()
    {
        return &m_updateChecker;
    }
    QString dismissedUpdateVersion() const
    {
        return m_dismissedUpdateVersion;
    }
    void setDismissedUpdateVersion(const QString& version);
    Q_INVOKABLE void dismissUpdate();

    // Layout accessors
    QVariantList layouts() const
    {
        return m_layouts;
    }

    // Screen accessors
    QVariantList screens() const
    {
        return m_screenHelper.screens();
    }

    // Virtual desktops / activities (reactive via D-Bus signals)
    Q_PROPERTY(int virtualDesktopCount READ virtualDesktopCount NOTIFY virtualDesktopsChanged)
    Q_PROPERTY(QStringList virtualDesktopNames READ virtualDesktopNames NOTIFY virtualDesktopsChanged)
    Q_PROPERTY(bool activitiesAvailable READ activitiesAvailable NOTIFY activitiesChanged)
    Q_PROPERTY(QVariantList activities READ activities NOTIFY activitiesChanged)
    Q_PROPERTY(QString currentActivity READ currentActivity NOTIFY activitiesChanged)

    int virtualDesktopCount() const
    {
        return m_virtualDesktopCount;
    }
    QStringList virtualDesktopNames() const
    {
        return m_virtualDesktopNames;
    }
    bool activitiesAvailable() const
    {
        return m_activitiesAvailable;
    }
    QVariantList activities() const
    {
        return m_activities;
    }
    QString currentActivity() const
    {
        return m_currentActivity;
    }

    // Layout CRUD (D-Bus to daemon)
    Q_INVOKABLE void createNewLayout();
    Q_INVOKABLE void deleteLayout(const QString& layoutId);
    Q_INVOKABLE void duplicateLayout(const QString& layoutId);
    Q_INVOKABLE void editLayout(const QString& layoutId);
    Q_INVOKABLE void editLayoutOnScreen(const QString& layoutId, const QString& screenId);
    Q_INVOKABLE void openLayoutsFolder();
    Q_INVOKABLE void importLayout(const QString& filePath);
    Q_INVOKABLE void exportLayout(const QString& layoutId, const QString& filePath);

    // KZones import
    Q_INVOKABLE bool hasKZonesConfig();
    Q_INVOKABLE int importFromKZones();
    Q_INVOKABLE int importFromKZonesFile(const QString& filePath);
    Q_INVOKABLE void setLayoutHidden(const QString& layoutId, bool hidden);
    Q_INVOKABLE void setLayoutAutoAssign(const QString& layoutId, bool enabled);

    // Screen helpers
    Q_INVOKABLE bool isMonitorDisabled(const QString& screenName) const;
    Q_INVOKABLE void setMonitorDisabled(const QString& screenName, bool disabled);

    // Font helpers (for FontPickerDialog)
    Q_INVOKABLE QStringList fontStylesForFamily(const QString& family) const;
    Q_INVOKABLE int fontStyleWeight(const QString& family, const QString& style) const;
    Q_INVOKABLE bool fontStyleItalic(const QString& family, const QString& style) const;

    // Assignment helpers (D-Bus to daemon)
    Q_INVOKABLE void assignLayoutToScreen(const QString& screenName, const QString& layoutId);
    Q_INVOKABLE void clearScreenAssignment(const QString& screenName);
    Q_INVOKABLE void assignTilingLayoutToScreen(const QString& screenName, const QString& layoutId);
    Q_INVOKABLE void clearTilingScreenAssignment(const QString& screenName);

    // Assignment query helpers (D-Bus to daemon)
    Q_INVOKABLE QString getLayoutForScreen(const QString& screenName) const;
    Q_INVOKABLE QString getTilingLayoutForScreen(const QString& screenName) const;

    // Per-desktop assignments (D-Bus to daemon)
    Q_INVOKABLE QString getLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const;
    Q_INVOKABLE void assignLayoutToScreenDesktop(const QString& screenName, int virtualDesktop,
                                                 const QString& layoutId);
    Q_INVOKABLE void clearScreenDesktopAssignment(const QString& screenName, int virtualDesktop);
    Q_INVOKABLE QString getSnappingLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const;
    Q_INVOKABLE bool hasExplicitAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop) const;

    // Tiling per-desktop assignments (D-Bus to daemon)
    Q_INVOKABLE void assignTilingLayoutToScreenDesktop(const QString& screenName, int virtualDesktop,
                                                       const QString& layoutId);
    Q_INVOKABLE void clearTilingScreenDesktopAssignment(const QString& screenName, int virtualDesktop);
    Q_INVOKABLE QString getTilingLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const;
    Q_INVOKABLE bool hasExplicitTilingAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop) const;

    // Per-activity assignments (D-Bus to daemon)
    Q_INVOKABLE QString getLayoutForScreenActivity(const QString& screenName, const QString& activityId) const;
    Q_INVOKABLE void assignLayoutToScreenActivity(const QString& screenName, const QString& activityId,
                                                  const QString& layoutId);
    Q_INVOKABLE void clearScreenActivityAssignment(const QString& screenName, const QString& activityId);
    Q_INVOKABLE QString getSnappingLayoutForScreenActivity(const QString& screenName, const QString& activityId) const;
    Q_INVOKABLE bool hasExplicitAssignmentForScreenActivity(const QString& screenName, const QString& activityId) const;

    // Tiling per-activity assignments (D-Bus to daemon)
    Q_INVOKABLE void assignTilingLayoutToScreenActivity(const QString& screenName, const QString& activityId,
                                                        const QString& layoutId);
    Q_INVOKABLE void clearTilingScreenActivityAssignment(const QString& screenName, const QString& activityId);
    Q_INVOKABLE QString getTilingLayoutForScreenActivity(const QString& screenName, const QString& activityId) const;
    Q_INVOKABLE bool hasExplicitTilingAssignmentForScreenActivity(const QString& screenName,
                                                                  const QString& activityId) const;

    // Quick layout slots (D-Bus to daemon)
    Q_INVOKABLE QString getQuickLayoutSlot(int slotNumber) const;
    Q_INVOKABLE void setQuickLayoutSlot(int slotNumber, const QString& layoutId);
    Q_INVOKABLE QString getQuickLayoutShortcut(int slotNumber) const;
    Q_INVOKABLE QString getTilingQuickLayoutSlot(int slotNumber) const;
    Q_INVOKABLE void setTilingQuickLayoutSlot(int slotNumber, const QString& layoutId);

    // App-to-zone rules (D-Bus to daemon)
    Q_INVOKABLE QVariantList getAppRulesForLayout(const QString& layoutId) const;
    Q_INVOKABLE void addAppRuleToLayout(const QString& layoutId, const QString& pattern, int zoneNumber,
                                        const QString& targetScreen = QString());
    Q_INVOKABLE void removeAppRuleFromLayout(const QString& layoutId, int index);

    // Assignment lock helpers
    Q_INVOKABLE bool isScreenLocked(const QString& screenName, int mode) const;
    Q_INVOKABLE void toggleScreenLock(const QString& screenName, int mode);
    Q_INVOKABLE bool isContextLocked(const QString& screenName, int virtualDesktop, const QString& activity,
                                     int mode) const;
    Q_INVOKABLE void toggleContextLock(const QString& screenName, int virtualDesktop, const QString& activity,
                                       int mode);

    // ── Editor settings (delegated to Settings class) ─────────────────────
    QString editorDuplicateShortcut() const;
    QString editorSplitHorizontalShortcut() const;
    QString editorSplitVerticalShortcut() const;
    QString editorFillShortcut() const;
    bool editorGridSnappingEnabled() const;
    bool editorEdgeSnappingEnabled() const;
    qreal editorSnapIntervalX() const;
    qreal editorSnapIntervalY() const;
    int editorSnapOverrideModifier() const;
    bool fillOnDropEnabled() const;
    int fillOnDropModifier() const;

    void setEditorDuplicateShortcut(const QString& shortcut);
    void setEditorSplitHorizontalShortcut(const QString& shortcut);
    void setEditorSplitVerticalShortcut(const QString& shortcut);
    void setEditorFillShortcut(const QString& shortcut);
    void setEditorGridSnappingEnabled(bool enabled);
    void setEditorEdgeSnappingEnabled(bool enabled);
    void setEditorSnapIntervalX(qreal interval);
    void setEditorSnapIntervalY(qreal interval);
    void setEditorSnapOverrideModifier(int mod);
    void setFillOnDropEnabled(bool enabled);
    void setFillOnDropModifier(int mod);

    Q_INVOKABLE void resetEditorDefaults();

    // ── Trigger configuration ────────────────────────────────────────────────
    bool alwaysActivateOnDrag() const;
    QVariantList dragActivationTriggers() const;
    QVariantList defaultDragActivationTriggers() const;
    QVariantList zoneSpanTriggers() const;
    QVariantList defaultZoneSpanTriggers() const;
    QVariantList snapAssistTriggers() const;
    QVariantList defaultSnapAssistTriggers() const;

    void setAlwaysActivateOnDrag(bool enabled);
    void setDragActivationTriggers(const QVariantList& triggers);
    void setZoneSpanTriggers(const QVariantList& triggers);
    void setSnapAssistTriggers(const QVariantList& triggers);

    // ── Cava detection ───────────────────────────────────────────────────────
    bool cavaAvailable() const;

    // ── Color import ─────────────────────────────────────────────────────────
    Q_INVOKABLE void loadColorsFromPywal();
    Q_INVOKABLE void loadColorsFromFile(const QString& filePath);
    Q_INVOKABLE QVariantList getRunningWindows() const;

    // ── Config export/import ────────────────────────────────────────────────
    Q_INVOKABLE bool exportAllSettings(const QString& filePath);
    Q_INVOKABLE bool importAllSettings(const QString& filePath);

    // ── Screen state query ─────────────────────────────────────────────────
    Q_INVOKABLE QVariantList getScreenStates() const;
    Q_INVOKABLE bool hasStagedAssignment(const QString& screenName, int virtualDesktop = 0,
                                         const QString& activityId = QString()) const;
    Q_INVOKABLE QVariantMap getStagedAssignment(const QString& screenName, int virtualDesktop = 0,
                                                const QString& activityId = QString()) const;

    // ── Atomic mode+layout staging (overview page) ──────────────────────────
    Q_INVOKABLE void stageAssignmentEntry(const QString& screenName, int virtualDesktop, const QString& activityId,
                                          int mode, const QString& snappingLayoutId, const QString& tilingAlgorithmId);

    // ── Algorithm helpers ────────────────────────────────────────────────────
    Q_INVOKABLE QVariantList availableAlgorithms() const;
    Q_INVOKABLE QVariantList generateAlgorithmPreview(const QString& algorithmId, int windowCount, double splitRatio,
                                                      int masterCount) const;

    // ── Per-screen autotile overrides ────────────────────────────────────────
    Q_INVOKABLE QVariantMap getPerScreenAutotileSettings(const QString& screenName) const;
    Q_INVOKABLE void setPerScreenAutotileSetting(const QString& screenName, const QString& key, const QVariant& value);
    Q_INVOKABLE void clearPerScreenAutotileSettings(const QString& screenName);
    Q_INVOKABLE bool hasPerScreenAutotileSettings(const QString& screenName) const;

    // ── Per-screen snapping overrides ────────────────────────────────────────
    Q_INVOKABLE QVariantMap getPerScreenSnappingSettings(const QString& screenName) const;
    Q_INVOKABLE void setPerScreenSnappingSetting(const QString& screenName, const QString& key, const QVariant& value);
    Q_INVOKABLE void clearPerScreenSnappingSettings(const QString& screenName);
    Q_INVOKABLE bool hasPerScreenSnappingSettings(const QString& screenName) const;

    // ── Per-screen zone selector overrides ───────────────────────────────────
    Q_INVOKABLE QVariantMap getPerScreenZoneSelectorSettings(const QString& screenName) const;
    Q_INVOKABLE void setPerScreenZoneSelectorSetting(const QString& screenName, const QString& key,
                                                     const QVariant& value);
    Q_INVOKABLE void clearPerScreenZoneSelectorSettings(const QString& screenName);
    Q_INVOKABLE bool hasPerScreenZoneSelectorSettings(const QString& screenName) const;

    Q_INVOKABLE QVariantMap loadWindowGeometry() const;
    Q_INVOKABLE void saveWindowGeometry(int x, int y, int width, int height);

    Q_INVOKABLE void load();
    Q_INVOKABLE void save();
    Q_INVOKABLE void defaults();
    Q_INVOKABLE void launchEditor();

Q_SIGNALS:
    void activePageChanged();
    void needsSaveChanged();
    void daemonRunningChanged();
    void layoutsChanged();
    void layoutAdded(const QString& layoutId);
    void screensChanged();
    void dismissedUpdateVersionChanged();

    // Virtual desktop / activity / assignment signals
    void virtualDesktopsChanged();
    void activitiesChanged();
    void screenLayoutChanged();
    void quickLayoutSlotsChanged();

    // Trigger signals
    void alwaysActivateOnDragChanged();
    void dragActivationTriggersChanged();
    void zoneSpanTriggersChanged();
    void snapAssistTriggersChanged();

    // Color import signals
    void colorImportError(const QString& error);
    void colorImportSuccess();

    // KZones import signals
    void kzonesImportFinished(int count, const QString& message);
    void lockedScreensChanged();

    // Editor signals
    void editorDuplicateShortcutChanged();
    void editorSplitHorizontalShortcutChanged();
    void editorSplitVerticalShortcutChanged();
    void editorFillShortcutChanged();
    void editorGridSnappingEnabledChanged();
    void editorEdgeSnappingEnabledChanged();
    void editorSnapIntervalXChanged();
    void editorSnapIntervalYChanged();
    void editorSnapOverrideModifierChanged();
    void fillOnDropEnabledChanged();
    void fillOnDropModifierChanged();

private Q_SLOTS:
    void onExternalSettingsChanged();
    void onSettingsPropertyChanged();
    void loadLayoutsAsync();
    void onVirtualDesktopsChanged();
    void onActivitiesChanged();
    void onScreenLayoutChanged(const QString& screenId, const QString& layoutId, int virtualDesktop);

private:
    void setNeedsSave(bool needs);
    void scheduleLayoutLoad();
    void refreshVirtualDesktops();
    void refreshActivities();

    void saveAppRulesToDaemon(const QString& layoutId, const QVariantList& rules);
    int importKZonesLayouts(const QJsonArray& kzonesArray);

    static QVariantList convertTriggersForQml(const QVariantList& triggers);
    static QVariantList convertTriggersForStorage(const QVariantList& triggers);

    Settings m_settings;
    DaemonController m_daemonController;
    UpdateChecker m_updateChecker;
    QString m_dismissedUpdateVersion;
    ScreenHelper m_screenHelper;
    QString m_activePage = QStringLiteral("overview");
    bool m_needsSave = false;
    bool m_saving = false;

    // Layout state
    QVariantList m_layouts;
    QTimer m_layoutLoadTimer;
    QString m_pendingSelectLayoutId;

    // Virtual desktop / activity state
    int m_virtualDesktopCount = 1;
    QStringList m_virtualDesktopNames;
    bool m_activitiesAvailable = false;
    QVariantList m_activities;
    QString m_currentActivity;

    // Staged assignment changes (applied on save, discarded on load/reset)
    struct StagedAssignment
    {
        QString screenId;
        int virtualDesktop = 0;
        QString activityId;
        // Snapping — nullopt means not staged (use D-Bus), empty string means cleared
        std::optional<QString> snappingLayoutId;
        // Tiling — nullopt means not staged, empty string means cleared
        std::optional<QString> tilingAlgorithmId;
        // Explicit mode — when set, flush uses setAssignmentEntry (atomic mode+layout)
        std::optional<int> stagedMode;
        // Full clear overrides individual fields
        bool fullCleared = false;
    };
    QHash<QString, StagedAssignment> m_stagedAssignments;

    // Staged quick layout slot changes (flushed on Apply via D-Bus)
    QHash<int, QString> m_stagedQuickSlots;
    // Staged tiling quick layout slot changes (flushed on Apply to config)
    QHash<int, QString> m_stagedTilingQuickSlots;

    static QString assignmentCacheKey(const QString& screen, int desktop, const QString& activity);
    StagedAssignment& stagedEntry(const QString& screen, int desktop, const QString& activity);
    const StagedAssignment* stagedEntryConst(const QString& screen, int desktop, const QString& activity) const;
    void flushStagedAssignments();

    // Staged mutation helpers (shared logic for Q_INVOKABLE assignment methods)
    void stageSnapping(const QString& screen, int desktop, const QString& activity, const QString& layoutId);
    void stageTiling(const QString& screen, int desktop, const QString& activity, const QString& layoutId);
    void stageFullClear(const QString& screen, int desktop, const QString& activity);
    void stageTilingClear(const QString& screen, int desktop, const QString& activity);

    // Staged getter helpers — return true if handled (result set), false to fall through to D-Bus
    bool stagedSnappingLayout(const QString& screen, int desktop, const QString& activity, QString& out) const;
    bool stagedTilingLayout(const QString& screen, int desktop, const QString& activity, QString& out) const;
};

} // namespace PlasmaZones
