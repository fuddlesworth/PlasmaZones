// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The Snapping-family slice of SettingsAdaptor's getSetting/setSetting
// registry: zone span, zone geometry thresholds, snapping behavior, snap
// assist, default layout / layout filtering, the zone selector, and the
// snap-to-zone-by-number shortcuts. Split out of
// settingsadaptor_registry.cpp alongside the autotile and scrolling slices;
// initializeRegistry() calls initializeRegistrySnapping() so the same keys
// land in the same maps.
//
// BOUNDARY RULE for the four registry TUs: a key belongs to a mode TU when
// its name carries that mode's prefix or the zone family prefix — snap* /
// snapping* / zone* here, autotile* in settingsadaptor_registry_autotile.cpp,
// scrolling* in settingsadaptor_registry_scrolling.cpp. Mode-neutral keys
// (drag activation, navigation / swap / span / quickLayout / cycle / rotate /
// global shortcuts, display, appearance, filtering, animation and profile
// trees, cava, gpu, and the per-mode disable lists, which are one shared
// three-mode mechanism) stay in settingsadaptor_registry.cpp.
//
// The REGISTER_* macros are duplicated from settingsadaptor_registry.cpp on
// purpose: every registry TU defines them inside its registration function and
// #undefs them at the end, which keeps them out of every other TU in a unity
// batch — a shared macro header with an include guard would define them only
// once per batch and leave the later TUs without them. Each TU defines only
// the forms its own keys use, so the set differs between them. KEEP THE
// SHARED FORMS IN SYNC across the four registry TUs (they are frozen
// boilerplate; a change to one spelling must land in every TU that carries
// that form, in the same commit).

#include "settingsadaptor.h"
#include "core/interfaces/interfaces.h"
#include "config/settings.h" // For concrete Settings type

