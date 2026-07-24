// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "configdefaults_screens.h"

namespace PhosphorAnimation {
class CurveRegistry;
}

namespace PlasmaZones {

/**
 * @brief Provides static access to default configuration values
 *
 * Canonical default values for all PlasmaZones configuration keys.
 * Used by Settings::load() when no persisted value exists.
 *
 * Usage:
 *   int cols = ConfigDefaults::gridColumns();  // Returns 5
 *   int rows = ConfigDefaults::maxRows();      // Returns 4
 */
class ConfigDefaults : public ConfigDefaultsScreens
{
public:
    // ═══════════════════════════════════════════════════════════════════════════
    // Activation Settings
    // ═══════════════════════════════════════════════════════════════════════════

    // Single source of truth for the per-action trigger cap. Consumed by
    // Settings::MaxTriggersPerAction (public API), the canonicalTriggerList
    // validator in settingsschema.cpp (schema-side cap), and any caller that
    // needs the limit. Keeping the constant here — reachable from both
    // settings.h and settingsschema.cpp without either depending on the
    // other — avoids the drift that motivated the original static_assert.
    static constexpr int maxTriggersPerAction()
    {
        return 4;
    }

    // Build a single-entry trigger list with the given modifier and mouse
    // button. Shared by the default accessors below so the canonical
    // {modifier, mouseButton} shape lives in one place.
    static QVariantList makeSingleTriggerList(int modifier, int mouseButton = 0)
    {
        QVariantMap trigger;
        trigger[ConfigKeys::triggerModifierField()] = modifier;
        trigger[ConfigKeys::triggerMouseButtonField()] = mouseButton;
        return {trigger};
    }

