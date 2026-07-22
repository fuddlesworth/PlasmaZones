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
//      settings/disable.cpp) and parseCommaList (used by settings.cpp's
//      Ordering section and by settings/disable.cpp's lockedScreens).
//      Kept inline in a nested namespace so each TU gets its own copy
//      without relying on unity-build merging for linkage.

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

#define P_STORE_SET_COLOR(fn, group, key, signal)                                                                      \
    void Settings::fn(const QColor& value)                                                                             \
    {                                                                                                                  \
        const QColor before = m_store->read<QColor>(ConfigDefaults::group(), ConfigDefaults::key());                   \
        m_store->write(ConfigDefaults::group(), ConfigDefaults::key(), value);                                         \
        const QColor after = m_store->read<QColor>(ConfigDefaults::group(), ConfigDefaults::key());                    \
        if (after == before) {                                                                                         \
            return;                                                                                                    \
        }                                                                                                              \
        /* applySystemColorScheme() derives under this flag in load() AND in                                           \
           eventFilter()'s palette re-derive: both announce once themselves                                            \
           (snapshot/diff + a single settingsChanged) — a setter-level                                               \
           emission here would duplicate both. */                                                                      \
        if (m_suppressDerivedColorEmissions) {                                                                         \
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
