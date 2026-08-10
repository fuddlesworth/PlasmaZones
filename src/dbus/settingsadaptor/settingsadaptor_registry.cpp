// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Getter/setter/schema registry population for SettingsAdaptor:
//   * initializeRegistry — registers the mode-neutral half of the generic
//     getSetting/setSetting D-Bus surface (REGISTER_* macro block,
//     enum-validated custom setters, JSON profile-tree blob round-trips) and
//     calls the three per-mode slices
//   * validProfileTreeBlob — shared wire validation for the two profile-tree
//     blob setters
//
// The registry is split across FOUR TUs, one per family, all filling the same
// m_getters / m_setters / m_schemas maps:
//   * settingsadaptor_registry.cpp (this file) — core / shared
//   * settingsadaptor_registry_snapping.cpp — initializeRegistrySnapping()
//   * settingsadaptor_registry_autotile.cpp — initializeRegistryAutotile()
//   * settingsadaptor_registry_scrolling.cpp — initializeRegistryScrolling()
//
// BOUNDARY RULE: a key belongs to a mode TU when its name carries that mode's
// prefix or the zone family prefix — snap* / snapping* / zone* to snapping,
// autotile* to autotile, scrolling* to scrolling. Mode-neutral keys stay here:
// drag activation, the navigation / swap / span / quickLayout / cycle /
// rotate / global shortcuts, display, appearance, filtering, the animation and
// profile trees, cava, gpu, and the per-mode disable lists, which are one
// shared three-mode mechanism rather than three per-mode settings.
//
// Same class as settingsadaptor.cpp, split into separate TUs without changing
// the adaptor's public interface (mirrors the SettingsController multi-TU
// split, e.g. settingscontroller_pagestate.cpp). The registry contents are
// otherwise unchanged by the split itself.

#include "settingsadaptor.h"
#include "core/interfaces/interfaces.h"
#include "config/settings.h" // For concrete Settings type
#include "config/configdefaults.h" // ConfigDefaults range bounds + normalizeGpuDevice
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/ProfileTree.h>
#include <PhosphorAnimation/ShaderProfileTree.h>
#include <PhosphorCompositor/DecorationDefaults.h>
#include <PhosphorSurface/DecorationProfileTree.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace PlasmaZones {

namespace {
// The rule vocabulary's follow-the-accent token (PhosphorRules::
// BorderColorToken::Accent), accepted on the colour keys as an alias for the
// empty theme-fallback sentinel so pre-v6 clients and rule-vocabulary
// scripts keep working against the unified surface. Spelled locally to keep
// this TU off the rules library; the migration freezes the same literal.
constexpr QLatin1String kAccentToken{"accent"};

// Exact-match predicate for the three window-appearance scope-token keys,
// mirroring their validStringOr schema validator's closed set. File-scope so
// the REGISTER_VALIDATED_STRING_SETTING lambdas can reach it without
// widening the macro's capture list.
bool isWindowScopeToken(const QString& requested)
{
    namespace WAS = ::PhosphorCompositor::WindowAppearanceScope;
    return requested == WAS::Tiled || requested == WAS::Normal || requested == WAS::All;
}

// Shared wire-size cap for the JSON profile-tree blobs (animation shader tree
// AND surface decoration tree) accepted over D-Bus.
constexpr qsizetype kMaxProfileTreeBytes = 64 * 1024;

// Shared wire validation for the two profile-tree blob setters: gate on UTF-8
// byte length — for multi-byte payloads QString::size() undercounts what a
// 64 KiB wire frame encodes to — and require a top-level JSON object. Fills
// @p outDoc with the parsed document on success.
bool validProfileTreeBlob(const QVariant& v, QJsonDocument* outDoc)
{
    const QByteArray raw = v.toString().toUtf8();
    if (raw.size() > kMaxProfileTreeBytes)
        return false;
    QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isObject())
        return false;
    *outDoc = std::move(doc);
    return true;
}
}

// Note: Macros are defined in initializeRegistry() to avoid namespace pollution

