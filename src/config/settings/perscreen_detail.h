// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Shared internals of the per-screen override stores, split out when the
// selector families moved to perscreen_selector.cpp. Everything here was
// file-local to perscreen.cpp before that split; it is a private header
// between the two TUs, not API.
//
// Two kinds of thing live here: the D-Bus boundary guards every per-screen
// validator is built from, and the storage-key resolution the stores share.
// The key resolution is what lets a lookup find an entry regardless of which
// of the two keying conventions (connector name or EDID id) it was written
// under, so it has to be identical across every family — which is exactly why
// it is shared rather than reimplemented per store.

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <PhosphorConfig/IBackend.h>

namespace PlasmaZones {

class Settings;

namespace PerScreenDetail {

/// Storage-key form of a screen identifier, canonicalized for WRITES.
/// Defined in perscreen.cpp alongside Settings::canonicalPerScreenKey, which
/// it must stay in lockstep with.
QStringList perScreenKeyVariants(const QString& screenIdOrName);

/// Canonical write-side key. A thin forwarder to Settings::canonicalPerScreenKey
/// so this header need not pull in settings.h.
QString canonicalWriteKey(const QString& screenIdOrName);

// ── D-Bus boundary guards ───────────────────────────────────────────────────
// Per-screen values arrive as raw QVariants from the settings adaptor
// dispatch. QVariant::toInt()/toDouble() silently coerce a non-numeric payload
// to 0, which the validators would then accept or clamp as a real override
// (e.g. Position "garbage" -> 0 = TopLeft stored). These reject a
// non-convertible payload with an invalid QVariant instead.

inline QVariant boundedInt(const QVariant& value, int min, int max)
{
    bool ok = false;
    const int v = value.toInt(&ok);
    return ok ? QVariant(qBound(min, v, max)) : QVariant();
}

/// Enum-range check over [0, max]; rejects non-numeric payloads.
inline QVariant enumInRange(const QVariant& value, int max)
{
    bool ok = false;
    const int v = value.toInt(&ok);
    return (ok && v >= 0 && v <= max) ? QVariant(v) : QVariant();
}

// ── Entry lookup across both keying conventions ─────────────────────────────

template<typename T>
typename QHash<QString, T>::const_iterator findPerScreenEntry(const QHash<QString, T>& hash,
                                                              const QString& screenIdOrName)
{
    for (const QString& key : perScreenKeyVariants(screenIdOrName)) {
        auto it = hash.constFind(key);
        if (it != hash.constEnd())
            return it;
    }
    return hash.constEnd();
}

template<typename T>
bool removePerScreenEntry(QHash<QString, T>& hash, const QString& screenIdOrName)
{
    for (const QString& key : perScreenKeyVariants(screenIdOrName)) {
        if (hash.remove(key))
            return true;
    }
    return false;
}

/// Mutable sibling of findPerScreenEntry — same id/connector resolution, used
/// by the per-screen setter (applyPerScreenSetting) and the gaps/algorithm
/// sub-domain partial-clears.
template<typename T>
typename QHash<QString, T>::iterator findPerScreenEntryMutable(QHash<QString, T>& hash, const QString& screenIdOrName)
{
    for (const QString& key : perScreenKeyVariants(screenIdOrName)) {
        auto it = hash.find(key);
        if (it != hash.end())
            return it;
    }
    return hash.end();
}

/// Store a single validated per-screen override under @p key. Matches any
/// existing entry under either the connector or EDID id form and updates it in
/// place, so a write never creates a duplicate under the alternate form and a
/// no-op write never default-inserts an empty husk entry (which the
/// hasPerScreen* readers would treat as a phantom override). New entries key by
/// the canonical EDID form. Returns true if the map was mutated (caller emits
/// the change signals).
inline bool applyPerScreenSetting(QHash<QString, QVariantMap>& hash, const QString& screenIdOrName, const QString& key,
                                  const QVariant& validated)
{
    auto it = findPerScreenEntryMutable(hash, screenIdOrName);
    if (it != hash.end()) {
        if (it.value().value(key) == validated)
            return false;
        it.value()[key] = validated;
    } else {
        hash[canonicalWriteKey(screenIdOrName)][key] = validated;
    }
    return true;
}

/// Whether the screen's override map holds any key the predicate selects
/// (@p wantMatch true) or any key OUTSIDE the predicate (@p wantMatch false).
/// The predicate answers a pure key-classification; callers pass whichever
/// sub-domain they own, so the parameter cannot be named after any one of
/// them. Hoisted from perscreen.cpp so the selector TU's sub-domain pairs
/// share the one definition.
inline bool hasPerScreenKeySubset(const QHash<QString, QVariantMap>& hash, const QString& screenIdOrName,
                                  bool (*isSubsetKey)(const QString&), bool wantMatch)
{
    auto it = findPerScreenEntry(hash, screenIdOrName);
    if (it == hash.constEnd())
        return false;
    for (auto k = it.value().constBegin(); k != it.value().constEnd(); ++k) {
        if (isSubsetKey(k.key()) == wantMatch)
            return true;
    }
    return false;
}

/// Remove only the keys the predicate selects from the screen's override map,
/// dropping the whole entry once empty. @p isSubsetKey and @p clearMatch pair
/// exactly as in hasPerScreenKeySubset. Returns true if anything changed.
inline bool clearPerScreenKeySubset(QHash<QString, QVariantMap>& hash, const QString& screenIdOrName,
                                    bool (*isSubsetKey)(const QString&), bool clearMatch)
{
    auto it = findPerScreenEntryMutable(hash, screenIdOrName);
    if (it == hash.end())
        return false;
    QVariantMap& overrides = it.value();
    bool changed = false;
    for (auto k = overrides.begin(); k != overrides.end();) {
        if (isSubsetKey(k.key()) == clearMatch) {
            k = overrides.erase(k);
            changed = true;
        } else {
            ++k;
        }
    }
    if (overrides.isEmpty())
        hash.erase(it);
    return changed;
}

// ── Zone-selector families (perscreen_selector.cpp) ─────────────────────────
// The load/save passes live in perscreen.cpp and drive loadPerScreenGroup with
// these, so the tables and their validators are visible across the split.

/// Every ZoneSelectorConfigKey — the snapping selector's full vocabulary.
extern const QLatin1String kZoneSelectorKeys[9];
/// The strip selector's subset: no LayoutMode / GridColumns / MaxRows.
extern const QLatin1String kStripSelectorKeys[6];

QVariant readZoneSelectorEntry(PhosphorConfig::IGroup& group, const QString& key);
QVariant validateZoneSelectorValue(const QString& key, const QVariant& value);
QVariant validateStripSelectorValue(const QString& key, const QVariant& value);

} // namespace PerScreenDetail
} // namespace PlasmaZones
