// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configmigration.h"

#include "configkeys.h"
#include "configmigration_util.h"

#include <QColor>
#include <QJsonObject>
#include <QLatin1String>
#include <QStringList>

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

// Parse a v5 colour value the way the v5 reader (JsonGroup::readColor) did:
// a #hex / named string via QColor, plus the legacy KConfig "r,g,b[,a]"
// comma form. Returns an invalid QColor for anything else.
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
        if (okR && okG && okB && okA) {
            QColor c(r, g, b);
            c.setAlpha(a);
            if (c.isValid()) {
                return c;
            }
        }
    }
    return {};
}

// Rewrite one colour key in place: drop it when the palette owned it
// (@p systemOn) or when its value cannot be parsed as a colour (the v6
// validator would snap it to the sentinel anyway); otherwise normalise the
// user's pick to the canonical #AARRGGBB spelling the v6 reader stores.
void convertColorKey(QJsonObject& group, QLatin1String key, bool systemOn)
{
    if (!group.contains(key)) {
        return;
    }
    if (systemOn) {
        group.remove(key);
        return;
    }
    const QColor c = parseV5Color(group.value(key).toString());
    if (!c.isValid()) {
        group.remove(key);
        return;
    }
    group[key] = c.name(QColor::HexArgb);
}

// Write @p group back at @p dotPath, pruning it (and now-empty ancestors)
// when the conversion emptied it.
void putGroupBack(QJsonObject& root, QLatin1String dotPath, const QJsonObject& group)
{
    const QStringList segments = QString(dotPath).split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (group.isEmpty()) {
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
    const QJsonValue useSystem = colors.value(kV5KeyUseSystem);
    const bool systemOn = useSystem.isBool() ? useSystem.toBool() : kV5DefUseSystemColors;

    // The stored v5 colours were palette snapshots while UseSystem was on, so
    // dropping them (rather than carrying them as pins) is what preserves the
    // user's intent: the keys fall back to the empty sentinel and keep
    // following the palette. With UseSystem off they were genuine picks and
    // are kept, normalised to #AARRGGBB.
    colors.remove(kV5KeyUseSystem);
    convertColorKey(colors, kV5KeyHighlight, systemOn);
    convertColorKey(colors, kV5KeyInactive, systemOn);
    convertColorKey(colors, kV5KeyBorder, systemOn);
    putGroupBack(root, kV5ColorsGroup, colors);

    QJsonObject labels = groupObjectAtPath(root, kV5LabelsGroup);
    convertColorKey(labels, kV5KeyFontColor, systemOn);
    putGroupBack(root, kV5LabelsGroup, labels);

    // Stamp the literal, not ConfigSchemaVersion — the historical step's
    // output must stay frozen when the chain grows again.
    root[ConfigKeys::versionKey()] = 6;
}

} // namespace PlasmaZones
