// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The Scrolling-family slice of SettingsAdaptor's getSetting/setSetting
// registry: every scrolling key on the generic wire (drag insert, core, tab
// indicator, drop indicator, behavior, shortcuts). Split out of
// settingsadaptor_registry.cpp alongside the snapping and autotile slices;
// initializeRegistry() calls initializeRegistryScrolling() so the same keys
// land in the same maps.
//
// BOUNDARY RULE for the four registry TUs: a key belongs to a mode TU when
// its name carries that mode's prefix, names that mode's family (e.g.
// defaultScrollingTemplate), or carries the zone family prefix — scrolling*
// here, snap* / snapping* / zone* in settingsadaptor_registry_snapping.cpp,
// autotile* in settingsadaptor_registry_autotile.cpp. Mode-neutral keys
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
#include "config/settings.h" // For concrete Settings type
#include "config/configdefaults.h" // ConfigDefaults::isValidScrolling* validators
#include <QColor>
#include <QUuid> // defaultScrollingTemplate id-format validation

namespace PlasmaZones {

void SettingsAdaptor::initializeRegistryScrolling()
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

#define REGISTER_DOUBLE_SETTING(name, getter, setter)                                                                  \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        m_settings->setter(v.toDouble());                                                                              \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(name)] = QStringLiteral("double");

// ISettings-flavoured theme-fallback colour: EMPTY (follow the theme), the
// legacy "accent" token (aliased to empty so every themeColor-typed key
// accepts one uniform vocabulary — the frozen PhosphorRules::
// BorderColorToken::Accent spelling, mirrored from the core TU's
// kAccentToken), or a colour QColor can parse; anything else is refused at
// the boundary so it never reaches a QML `color` property, and the false
// return surfaces the rejection to the D-Bus caller instead of silently
// dropping the write. Schema token "themeColor" (shared with the *Raw
// companion keys) tells a schema-aware client the empty value is the
// follow-the-scheme sentinel, not a missing string.
#define REGISTER_THEME_FALLBACK_COLOR_SETTING(name, getter, setter)                                                    \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        QString s = v.toString();                                                                                      \
        if (s == QLatin1String("accent")) {                                                                            \
            s = QString();                                                                                             \
        }                                                                                                              \
        if (!s.isEmpty() && !QColor(s).isValid()) {                                                                    \
            return false;                                                                                              \
        }                                                                                                              \
        m_settings->setter(s);                                                                                         \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(name)] = QStringLiteral("themeColor");

// The resolved half of a theme-fallback pair whose sentinel is resolved in the
// config layer: the getter serves a concrete #AARRGGBB and the setter pins one,
// with empty (or the legacy accent token) storing the sentinel through the
// QColor setter's invalid-means-unset guard. The resolution chain cannot
// produce an invalid QColor, so no isValid() guard is needed before name().
// Copied from the core TU's REGISTER_COLOR_SETTING per the sync rule above,
// with the accent token spelled inline: kAccentToken is that TU's file-local
// constant and a second definition here would collide in a unity batch.
#define REGISTER_COLOR_SETTING(keyName, getter, setter)                                                                \
    m_getters[QStringLiteral(keyName)] = [this]() {                                                                    \
        QColor color = m_settings->getter();                                                                           \
        return color.name(QColor::HexArgb);                                                                            \
    };                                                                                                                 \
    m_setters[QStringLiteral(keyName)] = [this](const QVariant& v) {                                                   \
        const QString s = v.toString();                                                                                \
        if (s.isEmpty() || s == QLatin1String("accent")) {                                                             \
            m_settings->setter(QColor());                                                                              \
            return true;                                                                                               \
        }                                                                                                              \
        const QColor parsed(s);                                                                                        \
        if (!parsed.isValid()) {                                                                                       \
            return false;                                                                                              \
        }                                                                                                              \
        m_settings->setter(parsed);                                                                                    \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(keyName)] = QStringLiteral("color");

