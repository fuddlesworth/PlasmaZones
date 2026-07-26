// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Validator helpers shared between settingsschema.cpp and the per-domain
// schema TUs split out of it (settingsschema_scrolling.cpp). Only the helpers
// with more than one TU's worth of users live here; the ones used by a single
// group stay in that TU's anonymous namespace.
//
// Each helper returns a function object with the same shape as
// PhosphorConfig::KeyDef::validator.

#include <QLatin1Char>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>
#include <QtGlobal>

#include <initializer_list>

namespace PlasmaZones::SchemaValidators {

inline auto clampDouble(double minVal, double maxVal)
{
    return [minVal, maxVal](const QVariant& v) -> QVariant {
        return qBound(minVal, v.toDouble(), maxVal);
    };
}

/// Snap-to-default enum validator: accept a value only if it appears in the
/// explicit valid set, otherwise return @p fallback. Used for enums where
/// qBound would silently reinterpret out-of-range values as the nearest
/// neighbour — that's the exact bug the effect-side cache loader avoids,
/// and both readers must agree (see testAutotile*_unknownValueClampsToFloat).
inline auto validIntOr(std::initializer_list<int> valid, int fallback)
{
    return [valid = QVector<int>(valid), fallback](const QVariant& v) -> QVariant {
        const int raw = v.toInt();
        return valid.contains(raw) ? raw : fallback;
    };
}

/// Canonicalize a comma-joined proportion list: parse each entry as a
/// decimal, keep only values in [@p floor, 1] (a @p floor of 0 keeps the bare
/// "greater than zero" rule), de-dupe, and re-serialize. When NOTHING
/// survives (all-garbage hand edit, a cleared field, or every entry below the
/// floor), snap to @p fallback — the key's default — matching the
/// validIntOr/validStringOr convention. Persisting the empty string instead
/// would leave the page showing an empty field while the engine silently
/// cycles its built-ins: the accepted-but-dead divergence this validator
/// exists to prevent.
///
/// @p floor exists because a preset list feeds the same width vocabulary as
/// the scalar width key, whose kind-aware setter refuses anything under
/// ConfigDefaults::scrollingDefaultColumnWidthValueMin(). A preset below that
/// floor would be accepted here and then clamped away downstream — the same
/// accepted-but-dead divergence, one layer deeper. Entries under the floor are
/// DROPPED rather than clamped, matching how this validator already treats
/// every other out-of-range entry (clamping would silently mint a duplicate of
/// the floor for each of them).
inline QVariant canonicalProportionList(const QVariant& v, const QString& fallback, double floor = 0.0)
{
    // Same size-cap rationale as canonicalTriggerList: a hand-edited file
    // must not smuggle an unbounded list past the setter path (each entry
    // is walked on every width/height preset cycle).
    constexpr int kMaxPresetEntries = 16;
    const QStringList parts = v.toString().split(QLatin1Char(','));
    QStringList kept;
    for (const QString& raw : parts) {
        if (kept.size() >= kMaxPresetEntries) {
            break;
        }
        bool ok = false;
        const double val = raw.trimmed().toDouble(&ok);
        if (!ok || val <= 0.0 || val > 1.0 || val < floor) {
            continue;
        }
        const QString canonical = QString::number(val);
        if (!kept.contains(canonical)) {
            kept.append(canonical);
        }
    }
    if (kept.isEmpty()) {
        return QVariant(fallback);
    }
    return QVariant(kept.join(QLatin1Char(',')));
}

} // namespace PlasmaZones::SchemaValidators