    static QVariantList dragActivationTriggers()
    {
        return makeSingleTriggerList(static_cast<int>(DragModifier::Alt));
    }
    static bool toggleActivation()
    {
        return false;
    }
    static bool snappingEnabled()
    {
        return true;
    }
    static bool zoneSpanEnabled()
    {
        return true;
    }
    static int zoneSpanModifier()
    {
        return static_cast<int>(DragModifier::Ctrl);
    }
    static QVariantList zoneSpanTriggers()
    {
        return makeSingleTriggerList(zoneSpanModifier());
    }
    static bool zoneSpanToggleMode()
    {
        return false;
    }
    static QVariantList autotileDragInsertTriggers()
    {
        // Held while dragging a window to dynamically insert it into the
        // autotile stack at the cursor position.
        return makeSingleTriggerList(static_cast<int>(DragModifier::Alt));
    }
    static bool autotileDragInsertToggle()
    {
        return false;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Display Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static bool showOnAllMonitors()
    {
        return false;
    }
    static bool showNumbers()
    {
        return true;
    }
    static bool flashOnSwitch()
    {
        return true;
    }
    static bool showOsdOnLayoutSwitch()
    {
        return true;
    }
    static bool showOsdOnDesktopSwitch()
    {
        return true;
    }
    static bool showNavigationOsd()
    {
        return true;
    }
    static constexpr int osdStyleMin()
    {
        return 0;
    }
    static constexpr int osdStyleMax()
    {
        return 2;
    }
    static int osdStyle()
    {
        return 2;
    }
    static constexpr int overlayDisplayModeMin()
    {
        return 0;
    }
    static constexpr int overlayDisplayModeMax()
    {
        return 1;
    }
    static int overlayDisplayMode()
    {
        return 0;
    }
    // ═══════════════════════════════════════════════════════════════════════════
    // Window Behavior Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static bool keepWindowsInZonesOnResolutionChange()
    {
        return true;
    }
    static bool moveNewWindowsToLastZone()
    {
        return false;
    }
    static bool restoreOriginalSizeOnUnsnap()
    {
        return true;
    }
    static int snappingStickyWindowHandling()
    {
        return 0;
    }
    static bool restoreWindowsToZonesOnLogin()
    {
        return true;
    }
    // Restore a FLOATED window to its previous position on reopen. Per-engine
    // (snap-floated vs autotile-floated); snapping delegates to the autotile
    // canonical so both modes start identical, mirroring the border defaults.
    static bool autotileRestoreFloatedWindowsOnLogin()
    {
        return true;
    }
    static bool snappingRestoreFloatedWindowsOnLogin()
    {
        return autotileRestoreFloatedWindowsOnLogin();
    }
    static bool snapUnfloatFallbackToZone()
    {
        return false;
    }
    static bool autoAssignAllLayouts()
    {
        return false;
    }
    // Off by default: every context still gets the synthesized level-1 default
    // layout (today's behavior). When on, no context is assigned an active
    // snapping or autotiling layout until the user explicitly assigns one —
    // overridable per context by a DefaultLayoutAssignment rule.
    static bool suppressDefaultLayoutAssignment()
    {
        return false;
    }
    static bool filterLayoutsByAspectRatio()
    {
        return true;
    }
    static bool snapAssistFeatureEnabled()
    {
        return true;
    }
    static bool snapAssistEnabled()
    {
        return true;
    }
    static QVariantList snapAssistTriggers()
    {
        // Default: Middle mouse
        return makeSingleTriggerList(static_cast<int>(DragModifier::Disabled), static_cast<int>(Qt::MiddleButton));
    }
    // ═══════════════════════════════════════════════════════════════════════════
    // PhosphorZones::Zone Selector Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static bool zoneSelectorEnabled()
    {
        return true;
    }
    static int triggerDistance()
    {
        return 50;
    }
    static constexpr int triggerDistanceMin()
    {
        return 10;
    }
    static constexpr int triggerDistanceMax()
    {
        return 200;
    }
    static int position()
    {
        return 1;
    }
    static int layoutMode()
    {
        return 0;
    }
    static int sizeMode()
    {
        return 0;
    }
    static int maxRows()
    {
        return 4;
    }
    static constexpr int maxRowsMin()
    {
        return 1;
    }
    static constexpr int maxRowsMax()
    {
        return 10;
    }
    static int previewWidth()
    {
        return 180;
    }
    static constexpr int previewWidthMin()
    {
        return 80;
    }
    static constexpr int previewWidthMax()
    {
        return 400;
    }
    // Zone-selector preview-size presets (Small / Large). Medium reuses the
    // default previewWidth() (180). Used by the Small/Medium/Large quick-size
    // buttons so the widths aren't hard-coded in QML.
    static constexpr int previewWidthSmall()
    {
        return 120;
    }
    static constexpr int previewWidthLarge()
    {
        return 260;
    }
    static int previewHeight()
    {
        return 101;
    }
    static constexpr int previewHeightMin()
    {
        return 60;
    }
    static constexpr int previewHeightMax()
    {
        return 300;
    }
    static bool previewLockAspect()
    {
        return true;
    }
    static int gridColumns()
    {
        return 5;
    }
    static constexpr int gridColumnsMin()
    {
        return 1;
    }
    static constexpr int gridColumnsMax()
    {
        return 10;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Path
    // ═══════════════════════════════════════════════════════════════════════════

    // Returns the absolute path to config.json.
    // Not cached — QStandardPaths respects $XDG_CONFIG_HOME changes at runtime,
    // which tests rely on via IsolatedConfigGuard.
    PLASMAZONES_EXPORT static QString configFilePath();

    // Returns the absolute path to session.json (ephemeral window tracking state).
    // Separate from config.json to avoid write contention between user preferences
    // and high-frequency session state saves.
    PLASMAZONES_EXPORT static QString sessionFilePath();

    // Returns the absolute path to rules.json (the unified Rule
    // store — schema v4). Separate from config.json so frequent daemon-driven
    // rule writes do not churn the cold user-settings blob. The daemon is the
    // sole writer.
    PLASMAZONES_EXPORT static QString rulesFilePath();

    // Returns the absolute path to the settings-profiles directory
    // (~/.config/plasmazones/profiles). Each profile is a single <uuid>.json
    // file holding a sparse config delta (and user-rule subset) relative to its
    // parent profile, plus an index.json sidecar tracking the active profile and
    // display order. Sits under the config dir (not the data dir) so it never
    // collides with the data-dir animation profiles subdir.
    PLASMAZONES_EXPORT static QString profilesDir();

    // Window appearance (border / title bar / gap) defaults live in the config
    // store now (the Windows.* / Gaps.* groups), NOT in managed baseline rules.
    // These three ids no longer identify seeded rules — they exist only so the
    // daemon (and the D-Bus Restore Defaults path) can STRIP any stale managed
    // baseline appearance rules an older build wrote to rules.json. See
    // managedAppearanceBaselineIds() and the strips in daemon.cpp / ruleadaptor.cpp.
    //
    // UUID suffix `...001` is RETIRED — it was the never-shipped single combined
    // "Default appearance" baseline rule, since split into the three ids below
    // (`...002`/`...003`/`...004`). Do not reuse it.

    /// Stable id of the (now-stripped) managed baseline BORDER rule.
    static QUuid baselineBorderRuleId()
    {
        return QUuid(QStringLiteral("{0a5e1b00-0000-4000-8000-000000000002}"));
    }

    /// Stable id of the (now-stripped) managed baseline TITLE BAR rule.
    static QUuid baselineTitleBarRuleId()
    {
        return QUuid(QStringLiteral("{0a5e1b00-0000-4000-8000-000000000003}"));
    }

    /// Stable id of the (now-stripped) managed baseline GAP rule. Per-monitor gap
    /// overrides are config-backed now, so this id is no longer used to namespace
    /// them; it survives only as a strip target.
    static QUuid baselineGapRuleId()
    {
        return QUuid(QStringLiteral("{0a5e1b00-0000-4000-8000-000000000004}"));
    }

    /// The three fixed ids above as a set — the single source of truth for the
    /// stale-managed-appearance-baseline strip, shared by the daemon's startup
    /// cleanup and the D-Bus Restore Defaults path so the two can't drift.
    static QSet<QUuid> managedAppearanceBaselineIds()
    {
        return {baselineBorderRuleId(), baselineTitleBarRuleId(), baselineGapRuleId()};
    }

    // Returns the absolute path to quicklayouts.json (the numbered quick-layout
    // shortcut slots 1..9). Quick-layout slots are NOT rules, so they
    // sit in a sibling sidecar next to rules.json rather than in the
    // rule store. LayoutRegistry reads/writes this file directly.
    PLASMAZONES_EXPORT static QString quickLayoutsFilePath();

    // Returns the absolute path to layout-settings.json — the per-layout
    // settings sidecar (keyed by layout UUID). Per-layout settings are NOT part
    // of the structural layout definition, so they live in a sibling sidecar
    // next to rules.json rather than inside each layout file.
    PLASMAZONES_EXPORT static QString layoutSettingsFilePath();

    // Curated default picker visibility, seeded into layout-settings.json on a
    // fresh install only (LayoutRegistry::seedDefaultLayoutSettingsIfFresh).
    // Returns an object keyed exactly as layout-settings.json — manual layouts
    // by bundled UUID (with braces), algorithms by the "autotile:<id>" form —
    // mapping each non-curated id to `{ "hiddenFromSelector": true }`. Listed
    // ids start hidden; everything else stays visible. Users re-show any item
    // via the eye toggle, and existing installs are never reseeded.
    PLASMAZONES_EXPORT static QJsonObject defaultLayoutVisibilitySettings();

    // Returns the absolute path to the legacy plasmazonesrc file (INI format).
    // Used only by the one-time migration module.
    PLASMAZONES_EXPORT static QString legacyConfigFilePath();

    /**
     * Read the rendering backend from the config file on disk.
     *
     * Primary path: reads Rendering/Backend from config.json (the
     * renderingGroup()/backendKey() accessors). Falls back to the legacy
     * plasmazonesrc INI (v1 key) when the JSON config is absent, unparseable,
     * or doesn't carry the key — in practice the pre-migration window, since
     * a successful migration renames the INI away. This helper provides a
     * single canonical read used by daemon, editor, and Settings.
     *
     * Safe to call before QCoreApplication exists (raw file access, no
     * config backend construction).
     * Returns the normalized backend string ("auto", "vulkan", or "opengl").
     */
    PLASMAZONES_EXPORT static QString readRenderingBackendFromDisk();
    // ═══════════════════════════════════════════════════════════════════════════
    // Autotile Settings
    // ═══════════════════════════════════════════════════════════════════════════

    // Autotiling is on out of the box so PlasmaZones tiles like a dynamic
    // tiler (krohnkite, dwm, xmonad) without setup. The companion
    // krohnkite-parity behaviors are already on by default elsewhere here:
    // autotileFocusNewWindows, autotileSmartGaps (sole window fills the
    // screen), autotileRespectMinimumSize, and excludeTransientWindows
    // (dialogs / utility windows float), with new windows inserted at the
    // stack end rather than as master.
    static bool autotileEnabled()
    {
        return true;
    }
    static QString defaultAutotileAlgorithm()
    {
        return QStringLiteral("bsp");
    }
    static constexpr qreal autotileSplitRatio()
    {
        return PhosphorTiles::AutotileDefaults::DefaultSplitRatio;
    }
    static constexpr qreal autotileSplitRatioMin()
    {
        return PhosphorTiles::AutotileDefaults::MinSplitRatio;
    }
    static constexpr qreal autotileSplitRatioMax()
    {
        return PhosphorTiles::AutotileDefaults::MaxSplitRatio;
    }
    static constexpr qreal autotileSplitRatioStep()
    {
        return 0.05;
    }
    static constexpr qreal autotileSplitRatioStepMin()
    {
        return 0.01;
    }
    static constexpr qreal autotileSplitRatioStepMax()
    {
        return 0.25;
    }
    static constexpr int autotileMasterCount()
    {
        return PhosphorTiles::AutotileDefaults::DefaultMasterCount;
    }
    static constexpr int autotileMasterCountMin()
    {
        return PhosphorTiles::AutotileDefaults::MinMasterCount;
    }
    static constexpr int autotileMasterCountMax()
    {
        return PhosphorTiles::AutotileDefaults::MaxMasterCount;
    }
    // Autotile inner/outer gaps are unified with snapping — see innerGap() /
    // outerGap*() above. Tiling reads the same shared accessors; no autotile-
    // specific gap defaults remain.
    static constexpr bool autotileFocusNewWindows()
    {
        return true;
    }
    static constexpr bool autotileSmartGaps()
    {
        return true;
    }
    static constexpr int autotileInsertPosition()
    {
        return 0;
    }
    static constexpr int autotileInsertPositionMin()
    {
        return PhosphorTiles::AutotileDefaults::MinInsertPosition;
    }
    static constexpr int autotileInsertPositionMax()
    {
        return PhosphorTiles::AutotileDefaults::MaxInsertPosition;
    }
    static constexpr int autotileMaxWindows()
    {
        return PhosphorTiles::AutotileDefaults::DefaultMaxWindows;
    }
    static constexpr int autotileMaxWindowsMin()
    {
        return PhosphorTiles::AutotileDefaults::MinMaxWindows;
    }
    static constexpr int autotileMaxWindowsMax()
    {
        return PhosphorTiles::AutotileDefaults::MaxMaxWindows;
    }
    static bool animationsEnabled()
    {
        return true;
    }
    static constexpr int animationDuration()
    {
        return 320;
    }
    static constexpr int animationDurationMin()
    {
        return PhosphorAnimation::Limits::MinAnimationDurationMs;
    }
    static constexpr int animationDurationMax()
    {
        return PhosphorAnimation::Limits::MaxAnimationDurationMs;
    }
    static constexpr int animationSequenceMode()
    {
        return 1;
    }
    static constexpr int animationSequenceModeMin()
    {
        return 0;
    }
    static constexpr int animationSequenceModeMax()
    {
        return 1;
    }
    static constexpr int animationStaggerInterval()
    {
        return 40;
    }
    static constexpr int animationStaggerIntervalMin()
    {
        return PhosphorAnimation::Limits::MinAnimationStaggerIntervalMs;
    }
    static constexpr int animationStaggerIntervalMax()
    {
        return PhosphorAnimation::Limits::MaxAnimationStaggerIntervalMs;
    }
    static QString animationEasingCurve()
    {
        return QStringLiteral("0.22,0.61,0.36,1.00");
    }
    static int animationMinDistance()
    {
        return 0;
    }
    static constexpr int animationMinDistanceMin()
    {
        return 0;
    }
    static constexpr int animationMinDistanceMax()
    {
        return 200;
    }
    /// Default Profile blob — animation settings live as a single
    /// nested-JSON entry under `animationsGroup/animationProfileKey`.
    /// Persisted as a `QVariantMap` so the on-disk JSON file shows the
    /// nested object structure directly (no escaped string-in-string).
    /// Assembled from the per-field defaults above so the library-default
    /// feel matches `Profile::toJson` shape.
    static QVariantMap animationProfile(const PhosphorAnimation::CurveRegistry& registry);

    static QVariantMap shaderProfileTree()
    {
        return {};
    }

    static bool autotileFocusFollowsMouse()
    {
        return false;
    }
    // Snapping focus behavior. Both default off: snapping is more manual than
    // autotile, and auto-focusing windows that the daemon places on open (e.g.
    // a session restore that opens many windows at once) would thrash focus.
    static bool snappingFocusNewWindows()
    {
        return false;
    }
    static bool snappingFocusFollowsMouse()
    {
        return false;
    }
    static bool autotileRespectMinimumSize()
    {
        return true;
    }
    static int autotileStickyWindowHandling()
    {
        return 0;
    }
    static int autotileDragBehavior()
    {
        return 0; // AutotileDragBehavior::Float — native drag-to-float
    }
    static int autotileOverflowBehavior()
    {
        return 0; // AutotileOverflowBehavior::Float — cap-enforcing (current)
    }
    static QStringList lockedScreens()
    {
        return {};
    }
    // ═══════════════════════════════════════════════════════════════════════════
    // Ordering Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static QStringList snappingLayoutOrder()
    {
        return {};
    }
    static QStringList tilingAlgorithmOrder()
    {
        return {};
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Editor Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static QString editorDuplicateShortcut()
    {
        return QStringLiteral("Ctrl+D");
    }
    static QString editorSplitHorizontalShortcut()
    {
        return QStringLiteral("Ctrl+Shift+H");
    }
    static QString editorSplitVerticalShortcut()
    {
        return QStringLiteral("Ctrl+Alt+V");
    }
    static QString editorFillShortcut()
    {
        return QStringLiteral("Ctrl+Shift+F");
    }
    static bool editorGridSnappingEnabled()
    {
        return true;
    }
    static bool editorEdgeSnappingEnabled()
    {
        return true;
    }
    static double editorSnapInterval()
    {
        return 0.1;
    }
    /// Per-axis defaults — currently share the same value as editorSnapInterval
    /// but split so future aspect-aware defaults don't require auditing every
    /// call site. Use these from resetDefaults / first-run paths.
    ///
    /// Return type is `qreal` to match `ISettings::editorSnapIntervalX/Y`
    /// (qreal is `double` on every Qt6-supported target, but the type
    /// alignment removes a category of "I forgot which one" mistakes in
    /// callers that take the value `auto`).
    static qreal editorSnapIntervalX()
    {
        return editorSnapInterval();
    }
    static qreal editorSnapIntervalY()
    {
        return editorSnapInterval();
    }
    static double editorSnapIntervalMin()
    {
        return 0.01;
    }
    static double editorSnapIntervalMax()
    {
        return 1.0;
    }
    static int editorSnapOverrideModifier()
    {
        return static_cast<int>(Qt::ShiftModifier);
    }
    static bool fillOnDropEnabled()
    {
        return true;
    }
    static int fillOnDropModifier()
    {
        return static_cast<int>(Qt::ControlModifier);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Global Shortcuts
    // ═══════════════════════════════════════════════════════════════════════════

    static QString openEditorShortcut()
    {
        return QStringLiteral("Meta+Shift+E");
    }
    static QString openSettingsShortcut()
    {
        return QStringLiteral("Meta+Shift+P");
    }
    static QString toggleCheatsheetShortcut()
    {
        // Meta+Alt+<key> matches the layouts family (Meta+Alt+[ ] Space).
        // "/" carries the help idiom without involving Shift — which
        // matters: a Shift+symbol chord like Meta+Shift+/ can NEVER fire
        // on Wayland because KWin strips Shift as a consumed modifier when
        // the keysym translation uses it (delivers Meta+Question). Any
        // future default here must avoid Shift+symbol spellings.
        return QStringLiteral("Meta+Alt+/");
    }
    static QString previousLayoutShortcut()
    {
        return QStringLiteral("Meta+Alt+[");
    }
    static QString nextLayoutShortcut()
    {
        return QStringLiteral("Meta+Alt+]");
    }
    static QString quickLayout1Shortcut()
    {
        return QStringLiteral("Meta+Alt+1");
    }
    static QString quickLayout2Shortcut()
    {
        return QStringLiteral("Meta+Alt+2");
    }
    static QString quickLayout3Shortcut()
    {
        return QStringLiteral("Meta+Alt+3");
    }
    static QString quickLayout4Shortcut()
    {
        return QStringLiteral("Meta+Alt+4");
    }
    static QString quickLayout5Shortcut()
    {
        return QStringLiteral("Meta+Alt+5");
    }
    static QString quickLayout6Shortcut()
    {
        return QStringLiteral("Meta+Alt+6");
    }
    static QString quickLayout7Shortcut()
    {
        return QStringLiteral("Meta+Alt+7");
    }
    static QString quickLayout8Shortcut()
    {
        return QStringLiteral("Meta+Alt+8");
    }
    static QString quickLayout9Shortcut()
    {
        return QStringLiteral("Meta+Alt+9");
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Navigation Shortcuts
    // ═══════════════════════════════════════════════════════════════════════════

    static QString moveWindowLeftShortcut()
    {
        return QStringLiteral("Meta+Alt+Shift+Left");
    }
    static QString moveWindowRightShortcut()
    {
        return QStringLiteral("Meta+Alt+Shift+Right");
    }
    static QString moveWindowUpShortcut()
    {
        return QStringLiteral("Meta+Alt+Shift+Up");
    }
    static QString moveWindowDownShortcut()
    {
        return QStringLiteral("Meta+Alt+Shift+Down");
    }
    static QString swapWindowLeftShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Alt+Left");
    }
    static QString swapWindowRightShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Alt+Right");
    }
    static QString swapWindowUpShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Alt+Up");
    }
    static QString swapWindowDownShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Alt+Down");
    }
    // Span (grow/shrink the zone span). Ctrl+Alt+Arrows: the only remaining
    // arrow chord free from both the stock Plasma 6 defaults (Meta+Arrows
    // quick tile, Meta+Shift screen move, Meta+Ctrl desktop switch,
    // Meta+Ctrl+Shift window-to-desktop, Meta+Alt KWin switch-window) and
    // our own directional families (moveWindow Meta+Alt+Shift, swapWindow
    // Meta+Ctrl+Alt, focusZone Alt+Shift). Non-Meta chords have precedent
    // in focusZone below.
    static QString spanWindowLeftShortcut()
    {
        return QStringLiteral("Ctrl+Alt+Left");
    }
    static QString spanWindowRightShortcut()
    {
        return QStringLiteral("Ctrl+Alt+Right");
    }
    static QString spanWindowUpShortcut()
    {
        return QStringLiteral("Ctrl+Alt+Up");
    }
    static QString spanWindowDownShortcut()
    {
        return QStringLiteral("Ctrl+Alt+Down");
    }
    static QString focusZoneLeftShortcut()
    {
        return QStringLiteral("Alt+Shift+Left");
    }
    static QString focusZoneRightShortcut()
    {
        return QStringLiteral("Alt+Shift+Right");
    }
    static QString focusZoneUpShortcut()
    {
        return QStringLiteral("Alt+Shift+Up");
    }
    static QString focusZoneDownShortcut()
    {
        return QStringLiteral("Alt+Shift+Down");
    }
    static QString pushToEmptyZoneShortcut()
    {
        return QStringLiteral("Meta+Alt+Return");
    }
    static QString restoreWindowSizeShortcut()
    {
        return QStringLiteral("Meta+Alt+Escape");
    }
    static QString toggleWindowFloatShortcut()
    {
        return QStringLiteral("Meta+F");
    }
    static QString snapToZone1Shortcut()
    {
        return QStringLiteral("Meta+Ctrl+1");
    }
    static QString snapToZone2Shortcut()
    {
        return QStringLiteral("Meta+Ctrl+2");
    }
    static QString snapToZone3Shortcut()
    {
        return QStringLiteral("Meta+Ctrl+3");
    }
    static QString snapToZone4Shortcut()
    {
        return QStringLiteral("Meta+Ctrl+4");
    }
    static QString snapToZone5Shortcut()
    {
        return QStringLiteral("Meta+Ctrl+5");
    }
    static QString snapToZone6Shortcut()
    {
        return QStringLiteral("Meta+Ctrl+6");
    }
    static QString snapToZone7Shortcut()
    {
        return QStringLiteral("Meta+Ctrl+7");
    }
    static QString snapToZone8Shortcut()
    {
        return QStringLiteral("Meta+Ctrl+8");
    }
    static QString snapToZone9Shortcut()
    {
        return QStringLiteral("Meta+Ctrl+9");
    }
    static QString rotateWindowsClockwiseShortcut()
    {
        return QStringLiteral("Meta+Ctrl+]");
    }
    static QString rotateWindowsCounterclockwiseShortcut()
    {
        return QStringLiteral("Meta+Ctrl+[");
    }
    static QString cycleWindowForwardShortcut()
    {
        return QStringLiteral("Meta+Alt+.");
    }
    static QString cycleWindowBackwardShortcut()
    {
        return QStringLiteral("Meta+Alt+,");
    }
    static QString resnapToNewLayoutShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Z");
    }
    static QString snapAllWindowsShortcut()
    {
        return QStringLiteral("Meta+Ctrl+S");
    }
    static QString layoutPickerShortcut()
    {
        return QStringLiteral("Meta+Alt+Space");
    }
    static QString toggleLayoutLockShortcut()
    {
        return QStringLiteral("Meta+Ctrl+L");
    }
    // ═══════════════════════════════════════════════════════════════════════════
    // Autotile Shortcuts
    // ═══════════════════════════════════════════════════════════════════════════

