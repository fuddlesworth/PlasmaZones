// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Private detail header for the Settings partial translation units
// (settings.cpp + settings/*.cpp). Two kinds of shared detail live here:
//
//   1. The P_STORE_* getter/setter macros. Every group migrated to
//      PhosphorConfig::Store expands one of these to a mechanical
//      "read through m_store / write through m_store, then check change
//      before NOTIFY" body. They are NOT #undef'd — the partials are
//      concatenated into one unity translation unit, and an #undef at the
//      end of any one file would leave the rest of the unit without them.
//      The names are unique to this component, so leaving them defined to
//      end-of-TU is safe.
//
//   2. Free helpers shared by more than one partial: the disable-list
//      canonicalization (used by settings.cpp's load()/reset() and by
//      settings/disable.cpp), parseCommaList (used by storescalars.cpp's
//      Ordering section, by settings/disable.cpp's lockedScreens, and by
//      settings/scrolling.cpp's column-width lists), and
//      the scrolling column-width kind repair pair (used by
//      settings/scrolling.cpp for the global width key and by
//      settings/perscreen.cpp for the per-monitor one).
//      Kept inline in a nested namespace so each TU gets its own copy
//      without relying on unity-build merging for linkage.

#include "config/configdefaults.h"

#include <PhosphorScreens/ScreenIdentity.h>

#include <QLatin1Char>
#include <QString>
#include <QStringList>

#define P_STORE_GET(retType, fn, group, key, readType)                                                                 \
    retType Settings::fn() const                                                                                       \
    {                                                                                                                  \
        return m_store->read<readType>(ConfigDefaults::group(), ConfigDefaults::key());                                \
    }

#define P_STORE_SET_BOOL(fn, group, key, signal)                                                                       \
    void Settings::fn(bool value)                                                                                      \
    {                                                                                                                  \
        const bool before = m_store->read<bool>(ConfigDefaults::group(), ConfigDefaults::key());                       \
        m_store->write(ConfigDefaults::group(), ConfigDefaults::key(), value);                                         \
        const bool after = m_store->read<bool>(ConfigDefaults::group(), ConfigDefaults::key());                        \
        if (after == before) {                                                                                         \
            return;                                                                                                    \
        }                                                                                                              \
        Q_EMIT signal();                                                                                               \
        Q_EMIT settingsChanged();                                                                                      \
    }

#define P_STORE_SET_INT(fn, group, key, signal)                                                                        \
    void Settings::fn(int value)                                                                                       \
    {                                                                                                                  \
        const int before = m_store->read<int>(ConfigDefaults::group(), ConfigDefaults::key());                         \
        m_store->write(ConfigDefaults::group(), ConfigDefaults::key(), value);                                         \
        const int after = m_store->read<int>(ConfigDefaults::group(), ConfigDefaults::key());                          \
        if (after == before) {                                                                                         \
            return;                                                                                                    \
        }                                                                                                              \
        Q_EMIT signal();                                                                                               \
        Q_EMIT settingsChanged();                                                                                      \
    }

#define P_STORE_SET_DOUBLE(fn, group, key, signal)                                                                     \
    void Settings::fn(qreal value)                                                                                     \
    {                                                                                                                  \
        const qreal before = m_store->read<double>(ConfigDefaults::group(), ConfigDefaults::key());                    \
        m_store->write(ConfigDefaults::group(), ConfigDefaults::key(), value);                                         \
        const qreal after = m_store->read<double>(ConfigDefaults::group(), ConfigDefaults::key());                     \
        if (qFuzzyCompare(1.0 + before, 1.0 + after)) {                                                                \
            return;                                                                                                    \
        }                                                                                                              \
        Q_EMIT signal();                                                                                               \
        Q_EMIT settingsChanged();                                                                                      \
    }

#define P_STORE_SET_STRING(fn, group, key, signal)                                                                     \
    void Settings::fn(const QString& value)                                                                            \
    {                                                                                                                  \
        const QString before = m_store->read<QString>(ConfigDefaults::group(), ConfigDefaults::key());                 \
        m_store->write(ConfigDefaults::group(), ConfigDefaults::key(), value);                                         \
        const QString after = m_store->read<QString>(ConfigDefaults::group(), ConfigDefaults::key());                  \
        if (after == before) {                                                                                         \
            return;                                                                                                    \
        }                                                                                                              \
        Q_EMIT signal();                                                                                               \
        Q_EMIT settingsChanged();                                                                                      \
    }

