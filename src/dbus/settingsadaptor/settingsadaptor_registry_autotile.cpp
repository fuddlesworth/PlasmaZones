// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The Autotile-family slice of SettingsAdaptor's getSetting/setSetting
// registry: the drag-insert triggers, the core autotile knobs and the autotile
// shortcuts. Split out of settingsadaptor_registry.cpp alongside the snapping
// and scrolling slices; initializeRegistry() calls initializeRegistryAutotile()
// so the same keys land in the same maps.
//
// BOUNDARY RULE for the four registry TUs: a key belongs to a mode TU when
// its name carries that mode's prefix or the zone family prefix — autotile*
// here, snap* / snapping* / zone* in settingsadaptor_registry_snapping.cpp,
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

void SettingsAdaptor::initializeRegistryAutotile()
{
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

#define REGISTER_CONCRETE_BOOL(name, getter, setter)                                                                   \
    m_getters[QStringLiteral(name)] = [concrete]() {                                                                   \
        return concrete->getter();                                                                                     \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [concrete](const QVariant& v) {                                                  \
        concrete->setter(v.toBool());                                                                                  \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(name)] = QStringLiteral("bool");
#define REGISTER_CONCRETE_INT(name, getter, setter)                                                                    \
    m_getters[QStringLiteral(name)] = [concrete]() {                                                                   \
        return concrete->getter();                                                                                     \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [concrete](const QVariant& v) {                                                  \
        concrete->setter(v.toInt());                                                                                   \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(name)] = QStringLiteral("int");
#define REGISTER_CONCRETE_DOUBLE(name, getter, setter)                                                                 \
    m_getters[QStringLiteral(name)] = [concrete]() {                                                                   \
        return concrete->getter();                                                                                     \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [concrete](const QVariant& v) {                                                  \
        concrete->setter(v.toDouble());                                                                                \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(name)] = QStringLiteral("double");
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

    // Autotile drag-insert triggers list (multi-bind). Consumers: the
    // daemon's own WindowDragAdaptor::beginDrag per-drag trigger cache is
    // the authoritative one (which combos count as "insert held" per tick),
    // and the KWin effect also fetches the list to OR a held insert trigger
    // into its tick-forwarding gate (detectActivationAndGrab).
    m_getters[QStringLiteral("autotileDragInsertTriggers")] = [this]() {
        return QVariant::fromValue(m_settings->autotileDragInsertTriggers());
    };
    m_setters[QStringLiteral("autotileDragInsertTriggers")] = [this](const QVariant& v) {
        m_settings->setAutotileDragInsertTriggers(v.toList());
        return true;
    };
    // "maplist" for the same reason as the scrolling twin: the payload is a
    // list of trigger maps, and "stringlist" invited a refused write.
    m_schemas[QStringLiteral("autotileDragInsertTriggers")] = QStringLiteral("maplist");

    REGISTER_BOOL_SETTING("autotileDragInsertToggle", autotileDragInsertToggle, setAutotileDragInsertToggle)
    REGISTER_INT_SETTING("autotileDragInsertGraceMs", autotileDragInsertGraceMs, setAutotileDragInsertGraceMs)
    REGISTER_BOOL_SETTING("autotileRestoreFloatedWindowsOnLogin", autotileRestoreFloatedWindowsOnLogin,
                          setAutotileRestoreFloatedWindowsOnLogin)
    // Effect-only consumer (reconcileRuleWindowLayer): keep autotile-floated
    // windows stacked above the tiles. Through the interface like its restore
    // twin, so a non-Settings backend keeps the key.
    REGISTER_BOOL_SETTING("autotileKeepFloatingAbove", autotileKeepFloatingAbove, setAutotileKeepFloatingAbove)
    REGISTER_BOOL_SETTING("autotileFocusFollowsMouse", autotileFocusFollowsMouse, setAutotileFocusFollowsMouse)
    // The three enum knobs get validated setters (closed ranges), same shape
    // as the concrete enum setters below.
    m_getters[QStringLiteral("autotileStickyWindowHandling")] = [this]() {
        return static_cast<int>(m_settings->autotileStickyWindowHandling());
    };
    m_setters[QStringLiteral("autotileStickyWindowHandling")] = [this](const QVariant& v) {
        int val = v.toInt();
        if (val >= static_cast<int>(StickyWindowHandling::TreatAsNormal)
            && val <= static_cast<int>(StickyWindowHandling::IgnoreAll)) {
            m_settings->setAutotileStickyWindowHandling(static_cast<StickyWindowHandling>(val));
            return true;
        }
        return false;
    };
    m_schemas[QStringLiteral("autotileStickyWindowHandling")] = QStringLiteral("int");
    m_getters[QStringLiteral("autotileDragBehavior")] = [this]() {
        return static_cast<int>(m_settings->autotileDragBehavior());
    };
    m_setters[QStringLiteral("autotileDragBehavior")] = [this](const QVariant& v) {
        int val = v.toInt();
        if (val >= static_cast<int>(AutotileDragBehavior::Float)
            && val <= static_cast<int>(AutotileDragBehavior::Reorder)) {
            m_settings->setAutotileDragBehavior(static_cast<AutotileDragBehavior>(val));
            return true;
        }
        return false;
    };
    m_schemas[QStringLiteral("autotileDragBehavior")] = QStringLiteral("int");
    m_getters[QStringLiteral("autotileOverflowBehavior")] = [this]() {
        return static_cast<int>(m_settings->autotileOverflowBehavior());
    };
    m_setters[QStringLiteral("autotileOverflowBehavior")] = [this](const QVariant& v) {
        int val = v.toInt();
        if (val >= static_cast<int>(AutotileOverflowBehavior::Float)
            && val <= static_cast<int>(AutotileOverflowBehavior::Unlimited)) {
            m_settings->setAutotileOverflowBehavior(static_cast<AutotileOverflowBehavior>(val));
            return true;
        }
        return false;
    };
    m_schemas[QStringLiteral("autotileOverflowBehavior")] = QStringLiteral("int");

    // Autotile core settings (concrete Settings only)
    if (concrete) {
        REGISTER_CONCRETE_BOOL("autotileEnabled", autotileEnabled, setAutotileEnabled)
        REGISTER_CONCRETE_STRING("defaultAutotileAlgorithm", defaultAutotileAlgorithm, setDefaultAutotileAlgorithm)
        REGISTER_CONCRETE_DOUBLE("autotileSplitRatio", autotileSplitRatio, setAutotileSplitRatio)
        REGISTER_CONCRETE_INT("autotileMasterCount", autotileMasterCount, setAutotileMasterCount)
        // Per-algorithm settings map (QVariantMap)
        m_getters[QStringLiteral("autotilePerAlgorithmSettings")] = [concrete]() {
            return QVariant::fromValue(concrete->autotilePerAlgorithmSettings());
        };
        m_setters[QStringLiteral("autotilePerAlgorithmSettings")] = [concrete](const QVariant& v) {
            concrete->setAutotilePerAlgorithmSettings(v.toMap());
            return true;
        };
        m_schemas[QStringLiteral("autotilePerAlgorithmSettings")] = QStringLiteral("map");
        REGISTER_CONCRETE_BOOL("autotileFocusNewWindows", autotileFocusNewWindows, setAutotileFocusNewWindows)
        REGISTER_CONCRETE_BOOL("autotileSmartGaps", autotileSmartGaps, setAutotileSmartGaps)
        REGISTER_CONCRETE_INT("autotileMaxWindows", autotileMaxWindows, setAutotileMaxWindows)
        REGISTER_CONCRETE_BOOL("autotileRespectMinimumSize", autotileRespectMinimumSize, setAutotileRespectMinimumSize)
        // autotileInsertPosition: enum (0=End, 1=AfterFocused, 2=AsMaster) — needs range validation
        m_getters[QStringLiteral("autotileInsertPosition")] = [concrete]() {
            return static_cast<int>(concrete->autotileInsertPosition());
        };
        m_setters[QStringLiteral("autotileInsertPosition")] = [concrete](const QVariant& v) {
            int val = v.toInt();
            if (val >= static_cast<int>(AutotileInsertPosition::End)
                && val <= static_cast<int>(AutotileInsertPosition::AsMaster)) {
                concrete->setAutotileInsertPosition(static_cast<Settings::AutotileInsertPosition>(val));
                return true;
            }
            return false;
        };
        m_schemas[QStringLiteral("autotileInsertPosition")] = QStringLiteral("int");
    }

    // Autotile shortcuts (concrete Settings only)
    if (concrete) {
        REGISTER_CONCRETE_STRING("autotileToggleShortcut", autotileToggleShortcut, setAutotileToggleShortcut)
        REGISTER_CONCRETE_STRING("autotileFocusMasterShortcut", autotileFocusMasterShortcut,
                                 setAutotileFocusMasterShortcut)
        REGISTER_CONCRETE_STRING("autotileSwapMasterShortcut", autotileSwapMasterShortcut,
                                 setAutotileSwapMasterShortcut)
        REGISTER_CONCRETE_STRING("autotileIncMasterRatioShortcut", autotileIncMasterRatioShortcut,
                                 setAutotileIncMasterRatioShortcut)
        REGISTER_CONCRETE_STRING("autotileDecMasterRatioShortcut", autotileDecMasterRatioShortcut,
                                 setAutotileDecMasterRatioShortcut)
        REGISTER_CONCRETE_STRING("autotileIncMasterCountShortcut", autotileIncMasterCountShortcut,
                                 setAutotileIncMasterCountShortcut)
        REGISTER_CONCRETE_STRING("autotileDecMasterCountShortcut", autotileDecMasterCountShortcut,
                                 setAutotileDecMasterCountShortcut)
        REGISTER_CONCRETE_STRING("autotileRetileShortcut", autotileRetileShortcut, setAutotileRetileShortcut)
    }

// Clean up macros (local scope; unity-batch hygiene)
#undef REGISTER_BOOL_SETTING
#undef REGISTER_INT_SETTING
#undef REGISTER_CONCRETE_BOOL
#undef REGISTER_CONCRETE_INT
#undef REGISTER_CONCRETE_DOUBLE
#undef REGISTER_CONCRETE_STRING
}

} // namespace PlasmaZones