    static QString autotileToggleShortcut()
    {
        return QStringLiteral("Meta+Shift+T");
    }
    static QString autotileFocusMasterShortcut()
    {
        return QStringLiteral("Meta+Shift+M");
    }
    static QString autotileSwapMasterShortcut()
    {
        return QStringLiteral("Meta+Shift+Return");
    }
    static QString autotileIncMasterRatioShortcut()
    {
        return QStringLiteral("Meta+Shift+L");
    }
    static QString autotileDecMasterRatioShortcut()
    {
        return QStringLiteral("Meta+Shift+H");
    }
    static QString autotileIncMasterCountShortcut()
    {
        return QStringLiteral("Meta+Shift+]");
    }
    static QString autotileDecMasterCountShortcut()
    {
        return QStringLiteral("Meta+Shift+[");
    }
    static QString autotileRetileShortcut()
    {
        return QStringLiteral("Meta+Ctrl+R");
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // XDG Data Sub-directories
    // ═══════════════════════════════════════════════════════════════════════════
    //
    // Sub-paths under `QStandardPaths::GenericDataLocation` (typically
    // `~/.local/share`) for assets the settings app and daemon write at
    // runtime. Centralised here so a directory rename is a one-line edit.

    static QString userProfilesSubdir()
    {
        return QStringLiteral("/plasmazones/profiles");
    }
    static QString userAnimationsSubdir()
    {
        return QStringLiteral("/plasmazones/animations");
    }
    static QString userMotionSetsSubdir()
    {
        return QStringLiteral("/plasmazones/motionsets");
    }
    /// Snapping overlay shader packs (the `data/overlays/` family — wallpaper
    /// drift, neon-city, cosmic-flow, etc.). Mirrors the
    /// `userAnimationsSubdir()` convention so settings + daemon code share
    /// one source of truth for the on-disk location.
    static QString userOverlayShadersSubdir()
    {
        return QStringLiteral("/plasmazones/overlays");
    }

    /// Decoration sets — named snapshots of the decoration profile tree
    /// (the surface-pack chains + parameters across all surfaces), the
    /// decoration twin of `userMotionSetsSubdir()`.
    static QString userDecorationSetsSubdir()
    {
        return QStringLiteral("/plasmazones/decorationsets");
    }

    /// Surface shader packs (the `data/surface/` family — border, etc.).
    /// Mirrors the `userAnimationsSubdir()` convention so settings + daemon
    /// + compositor code share one source of truth for the on-disk location.
    static QString userSurfaceSubdir()
    {
        return QStringLiteral("/plasmazones/surface");
    }

private:
    // Non-instantiable
    ConfigDefaults() = delete;
};

// Compile-time bound checks for defaults that declare min/max accessors,
// so a future bump can never silently exceed the declared slider range.
// Mirrors the
// pattern in AnimationLimits.h where library defaults assert against
// their own min/max. Mirrored at runtime by QVERIFY in
// tests/unit/config/test_configdefaults.cpp; the compile-time form
// catches a bad default during build, the runtime form survives header
// changes that drop constexpr from any accessor.
static_assert(ConfigDefaults::animationDuration() >= ConfigDefaults::animationDurationMin()
                  && ConfigDefaults::animationDuration() <= ConfigDefaults::animationDurationMax(),
              "ConfigDefaults::animationDuration() outside declared [min, max] slider range");
static_assert(ConfigDefaults::focusFadeDuration() >= ConfigDefaults::focusFadeDurationMin()
                  && ConfigDefaults::focusFadeDuration() <= ConfigDefaults::focusFadeDurationMax(),
              "ConfigDefaults::focusFadeDuration() outside declared [min, max] slider range");
static_assert(ConfigDefaults::decorationIdleTimeoutSec() >= ConfigDefaults::decorationIdleTimeoutSecMin()
                  && ConfigDefaults::decorationIdleTimeoutSec() <= ConfigDefaults::decorationIdleTimeoutSecMax(),
              "ConfigDefaults::decorationIdleTimeoutSec() outside declared [min, max] slider range");
static_assert(ConfigDefaults::animationStaggerInterval() >= ConfigDefaults::animationStaggerIntervalMin()
                  && ConfigDefaults::animationStaggerInterval() <= ConfigDefaults::animationStaggerIntervalMax(),
              "ConfigDefaults::animationStaggerInterval() outside declared [min, max] slider range");
static_assert(ConfigDefaults::animationSequenceMode() >= ConfigDefaults::animationSequenceModeMin()
                  && ConfigDefaults::animationSequenceMode() <= ConfigDefaults::animationSequenceModeMax(),
              "ConfigDefaults::animationSequenceMode() outside declared [min, max] range");
// The autotile five. Every OTHER constexpr accessor that declares a [min, max] was checked
// here and these were not, for no reason anyone could name — the guard is free, and a
// default outside its own declared slider range is a bug the compiler can simply refuse.
// (The many non-constexpr accessors cannot be checked this way; test_configdefaults.cpp
// covers those at runtime.)
static_assert(ConfigDefaults::autotileInsertPosition() >= ConfigDefaults::autotileInsertPositionMin()
                  && ConfigDefaults::autotileInsertPosition() <= ConfigDefaults::autotileInsertPositionMax(),
              "ConfigDefaults::autotileInsertPosition() outside declared [min, max] range");
static_assert(ConfigDefaults::autotileMasterCount() >= ConfigDefaults::autotileMasterCountMin()
                  && ConfigDefaults::autotileMasterCount() <= ConfigDefaults::autotileMasterCountMax(),
              "ConfigDefaults::autotileMasterCount() outside declared [min, max] range");
static_assert(ConfigDefaults::autotileMaxWindows() >= ConfigDefaults::autotileMaxWindowsMin()
                  && ConfigDefaults::autotileMaxWindows() <= ConfigDefaults::autotileMaxWindowsMax(),
              "ConfigDefaults::autotileMaxWindows() outside declared [min, max] range");
static_assert(ConfigDefaults::autotileSplitRatio() >= ConfigDefaults::autotileSplitRatioMin()
                  && ConfigDefaults::autotileSplitRatio() <= ConfigDefaults::autotileSplitRatioMax(),
              "ConfigDefaults::autotileSplitRatio() outside declared [min, max] range");
static_assert(ConfigDefaults::autotileSplitRatioStep() >= ConfigDefaults::autotileSplitRatioStepMin()
                  && ConfigDefaults::autotileSplitRatioStep() <= ConfigDefaults::autotileSplitRatioStepMax(),
              "ConfigDefaults::autotileSplitRatioStep() outside declared [min, max] range");

} // namespace PlasmaZones
