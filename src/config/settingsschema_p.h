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

/// Range clamp for numeric int keys (NOT enums — see validIntOr below for
/// why clamping an enum is wrong). Hoisted here so settingsschema.cpp and
/// settingsschema_scrolling.cpp share one definition.
inline auto clampInt(int minVal, int maxVal)
{
    return [minVal, maxVal](const QVariant& v) -> QVariant {
        return qBound(minVal, v.toInt(), maxVal);
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

/// How many entries a canonicalized preset list may keep. Same size-cap
/// rationale as canonicalTriggerList: a hand-edited file must not smuggle an
/// unbounded list past the setter path, since every entry is walked on each
/// width/height preset cycle. At namespace scope rather than inside the
/// function so ConfigDefaults::scrollingPresetIndexMax() — the schema ceiling
/// for a stored index INTO such a list — can be pinned against it by a
/// static_assert instead of by a comment (see settingsschema_scrolling.cpp).
inline constexpr int kMaxPresetEntries = 16;

/// How many raw entries the parse itself will examine. The keep-cap alone
/// bounds what SURVIVES, not the work: a list of ten thousand rejects never
/// fills `kept`, so the loop would split and toDouble every one of them on
/// every read, and the validator runs on the read path. Generous enough that
/// no plausible real list is truncated by it, small enough that a garbage
/// blob cannot turn a preset cycle into a parse of unbounded length.
inline constexpr int kMaxPresetScan = 256;

/// Canonicalize a comma-joined proportion list: parse each entry as a
/// decimal, keep only values in (0, @p maxValue] — the range the engine itself
/// accepts for a preset entry — de-dupe, and re-serialize. When NOTHING
/// survives (all-garbage hand edit or a cleared field), snap to @p fallback —
/// the key's default — matching the validIntOr/validStringOr convention.
/// Persisting the empty string instead would leave the page showing an empty
/// field while the engine silently cycles its built-ins: the accepted-but-dead
/// divergence this validator exists to prevent.
///
/// @p maxValue is a parameter rather than a literal 1.0 so each domain's
/// proportion ceiling has exactly one home: the width call site passes
/// ConfigDefaults::scrollingDefaultColumnWidthProportionMax() and the height
/// one its delegating twin scrollingWindowHeightProportionMax(), so the two
/// vocabularies can be retuned independently. This TU deliberately does not
/// include ConfigDefaults, since nothing else here is domain-specific.
inline QVariant canonicalProportionList(const QVariant& v, const QString& fallback, double maxValue)
{
    const auto canonicalize = [maxValue](const QString& raw) {
        QStringList kept;
        const QStringList parts = raw.split(QLatin1Char(','));
        const int scanned = qMin(static_cast<int>(parts.size()), kMaxPresetScan);
        for (int i = 0; i < scanned; ++i) {
            if (kept.size() >= kMaxPresetEntries) {
                break;
            }
            bool ok = false;
            const double val = parts.at(i).trimmed().toDouble(&ok);
            if (!ok || val <= 0.0 || val > maxValue) {
                continue;
            }
            const QString canonical = QString::number(val);
            if (!kept.contains(canonical)) {
                kept.append(canonical);
            }
        }
        return kept;
    };

    const QStringList kept = canonicalize(v.toString());
    if (!kept.isEmpty()) {
        return QVariant(kept.join(QLatin1Char(',')));
    }
    // Nothing survived. Run the fallback through the same canonicalization
    // rather than persisting it verbatim, so this validator is idempotent over
    // its own output. The defaults are already canonical today, which is
    // exactly why the verbatim return went unnoticed — a future default spelled
    // "0.50, 0.5" would otherwise persist with its duplicate intact on the
    // all-garbage path, defeating the de-dupe for that one input.
    const QStringList canonicalFallback = canonicalize(fallback);
    return QVariant(canonicalFallback.isEmpty() ? fallback : canonicalFallback.join(QLatin1Char(',')));
}

} // namespace PlasmaZones::SchemaValidators
