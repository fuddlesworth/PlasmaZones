// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Per-screen-config + effective-config resolvers for ScrollEngine. Split out
// of ScrollEngine.cpp to keep that translation unit under the 800-line limit.

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace PhosphorScrollEngine {

using PhosphorEngine::IScrollSettings;

// ─────────────────────────────────────────────────────────────────────────
// Per-screen config (override → global default)
// ─────────────────────────────────────────────────────────────────────────

void ScrollEngine::applyPerScreenConfig(const QString& screenId, const QVariantMap& overrides)
{
    if (screenId.isEmpty()) {
        return;
    }
    if (overrides.isEmpty()) {
        m_perScreenConfig.remove(screenId);
    } else {
        m_perScreenConfig.insert(screenId, overrides);
    }
}

void ScrollEngine::clearPerScreenConfig(const QString& screenId)
{
    m_perScreenConfig.remove(screenId);
}

QVariantMap ScrollEngine::perScreenOverrides(const QString& screenId) const
{
    return m_perScreenConfig.value(screenId);
}

QVariant ScrollEngine::perScreenValue(const QString& screenId, QLatin1String key) const
{
    const auto it = m_perScreenConfig.constFind(screenId);
    return it == m_perScreenConfig.constEnd() ? QVariant() : it->value(key);
}

QVector<qreal> ScrollEngine::clampedFractionVector(const QVariantList& list)
{
    QVector<qreal> out = toFractionVector(list);
    for (qreal& fraction : out) {
        fraction = qBound(kMinSizeFraction, fraction, kMaxSizeFraction);
    }
    return out;
}

// The effective*() resolvers clamp every per-screen override on read — the
// daemon already passes Settings-validated values, but clamping here too is
// the defence-in-depth pattern AutotileEngine's PerScreenConfigResolver uses,
// and it keeps a malformed override (e.g. a non-numeric DefaultColumnWidth
// coerced to 0.0) from yielding a degenerate column instead of a sane bound.

// Hard default the effective*() resolvers fall back to when no IScrollSettings
// is wired. The daemon always wires one in production, so these only ever
// apply in engine tests that do not install an IScrollSettings (e.g.
// FakeScrollSettings).
namespace {
// niri's default preset fractions — one third, one half, two thirds.
const QVector<qreal> kNiriPresetFractions = {1.0 / 3.0, 0.5, 2.0 / 3.0};
}

QVector<qreal> ScrollEngine::effectivePresetColumnWidths(const QString& screenId) const
{
    // A user that clears every preset would otherwise turn the cycle-width
    // shortcut into a silent no-op (cyclePresetColumnWidth bails on an empty
    // list). Fall back to the niri defaults when both the per-screen override
    // and the global setting resolve to an empty list, so the shortcut keeps
    // working — symmetric with effectivePresetWindowHeights below.
    const QVariant v = perScreenValue(screenId, QLatin1String("PresetColumnWidths"));
    if (v.isValid()) {
        const QVector<qreal> overrideList = clampedFractionVector(v.toList());
        if (!overrideList.isEmpty()) {
            return overrideList;
        }
    }
    if (const IScrollSettings* s = scrollSettings()) {
        const QVector<qreal> global = clampedFractionVector(s->scrollPresetColumnWidths());
        if (!global.isEmpty()) {
            return global;
        }
    }
    return kNiriPresetFractions;
}

QVector<qreal> ScrollEngine::effectivePresetWindowHeights(const QString& screenId) const
{
    const QVariant v = perScreenValue(screenId, QLatin1String("PresetWindowHeights"));
    if (v.isValid()) {
        const QVector<qreal> overrideList = clampedFractionVector(v.toList());
        if (!overrideList.isEmpty()) {
            return overrideList;
        }
    }
    if (const IScrollSettings* s = scrollSettings()) {
        const QVector<qreal> global = clampedFractionVector(s->scrollPresetWindowHeights());
        if (!global.isEmpty()) {
            return global;
        }
    }
    return kNiriPresetFractions;
}

qreal ScrollEngine::effectiveDefaultColumnWidth(const QString& screenId) const
{
    const QVariant v = perScreenValue(screenId, QLatin1String("DefaultColumnWidth"));
    if (v.isValid()) {
        return qBound(kMinSizeFraction, v.toReal(), kMaxSizeFraction);
    }
    const IScrollSettings* s = scrollSettings();
    return s ? qBound(kMinSizeFraction, s->scrollDefaultColumnWidth(), kMaxSizeFraction) : kDefaultColumnWidthFraction;
}

ScrollViewportMode ScrollEngine::effectiveViewportMode(const QString& screenId) const
{
    const QVariant v = perScreenValue(screenId, QLatin1String("CenterFocusedColumn"));
    if (v.isValid()) {
        return v.toBool() ? ScrollViewportMode::Centered : ScrollViewportMode::Fit;
    }
    const IScrollSettings* s = scrollSettings();
    if (s && s->scrollCenterFocusedColumn()) {
        return ScrollViewportMode::Centered;
    }
    return ScrollViewportMode::Fit;
}

int ScrollEngine::effectiveInnerGap(const QString& screenId) const
{
    const QVariant v = perScreenValue(screenId, QLatin1String("InnerGap"));
    if (v.isValid()) {
        return qBound(kMinStripGap, v.toInt(), kMaxStripGap);
    }
    const IScrollSettings* s = scrollSettings();
    return s ? qBound(kMinStripGap, s->scrollInnerGap(), kMaxStripGap) : kDefaultStripGap;
}

int ScrollEngine::effectiveOuterGap(const QString& screenId) const
{
    const QVariant v = perScreenValue(screenId, QLatin1String("OuterGap"));
    if (v.isValid()) {
        return qBound(kMinStripGap, v.toInt(), kMaxStripGap);
    }
    const IScrollSettings* s = scrollSettings();
    return s ? qBound(kMinStripGap, s->scrollOuterGap(), kMaxStripGap) : kDefaultStripGap;
}

} // namespace PhosphorScrollEngine