namespace PlasmaZones {

void SettingsAdaptor::initializeRegistrySnapping()
{
#define REGISTER_STRING_SETTING(name, getter, setter)                                                                  \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        m_settings->setter(v.toString());                                                                              \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(name)] = QStringLiteral("string");

#define REGISTER_BOOL_SETTING(name, getter, setter)                                                                    \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        m_settings->setter(v.toBool());                                                                                \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(name)] = QStringLiteral("bool");

#define REGISTER_INT_SETTING(name, getter, setter)                                                                     \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        m_settings->setter(v.toInt());                                                                                 \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(name)] = QStringLiteral("int");

#define REGISTER_CONCRETE_STRING(name, getter, setter)                                                                 \
    m_getters[QStringLiteral(name)] = [concrete]() {                                                                   \
        return concrete->getter();                                                                                     \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [concrete](const QVariant& v) {                                                  \
        concrete->setter(v.toString());                                                                                \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(name)] = QStringLiteral("string");

    auto* concrete = qobject_cast<Settings*>(m_settings);

    REGISTER_BOOL_SETTING("zoneSpanEnabled", zoneSpanEnabled, setZoneSpanEnabled)

    // PhosphorZones::Zone span modifier (legacy single value)
    m_getters[QStringLiteral("zoneSpanModifier")] = [this]() {
        return static_cast<int>(m_settings->zoneSpanModifier());
    };
    m_setters[QStringLiteral("zoneSpanModifier")] = [this](const QVariant& v) {
        bool modOk = false;
        const int mod = v.toInt(&modOk);
        if (!modOk) {
            return false; // non-numeric payload: refuse, do not coerce to the zero member
        }
        if (mod >= 0 && mod <= static_cast<int>(DragModifier::CtrlAltMeta)) {
            m_settings->setZoneSpanModifier(static_cast<DragModifier>(mod));
            return true;
        }
        return false;
    };
    m_schemas[QStringLiteral("zoneSpanModifier")] = QStringLiteral("int");

    // PhosphorZones::Zone span triggers list (multi-bind)
    m_getters[QStringLiteral("zoneSpanTriggers")] = [this]() {
        return QVariant::fromValue(m_settings->zoneSpanTriggers());
    };
    m_setters[QStringLiteral("zoneSpanTriggers")] = [this](const QVariant& v) {
        m_settings->setZoneSpanTriggers(v.toList());
        return true;
    };
    m_schemas[QStringLiteral("zoneSpanTriggers")] = QStringLiteral("stringlist");

    REGISTER_BOOL_SETTING("zoneSpanToggleMode", zoneSpanToggleMode, setZoneSpanToggleMode)
    REGISTER_BOOL_SETTING("snappingEnabled", snappingEnabled, setSnappingEnabled)

    // Zone settings. The shared inner/outer gaps have NO SETTERS here: they are
    // config-backed (the Gaps group) and consumed daemon-side by the geometry
    // cascade, so nothing writes them over this generic map. Read-only getters
    // and their schemas are registered in settingsadaptor_registry.cpp for the
    // effect's benefit.
    REGISTER_INT_SETTING("adjacentThreshold", adjacentThreshold, setAdjacentThreshold)
    REGISTER_INT_SETTING("pollIntervalMs", pollIntervalMs, setPollIntervalMs)
    REGISTER_INT_SETTING("minimumZoneSizePx", minimumZoneSizePx, setMinimumZoneSizePx)
    REGISTER_INT_SETTING("minimumZoneDisplaySizePx", minimumZoneDisplaySizePx, setMinimumZoneDisplaySizePx)

    // Behavior settings
    REGISTER_BOOL_SETTING("keepWindowsInZonesOnResolutionChange", keepWindowsInZonesOnResolutionChange,
                          setKeepWindowsInZonesOnResolutionChange)
    REGISTER_BOOL_SETTING("moveNewWindowsToLastZone", moveNewWindowsToLastZone, setMoveNewWindowsToLastZone)
    REGISTER_BOOL_SETTING("restoreOriginalSizeOnUnsnap", restoreOriginalSizeOnUnsnap, setRestoreOriginalSizeOnUnsnap)
    // snappingStickyWindowHandling: enum (0=TreatAsNormal, 1=RestoreOnly, 2=IgnoreAll)
    m_getters[QStringLiteral("snappingStickyWindowHandling")] = [this]() {
        return static_cast<int>(m_settings->snappingStickyWindowHandling());
    };
    m_setters[QStringLiteral("snappingStickyWindowHandling")] = [this](const QVariant& v) {
        bool valOk = false;
        const int val = v.toInt(&valOk);
        if (!valOk) {
            return false; // non-numeric payload: refuse, do not coerce to the zero member
        }
        if (val >= static_cast<int>(StickyWindowHandling::TreatAsNormal)
            && val <= static_cast<int>(StickyWindowHandling::IgnoreAll)) {
            m_settings->setSnappingStickyWindowHandling(static_cast<StickyWindowHandling>(val));
            return true;
        }
        return false;
    };
    m_schemas[QStringLiteral("snappingStickyWindowHandling")] = QStringLiteral("int");
    REGISTER_BOOL_SETTING("restoreWindowsToZonesOnLogin", restoreWindowsToZonesOnLogin, setRestoreWindowsToZonesOnLogin)
    REGISTER_BOOL_SETTING("snappingRestoreFloatedWindowsOnLogin", snappingRestoreFloatedWindowsOnLogin,
                          setSnappingRestoreFloatedWindowsOnLogin)
    // Effect-only consumer (reconcileRuleWindowLayer): keep snap-floated
    // windows stacked above the zones. Through the interface like its restore
    // twin, so a non-Settings backend keeps the key.
    REGISTER_BOOL_SETTING("snappingKeepFloatingAbove", snappingKeepFloatingAbove, setSnappingKeepFloatingAbove)
    REGISTER_BOOL_SETTING("snapUnfloatFallbackToZone", snapUnfloatFallbackToZone, setSnapUnfloatFallbackToZone)
    REGISTER_BOOL_SETTING("autoAssignAllLayouts", autoAssignAllLayouts, setAutoAssignAllLayouts)
    REGISTER_BOOL_SETTING("suppressDefaultLayoutAssignment", suppressDefaultLayoutAssignment,
                          setSuppressDefaultLayoutAssignment)
    REGISTER_BOOL_SETTING("snapAssistFeatureEnabled", snapAssistFeatureEnabled, setSnapAssistFeatureEnabled)
    REGISTER_BOOL_SETTING("snapAssistEnabled", snapAssistEnabled, setSnapAssistEnabled)
    REGISTER_BOOL_SETTING("snappingFocusNewWindows", snappingFocusNewWindows, setSnappingFocusNewWindows)
    REGISTER_BOOL_SETTING("snappingFocusFollowsMouse", snappingFocusFollowsMouse, setSnappingFocusFollowsMouse)

    // Snap assist triggers (when always-enabled is off, hold any trigger at drop to enable)
    m_getters[QStringLiteral("snapAssistTriggers")] = [this]() {
        return QVariant::fromValue(m_settings->snapAssistTriggers());
    };
    m_setters[QStringLiteral("snapAssistTriggers")] = [this](const QVariant& v) {
        m_settings->setSnapAssistTriggers(v.toList());
        return true;
    };
    m_schemas[QStringLiteral("snapAssistTriggers")] = QStringLiteral("stringlist");

    // Default layout
    REGISTER_STRING_SETTING("defaultLayoutId", defaultLayoutId, setDefaultLayoutId)

    // Layout filtering
    REGISTER_BOOL_SETTING("filterLayoutsByAspectRatio", filterLayoutsByAspectRatio, setFilterLayoutsByAspectRatio)

    // PhosphorZones::Zone selector settings
    REGISTER_BOOL_SETTING("zoneSelectorEnabled", zoneSelectorEnabled, setZoneSelectorEnabled)
    REGISTER_INT_SETTING("zoneSelectorTriggerDistance", zoneSelectorTriggerDistance, setZoneSelectorTriggerDistance)
    // zoneSelectorPosition: enum (0=TopLeft .. 8=BottomRight)
    m_getters[QStringLiteral("zoneSelectorPosition")] = [this]() {
        return static_cast<int>(m_settings->zoneSelectorPosition());
    };
    m_setters[QStringLiteral("zoneSelectorPosition")] = [this](const QVariant& v) {
        bool valOk = false;
        const int val = v.toInt(&valOk);
        if (!valOk) {
            return false; // non-numeric payload: refuse, do not coerce to the zero member
        }
        if (val >= static_cast<int>(ZoneSelectorPosition::TopLeft)
            && val <= static_cast<int>(ZoneSelectorPosition::BottomRight)) {
            m_settings->setZoneSelectorPosition(static_cast<ZoneSelectorPosition>(val));
            return true;
        }
        return false;
    };
    m_schemas[QStringLiteral("zoneSelectorPosition")] = QStringLiteral("int");
    // zoneSelectorLayoutMode: enum (0=Grid, 1=Horizontal, 2=Vertical)
    m_getters[QStringLiteral("zoneSelectorLayoutMode")] = [this]() {
        return static_cast<int>(m_settings->zoneSelectorLayoutMode());
    };
    m_setters[QStringLiteral("zoneSelectorLayoutMode")] = [this](const QVariant& v) {
        bool valOk = false;
        const int val = v.toInt(&valOk);
        if (!valOk) {
            return false; // non-numeric payload: refuse, do not coerce to the zero member
        }
        if (val >= static_cast<int>(ZoneSelectorLayoutMode::Grid)
            && val <= static_cast<int>(ZoneSelectorLayoutMode::Vertical)) {
            m_settings->setZoneSelectorLayoutMode(static_cast<ZoneSelectorLayoutMode>(val));
            return true;
        }
        return false;
    };
    m_schemas[QStringLiteral("zoneSelectorLayoutMode")] = QStringLiteral("int");
    // zoneSelectorSizeMode: enum (0=Auto, 1=Manual)
    m_getters[QStringLiteral("zoneSelectorSizeMode")] = [this]() {
        return static_cast<int>(m_settings->zoneSelectorSizeMode());
    };
    m_setters[QStringLiteral("zoneSelectorSizeMode")] = [this](const QVariant& v) {
        bool valOk = false;
        const int val = v.toInt(&valOk);
        if (!valOk) {
            return false; // non-numeric payload: refuse, do not coerce to the zero member
        }
        if (val >= static_cast<int>(ZoneSelectorSizeMode::Auto)
            && val <= static_cast<int>(ZoneSelectorSizeMode::Manual)) {
            m_settings->setZoneSelectorSizeMode(static_cast<ZoneSelectorSizeMode>(val));
            return true;
        }
        return false;
    };
    m_schemas[QStringLiteral("zoneSelectorSizeMode")] = QStringLiteral("int");
    REGISTER_INT_SETTING("zoneSelectorMaxRows", zoneSelectorMaxRows, setZoneSelectorMaxRows)
    REGISTER_INT_SETTING("zoneSelectorPreviewWidth", zoneSelectorPreviewWidth, setZoneSelectorPreviewWidth)
    REGISTER_INT_SETTING("zoneSelectorPreviewHeight", zoneSelectorPreviewHeight, setZoneSelectorPreviewHeight)
    REGISTER_BOOL_SETTING("zoneSelectorPreviewLockAspect", zoneSelectorPreviewLockAspect,
                          setZoneSelectorPreviewLockAspect)
    REGISTER_INT_SETTING("zoneSelectorGridColumns", zoneSelectorGridColumns, setZoneSelectorGridColumns)

    // Snapping shortcuts (concrete Settings only)
    if (concrete) {
        // Snap to zone by number shortcuts
        REGISTER_CONCRETE_STRING("snapToZone1Shortcut", snapToZone1Shortcut, setSnapToZone1Shortcut)
        REGISTER_CONCRETE_STRING("snapToZone2Shortcut", snapToZone2Shortcut, setSnapToZone2Shortcut)
        REGISTER_CONCRETE_STRING("snapToZone3Shortcut", snapToZone3Shortcut, setSnapToZone3Shortcut)
        REGISTER_CONCRETE_STRING("snapToZone4Shortcut", snapToZone4Shortcut, setSnapToZone4Shortcut)
        REGISTER_CONCRETE_STRING("snapToZone5Shortcut", snapToZone5Shortcut, setSnapToZone5Shortcut)
        REGISTER_CONCRETE_STRING("snapToZone6Shortcut", snapToZone6Shortcut, setSnapToZone6Shortcut)
        REGISTER_CONCRETE_STRING("snapToZone7Shortcut", snapToZone7Shortcut, setSnapToZone7Shortcut)
        REGISTER_CONCRETE_STRING("snapToZone8Shortcut", snapToZone8Shortcut, setSnapToZone8Shortcut)
        REGISTER_CONCRETE_STRING("snapToZone9Shortcut", snapToZone9Shortcut, setSnapToZone9Shortcut)

        REGISTER_CONCRETE_STRING("resnapToNewLayoutShortcut", resnapToNewLayoutShortcut, setResnapToNewLayoutShortcut)
        REGISTER_CONCRETE_STRING("snapAllWindowsShortcut", snapAllWindowsShortcut, setSnapAllWindowsShortcut)
    }

// Clean up macros (local scope; unity-batch hygiene)
#undef REGISTER_STRING_SETTING
#undef REGISTER_BOOL_SETTING
#undef REGISTER_INT_SETTING
#undef REGISTER_CONCRETE_STRING
}

} // namespace PlasmaZones
