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
#include <QUuid> // id-format validation (defaultScrollingTemplate, scrollingTemplateOrder)

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

#define REGISTER_STRING_SETTING(name, getter, setter)                                                                  \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        m_settings->setter(v.toString());                                                                              \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(name)] = QStringLiteral("string");

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
        // Refuse non-QVariantList payloads rather than coercing: toList() on
        // an int, bool or map yields an EMPTY list, which would silently
        // clear the triggers while reporting success — same refuse-don't-
        // coerce posture as the enum setters below. QStringList is refused
        // too: this key's payload is a list of trigger MAPS, so a string
        // list is the same coerced garbage in different clothing.
        if (v.typeId() != QMetaType::QVariantList) {
            return false;
        }
        m_settings->setScrollingDragInsertTriggers(v.toList());
        return true;
    };
    // "maplist", not "stringlist": the payload is a QVariantList of trigger
    // MAPS and the setter refuses anything else, so a schema-honouring client
    // that sent an actual string list would be silently refused. The token is
    // advisory JSON for external clients; nothing in-repo parses it.
    m_schemas[QStringLiteral("scrollingDragInsertTriggers")] = QStringLiteral("maplist");

    // The two wheel chords ("scroll keys"). ISettings level, NOT the concrete
    // block below: both are pure virtuals on ISettings, so registering them
    // behind the concrete cast would lose them entirely on a non-Settings
    // backend while the getters would still have compiled fine.
    m_getters[QStringLiteral("scrollingWheelFocusTriggers")] = [this]() {
        return QVariant::fromValue(m_settings->scrollingWheelFocusTriggers());
    };
    // Same refuse-don't-coerce posture as scrollingDragInsertTriggers above,
    // for the same reason: toList() on a non-list yields an empty list, which
    // here would clear the scroll key while reporting success.
    m_setters[QStringLiteral("scrollingWheelFocusTriggers")] = [this](const QVariant& v) {
        if (v.typeId() != QMetaType::QVariantList) {
            return false;
        }
        m_settings->setScrollingWheelFocusTriggers(v.toList());
        return true;
    };
    m_schemas[QStringLiteral("scrollingWheelFocusTriggers")] = QStringLiteral("maplist");
    m_getters[QStringLiteral("scrollingWheelViewTriggers")] = [this]() {
        return QVariant::fromValue(m_settings->scrollingWheelViewTriggers());
    };
    m_setters[QStringLiteral("scrollingWheelViewTriggers")] = [this](const QVariant& v) {
        if (v.typeId() != QMetaType::QVariantList) {
            return false;
        }
        m_settings->setScrollingWheelViewTriggers(v.toList());
        return true;
    };
    m_schemas[QStringLiteral("scrollingWheelViewTriggers")] = QStringLiteral("maplist");

    REGISTER_BOOL_SETTING("scrollingDragInsertToggle", scrollingDragInsertToggle, setScrollingDragInsertToggle)

    // The template priority order (IOrderingSettings) — the scrolling member
    // of the ordering family. The snapping and tiling orders are not on the
    // generic wire; the settings app writes them through its own Settings.
    // The daemon consumes this one in-process via buildCustomOrder; the
    // registration keeps the every-scrolling-property D-Bus contract
    // complete (test_settings_registry_contract). Hand-written rather than
    // REGISTER_STRINGLIST_SETTING: an ordering write must not coerce — a
    // non-list payload converted through toStringList() becomes an EMPTY
    // list, silently wiping the user's saved order while reporting success.
    // Entries are validated as UUID form the same way defaultScrollingTemplate
    // refuses malformed ids below (existence against the store cannot be
    // checked here either), which also keeps the reserved "none" sentinel and
    // comma-bearing strings out of the persisted comma-joined key, and the
    // count cap bounds what an errant session process can persist.
    m_getters[QStringLiteral("scrollingTemplateOrder")] = [this]() {
        return m_settings->scrollingTemplateOrder();
    };
    m_setters[QStringLiteral("scrollingTemplateOrder")] = [this](const QVariant& v) {
        const int type = v.typeId();
        if (type != QMetaType::QStringList && type != QMetaType::QVariantList) {
            return false;
        }
        const QStringList order = v.toStringList();
        constexpr qsizetype kMaxOrderEntries = 1024;
        if (order.size() > kMaxOrderEntries) {
            return false;
        }
        for (const QString& id : order) {
            if (QUuid::fromString(id).isNull()) {
                return false;
            }
        }
        m_settings->setScrollingTemplateOrder(order);
        return true;
    };
    m_schemas[QStringLiteral("scrollingTemplateOrder")] = QStringLiteral("stringlist");

    // The scrolling twins of the snap / autotile restore-floated and
    // keep-floating-above pairs, plus the tab-strip toggle. All four are
    // ISettings virtuals with defaults, so they register through the
    // interface like their snap / autotile counterparts rather than inside
    // the concrete-Settings block below, where a non-Settings backend would
    // lose the keys entirely.
    REGISTER_BOOL_SETTING("scrollingRestoreFloatedWindowsOnLogin", scrollingRestoreFloatedWindowsOnLogin,
                          setScrollingRestoreFloatedWindowsOnLogin)
    // Effect-only consumer (reconcileRuleWindowLayer): keep scroll-floated
    // windows stacked above the strip.
    REGISTER_BOOL_SETTING("scrollingKeepFloatingAbove", scrollingKeepFloatingAbove, setScrollingKeepFloatingAbove)
    REGISTER_BOOL_SETTING("scrollingTabIndicatorEnabled", scrollingTabIndicatorEnabled, setScrollingTabIndicatorEnabled)
    REGISTER_BOOL_SETTING("scrollingDropIndicatorEnabled", scrollingDropIndicatorEnabled,
                          setScrollingDropIndicatorEnabled)

    // The PAINT keys of both indicator families register through the
    // interface too — isettings.h declares getter+no-op-setter virtual pairs
    // for exactly these sixteen so a non-Settings backend keeps them (its
    // stated rationale); only the tab indicator's GEOMETRY keys are
    // deliberately concrete-only (isettings.h documents that exclusion).
    // The Style enum keeps its validated hand registration, via the
    // interface.
    m_getters[QStringLiteral("scrollingTabIndicatorStyle")] = [this]() {
        return m_settings->scrollingTabIndicatorStyle();
    };
    m_setters[QStringLiteral("scrollingTabIndicatorStyle")] = [this](const QVariant& v) {
        bool styleOk = false;
        const int style = v.toInt(&styleOk);
        if (!styleOk) {
            return false; // non-numeric payload: refuse, do not coerce to the zero member
        }
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
    // The label font, paint keys like the colours above and registered through
    // the interface for the same reason: this generic wire is how the settings
    // app writes them AND how the KWin effect's loadCachedSettings reads them
    // back, so a missing entry here ships the whole font feature dead. The
    // family is a plain string with no closed set — empty means the system
    // font and any installed family is legal. There is no size key: Width
    // gives the pill its thickness and the painter fits the label to it.
    REGISTER_STRING_SETTING("scrollingTabIndicatorFontFamily", scrollingTabIndicatorFontFamily,
                            setScrollingTabIndicatorFontFamily)
    REGISTER_INT_SETTING("scrollingTabIndicatorFontWeight", scrollingTabIndicatorFontWeight,
                         setScrollingTabIndicatorFontWeight)
    REGISTER_BOOL_SETTING("scrollingTabIndicatorFontItalic", scrollingTabIndicatorFontItalic,
                          setScrollingTabIndicatorFontItalic)
    REGISTER_BOOL_SETTING("scrollingTabIndicatorFontUnderline", scrollingTabIndicatorFontUnderline,
                          setScrollingTabIndicatorFontUnderline)
    REGISTER_BOOL_SETTING("scrollingTabIndicatorFontStrikeout", scrollingTabIndicatorFontStrikeout,
                          setScrollingTabIndicatorFontStrikeout)
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

    // Strip-mode selector settings. PURE ISettings virtuals (every backend
    // must implement them — a stricter contract than the indicator PAINT
    // keys above, whose getter+no-op-setter default pairs exist so a
    // non-Settings backend keeps the keys), so they register through the
    // interface rather than in the concrete block below. No LayoutMode /
    // GridColumns / MaxRows twin: the strip popup is one card row along the
    // strip. The int/bool keys keep the shared macros' coercing setters ON
    // PURPOSE (the macros are frozen boilerplate across four registry TUs,
    // and the config schema's clampInt one layer down bounds anything a
    // coerced 0 could write); only the two ENUM keys need the hand-written
    // refuse-non-numeric form, because a coerced 0 IS a meaningful member
    // there.
    REGISTER_BOOL_SETTING("scrollingZoneSelectorEnabled", scrollingZoneSelectorEnabled, setScrollingZoneSelectorEnabled)
    REGISTER_INT_SETTING("scrollingZoneSelectorTriggerDistance", scrollingZoneSelectorTriggerDistance,
                         setScrollingZoneSelectorTriggerDistance)
    // scrollingZoneSelectorPosition: enum (0=TopLeft .. 8=BottomRight)
    m_getters[QStringLiteral("scrollingZoneSelectorPosition")] = [this]() {
        return static_cast<int>(m_settings->scrollingZoneSelectorPosition());
    };
    m_setters[QStringLiteral("scrollingZoneSelectorPosition")] = [this](const QVariant& v) {
        bool valOk = false;
        const int val = v.toInt(&valOk);
        if (!valOk) {
            return false; // non-numeric payload: refuse, do not coerce to the zero member
        }
        if (val >= static_cast<int>(ZoneSelectorPosition::TopLeft)
            && val <= static_cast<int>(ZoneSelectorPosition::BottomRight)) {
            m_settings->setScrollingZoneSelectorPosition(static_cast<ZoneSelectorPosition>(val));
            return true;
        }
        return false;
    };
    m_schemas[QStringLiteral("scrollingZoneSelectorPosition")] = QStringLiteral("int");
    // scrollingZoneSelectorSizeMode: enum (0=Auto, 1=Manual)
    m_getters[QStringLiteral("scrollingZoneSelectorSizeMode")] = [this]() {
        return static_cast<int>(m_settings->scrollingZoneSelectorSizeMode());
    };
    m_setters[QStringLiteral("scrollingZoneSelectorSizeMode")] = [this](const QVariant& v) {
        bool valOk = false;
        const int val = v.toInt(&valOk);
        if (!valOk) {
            return false; // non-numeric payload: refuse, do not coerce to the zero member
        }
        if (val >= static_cast<int>(ZoneSelectorSizeMode::Auto)
            && val <= static_cast<int>(ZoneSelectorSizeMode::Manual)) {
            m_settings->setScrollingZoneSelectorSizeMode(static_cast<ZoneSelectorSizeMode>(val));
            return true;
        }
        return false;
    };
    m_schemas[QStringLiteral("scrollingZoneSelectorSizeMode")] = QStringLiteral("int");
    REGISTER_INT_SETTING("scrollingZoneSelectorPreviewWidth", scrollingZoneSelectorPreviewWidth,
                         setScrollingZoneSelectorPreviewWidth)
    REGISTER_INT_SETTING("scrollingZoneSelectorPreviewHeight", scrollingZoneSelectorPreviewHeight,
                         setScrollingZoneSelectorPreviewHeight)
    REGISTER_BOOL_SETTING("scrollingZoneSelectorPreviewLockAspect", scrollingZoneSelectorPreviewLockAspect,
                          setScrollingZoneSelectorPreviewLockAspect)

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
            bool modeOk = false;
            const int mode = v.toInt(&modeOk);
            if (!modeOk) {
                return false; // non-numeric payload: refuse, do not coerce to the zero member
            }
            if (!ConfigDefaults::isValidScrollingCenterFocusedColumn(mode)) {
                return false;
            }
            concrete->setScrollingCenterFocusedColumn(mode);
            return true;
        };
        m_schemas[QStringLiteral("scrollingCenterFocusedColumn")] = QStringLiteral("int");
        // scrollingStripAxis: enum (0=Auto, 1=Horizontal, 2=Vertical) — same
        // shape as the mode above, including the refuse-do-not-coerce rule on
        // a non-numeric payload. The closed-set check matters more here than
        // usual: a coerced 0 would silently mean Auto and quietly undo the
        // user's explicit choice.
        //
        // Registered in the CONCRETE block only because the SETTER is
        // concrete-only; the getter is already an IScrollSettings virtual. That
        // is the pattern isettings.h calls "a backlog to hoist, not a second
        // sanctioned pattern", so this entry belongs on that hoist backlog:
        // once ISettings carries a setScrollingStripAxis virtual it moves up to
        // the ISettings-level block above with its siblings.
        m_getters[QStringLiteral("scrollingStripAxis")] = [concrete]() {
            return concrete->scrollingStripAxis();
        };
        m_setters[QStringLiteral("scrollingStripAxis")] = [concrete](const QVariant& v) {
            bool axisOk = false;
            const int axis = v.toInt(&axisOk);
            if (!axisOk) {
                return false;
            }
            if (!ConfigDefaults::isValidScrollingStripAxis(axis)) {
                return false;
            }
            concrete->setScrollingStripAxis(axis);
            return true;
        };
        m_schemas[QStringLiteral("scrollingStripAxis")] = QStringLiteral("int");
        REGISTER_CONCRETE_BOOL("scrollingAlwaysCenterSingleColumn", scrollingAlwaysCenterSingleColumn,
                               setScrollingAlwaysCenterSingleColumn)
        REGISTER_CONCRETE_BOOL("scrollingCropStraddlers", scrollingCropStraddlers, setScrollingCropStraddlers)
        // Edge auto-scroll during a drag re-insert (Scrolling.Behavior.DragScroll).
        // Concrete-only for the same reason as scrollingStripAxis above: the
        // getters are IScrollSettings virtuals, only the setters are
        // concrete. Same hoist backlog — once ISettings grows the setter
        // virtuals, these four move up to the ISettings-level block.
        REGISTER_CONCRETE_BOOL("scrollingDragScrollEnabled", scrollingDragScrollEnabled, setScrollingDragScrollEnabled)
        REGISTER_CONCRETE_INT("scrollingDragScrollTriggerWidth", scrollingDragScrollTriggerWidth,
                              setScrollingDragScrollTriggerWidth)
        REGISTER_CONCRETE_INT("scrollingDragScrollDelayMs", scrollingDragScrollDelayMs, setScrollingDragScrollDelayMs)
        REGISTER_CONCRETE_INT("scrollingDragScrollMaxSpeed", scrollingDragScrollMaxSpeed,
                              setScrollingDragScrollMaxSpeed)
        // scrollingDefaultColumnWidthKind: enum (0=Proportion, 1=Fixed, 2=ClientDecides, 3=Preset)
        m_getters[QStringLiteral("scrollingDefaultColumnWidthKind")] = [concrete]() {
            return concrete->scrollingDefaultColumnWidthKind();
        };
        m_setters[QStringLiteral("scrollingDefaultColumnWidthKind")] = [concrete](const QVariant& v) {
            bool kindOk = false;
            const int kind = v.toInt(&kindOk);
            if (!kindOk) {
                return false; // non-numeric payload: refuse, do not coerce to the zero member
            }
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
            bool displayOk = false;
            const int display = v.toInt(&displayOk);
            if (!displayOk) {
                return false; // non-numeric payload: refuse, do not coerce to the zero member
            }
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
            bool kindOk = false;
            const int kind = v.toInt(&kindOk);
            if (!kindOk) {
                return false; // non-numeric payload: refuse, do not coerce to the zero member
            }
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
        // scrollingTabIndicatorEnabled and the PAINT keys (style, spacing,
        // radius, colours) register through the ISettings section above; the
        // six GEOMETRY keys live here, per the split the trailing comment of
        // this block states. EVERY key of the group must be present
        // somewhere: this generic surface is the ONLY channel the settings
        // app has to the daemon, so an unregistered key is a control that
        // writes the settings app's own store and never reaches the engine
        // or the overlay — it looks wired and does nothing.
        //
        // The one enum in THIS block (Position) gets a validated setter (the
        // isValidScrollingTabIndicator* closed set, same shape as
        // scrollingDefaultColumnDisplay above) rather than a bare int, so a
        // garbage wire value is refused instead of being cast into a
        // nonexistent edge; the other enum of the family (Style) is
        // validated in the ISettings section.
        m_getters[QStringLiteral("scrollingTabIndicatorPosition")] = [concrete]() {
            return concrete->scrollingTabIndicatorPosition();
        };
        m_setters[QStringLiteral("scrollingTabIndicatorPosition")] = [concrete](const QVariant& v) {
            bool positionOk = false;
            const int position = v.toInt(&positionOk);
            if (!positionOk) {
                return false; // non-numeric payload: refuse, do not coerce to the zero member
            }
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
            bool positionOk = false;
            const int position = v.toInt(&positionOk);
            if (!positionOk) {
                return false; // non-numeric payload: refuse, do not coerce to the zero member
            }
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
            bool handlingOk = false;
            const int handling = v.toInt(&handlingOk);
            if (!handlingOk) {
                return false; // non-numeric payload: refuse, do not coerce to the zero member
            }
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
        // scrollingRestoreFloatedWindowsOnLogin and scrollingKeepFloatingAbove are
        // registered in the ISettings section above.
        REGISTER_CONCRETE_INT("scrollingColumnWidthStepPercent", scrollingColumnWidthStepPercent,
                              setScrollingColumnWidthStepPercent)
        REGISTER_CONCRETE_INT("scrollingWindowHeightStepPercent", scrollingWindowHeightStepPercent,
                              setScrollingWindowHeightStepPercent)
        REGISTER_CONCRETE_INT("scrollingViewScrollStepPercent", scrollingViewScrollStepPercent,
                              setScrollingViewScrollStepPercent)

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
        REGISTER_CONCRETE_STRING("scrollingViewPageBackShortcut", scrollingViewPageBackShortcut,
                                 setScrollingViewPageBackShortcut)
        REGISTER_CONCRETE_STRING("scrollingViewPageForwardShortcut", scrollingViewPageForwardShortcut,
                                 setScrollingViewPageForwardShortcut)
        REGISTER_CONCRETE_STRING("scrollingEqualizeColumnWidthsShortcut", scrollingEqualizeColumnWidthsShortcut,
                                 setScrollingEqualizeColumnWidthsShortcut)
        REGISTER_CONCRETE_STRING("scrollingMinimizeColumnWidthShortcut", scrollingMinimizeColumnWidthShortcut,
                                 setScrollingMinimizeColumnWidthShortcut)
    }

// Clean up macros (local scope; unity-batch hygiene)
#undef REGISTER_BOOL_SETTING
#undef REGISTER_INT_SETTING
#undef REGISTER_STRING_SETTING
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
