// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Validator helpers shared between settingsschema.cpp and the per-domain
// schema TUs split out of it (settingsschema_scrolling.cpp). A helper lives
// here when it is reusable ACROSS DOMAINS, so the next domain TU to need it
// finds it instead of copying it. canonicalProportionList is here on that
// basis — both its current users are scrolling preset lists, but nothing
// about it is scrolling-specific. Helpers genuinely tied to one domain stay
// in that TU's anonymous namespace.
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
        const double d = v.toDouble();
        // qBound on NaN is unspecified — it decays to qMin/qMax comparisons,
        // all false for NaN, so a corrupt "nan" in the config would land on an
        // arbitrary bound depending on argument order. NaN is not a value, so
        // pin it deterministically to the minimum rather than leaving it to
        // that pathology.
        if (qIsNaN(d)) {
            return minVal;
        }
        return qBound(minVal, d, maxVal);
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
/// decimal, keep only values in (0, 1] — the range the engine itself accepts
/// for a preset entry — de-dupe, and re-serialize. When NOTHING survives
/// (all-garbage hand edit or a cleared field), snap to @p fallback — the
/// key's default — matching the validIntOr/validStringOr convention.
/// Persisting the empty string instead would leave the page showing an empty
/// field while the engine silently cycles its built-ins: the accepted-but-dead
/// divergence this validator exists to prevent.
inline QVariant canonicalProportionList(const QVariant& v, const QString& fallback)
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
        if (!ok || val <= 0.0 || val > 1.0) {
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