// The stored half of that pair, on the concrete Settings: the sentinel itself,
// readable and writable so a round-trip can restore "follow the theme". Same
// body as the core TU's REGISTER_RAW_THEME_COLOR, accent token inlined for the
// reason above.
#define REGISTER_RAW_THEME_COLOR(keyName, capture, obj, getter, setter)                                                \
    m_getters[QStringLiteral(keyName)] = [capture]() {                                                                 \
        return obj->getter();                                                                                          \
    };                                                                                                                 \
    m_setters[QStringLiteral(keyName)] = [capture](const QVariant& v) {                                                \
        QString s = v.toString();                                                                                      \
        if (s == QLatin1String("accent")) {                                                                            \
            s = QString();                                                                                             \
        }                                                                                                              \
        if (!s.isEmpty() && !QColor(s).isValid()) {                                                                    \
            return false;                                                                                              \
        }                                                                                                              \
        obj->setter(s);                                                                                                \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(keyName)] = QStringLiteral("themeColor");

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

    // ── ISettings-level scrolling keys ──
    // Scrolling twin — same consumer contract (daemon beginDrag cache +
    // the effect's tick-forwarding gate).
    m_getters[QStringLiteral("scrollingDragInsertTriggers")] = [this]() {
        return QVariant::fromValue(m_settings->scrollingDragInsertTriggers());
    };
    m_setters[QStringLiteral("scrollingDragInsertTriggers")] = [this](const QVariant& v) {
        m_settings->setScrollingDragInsertTriggers(v.toList());
        return true;
    };
    m_schemas[QStringLiteral("scrollingDragInsertTriggers")] = QStringLiteral("stringlist");

    REGISTER_BOOL_SETTING("scrollingDragInsertToggle", scrollingDragInsertToggle, setScrollingDragInsertToggle)

    // The scrolling twin of the snap / autotile restore-floated pair, plus the
    // tab-strip toggle. All three are ISettings virtuals with defaults, so they
    // register through the interface like their snap / autotile counterparts
    // rather than inside the concrete-Settings block below, where a
    // non-Settings backend would lose the keys entirely.
    REGISTER_BOOL_SETTING("scrollingRestoreFloatedWindowsOnLogin", scrollingRestoreFloatedWindowsOnLogin,
                          setScrollingRestoreFloatedWindowsOnLogin)
    REGISTER_BOOL_SETTING("scrollingTabIndicatorEnabled", scrollingTabIndicatorEnabled, setScrollingTabIndicatorEnabled)
    REGISTER_BOOL_SETTING("scrollingDropIndicatorEnabled", scrollingDropIndicatorEnabled,
                          setScrollingDropIndicatorEnabled)

    // The PAINT keys of both indicator families register through the
    // interface too — isettings.h declares getter+no-op-setter virtual pairs
    // for exactly these eleven so a non-Settings backend keeps them (its
    // stated rationale); only the tab indicator's GEOMETRY keys are
    // deliberately concrete-only (isettings.h documents that exclusion).
    // The Style enum keeps its validated hand registration, via the
    // interface.
    m_getters[QStringLiteral("scrollingTabIndicatorStyle")] = [this]() {
        return m_settings->scrollingTabIndicatorStyle();
    };
    m_setters[QStringLiteral("scrollingTabIndicatorStyle")] = [this](const QVariant& v) {
        const int style = v.toInt();
        if (!ConfigDefaults::isValidScrollingTabIndicatorStyle(style)) {
            return false;
        }
        m_settings->setScrollingTabIndicatorStyle(style);
        return true;
    };
    m_schemas[QStringLiteral("scrollingTabIndicatorStyle")] = QStringLiteral("int");
    REGISTER_INT_SETTING("scrollingTabIndicatorGapsBetweenTabs", scrollingTabIndicatorGapsBetweenTabs,
                         setScrollingTabIndicatorGapsBetweenTabs)
    REGISTER_INT_SETTING("scrollingTabIndicatorCornerRadius", scrollingTabIndicatorCornerRadius,
                         setScrollingTabIndicatorCornerRadius)
    // Colours are strings, not REGISTER_COLOR_SETTING: EMPTY is the
    // meaningful "follow the theme" value and a QColor round-trip cannot
    // carry it. They are still validated at this boundary rather than
    // passed through — the string lands on a QML `color` property, where an
    // unparseable value renders as an invalid colour instead of falling
    // back to the theme, so an arbitrary D-Bus write must not reach it.
    REGISTER_THEME_FALLBACK_COLOR_SETTING("scrollingTabIndicatorActiveColor", scrollingTabIndicatorActiveColor,
                                          setScrollingTabIndicatorActiveColor)
    REGISTER_THEME_FALLBACK_COLOR_SETTING("scrollingTabIndicatorInactiveColor", scrollingTabIndicatorInactiveColor,
                                          setScrollingTabIndicatorInactiveColor)
    REGISTER_THEME_FALLBACK_COLOR_SETTING("scrollingTabIndicatorUrgentColor", scrollingTabIndicatorUrgentColor,
                                          setScrollingTabIndicatorUrgentColor)
    // The drop indicator's two colours resolve in the config layer instead
    // (Settings::resolvedSystemColor), so they take the zone quartet's shape:
    // the plain key carries the RESOLVED colour and the "Raw" companion carries
    // the stored sentinel. A round-trip through a QVariantMap stays lossless
    // because keys are applied in sorted order, so "<key>Raw" lands after
    // "<key>" and the sentinel wins. (That holds ON A CONCRETE Settings
    // backend: the two Raw keys register inside the `concrete` guard below, so
    // on anything else the sentinel does not survive a round-trip — the
    // interface's own setter bodies are empty, so a literally-bare ISettings
    // drops the write rather than storing it. Every production composition
    // root passes the concrete Settings.)
    REGISTER_COLOR_SETTING("scrollingDropIndicatorColor", scrollingDropIndicatorColor, setScrollingDropIndicatorColor)
    REGISTER_COLOR_SETTING("scrollingDropIndicatorBorderColor", scrollingDropIndicatorBorderColor,
                           setScrollingDropIndicatorBorderColor)
    if (concrete) {
        REGISTER_RAW_THEME_COLOR("scrollingDropIndicatorColorRaw", concrete, concrete, scrollingDropIndicatorColorRaw,
                                 setScrollingDropIndicatorColorRaw)
        REGISTER_RAW_THEME_COLOR("scrollingDropIndicatorBorderColorRaw", concrete, concrete,
                                 scrollingDropIndicatorBorderColorRaw, setScrollingDropIndicatorBorderColorRaw)
    }
    REGISTER_DOUBLE_SETTING("scrollingDropIndicatorOpacity", scrollingDropIndicatorOpacity,
                            setScrollingDropIndicatorOpacity)
    REGISTER_INT_SETTING("scrollingDropIndicatorBorderWidth", scrollingDropIndicatorBorderWidth,
                         setScrollingDropIndicatorBorderWidth)
    REGISTER_INT_SETTING("scrollingDropIndicatorBorderRadius", scrollingDropIndicatorBorderRadius,
                         setScrollingDropIndicatorBorderRadius)

    // Scrolling settings (concrete Settings only)
    if (concrete) {
        REGISTER_CONCRETE_BOOL("scrollingEnabled", scrollingEnabled, setScrollingEnabled)
        // defaultScrollingTemplate holds a ScrollingTemplate UUID (empty means
        // "no configured default"). Refuse anything that is not one, the same
        // way the enum setters below refuse an out-of-range int, so a string
        // that could never name a template does not reach config.json.
        //
        // Existence against the template store is NOT checked here and cannot
        // be: this adaptor holds only ISettings, no store. A well-formed id
        // naming a deleted template is therefore accepted and stays inert —
        // LayoutRegistry::scrollingTemplateForContext parses the configured
        // default, finds no template for it, and the context falls back to the
        // engine's compiled defaults. Template deletion deliberately leaves
        // this key alone (see deleteScrollingTemplate's contract), so the
        // dangling-id case is expected rather than exceptional.
        m_getters[QStringLiteral("defaultScrollingTemplate")] = [concrete]() {
            return concrete->defaultScrollingTemplate();
        };
        m_setters[QStringLiteral("defaultScrollingTemplate")] = [concrete](const QVariant& v) {
            const QString id = v.toString();
            if (!id.isEmpty() && QUuid::fromString(id).isNull()) {
                return false;
            }
            concrete->setDefaultScrollingTemplate(id);
            return true;
        };
        m_schemas[QStringLiteral("defaultScrollingTemplate")] = QStringLiteral("string");
        // scrollingCenterFocusedColumn: enum (0=Never, 1=Always, 2=OnOverflow) — needs range validation
        m_getters[QStringLiteral("scrollingCenterFocusedColumn")] = [concrete]() {
            return concrete->scrollingCenterFocusedColumn();
        };
        m_setters[QStringLiteral("scrollingCenterFocusedColumn")] = [concrete](const QVariant& v) {
            const int mode = v.toInt();
            if (!ConfigDefaults::isValidScrollingCenterFocusedColumn(mode)) {
                return false;
            }
            concrete->setScrollingCenterFocusedColumn(mode);
            return true;
        };
        m_schemas[QStringLiteral("scrollingCenterFocusedColumn")] = QStringLiteral("int");
        REGISTER_CONCRETE_BOOL("scrollingAlwaysCenterSingleColumn", scrollingAlwaysCenterSingleColumn,
                               setScrollingAlwaysCenterSingleColumn)
        REGISTER_CONCRETE_BOOL("scrollingCropStraddlers", scrollingCropStraddlers, setScrollingCropStraddlers)
        // scrollingDefaultColumnWidthKind: enum (0=Proportion, 1=Fixed, 2=ClientDecides, 3=Preset)
        m_getters[QStringLiteral("scrollingDefaultColumnWidthKind")] = [concrete]() {
            return concrete->scrollingDefaultColumnWidthKind();
        };
        m_setters[QStringLiteral("scrollingDefaultColumnWidthKind")] = [concrete](const QVariant& v) {
            const int kind = v.toInt();
            if (!ConfigDefaults::isValidScrollingWidthKind(kind)) {
                return false;
            }
            concrete->setScrollingDefaultColumnWidthKind(kind);
            return true;
        };
        m_schemas[QStringLiteral("scrollingDefaultColumnWidthKind")] = QStringLiteral("int");
        REGISTER_CONCRETE_DOUBLE("scrollingDefaultColumnWidthValue", scrollingDefaultColumnWidthValue,
                                 setScrollingDefaultColumnWidthValue)
        // scrollingDefaultColumnDisplay: enum (0=Normal, 1=Tabbed)
        m_getters[QStringLiteral("scrollingDefaultColumnDisplay")] = [concrete]() {
            return concrete->scrollingDefaultColumnDisplay();
        };
        m_setters[QStringLiteral("scrollingDefaultColumnDisplay")] = [concrete](const QVariant& v) {
            const int display = v.toInt();
            if (!ConfigDefaults::isValidScrollingColumnDisplay(display)) {
                return false;
            }
            concrete->setScrollingDefaultColumnDisplay(display);
            return true;
        };
        m_schemas[QStringLiteral("scrollingDefaultColumnDisplay")] = QStringLiteral("int");
        REGISTER_CONCRETE_STRING("scrollingPresetColumnWidths", scrollingPresetColumnWidthsString,
                                 setScrollingPresetColumnWidths)
        REGISTER_CONCRETE_STRING("scrollingPresetWindowHeights", scrollingPresetWindowHeightsString,
                                 setScrollingPresetWindowHeights)

        REGISTER_CONCRETE_INT("scrollingDefaultColumnWidthPresetIndex", scrollingDefaultColumnWidthPresetIndex,
                              setScrollingDefaultColumnWidthPresetIndex)
        // scrollingDefaultWindowHeightKind: enum (0=Auto, 1=Fixed, 2=Preset)
        m_getters[QStringLiteral("scrollingDefaultWindowHeightKind")] = [concrete]() {
            return concrete->scrollingDefaultWindowHeightKind();
        };
        m_setters[QStringLiteral("scrollingDefaultWindowHeightKind")] = [concrete](const QVariant& v) {
            const int kind = v.toInt();
            if (!ConfigDefaults::isValidScrollingHeightKind(kind)) {
                return false;
            }
            concrete->setScrollingDefaultWindowHeightKind(kind);
            return true;
        };
        m_schemas[QStringLiteral("scrollingDefaultWindowHeightKind")] = QStringLiteral("int");
        REGISTER_CONCRETE_DOUBLE("scrollingDefaultWindowHeightValue", scrollingDefaultWindowHeightValue,
                                 setScrollingDefaultWindowHeightValue)
        REGISTER_CONCRETE_INT("scrollingDefaultWindowHeightPresetIndex", scrollingDefaultWindowHeightPresetIndex,
                              setScrollingDefaultWindowHeightPresetIndex)
        // ── Scrolling.TabIndicator ──
        // scrollingTabIndicatorEnabled is registered in the ISettings section
        // above; the other twelve live here so the whole family
        // sits in one block. EVERY key of the group must be present: this
        // generic surface is the ONLY channel the settings app has to the
        // daemon, so an unregistered key is a control that writes the settings
        // app's own store and never reaches the engine or the overlay — it
        // looks wired and does nothing.
        //
        // The two enums get validated setters (the isValidScrollingTabIndicator*
        // closed sets, same shape as scrollingDefaultColumnDisplay above) rather
        // than a bare int, so a garbage wire value is refused instead of being
        // cast into a nonexistent style or edge.
        m_getters[QStringLiteral("scrollingTabIndicatorPosition")] = [concrete]() {
            return concrete->scrollingTabIndicatorPosition();
        };
        m_setters[QStringLiteral("scrollingTabIndicatorPosition")] = [concrete](const QVariant& v) {
            const int position = v.toInt();
            if (!ConfigDefaults::isValidScrollingTabIndicatorPosition(position)) {
                return false;
            }
            concrete->setScrollingTabIndicatorPosition(position);
            return true;
        };
        m_schemas[QStringLiteral("scrollingTabIndicatorPosition")] = QStringLiteral("int");
        REGISTER_CONCRETE_BOOL("scrollingTabIndicatorHideWhenSingleTab", scrollingTabIndicatorHideWhenSingleTab,
                               setScrollingTabIndicatorHideWhenSingleTab)
        REGISTER_CONCRETE_BOOL("scrollingTabIndicatorPlaceWithinColumn", scrollingTabIndicatorPlaceWithinColumn,
                               setScrollingTabIndicatorPlaceWithinColumn)
        REGISTER_CONCRETE_INT("scrollingTabIndicatorGap", scrollingTabIndicatorGap, setScrollingTabIndicatorGap)
        REGISTER_CONCRETE_INT("scrollingTabIndicatorWidth", scrollingTabIndicatorWidth, setScrollingTabIndicatorWidth)
        REGISTER_CONCRETE_DOUBLE("scrollingTabIndicatorLengthProportion", scrollingTabIndicatorLengthProportion,
                                 setScrollingTabIndicatorLengthProportion)
        // The tab-indicator and drop-indicator PAINT keys (style, spacing,
        // radius, colours, opacity, border) are registered through the
        // ISettings section above — only the GEOMETRY keys stay in this
        // concrete block, per isettings.h's stated split. Same
        // every-key-must-be-present rule throughout: this generic surface is
        // the settings app's ONLY channel to the daemon, so an unregistered
        // key looks wired and does nothing.

        REGISTER_CONCRETE_BOOL("scrollingWheelFocusEnabled", scrollingWheelFocusEnabled, setScrollingWheelFocusEnabled)
        REGISTER_CONCRETE_BOOL("scrollingWheelFocusInverted", scrollingWheelFocusInverted,
                               setScrollingWheelFocusInverted)

        // Scrolling behavior settings
        // scrollingInsertPosition: enum (0=RightOfActive .. 4=IntoActiveColumn)
        m_getters[QStringLiteral("scrollingInsertPosition")] = [concrete]() {
            return concrete->scrollingInsertPosition();
        };
        m_setters[QStringLiteral("scrollingInsertPosition")] = [concrete](const QVariant& v) {
            const int position = v.toInt();
            if (!ConfigDefaults::isValidScrollingInsertPosition(position)) {
                return false;
            }
            concrete->setScrollingInsertPosition(position);
            return true;
        };
        m_schemas[QStringLiteral("scrollingInsertPosition")] = QStringLiteral("int");
        REGISTER_CONCRETE_BOOL("scrollingFocusNewWindows", scrollingFocusNewWindows, setScrollingFocusNewWindows)
        REGISTER_CONCRETE_BOOL("scrollingFocusFollowsMouse", scrollingFocusFollowsMouse, setScrollingFocusFollowsMouse)
        // scrollingStickyWindowHandling: enum (0=TreatAsNormal, 1=RestoreOnly, 2=IgnoreAll)
        m_getters[QStringLiteral("scrollingStickyWindowHandling")] = [concrete]() {
            return concrete->scrollingStickyWindowHandling();
        };
        m_setters[QStringLiteral("scrollingStickyWindowHandling")] = [concrete](const QVariant& v) {
            const int handling = v.toInt();
            if (!ConfigDefaults::isValidScrollingStickyWindowHandling(handling)) {
                return false;
            }
            concrete->setScrollingStickyWindowHandling(handling);
            return true;
        };
        m_schemas[QStringLiteral("scrollingStickyWindowHandling")] = QStringLiteral("int");
        REGISTER_CONCRETE_BOOL("scrollingRespectMinimumSize", scrollingRespectMinimumSize,
                               setScrollingRespectMinimumSize)
        REGISTER_CONCRETE_BOOL("scrollingRestoreStripsOnLogin", scrollingRestoreStripsOnLogin,
                               setScrollingRestoreStripsOnLogin)
        // scrollingRestoreFloatedWindowsOnLogin is registered in the ISettings section above.
        REGISTER_CONCRETE_INT("scrollingColumnWidthStepPercent", scrollingColumnWidthStepPercent,
                              setScrollingColumnWidthStepPercent)
        REGISTER_CONCRETE_INT("scrollingWindowHeightStepPercent", scrollingWindowHeightStepPercent,
                              setScrollingWindowHeightStepPercent)

        // Scrolling shortcuts
        REGISTER_CONCRETE_STRING("scrollingFocusColumnFirstShortcut", scrollingFocusColumnFirstShortcut,
                                 setScrollingFocusColumnFirstShortcut)
        REGISTER_CONCRETE_STRING("scrollingFocusColumnLastShortcut", scrollingFocusColumnLastShortcut,
                                 setScrollingFocusColumnLastShortcut)
        REGISTER_CONCRETE_STRING("scrollingMoveColumnToFirstShortcut", scrollingMoveColumnToFirstShortcut,
                                 setScrollingMoveColumnToFirstShortcut)
        REGISTER_CONCRETE_STRING("scrollingMoveColumnToLastShortcut", scrollingMoveColumnToLastShortcut,
                                 setScrollingMoveColumnToLastShortcut)
        REGISTER_CONCRETE_STRING("scrollingConsumeWindowShortcut", scrollingConsumeWindowShortcut,
                                 setScrollingConsumeWindowShortcut)
        REGISTER_CONCRETE_STRING("scrollingExpelWindowShortcut", scrollingExpelWindowShortcut,
                                 setScrollingExpelWindowShortcut)
        REGISTER_CONCRETE_STRING("scrollingConsumeOrExpelLeftShortcut", scrollingConsumeOrExpelLeftShortcut,
                                 setScrollingConsumeOrExpelLeftShortcut)
        REGISTER_CONCRETE_STRING("scrollingConsumeOrExpelRightShortcut", scrollingConsumeOrExpelRightShortcut,
                                 setScrollingConsumeOrExpelRightShortcut)
        REGISTER_CONCRETE_STRING("scrollingCenterColumnShortcut", scrollingCenterColumnShortcut,
                                 setScrollingCenterColumnShortcut)
        REGISTER_CONCRETE_STRING("scrollingToggleColumnTabbedShortcut", scrollingToggleColumnTabbedShortcut,
                                 setScrollingToggleColumnTabbedShortcut)
        REGISTER_CONCRETE_STRING("scrollingToggleWindowedFullscreenShortcut", scrollingToggleWindowedFullscreenShortcut,
                                 setScrollingToggleWindowedFullscreenShortcut)
        REGISTER_CONCRETE_STRING("scrollingCycleColumnWidthShortcut", scrollingCycleColumnWidthShortcut,
                                 setScrollingCycleColumnWidthShortcut)
        REGISTER_CONCRETE_STRING("scrollingCycleColumnWidthBackShortcut", scrollingCycleColumnWidthBackShortcut,
                                 setScrollingCycleColumnWidthBackShortcut)
        REGISTER_CONCRETE_STRING("scrollingIncreaseColumnWidthShortcut", scrollingIncreaseColumnWidthShortcut,
                                 setScrollingIncreaseColumnWidthShortcut)
        REGISTER_CONCRETE_STRING("scrollingDecreaseColumnWidthShortcut", scrollingDecreaseColumnWidthShortcut,
                                 setScrollingDecreaseColumnWidthShortcut)
        REGISTER_CONCRETE_STRING("scrollingMaximizeColumnShortcut", scrollingMaximizeColumnShortcut,
                                 setScrollingMaximizeColumnShortcut)
        REGISTER_CONCRETE_STRING("scrollingExpandColumnShortcut", scrollingExpandColumnShortcut,
                                 setScrollingExpandColumnShortcut)
        REGISTER_CONCRETE_STRING("scrollingCycleWindowHeightShortcut", scrollingCycleWindowHeightShortcut,
                                 setScrollingCycleWindowHeightShortcut)
        REGISTER_CONCRETE_STRING("scrollingCycleWindowHeightBackShortcut", scrollingCycleWindowHeightBackShortcut,
                                 setScrollingCycleWindowHeightBackShortcut)
        REGISTER_CONCRETE_STRING("scrollingIncreaseWindowHeightShortcut", scrollingIncreaseWindowHeightShortcut,
                                 setScrollingIncreaseWindowHeightShortcut)
        REGISTER_CONCRETE_STRING("scrollingDecreaseWindowHeightShortcut", scrollingDecreaseWindowHeightShortcut,
                                 setScrollingDecreaseWindowHeightShortcut)
        REGISTER_CONCRETE_STRING("scrollingResetWindowHeightsShortcut", scrollingResetWindowHeightsShortcut,
                                 setScrollingResetWindowHeightsShortcut)
        REGISTER_CONCRETE_STRING("scrollingCenterVisibleColumnsShortcut", scrollingCenterVisibleColumnsShortcut,
                                 setScrollingCenterVisibleColumnsShortcut)
        REGISTER_CONCRETE_STRING("scrollingFocusWindowTopShortcut", scrollingFocusWindowTopShortcut,
                                 setScrollingFocusWindowTopShortcut)
        REGISTER_CONCRETE_STRING("scrollingFocusWindowBottomShortcut", scrollingFocusWindowBottomShortcut,
                                 setScrollingFocusWindowBottomShortcut)
        REGISTER_CONCRETE_STRING("scrollingFocusColumnLeftShortcut", scrollingFocusColumnLeftShortcut,
                                 setScrollingFocusColumnLeftShortcut)
        REGISTER_CONCRETE_STRING("scrollingFocusColumnRightShortcut", scrollingFocusColumnRightShortcut,
                                 setScrollingFocusColumnRightShortcut)
        REGISTER_CONCRETE_STRING("scrollingFocusColumnLeftOrLastShortcut", scrollingFocusColumnLeftOrLastShortcut,
                                 setScrollingFocusColumnLeftOrLastShortcut)
        REGISTER_CONCRETE_STRING("scrollingFocusColumnRightOrFirstShortcut", scrollingFocusColumnRightOrFirstShortcut,
                                 setScrollingFocusColumnRightOrFirstShortcut)
        REGISTER_CONCRETE_STRING("scrollingMoveToFloatingShortcut", scrollingMoveToFloatingShortcut,
                                 setScrollingMoveToFloatingShortcut)
        REGISTER_CONCRETE_STRING("scrollingMoveToTilingShortcut", scrollingMoveToTilingShortcut,
                                 setScrollingMoveToTilingShortcut)
    }

// Clean up macros (local scope; unity-batch hygiene)
#undef REGISTER_BOOL_SETTING
#undef REGISTER_INT_SETTING
#undef REGISTER_DOUBLE_SETTING
#undef REGISTER_THEME_FALLBACK_COLOR_SETTING
#undef REGISTER_COLOR_SETTING
#undef REGISTER_RAW_THEME_COLOR
#undef REGISTER_CONCRETE_BOOL
#undef REGISTER_CONCRETE_INT
#undef REGISTER_CONCRETE_DOUBLE
#undef REGISTER_CONCRETE_STRING
}

} // namespace PlasmaZones