void SettingsAdaptor::initializeRegistry()
{
// Register getters and setters for all settings
// This registry pattern allows adding new settings without modifying setSetting()
// Using macros to reduce repetition

// Helper macros
#define REGISTER_STRING_SETTING(name, getter, setter)                                                                  \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        m_settings->setter(v.toString());                                                                              \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(name)] = QStringLiteral("string");

// For token-enum string keys whose store validator COERCES an unrecognized
// token to the default: the plain string form would report success while the
// store silently substituted, which is the accept-then-discard failure the
// colour setter's comment names. Validate BEFORE writing — a
// write-then-revert would emit the key's NOTIFY twice (once with the
// coerced wrong value) and run its consumers on both, and would refuse
// legal non-canonical spellings the store itself normalizes. @p acceptExpr
// is a bool expression over `requested`; the case-normalizing keys accept
// any spelling their option set contains after lower/trim (the store
// canonicalizes on write), matching the gpuDevice custom setter's
// tolerance, while the scope keys are exact-match like their validStringOr
// schema validator.
#define REGISTER_VALIDATED_STRING_SETTING(name, getter, setter, acceptExpr)                                            \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        const QString requested = v.toString();                                                                        \
        if (!(acceptExpr)) {                                                                                           \
            return false;                                                                                              \
        }                                                                                                              \
        m_settings->setter(requested);                                                                                 \
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

// Numeric keys accept out-of-range values and report success while the store
// validator CLAMPS: deliberate — a clamp lands on the nearest legal value
// (unlike the token-enum substitution the validated-string form refuses),
// and rejecting would break sloppy-but-harmless callers for no gain. The
// labelFontSizeScale custom setter shows the stricter pattern where a key
// wants it.
#define REGISTER_DOUBLE_SETTING(name, getter, setter)                                                                  \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        m_settings->setter(v.toDouble());                                                                              \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(name)] = QStringLiteral("double");

// Unlike the numeric macros, the colour setter can be handed a string that
// names no colour at all. Parsing it and storing the resulting invalid QColor
// would leave the old value in place while setSetting reported success, which
// is the one thing its documented post-condition must not do. Reject instead:
// a malformed hex returns false and the caller sees the write fail. Two
// SENTINEL spellings are accepted and put the key back on the theme: the
// empty string (the stored theme-fallback sentinel) and the legacy "accent"
// token the pre-v6 wire accepted — both store the sentinel via the QColor
// setter's invalid-colour branch. The getter marshals the RESOLVED colour;
// the resolution chain (resolvedSystemColor / the stored #AARRGGBB) cannot
// produce an invalid QColor, so no isValid() guard is needed before name().
// The stored sentinel itself is visible through the *Raw companion keys.
#define REGISTER_COLOR_SETTING(keyName, getter, setter)                                                                \
    m_getters[QStringLiteral(keyName)] = [this]() {                                                                    \
        QColor color = m_settings->getter();                                                                           \
        return color.name(QColor::HexArgb);                                                                            \
    };                                                                                                                 \
    m_setters[QStringLiteral(keyName)] = [this](const QVariant& v) {                                                   \
        const QString s = v.toString();                                                                                \
        if (s.isEmpty() || s == kAccentToken) {                                                                        \
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

#define REGISTER_STRINGLIST_SETTING(name, getter, setter)                                                              \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        m_settings->setter(v.toStringList());                                                                          \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(name)] = QStringLiteral("stringlist");

    // Concrete Settings pointer for properties not on ISettings interface.
    // The per-screen override API is fully on ISettings — this cast is
    // used only by the REGISTER_CONCRETE_* macros below for dozens of
    // global properties that haven't been hoisted to the segregated
    // interfaces yet. Every REGISTER_CONCRETE_* call site is wrapped in
    // an `if (concrete)` block, so test backends that don't supply a
    // concrete Settings simply don't register those keys — setSetting
    // returns "key not found" instead of dereferencing a null pointer.
    auto* concrete = qobject_cast<Settings*>(m_settings);

// Macro for concrete Settings entries (same pattern as REGISTER_* but captures
// 'concrete'). Only the string form is left here: the bool / int / double
// forms went with the keys that used them when the registry split into
// per-mode TUs, and each mode TU carries its own copy of whichever forms it
// needs (the scrolling TU also carries the ISettings-flavoured
// REGISTER_THEME_FALLBACK_COLOR_SETTING; no concrete colour form exists).
#define REGISTER_CONCRETE_STRING(name, getter, setter)                                                                 \
    m_getters[QStringLiteral(name)] = [concrete]() {                                                                   \
        return concrete->getter();                                                                                     \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [concrete](const QVariant& v) {                                                  \
        concrete->setter(v.toString());                                                                                \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(name)] = QStringLiteral("string");

    // Activation settings — drag activation triggers list (multi-bind)
    m_getters[QStringLiteral("dragActivationTriggers")] = [this]() {
        return QVariant::fromValue(m_settings->dragActivationTriggers());
    };
    m_setters[QStringLiteral("dragActivationTriggers")] = [this](const QVariant& v) {
        m_settings->setDragActivationTriggers(v.toList());
        return true;
    };
    m_schemas[QStringLiteral("dragActivationTriggers")] = QStringLiteral("stringlist");

    REGISTER_BOOL_SETTING("toggleActivation", toggleActivation, setToggleActivation)

    // Display settings
    REGISTER_BOOL_SETTING("showZonesOnAllMonitors", showZonesOnAllMonitors, setShowZonesOnAllMonitors)
    REGISTER_BOOL_SETTING("showZoneNumbers", showZoneNumbers, setShowZoneNumbers)
    REGISTER_BOOL_SETTING("flashZonesOnSwitch", flashZonesOnSwitch, setFlashZonesOnSwitch)
    REGISTER_BOOL_SETTING("showOsdOnLayoutSwitch", showOsdOnLayoutSwitch, setShowOsdOnLayoutSwitch)
    REGISTER_BOOL_SETTING("showOsdOnDesktopSwitch", showOsdOnDesktopSwitch, setShowOsdOnDesktopSwitch)
    REGISTER_BOOL_SETTING("showNavigationOsd", showNavigationOsd, setShowNavigationOsd)
    // osdStyle: enum (0=None, 1=Text, 2=Preview) — use interface's OsdStyle
    m_getters[QStringLiteral("osdStyle")] = [this]() {
        return static_cast<int>(m_settings->osdStyle());
    };
    m_setters[QStringLiteral("osdStyle")] = [this](const QVariant& v) {
        int val = v.toInt();
        if (val >= ConfigDefaults::osdStyleMin() && val <= ConfigDefaults::osdStyleMax()) {
            m_settings->setOsdStyle(static_cast<OsdStyle>(val));
            return true;
        }
        return false;
    };
    m_schemas[QStringLiteral("osdStyle")] = QStringLiteral("int");
    // overlayDisplayMode: enum (0=ZoneRectangles, 1=LayoutPreview)
    m_getters[QStringLiteral("overlayDisplayMode")] = [this]() {
        return static_cast<int>(m_settings->overlayDisplayMode());
    };
    m_setters[QStringLiteral("overlayDisplayMode")] = [this](const QVariant& v) {
        int val = v.toInt();
        if (val >= ConfigDefaults::overlayDisplayModeMin() && val <= ConfigDefaults::overlayDisplayModeMax()) {
            m_settings->setOverlayDisplayMode(static_cast<OverlayDisplayMode>(val));
            return true;
        }
        return false;
    };
    m_schemas[QStringLiteral("overlayDisplayMode")] = QStringLiteral("int");
    // Per-mode disable lists — one entry per (context, mode) pair, three
    // contexts by three modes — because both the read and the write need a
    // Mode argument that the REGISTER_STRINGLIST_SETTING macro doesn't
    // expose. Pre-v3 these were a single set of three keys whose values
    // silently gated every mode; the new wire schema names the mode
    // explicitly so consumers can't conflate.
#define REGISTER_PER_MODE_DISABLE(keyName, modeEnum, getterFn, setterFn)                                               \
    m_getters[QStringLiteral(keyName)] = [this]() {                                                                    \
        return m_settings->getterFn(modeEnum);                                                                         \
    };                                                                                                                 \
    m_setters[QStringLiteral(keyName)] = [this](const QVariant& v) {                                                   \
        m_settings->setterFn(modeEnum, v.toStringList());                                                              \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(keyName)] = QStringLiteral("stringlist");

    REGISTER_PER_MODE_DISABLE("snappingDisabledMonitors", PhosphorZones::AssignmentEntry::Snapping, disabledMonitors,
                              setDisabledMonitors)
    REGISTER_PER_MODE_DISABLE("autotileDisabledMonitors", PhosphorZones::AssignmentEntry::Autotile, disabledMonitors,
                              setDisabledMonitors)
    REGISTER_PER_MODE_DISABLE("snappingDisabledDesktops", PhosphorZones::AssignmentEntry::Snapping, disabledDesktops,
                              setDisabledDesktops)
    REGISTER_PER_MODE_DISABLE("autotileDisabledDesktops", PhosphorZones::AssignmentEntry::Autotile, disabledDesktops,
                              setDisabledDesktops)
    REGISTER_PER_MODE_DISABLE("snappingDisabledActivities", PhosphorZones::AssignmentEntry::Snapping,
                              disabledActivities, setDisabledActivities)
    REGISTER_PER_MODE_DISABLE("autotileDisabledActivities", PhosphorZones::AssignmentEntry::Autotile,
                              disabledActivities, setDisabledActivities)
    // Scrolling tier: the daemon consults handleForMode(..., Scrolling)
    // (updateEngineScreens' derive phase and the scroll shortcut gate), so
    // the wire must be able to populate these lists like the other modes'.
    REGISTER_PER_MODE_DISABLE("scrollingDisabledMonitors", PhosphorZones::AssignmentEntry::Scrolling, disabledMonitors,
                              setDisabledMonitors)
    REGISTER_PER_MODE_DISABLE("scrollingDisabledDesktops", PhosphorZones::AssignmentEntry::Scrolling, disabledDesktops,
                              setDisabledDesktops)
    REGISTER_PER_MODE_DISABLE("scrollingDisabledActivities", PhosphorZones::AssignmentEntry::Scrolling,
                              disabledActivities, setDisabledActivities)
#undef REGISTER_PER_MODE_DISABLE

    // Appearance settings. The zone colours marshal RESOLVED (the getter
    // serves a concrete palette-derived colour while the stored value is the
    // empty theme-fallback sentinel), so the wire keeps its concrete-colour
    // contract; setting one over D-Bus pins a concrete colour, and writing
    // the empty sentinel (or the legacy "accent" token) puts it back on the
    // theme. The stored follow/pin state itself is readable and writable
    // through the *Raw companion keys registered further down.
    REGISTER_COLOR_SETTING("highlightColor", highlightColor, setHighlightColor)
    REGISTER_COLOR_SETTING("inactiveColor", inactiveColor, setInactiveColor)
    REGISTER_COLOR_SETTING("borderColor", borderColor, setBorderColor)
    REGISTER_COLOR_SETTING("labelFontColor", labelFontColor, setLabelFontColor)
    REGISTER_DOUBLE_SETTING("activeOpacity", activeOpacity, setActiveOpacity)
    REGISTER_DOUBLE_SETTING("inactiveOpacity", inactiveOpacity, setInactiveOpacity)
    REGISTER_INT_SETTING("borderWidth", borderWidth, setBorderWidth)
    REGISTER_INT_SETTING("borderRadius", borderRadius, setBorderRadius)

    // Window decoration appearance (config-backed default the KWin effect resolves
    // against, with user rules overriding per slot).
    //
    // The three colour keys are theme-fallback strings whose EMPTY sentinel
    // means "follow the zone highlight / inactive colour" — which itself
    // follows the system palette only while unpinned, so a pinned zone
    // colour flows through to an unpinned window colour by design. The
    // sentinel is RESOLVED here, before the value crosses D-Bus: the
    // effect's settings reader treats an empty colour reply as version skew
    // and drops it, so the wire must only ever carry concrete colours. The
    // resolution targets are the same system colours the effect resolves a
    // rule-side "accent" token against (zone highlight for the active/tint
    // slots, zone inactive for the inactive slot), so config-default and
    // rule-side accent stay one colour. The setter accepts the sentinel
    // (empty, or the legacy "accent" token these keys historically stored)
    // or any colour QColor parses, rejecting garbage at the boundary the
    // way REGISTER_THEME_FALLBACK_COLOR_SETTING (the scrolling TU's
    // raw-string form) does — every themeColor-vocabulary key accepts the
    // same sentinel spellings. The stored sentinel is visible through the
    // *Raw companion keys registered below.
#define REGISTER_RESOLVED_FALLBACK_COLOR(keyName, getter, setter, resolver)                                            \
    m_getters[QStringLiteral(keyName)] = [this]() {                                                                    \
        const QString raw = m_settings->getter();                                                                      \
        return raw.isEmpty() ? m_settings->resolver().name(QColor::HexArgb) : raw;                                     \
    };                                                                                                                 \
    m_setters[QStringLiteral(keyName)] = [this](const QVariant& v) {                                                   \
        QString s = v.toString();                                                                                      \
        if (s == kAccentToken) {                                                                                       \
            s = QString();                                                                                             \
        }                                                                                                              \
        if (!s.isEmpty() && !QColor(s).isValid()) {                                                                    \
            return false;                                                                                              \
        }                                                                                                              \
        m_settings->setter(s);                                                                                         \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(keyName)] = QStringLiteral("color");

    REGISTER_BOOL_SETTING("showWindowBorder", showWindowBorder, setShowWindowBorder)
    REGISTER_VALIDATED_STRING_SETTING("windowBorderScope", windowBorderScope, setWindowBorderScope,
                                      isWindowScopeToken(requested))
    REGISTER_INT_SETTING("windowBorderWidth", windowBorderWidth, setWindowBorderWidth)
    REGISTER_INT_SETTING("windowBorderRadius", windowBorderRadius, setWindowBorderRadius)
    REGISTER_RESOLVED_FALLBACK_COLOR("windowBorderColorActive", windowBorderColorActive, setWindowBorderColorActive,
                                     highlightColor)
    REGISTER_RESOLVED_FALLBACK_COLOR("windowBorderColorInactive", windowBorderColorInactive,
                                     setWindowBorderColorInactive, inactiveColor)
    REGISTER_BOOL_SETTING("hideWindowTitleBars", hideWindowTitleBars, setHideWindowTitleBars)
    REGISTER_VALIDATED_STRING_SETTING("windowTitleBarScope", windowTitleBarScope, setWindowTitleBarScope,
                                      isWindowScopeToken(requested))
    // Plain opacity+tint layer (same config-backed-default model as the
    // border block above); the tint colour resolves like the active border.
    REGISTER_BOOL_SETTING("showWindowOpacityTint", showWindowOpacityTint, setShowWindowOpacityTint)
    REGISTER_VALIDATED_STRING_SETTING("windowOpacityTintScope", windowOpacityTintScope, setWindowOpacityTintScope,
                                      isWindowScopeToken(requested))
    REGISTER_DOUBLE_SETTING("windowOpacity", windowOpacity, setWindowOpacity)
    REGISTER_DOUBLE_SETTING("windowTintStrength", windowTintStrength, setWindowTintStrength)
    REGISTER_RESOLVED_FALLBACK_COLOR("windowTintColor", windowTintColor, setWindowTintColor, highlightColor)
#undef REGISTER_RESOLVED_FALLBACK_COLOR

    // Companion RAW keys for every resolved theme-fallback colour above. The
    // resolved keys can never carry the sentinel (their getters resolve it
    // away), which made a getAllSettings → setSettings round-trip silently
    // pin every following colour and left external clients no way to READ
    // the follow/pin state. The raw keys serve the stored string verbatim
    // (empty = following) and their setters store verbatim, so a client
    // that wants an exact pin or an un-pin has an unambiguous surface — and
    // the documented round-trip is lossless again by construction ON A
    // CONCRETE Settings backend: QVariantMap iterates keys sorted, so
    // "<key>Raw" is always applied after "<key>" and the sentinel state
    // wins. (A bare-ISettings backend registers only the three window raws,
    // so the four zone colours still pin on a round-trip there — every
    // production composition root passes the concrete Settings.) Schema
    // token "themeColor" distinguishes them from plain strings for
    // schema-aware clients.
// @p capture names the one object each flavour needs (`this` for the
// m_settings forms, `concrete` for the zone raws) so neither flavour carries
// an unused capture.
#define REGISTER_RAW_THEME_COLOR(keyName, capture, obj, getter, setter)                                                \
    m_getters[QStringLiteral(keyName)] = [capture]() {                                                                 \
        return obj->getter();                                                                                          \
    };                                                                                                                 \
    m_setters[QStringLiteral(keyName)] = [capture](const QVariant& v) {                                                \
        QString s = v.toString();                                                                                      \
        if (s == kAccentToken) {                                                                                       \
            s = QString();                                                                                             \
        }                                                                                                              \
        if (!s.isEmpty() && !QColor(s).isValid()) {                                                                    \
            return false;                                                                                              \
        }                                                                                                              \
        obj->setter(s);                                                                                                \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(keyName)] = QStringLiteral("themeColor");

    REGISTER_RAW_THEME_COLOR("windowBorderColorActiveRaw", this, m_settings, windowBorderColorActive,
                             setWindowBorderColorActive)
    REGISTER_RAW_THEME_COLOR("windowBorderColorInactiveRaw", this, m_settings, windowBorderColorInactive,
                             setWindowBorderColorInactive)
    REGISTER_RAW_THEME_COLOR("windowTintColorRaw", this, m_settings, windowTintColor, setWindowTintColor)
    // The zone quartet's raw accessors live on the concrete Settings only.
    if (concrete) {
        REGISTER_RAW_THEME_COLOR("highlightColorRaw", concrete, concrete, highlightColorRaw, setHighlightColorRaw)
        REGISTER_RAW_THEME_COLOR("inactiveColorRaw", concrete, concrete, inactiveColorRaw, setInactiveColorRaw)
        REGISTER_RAW_THEME_COLOR("borderColorRaw", concrete, concrete, borderColorRaw, setBorderColorRaw)
        REGISTER_RAW_THEME_COLOR("labelFontColorRaw", concrete, concrete, labelFontColorRaw, setLabelFontColorRaw)
    }
#undef REGISTER_RAW_THEME_COLOR
    REGISTER_INT_SETTING("focusFadeDuration", focusFadeDuration, setFocusFadeDuration)
    REGISTER_STRING_SETTING("labelFontFamily", labelFontFamily, setLabelFontFamily)
    // Custom setter with range validation instead of REGISTER_DOUBLE_SETTING.
    // Bounds through ConfigDefaults, per CLAUDE.md's rule that config values
    // route through the accessors: with the numbers inlined this guard and the
    // schema's clamp were two independently-maintained spellings of the same
    // range, and retuning one left the other rejecting values the other
    // accepts.
    m_getters[QStringLiteral("labelFontSizeScale")] = [this]() {
        return m_settings->labelFontSizeScale();
    };
    m_setters[QStringLiteral("labelFontSizeScale")] = [this](const QVariant& v) {
        bool ok;
        double val = v.toDouble(&ok);
        if (!ok || val < ConfigDefaults::labelFontSizeScaleMin() || val > ConfigDefaults::labelFontSizeScaleMax()) {
            return false;
        }
        m_settings->setLabelFontSizeScale(val);
        return true;
    };
    m_schemas[QStringLiteral("labelFontSizeScale")] = QStringLiteral("double");
    REGISTER_INT_SETTING("labelFontWeight", labelFontWeight, setLabelFontWeight)
    REGISTER_BOOL_SETTING("labelFontItalic", labelFontItalic, setLabelFontItalic)
    REGISTER_BOOL_SETTING("labelFontUnderline", labelFontUnderline, setLabelFontUnderline)
    REGISTER_BOOL_SETTING("labelFontStrikeout", labelFontStrikeout, setLabelFontStrikeout)
    REGISTER_VALIDATED_STRING_SETTING("renderingBackend", renderingBackend, setRenderingBackend,
                                      ConfigDefaults::renderingBackendOptions().contains(requested.toLower().trimmed()))
    // gpuDevice: custom validating setter. The schema validator COERCES a
    // malformed value to "auto" (and the D-Bus schema surface exposes only
    // {key, type} for EVERY key — no token choices cross the bus for any of
    // them) — so a plain pass-through setter would report success for a
    // write that was silently discarded. Reject at the boundary instead:
    // only "auto" or a well-formed vendor:device pair (in any
    // case/whitespace variation that normalizes to itself) is accepted.
    m_getters[QStringLiteral("gpuDevice")] = [this]() {
        return m_settings->gpuDevice();
    };
    m_setters[QStringLiteral("gpuDevice")] = [this](const QVariant& v) {
        const QString raw = v.toString();
        const QString normalized = ConfigDefaults::normalizeGpuDevice(raw);
        if (normalized != raw.toLower().trimmed()) {
            return false;
        }
        m_settings->setGpuDevice(normalized);
        return true;
    };
    m_schemas[QStringLiteral("gpuDevice")] = QStringLiteral("string");
    REGISTER_INT_SETTING("shaderFrameRate", shaderFrameRate, setShaderFrameRate)
    REGISTER_BOOL_SETTING("enableAudioVisualizer", enableAudioVisualizer, setEnableAudioVisualizer)
    REGISTER_INT_SETTING("audioSpectrumBarCount", audioSpectrumBarCount, setAudioSpectrumBarCount)
    // The full CAVA analysis parameter set (Shaders.Audio): the KWin effect
    // runs its own cava instance and pulls every knob through this map via
    // loadSettingAsync, so each one must be registered here. An unregistered key
    // answers with a D-Bus error, the effect's value callback never runs, and the
    // knob is stuck on the effect's own built-in default with nothing to show for it
    // but a daemon-side warning.
    REGISTER_BOOL_SETTING("audioAutosens", audioAutosens, setAudioAutosens)
    REGISTER_INT_SETTING("audioSensitivity", audioSensitivity, setAudioSensitivity)
    REGISTER_INT_SETTING("audioNoiseReduction", audioNoiseReduction, setAudioNoiseReduction)
    REGISTER_INT_SETTING("audioLowerCutoffHz", audioLowerCutoffHz, setAudioLowerCutoffHz)
    REGISTER_INT_SETTING("audioHigherCutoffHz", audioHigherCutoffHz, setAudioHigherCutoffHz)
    REGISTER_BOOL_SETTING("audioMonstercat", audioMonstercat, setAudioMonstercat)
    REGISTER_BOOL_SETTING("audioWaves", audioWaves, setAudioWaves)
    REGISTER_VALIDATED_STRING_SETTING("audioChannelMode", audioChannelMode, setAudioChannelMode,
                                      ConfigDefaults::audioChannelModeOptions().contains(requested.toLower().trimmed()))
    REGISTER_BOOL_SETTING("audioReverse", audioReverse, setAudioReverse)
    REGISTER_INT_SETTING("audioExtraSmoothing", audioExtraSmoothing, setAudioExtraSmoothing)
    REGISTER_VALIDATED_STRING_SETTING("audioInputMethod", audioInputMethod, setAudioInputMethod,
                                      ConfigDefaults::audioInputMethodOptions().contains(requested.toLower().trimmed()))
    REGISTER_STRING_SETTING("audioInputSource", audioInputSource, setAudioInputSource)

    // Window filtering — the per-app / per-class exclusion lists
    // (excludedApplications, excludedWindowClasses) retired in v4 along
    // with their settings page; only the three global knobs below remain.
    REGISTER_BOOL_SETTING("excludeTransientWindows", excludeTransientWindows, setExcludeTransientWindows)
    REGISTER_INT_SETTING("minimumWindowWidth", minimumWindowWidth, setMinimumWindowWidth)
    REGISTER_INT_SETTING("minimumWindowHeight", minimumWindowHeight, setMinimumWindowHeight)

    // Animation window filtering — exposed on the same getSetting/setSetting
    // wire as the snapping/tiling exclusions but stored independently so a
    // user can disable animations for an app while still snapping it.
    REGISTER_BOOL_SETTING("animationExcludeTransientWindows", animationExcludeTransientWindows,
                          setAnimationExcludeTransientWindows)
    REGISTER_BOOL_SETTING("animationExcludeNotificationsAndOsd", animationExcludeNotificationsAndOsd,
                          setAnimationExcludeNotificationsAndOsd)
    REGISTER_INT_SETTING("animationMinimumWindowWidth", animationMinimumWindowWidth, setAnimationMinimumWindowWidth)
    REGISTER_INT_SETTING("animationMinimumWindowHeight", animationMinimumWindowHeight, setAnimationMinimumWindowHeight)

    // Decoration window filtering — same getSetting/setSetting wire, stored
    // independently so the KWin effect's border pass can be tuned separately
    // from snapping and animation filtering.
    REGISTER_BOOL_SETTING("decorationExcludeTransientWindows", decorationExcludeTransientWindows,
                          setDecorationExcludeTransientWindows)
    REGISTER_INT_SETTING("decorationMinimumWindowWidth", decorationMinimumWindowWidth, setDecorationMinimumWindowWidth)
    REGISTER_INT_SETTING("decorationMinimumWindowHeight", decorationMinimumWindowHeight,
                         setDecorationMinimumWindowHeight)

    // Decoration performance (Decorations.Performance). The KWin effect fetches these
    // by name over getSetting, which resolves through THIS registry, not through Qt
    // property reflection — so leaving a key out here disables the setting on the
    // effect side however complete the rest of its wiring looks. That is not
    // hypothetical: all three of these were missing once, and because getSetting then
    // answered an unknown key with a valid empty string, PauseWhenIdle's default of
    // true was being read back as false on every startup. tests/unit/dbus/
    // test_settings_registry_contract.cpp is the tripwire for the next one.
    //
    // decorationIdleTimeoutSec is read by the daemon directly, but registering it
    // keeps the wire surface complete (getSettingKeys / getAllSettingSchemas
    // enumerate this map).
    REGISTER_BOOL_SETTING("decorationAnimateFocusedOnly", decorationAnimateFocusedOnly, setDecorationAnimateFocusedOnly)
    REGISTER_BOOL_SETTING("decorationPauseWhenIdle", decorationPauseWhenIdle, setDecorationPauseWhenIdle)
    REGISTER_INT_SETTING("decorationIdleTimeoutSec", decorationIdleTimeoutSec, setDecorationIdleTimeoutSec)
    // animationExcludedApplications / animationExcludedWindowClasses
    // retired in v4 — folded into ExcludeAnimations Rules; the
    // effect derives its animation exclusion rule set from the unified
    // store via the Rules.rulesChanged subscription instead.

    // Animation settings (global — applies to snapping and autotiling)
    REGISTER_BOOL_SETTING("animationsEnabled", animationsEnabled, setAnimationsEnabled)
    REGISTER_INT_SETTING("animationDuration", animationDuration, setAnimationDuration)
    REGISTER_STRING_SETTING("animationEasingCurve", animationEasingCurve, setAnimationEasingCurve)
    REGISTER_INT_SETTING("animationMinDistance", animationMinDistance, setAnimationMinDistance)
    REGISTER_INT_SETTING("animationSequenceMode", animationSequenceMode, setAnimationSequenceMode)
    REGISTER_INT_SETTING("animationStaggerInterval", animationStaggerInterval, setAnimationStaggerInterval)

    // Phase 6: shader profile tree (JSON blob round-trip via D-Bus)
    if (concrete) {
        m_getters[QString(PhosphorProtocol::Service::SettingProperty::ShaderProfileTree)] = [concrete]() {
            return QString::fromUtf8(
                QJsonDocument(concrete->shaderProfileTree().toJson()).toJson(QJsonDocument::Compact));
        };
        m_setters[QString(PhosphorProtocol::Service::SettingProperty::ShaderProfileTree)] =
            [concrete](const QVariant& v) -> bool {
            QJsonDocument doc;
            if (!validProfileTreeBlob(v, &doc))
                return false;
            concrete->setShaderProfileTree(PhosphorAnimationShaders::ShaderProfileTree::fromJson(doc.object()));
            return true;
        };
        m_schemas[QString(PhosphorProtocol::Service::SettingProperty::ShaderProfileTree)] = QStringLiteral("string");
    }

    // Per-event motion-profile tree (read-only, JSON blob round-trip).
    //
    // The merged per-event `PhosphorAnimation::Profile` set — every
    // entry the daemon's `m_profileRegistry` holds, which is the SAME
    // registry the OverlayService SurfaceAnimator resolves OSD / popup
    // durations from. Settings persists per-event overrides as one
    // `profiles/<path>.json` file each; the daemon's ProfileLoader
    // scans them into the registry, and `publishActiveAnimationProfile`
    // registers the settings-driven `Global` profile on top.
    //
    // The kwin-effect lives in a separate process and cannot share the
    // registry object, so the merged set is flattened into a
    // `ProfileTree` and shipped over the bus: `Global` becomes the
    // tree baseline, every other path an override. The effect resolves
    // per-event durations from the tree exactly as the SurfaceAnimator
    // resolves them from the registry. Read-only — Settings owns the
    // authoritative per-event files, never the effect.
    if (m_profileRegistry) {
        auto* registry = m_profileRegistry;
        m_getters[QString(PhosphorProtocol::Service::SettingProperty::MotionProfileTree)] = [registry]() {
            // Seed-owned family defaults must NOT ship: every non-Global entry
            // below becomes a tree OVERRIDE, and an override's engaged fields
            // beat the effect's animator baseline in overlayChainOnto — a
            // `window` seed would pin all window legs to its duration/curve and
            // turn the global animation settings into a no-op (#795). Only
            // user-authored per-event profiles may outrank the baseline.
            const QHash<QString, PhosphorAnimation::Profile> profiles = registry->snapshotExcludingLowPrecedence();
            PhosphorAnimation::ProfileTree tree;
            for (auto it = profiles.constBegin(); it != profiles.constEnd(); ++it) {
                if (it.key() == PhosphorAnimation::ProfilePaths::Global) {
                    tree.setBaseline(it.value());
                } else {
                    tree.setOverride(it.key(), it.value());
                }
            }
            return QString::fromUtf8(QJsonDocument(tree.toJson()).toJson(QJsonDocument::Compact));
        };
        m_schemas[QString(PhosphorProtocol::Service::SettingProperty::MotionProfileTree)] = QStringLiteral("string");
    }

    // Phase 6: shader search paths (read-only, for KWin effect registry population).
    // Serialized as a JSON array string — QStringList in QDBusVariant can
    // deserialize as QDBusArgument on the receiving side, making toStringList()
    // return empty. Cached on first access since XDG dirs don't change at runtime.
    m_getters[QString(PhosphorProtocol::Service::SettingProperty::AnimationShaderSearchPaths)] = [this]() {
        if (!m_cachedShaderSearchPaths.isEmpty()) {
            return m_cachedShaderSearchPaths;
        }
        QStringList paths =
            QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/animations"),
                                      QStandardPaths::LocateDirectory);
        // REVERSE into ascending priority, exactly as the daemon's own registry
        // population does (shader_warmup.cpp). This list is registered verbatim
        // by the effect via addSearchPaths, and MetadataPackScanStrategy
        // reverse-iterates the registered paths and applies first-wins on an id
        // collision — so it requires lowest-priority-FIRST, while locateAll
        // hands back highest-priority-first. Publishing locateAll's order made
        // the strategy examine /usr/share before the user dir, so a user pack
        // that deliberately overrides a bundled id ("window-morph") was silently
        // shadowed in the COMPOSITOR while the daemon (which reverses) honoured
        // it — the same pack id resolving to different files in the two
        // processes.
        std::reverse(paths.begin(), paths.end());
        const QString userDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/plasmazones/animations");
        if (!paths.contains(userDir))
            paths.append(userDir);
        m_cachedShaderSearchPaths =
            QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(paths)).toJson(QJsonDocument::Compact));
        return m_cachedShaderSearchPaths;
    };
    m_schemas[QString(PhosphorProtocol::Service::SettingProperty::AnimationShaderSearchPaths)] =
        QStringLiteral("string");

    // The shared inner/outer gaps are config-backed (the Gaps group), written
    // by the settings app's Window Appearance page — not over this generic
    // map. But the editor (a separate process) reads the resolved global gap
    // values over D-Bus, so register READ-ONLY getters (no setter) for them; a
    // write attempt still fails because no setter is registered. All seven are
    // on the ISettings geometry interface (IZoneGeometrySettings), so they
    // register through m_settings like adjacentThreshold — a non-Settings
    // backend keeps the keys.
    m_getters[QStringLiteral("innerGap")] = [this]() {
        return m_settings->innerGap();
    };
    m_schemas[QStringLiteral("innerGap")] = QStringLiteral("int");
    m_getters[QStringLiteral("outerGap")] = [this]() {
        return m_settings->outerGap();
    };
    m_schemas[QStringLiteral("outerGap")] = QStringLiteral("int");
    m_getters[QStringLiteral("usePerSideOuterGap")] = [this]() {
        return m_settings->usePerSideOuterGap();
    };
    m_schemas[QStringLiteral("usePerSideOuterGap")] = QStringLiteral("bool");
    m_getters[QStringLiteral("outerGapTop")] = [this]() {
        return m_settings->outerGapTop();
    };
    m_schemas[QStringLiteral("outerGapTop")] = QStringLiteral("int");
    m_getters[QStringLiteral("outerGapBottom")] = [this]() {
        return m_settings->outerGapBottom();
    };
    m_schemas[QStringLiteral("outerGapBottom")] = QStringLiteral("int");
    m_getters[QStringLiteral("outerGapLeft")] = [this]() {
        return m_settings->outerGapLeft();
    };
    m_schemas[QStringLiteral("outerGapLeft")] = QStringLiteral("int");
    m_getters[QStringLiteral("outerGapRight")] = [this]() {
        return m_settings->outerGapRight();
    };
    m_schemas[QStringLiteral("outerGapRight")] = QStringLiteral("int");

    // Per-surface decoration tree (JSON blob round-trip via D-Bus), mirroring
    // the animation shaderProfileTree registration above. The out-of-process
    // settings app round-trips the tree here over D-Bus; the daemon consumes it
    // in-process (applyDecoration) to resolve each surface's decoration (the
    // user-applied shader-pack chain).
    m_getters[QString(PhosphorProtocol::Service::SettingProperty::DecorationProfileTree)] = [this]() {
        return m_settings->decorationProfileTreeJson();
    };
    m_setters[QString(PhosphorProtocol::Service::SettingProperty::DecorationProfileTree)] =
        [this](const QVariant& v) -> bool {
        QJsonDocument doc;
        if (!validProfileTreeBlob(v, &doc))
            return false;
        // Reuse the doc validProfileTreeBlob already parsed instead of
        // re-parsing the same UTF-8 through the JSON facade (mirrors the
        // ShaderProfileTree setter above).
        m_settings->setDecorationProfileTree(PhosphorSurfaceShaders::DecorationProfileTree::fromJson(doc.object()));
        return true;
    };
    m_schemas[QString(PhosphorProtocol::Service::SettingProperty::DecorationProfileTree)] = QStringLiteral("string");
    REGISTER_STRINGLIST_SETTING("lockedScreens", lockedScreens, setLockedScreens)

    // Per-mode families, one TU each: settingsadaptor_registry_snapping.cpp,
    // settingsadaptor_registry_autotile.cpp and
    // settingsadaptor_registry_scrolling.cpp. Split out of this file when it
    // crossed the size ceiling; the maps and the keys are the same, and
    // intra-family order is preserved. See the boundary rule in each file's
    // banner for which keys live where. Each callee re-derives `concrete`
    // itself.
    initializeRegistrySnapping();
    initializeRegistryAutotile();
    initializeRegistryScrolling();

    // Global shortcuts (concrete Settings only)
    if (concrete) {
        REGISTER_CONCRETE_STRING("openEditorShortcut", openEditorShortcut, setOpenEditorShortcut)
        REGISTER_CONCRETE_STRING("openSettingsShortcut", openSettingsShortcut, setOpenSettingsShortcut)
        REGISTER_CONCRETE_STRING("previousLayoutShortcut", previousLayoutShortcut, setPreviousLayoutShortcut)
        REGISTER_CONCRETE_STRING("nextLayoutShortcut", nextLayoutShortcut, setNextLayoutShortcut)
        REGISTER_CONCRETE_STRING("quickLayout1Shortcut", quickLayout1Shortcut, setQuickLayout1Shortcut)
        REGISTER_CONCRETE_STRING("quickLayout2Shortcut", quickLayout2Shortcut, setQuickLayout2Shortcut)
        REGISTER_CONCRETE_STRING("quickLayout3Shortcut", quickLayout3Shortcut, setQuickLayout3Shortcut)
        REGISTER_CONCRETE_STRING("quickLayout4Shortcut", quickLayout4Shortcut, setQuickLayout4Shortcut)
        REGISTER_CONCRETE_STRING("quickLayout5Shortcut", quickLayout5Shortcut, setQuickLayout5Shortcut)
        REGISTER_CONCRETE_STRING("quickLayout6Shortcut", quickLayout6Shortcut, setQuickLayout6Shortcut)
        REGISTER_CONCRETE_STRING("quickLayout7Shortcut", quickLayout7Shortcut, setQuickLayout7Shortcut)
        REGISTER_CONCRETE_STRING("quickLayout8Shortcut", quickLayout8Shortcut, setQuickLayout8Shortcut)
        REGISTER_CONCRETE_STRING("quickLayout9Shortcut", quickLayout9Shortcut, setQuickLayout9Shortcut)
        REGISTER_CONCRETE_STRING("toggleCheatsheetShortcut", toggleCheatsheetShortcut, setToggleCheatsheetShortcut)

        // Navigation shortcuts
        REGISTER_CONCRETE_STRING("moveWindowLeftShortcut", moveWindowLeftShortcut, setMoveWindowLeftShortcut)
        REGISTER_CONCRETE_STRING("moveWindowRightShortcut", moveWindowRightShortcut, setMoveWindowRightShortcut)
        REGISTER_CONCRETE_STRING("moveWindowUpShortcut", moveWindowUpShortcut, setMoveWindowUpShortcut)
        REGISTER_CONCRETE_STRING("moveWindowDownShortcut", moveWindowDownShortcut, setMoveWindowDownShortcut)
        REGISTER_CONCRETE_STRING("focusZoneLeftShortcut", focusZoneLeftShortcut, setFocusZoneLeftShortcut)
        REGISTER_CONCRETE_STRING("focusZoneRightShortcut", focusZoneRightShortcut, setFocusZoneRightShortcut)
        REGISTER_CONCRETE_STRING("focusZoneUpShortcut", focusZoneUpShortcut, setFocusZoneUpShortcut)
        REGISTER_CONCRETE_STRING("focusZoneDownShortcut", focusZoneDownShortcut, setFocusZoneDownShortcut)
        REGISTER_CONCRETE_STRING("pushToEmptyZoneShortcut", pushToEmptyZoneShortcut, setPushToEmptyZoneShortcut)
        REGISTER_CONCRETE_STRING("restoreWindowSizeShortcut", restoreWindowSizeShortcut, setRestoreWindowSizeShortcut)
        REGISTER_CONCRETE_STRING("toggleWindowFloatShortcut", toggleWindowFloatShortcut, setToggleWindowFloatShortcut)

        // Swap window shortcuts
        REGISTER_CONCRETE_STRING("swapWindowLeftShortcut", swapWindowLeftShortcut, setSwapWindowLeftShortcut)
        REGISTER_CONCRETE_STRING("swapWindowRightShortcut", swapWindowRightShortcut, setSwapWindowRightShortcut)
        REGISTER_CONCRETE_STRING("swapWindowUpShortcut", swapWindowUpShortcut, setSwapWindowUpShortcut)
        REGISTER_CONCRETE_STRING("swapWindowDownShortcut", swapWindowDownShortcut, setSwapWindowDownShortcut)

        // Span window shortcuts
        REGISTER_CONCRETE_STRING("spanWindowLeftShortcut", spanWindowLeftShortcut, setSpanWindowLeftShortcut)
        REGISTER_CONCRETE_STRING("spanWindowRightShortcut", spanWindowRightShortcut, setSpanWindowRightShortcut)
        REGISTER_CONCRETE_STRING("spanWindowUpShortcut", spanWindowUpShortcut, setSpanWindowUpShortcut)
        REGISTER_CONCRETE_STRING("spanWindowDownShortcut", spanWindowDownShortcut, setSpanWindowDownShortcut)

        // Other action shortcuts
        REGISTER_CONCRETE_STRING("rotateWindowsClockwiseShortcut", rotateWindowsClockwiseShortcut,
                                 setRotateWindowsClockwiseShortcut)
        REGISTER_CONCRETE_STRING("rotateWindowsCounterclockwiseShortcut", rotateWindowsCounterclockwiseShortcut,
                                 setRotateWindowsCounterclockwiseShortcut)
        REGISTER_CONCRETE_STRING("cycleWindowForwardShortcut", cycleWindowForwardShortcut,
                                 setCycleWindowForwardShortcut)
        REGISTER_CONCRETE_STRING("cycleWindowBackwardShortcut", cycleWindowBackwardShortcut,
                                 setCycleWindowBackwardShortcut)
        REGISTER_CONCRETE_STRING("layoutPickerShortcut", layoutPickerShortcut, setLayoutPickerShortcut)
        REGISTER_CONCRETE_STRING("toggleLayoutLockShortcut", toggleLayoutLockShortcut, setToggleLayoutLockShortcut)

        // Virtual screen shortcuts
        REGISTER_CONCRETE_STRING("swapVirtualScreenLeftShortcut", swapVirtualScreenLeftShortcut,
                                 setSwapVirtualScreenLeftShortcut)
        REGISTER_CONCRETE_STRING("swapVirtualScreenRightShortcut", swapVirtualScreenRightShortcut,
                                 setSwapVirtualScreenRightShortcut)
        REGISTER_CONCRETE_STRING("swapVirtualScreenUpShortcut", swapVirtualScreenUpShortcut,
                                 setSwapVirtualScreenUpShortcut)
        REGISTER_CONCRETE_STRING("swapVirtualScreenDownShortcut", swapVirtualScreenDownShortcut,
                                 setSwapVirtualScreenDownShortcut)
        REGISTER_CONCRETE_STRING("rotateVirtualScreensClockwiseShortcut", rotateVirtualScreensClockwiseShortcut,
                                 setRotateVirtualScreensClockwiseShortcut)
        REGISTER_CONCRETE_STRING("rotateVirtualScreensCounterclockwiseShortcut",
                                 rotateVirtualScreensCounterclockwiseShortcut,
                                 setRotateVirtualScreensCounterclockwiseShortcut)
    }

// Clean up macros (local scope)
#undef REGISTER_STRING_SETTING
#undef REGISTER_VALIDATED_STRING_SETTING
#undef REGISTER_BOOL_SETTING
#undef REGISTER_INT_SETTING
#undef REGISTER_DOUBLE_SETTING
#undef REGISTER_COLOR_SETTING
#undef REGISTER_STRINGLIST_SETTING
#undef REGISTER_CONCRETE_STRING
}

} // namespace PlasmaZones