// Like P_STORE_SET_STRING but announcing on TWO signals — for the raw
// theme-fallback colour strings, whose resolved QColor twin reads through the
// same stored value and must refresh alongside it.
#define P_STORE_SET_STRING2(fn, group, key, signal, twinSignal)                                                        \
    void Settings::fn(const QString& value)                                                                            \
    {                                                                                                                  \
        const QString before = m_store->read<QString>(ConfigDefaults::group(), ConfigDefaults::key());                 \
        m_store->write(ConfigDefaults::group(), ConfigDefaults::key(), value);                                         \
        const QString after = m_store->read<QString>(ConfigDefaults::group(), ConfigDefaults::key());                  \
        if (after == before) {                                                                                         \
            return;                                                                                                    \
        }                                                                                                              \
        Q_EMIT signal();                                                                                               \
        Q_EMIT twinSignal();                                                                                           \
        Q_EMIT settingsChanged();                                                                                      \
    }

namespace PlasmaZones {
namespace settings_detail {

// ── Per-mode disable-list helpers ────────────────────────────────────────────
// Declared up here rather than beside the disable-list section further down
// because load()'s change detection is the first user of them. See that
// section for what the (axis, mode) rule families are.

// Disable-axis enum mirrors the persisted (axis, mode) family layout. Distinct
// from `ContextRuleBridge::ContextAxis` only because the bridge enum also
// carries `CatchAll` and `Combined`, neither of which is a managed disable
// family — see `axisOf` for why `Combined` is excluded.
enum class DisableAxis {
    Monitor,
    Desktop,
    Activity
};

// Resolve a connector name ("DP-2") to its stable screen id
// ("Manuf:Model:Serial"), or return @p screen unchanged.
//
// The ONE resolution rule for the disable lists, shared by the getter
// (disabledMonitors, which resolves on every read) and by
// canonicalDisableEntries. The two must agree exactly: if the canonical form
// resolved an entry differently from the getter, re-saving an unchanged list
// would look like a change and misfire disabled*Changed.
//
// An unresolvable name falls through to the name itself, which is what we want:
// canonicalizing an entry down to an empty screen segment would erase which
// screen it names. That fall-through is idForName's own contract — when no live
// screen carries the connector (an unplugged monitor, a name from another
// machine) it returns the NAME UNCHANGED, never empty. So there is nothing to
// guard against here: for a connector name the result is either the resolved id
// or the name back, and a non-connector name (which includes the empty string,
// isConnectorName rejects it) is already canonical. Mirrors the fall-through in
// ScreenIdentity::variantsFor.
inline QString resolveScreenId(const QString& screen)
{
    if (PhosphorScreens::ScreenIdentity::isConnectorName(screen)) {
        return PhosphorScreens::ScreenIdentity::idForName(screen);
    }
    return screen;
}

// The canonical form of a disable list for @p axis: entries trimmed, their
// screen segment resolved through resolveScreenId, malformed entries dropped,
// duplicates collapsed, and the result sorted.
//
// This is what "the same disable list" means. Two lists are the same set of
// disable rules iff their canonical forms are equal — the store returns
// entries in rule order, so a raw list compare would report a change for a
// mere reordering (setAllRules rewrites the whole list, so rule order churns
// for reasons that have nothing to do with this axis).
//
// For Desktop/Activity the entry is a composite (`screenId/desktop`,
// `screenId/activity`); split on the LAST '/' so a screen id that legitimately
// contains one (the disambiguated `Manuf:Model:Serial/CONNECTOR` shape) isn't
// truncated. Entries the write path's parse loop would reject (missing or edge
// '/', and for Desktop a non-positive or non-numeric desktop segment) are
// dropped here as well: the canonical form has to be the EFFECTIVE set, or a
// write whose every kept entry already matches the current rules would look
// like a change.
inline QStringList canonicalDisableEntries(DisableAxis axis, const QStringList& list)
{
    QStringList c;
    for (const QString& raw : list) {
        QString value = raw.trimmed();
        if (axis == DisableAxis::Monitor) {
            value = resolveScreenId(value);
        } else {
            const int slash = value.lastIndexOf(QLatin1Char('/'));
            if (slash <= 0 || slash == value.size() - 1) {
                continue;
            }
            const QString screen = resolveScreenId(value.left(slash));
            if (axis == DisableAxis::Desktop) {
                bool ok = false;
                const int desktop = value.mid(slash + 1).toInt(&ok);
                if (!ok || desktop <= 0) {
                    continue;
                }
                // Rebuild the desktop segment via QString::number so the
                // canonical form matches the getter's serialization
                // (disableEntriesFor) — otherwise numeric aliases like "+3" or
                // "03" survive as distinct entries, defeat the write path's
                // no-op guard, and produce a second disable rule with the same
                // deterministic UUID.
                value = screen + QLatin1Char('/') + QString::number(desktop);
            } else {
                value = screen + QLatin1Char('/') + value.mid(slash + 1);
            }
        }
        if (!value.isEmpty() && !c.contains(value)) {
            c.append(value);
        }
    }
    c.sort();
    return c;
}

// ── Scrolling column-width kind repair ───────────────────────────────────────
// The width KIND and the width VALUE are two keys sharing one meaning, and the
// value's legal range depends entirely on the kind in force. The schema's
// clampDouble has to span both kinds' ranges (one key, one validator) and so
// cannot reject a value that is out of range for the kind actually stored.
// These two helpers are the real bound, and they live here rather than in
// either caller's TU because the global width pair (settings/scrolling.cpp)
// and the per-monitor one (settings/perscreen.cpp) MUST heal an inconsistent
// pair the same way. A second copy of the thresholds is how the two drift.
//
// Both take the kind as its stored wire value rather than an isFixed bool:
// ClientDecides and Preset store no width of their own and deliberately leave
// whatever the previous kind wrote in place (Preset resolves through its index
// key), so for those two the stored value is not theirs to touch. Collapsing
// the four kinds to a bool would clamp a retained 800px pixel width down to
// the proportion ceiling on a Fixed→ClientDecides→Fixed round trip.

/// Clamp into the kind's range. What a SETTER applies: a user dragging a
/// slider or typing in the SpinBox wants the nearest legal value, not a jump
/// back to a default.
inline qreal clampColumnWidthForKind(qreal value, int kind)
{
    if (kind == ConfigDefaults::scrollingWidthKindClientDecides()
        || kind == ConfigDefaults::scrollingWidthKindPreset()) {
        return value;
    }
    if (kind == ConfigDefaults::scrollingWidthKindFixed()) {
        return qBound<qreal>(ConfigDefaults::scrollingDefaultColumnWidthFixedMin(), value,
                             ConfigDefaults::scrollingDefaultColumnWidthFixedMax());
    }
    return qBound<qreal>(ConfigDefaults::scrollingDefaultColumnWidthProportionMin(), value,
                         ConfigDefaults::scrollingDefaultColumnWidthProportionMax());
}

/// Repair a value that cannot belong to @p kind at all, the way the KIND
/// setter's arms do: re-seed rather than clamp.
///
/// Clamping is wrong for this case and quietly produces the exact failure the
/// kind setter exists to avoid. A proportion of 0.5 left behind under Fixed
/// clamps to the 100px floor; worse, a pixel count of 800 left behind under
/// Proportion clamps to 1.0, which opens every column at 100% of the work
/// area. The repair paths have to agree, or the same broken pair heals
/// differently depending on which one found it.
inline qreal reseedColumnWidthForKind(qreal value, int kind)
{
    if (kind == ConfigDefaults::scrollingWidthKindClientDecides()
        || kind == ConfigDefaults::scrollingWidthKindPreset()) {
        return value;
    }
    const bool isFixed = kind == ConfigDefaults::scrollingWidthKindFixed();
    if (isFixed && value < ConfigDefaults::scrollingDefaultColumnWidthFixedMin()) {
        return ConfigDefaults::scrollingDefaultColumnWidthFixedPx();
    }
    if (!isFixed && value > ConfigDefaults::scrollingDefaultColumnWidthProportionMax()) {
        return ConfigDefaults::scrollingDefaultColumnWidthValue();
    }
    // In-kind but out of range: a clamp is the right repair, since the value
    // is the right SORT of thing.
    //
    // UNREACHABLE through the global pair, and deliberately kept as defence
    // rather than removed: the schema's clampDouble(ProportionMin, FixedMax)
    // validator runs on the READ path too, so that caller receives an
    // already-bounded value and this tail is an identity. It only starts
    // mattering if that clamp is widened for a future kind, or if a caller
    // ever reads the raw backend. Not unit-testable through the global caller
    // for the same reason — see the note in test_scrolling_settings.cpp's data
    // table.
    return clampColumnWidthForKind(value, kind);
}

inline QStringList parseCommaList(const QString& raw)
{
    if (raw.isEmpty()) {
        return {};
    }
    QStringList parts = raw.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (auto& s : parts) {
        s = s.trimmed();
    }
    return parts;
}

} // namespace settings_detail
} // namespace PlasmaZones
