// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configmigration.h"

#include "configkeys.h"
#include "configmigration_util.h"

#include <QColor>
#include <QJsonObject>
#include <QLatin1String>
#include <QStringList>
#include <QVariant>

namespace PlasmaZones {

// ── v5 → v6: snapping zone colours → theme-fallback strings ─────────────────
//
// The v5 schema stored the four snapping zone colours as concrete colours
// gated by one Snapping.Zones.Colors/UseSystem bool; when the bool was on,
// the settings layer WROTE palette-derived snapshots into those keys. v6
// makes the colour keys theme-fallback strings (the scrolling convention:
// EMPTY / absent means "follow the system palette", resolved in the getters)
// and drops the bool.
//
// Schema-migration freeze policy (mirrors migrateV4ToV5): every v5 group/key
// spelling and the one v5 compile default this step depends on is frozen here
// as a file-scope constant, decoupling the migration's stable wire-format
// contract from the live code (the UseSystem accessors were DELETED on this
// branch, and the colour key accessors could in principle be renamed later).

namespace {

// Frozen v5 group paths and key spellings.
constexpr QLatin1String kV5ColorsGroup{"Snapping.Zones.Colors"};
constexpr QLatin1String kV5LabelsGroup{"Snapping.Zones.Labels"};
constexpr QLatin1String kV5KeyUseSystem{"UseSystem"};
constexpr QLatin1String kV5KeyHighlight{"Highlight"};
constexpr QLatin1String kV5KeyInactive{"Inactive"};
constexpr QLatin1String kV5KeyBorder{"Border"};
constexpr QLatin1String kV5KeyFontColor{"FontColor"};
// The v5 compile default for UseSystem: an absent key means "on".
constexpr bool kV5DefUseSystemColors = true;
// The v5 compile defaults for the four zone colours, frozen as the exact
// #AARRGGBB spellings of PhosphorZones::ZoneDefaults at the time of the v6
// bump (HighlightColor 0,120,212@128; InactiveColor 128,128,128@64;
// BorderColor 255,255,255@200; LabelFontColor 255,255,255@255). Sparse
// persistence stored a default-equal pick as key ABSENCE, so with UseSystem
// off an absent key still meant "pinned to this constant" — the migration
// must materialise it or the pick silently starts following the palette.
constexpr QLatin1String kV5DefHighlightHex{"#800078d4"};
constexpr QLatin1String kV5DefInactiveHex{"#40808080"};
constexpr QLatin1String kV5DefBorderHex{"#c8ffffff"};
constexpr QLatin1String kV5DefFontColorHex{"#ffffffff"};
// The window-appearance colour keys, whose v5 sentinel was the literal
// "accent" (the rule vocabulary's token); v6 spells "follow the system
// accent" as the same empty sentinel every theme-fallback key uses.
constexpr QLatin1String kV5WindowsGroup{"Windows"};
constexpr QLatin1String kV5KeyBorderColorActive{"BorderColorActive"};
constexpr QLatin1String kV5KeyBorderColorInactive{"BorderColorInactive"};
constexpr QLatin1String kV5KeyTintColor{"TintColor"};
constexpr QLatin1String kV5AccentToken{"accent"};

// Parse a v5 colour value the way the v5 reader (JsonGroup::readColor) did:
// a #hex / named string via QColor, plus the legacy KConfig "r,g,b[,a]"
// comma form. Returns an invalid QColor for anything else. The comma form is
// reachable only from a hand-edited file: the INI conversion step already
// rewrites comma colours to #AARRGGBB before the chain runs, and JsonBackend
// only ever writes hex.
QColor parseV5Color(const QString& raw)
{
    if (raw.isEmpty()) {
        return {};
    }
    const QColor direct(raw);
    if (direct.isValid()) {
        return direct;
    }
    const QStringList parts = raw.split(QLatin1Char(','));
    if (parts.size() == 3 || parts.size() == 4) {
        bool okR = false;
        bool okG = false;
        bool okB = false;
        bool okA = true;
        const int r = parts.at(0).trimmed().toInt(&okR);
        const int g = parts.at(1).trimmed().toInt(&okG);
        const int b = parts.at(2).trimmed().toInt(&okB);
        const int a = parts.size() == 4 ? parts.at(3).trimmed().toInt(&okA) : 255;
        // Bound-check every component the way the INI conversion path does:
        // QColor::setAlpha silently ignores an out-of-range value, which
        // would keep a hand-edited bad alpha as opaque instead of rejecting.
        const auto inRange = [](int v) {
            return v >= 0 && v <= 255;
        };
        if (okR && okG && okB && okA && inRange(r) && inRange(g) && inRange(b) && inRange(a)) {
            QColor c(r, g, b);
            c.setAlpha(a);
            if (c.isValid()) {
                return c;
            }
        }
    }
    return {};
}

// Rewrite one colour key in place. Palette-owned (@p systemOn) or unparseable
// values become the EXPLICIT empty sentinel rather than being removed: in
// config.json the two are equivalent (the sentinel is the schema default, so
// the next sparse save prunes the key), but in a sparse profile delta removal
// means "inherit from the parent" while the explicit sentinel is what
// actually preserves "follow the palette". A genuine pick is normalised to
// the canonical #AARRGGBB spelling the v6 reader stores. An ABSENT key with
// UseSystem off is materialised to the frozen v5 default (@p v5DefaultHex):
// sparse persistence pruned default-equal picks, and under v5 that absence
// still resolved to the shipped constant, not to the palette.
void convertColorKey(QJsonObject& group, QLatin1String key, bool systemOn, QLatin1String v5DefaultHex)
{
    if (!group.contains(key)) {
        if (!systemOn) {
            group[key] = QString(v5DefaultHex);
        }
        return;
    }
    if (systemOn) {
        group[key] = QString();
        return;
    }
    const QColor c = parseV5Color(group.value(key).toString());
    if (!c.isValid()) {
        group[key] = QString();
        return;
    }
    group[key] = c.name(QColor::HexArgb);
}

// Whether a hand-edited NON-object value sits at @p segments or any ancestor
// of it. Such a value was read as {} by groupObjectAtPath and converted to
// nothing — it is not ours to delete OR to rebuild into an object, so both
// putGroupBack branches leave it in place rather than silently destroying
// user data the conversion never understood. NOTE: an ABSENT ancestor also
// returns true (the walk sees undefined, which is not an object), so
// putGroupBack refuses to CREATE missing ancestor chains. Every write this
// step performs is reachable only with its ancestors present, so that is
// correct here — a future step that must create ancestors needs its own
// path through setGroupAtSegments.
bool nonObjectOnPath(const QJsonObject& root, const QStringList& segments)
{
    QJsonValue cur(root);
    for (const QString& segment : segments) {
        if (!cur.isObject()) {
            return true; // non-object ancestor
        }
        cur = cur.toObject().value(segment);
    }
    return !cur.isUndefined() && !cur.isObject(); // scalar at the path itself
}

// Write @p group back at @p dotPath, pruning it (and now-empty ancestors)
// when the conversion emptied it.
void putGroupBack(QJsonObject& root, QLatin1String dotPath, const QJsonObject& group)
{
    const QStringList segments = QString(dotPath).split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (nonObjectOnPath(root, segments)) {
        return;
    }
    if (group.isEmpty()) {
        QJsonValue cur(root);
        for (const QString& segment : segments) {
            cur = cur.toObject().value(segment);
        }
        if (cur.isUndefined()) {
            return; // absent: nothing to prune
        }
        removeGroupAtSegments(root, segments);
    } else {
        setGroupAtSegments(root, segments, group);
    }
}

} // namespace

void ConfigMigration::migrateV5ToV6(QJsonObject& root)
{
    // Defense-in-depth idempotency guard, mirroring the earlier steps.
    if (root.value(ConfigKeys::versionKey()).toInt(0) >= 6) {
        return;
    }

    QJsonObject colors = groupObjectAtPath(root, kV5ColorsGroup);
    const QJsonObject colorsBefore = colors;
    const QJsonValue useSystem = colors.value(kV5KeyUseSystem);
    // Coerce any PRESENT value instead of gating on isBool(): an INI-era
    // config arrives with the bool as an int ("UseSystemColors=0" converts
    // to 0, not false), and the isBool() gate would silently flip an
    // explicit off back to the on default and drop the user's pinned
    // colours. QVariant::toBool handles the int and string spellings.
    const bool systemOn = useSystem.isUndefined() ? kV5DefUseSystemColors : useSystem.toVariant().toBool();

    // The stored v5 colours were palette snapshots while UseSystem was on,
    // so converting them to the explicit sentinel is what preserves the
    // user's intent: the keys keep following the palette. With UseSystem off
    // they were genuine picks and are kept, normalised to #AARRGGBB, with an
    // absent key materialised to the frozen v5 default it resolved to.
    colors.remove(kV5KeyUseSystem);
    convertColorKey(colors, kV5KeyHighlight, systemOn, kV5DefHighlightHex);
    convertColorKey(colors, kV5KeyInactive, systemOn, kV5DefInactiveHex);
    convertColorKey(colors, kV5KeyBorder, systemOn, kV5DefBorderHex);
    if (colors != colorsBefore) {
        putGroupBack(root, kV5ColorsGroup, colors);
    }

    // The label font colour rode the SAME Colors-group bool in v5: the
    // deleted applySystemColorScheme derived FontColor under that one
    // toggle, so the cross-group gate here reproduces v5 behaviour.
    QJsonObject labels = groupObjectAtPath(root, kV5LabelsGroup);
    const QJsonObject labelsBefore = labels;
    convertColorKey(labels, kV5KeyFontColor, systemOn, kV5DefFontColorHex);
    if (labels != labelsBefore) {
        putGroupBack(root, kV5LabelsGroup, labels);
    }

    // Window-appearance colours: the v5 "accent" sentinel becomes the empty
    // sentinel, written explicitly for the same sparse-delta reason as the
    // zone colours above (in config.json the next sparse save prunes it).
    // Sparse pruning makes an on-disk "accent" unlikely, but a hand-edited
    // config can carry it. Concrete hex picks stay VERBATIM — unlike the
    // zone keys these were already stored as strings in v5, so there is no
    // wire-format change to normalise; the read-side
    // canonicalThemeFallbackColor validator snaps any garbage to the
    // sentinel like the other colour keys.
    QJsonObject windows = groupObjectAtPath(root, kV5WindowsGroup);
    const QJsonObject windowsBefore = windows;
    for (const QLatin1String key : {kV5KeyBorderColorActive, kV5KeyBorderColorInactive, kV5KeyTintColor}) {
        if (windows.value(key).toString() == kV5AccentToken) {
            windows[key] = QString();
        }
    }
    if (windows != windowsBefore) {
        putGroupBack(root, kV5WindowsGroup, windows);
    }

    // Stamp the literal, not ConfigSchemaVersion — the historical step's
    // output must stay frozen when the chain grows again.
    root[ConfigKeys::versionKey()] = 6;
}

} // namespace PlasmaZones
