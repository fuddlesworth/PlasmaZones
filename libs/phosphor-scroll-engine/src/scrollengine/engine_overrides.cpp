// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Per-context rule/template override resolution: the effective* readers that
// layer a screen's override map over the cached config defaults. Split out of
// engine_core.cpp at the file-size ceiling; the map's writers
// (applyPerScreenConfig / clearPerScreenConfig) stay there with the rest of
// the engine's state handling.

#include <PhosphorScrollEngine/ScrollEngine.h>

#include "enginelimits.h"

#include <QList>
#include <QRect>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtGlobal>

namespace PhosphorScrollEngine {
namespace {
// Sanity bounds for tab-indicator overrides arriving through the public
// per-screen map. Deliberately NOT the rules layer's constants — this library
// does not depend on phosphor-rules — but the same numbers, because both
// ultimately mirror the config schema's clamps. They exist to reject a
// grossly malformed embedder-supplied override, not to re-implement the
// cascade's validation, so they are wide.
constexpr int kMinTabIndicatorGap = -64;
constexpr int kMaxTabIndicatorGap = 64;
constexpr int kMinTabIndicatorWidth = 1;
constexpr int kMaxTabIndicatorWidth = 64;

/// Shared validation for both template preset lists: every entry must clear
/// the given floor (the same bound the settings parser applies), and an
/// empty or entirely-invalid list means "no template" — never an empty
/// preset vocabulary, which would break cycling (the cycle verbs step
/// through this list).
/// Length is capped at kMaxTemplateEntries like the other public-API override
/// ingresses, and the raw scan is bounded by kMaxTemplateScan before that: the
/// keep cap alone bounds what survives, not the work, so an embedder-supplied
/// list of ten thousand rejects would be converted in full on every relayout.
/// See those constants for why.
QList<qreal> presetListFromOverride(const QVariantMap& overrides, const QString& key, qreal minFraction,
                                    const QList<qreal>& fallback)
{
    const auto it = overrides.constFind(key);
    if (it == overrides.constEnd()) {
        return fallback;
    }
    QList<qreal> out;
    const QVariantList raw = it->toList();
    out.reserve(qMin(int(raw.size()), kMaxTemplateEntries));
    const int scanned = qMin(int(raw.size()), kMaxTemplateScan);
    for (int i = 0; i < scanned; ++i) {
        if (out.size() == kMaxTemplateEntries) {
            break;
        }
        bool ok = false;
        const qreal v = raw.at(i).toDouble(&ok);
        if (ok && v >= minFraction && v <= 1.0) {
            out.append(v);
        }
    }
    return out.isEmpty() ? fallback : out;
}
} // namespace

CenterFocusedColumn ScrollEngine::effectiveCenterFocusedColumn(const QString& screenId) const
{
    return effectiveCenterFocusedColumn(m_perScreenOverrides.value(screenId));
}

CenterFocusedColumn ScrollEngine::effectiveCenterFocusedColumn(const QVariantMap& overrides) const
{
    const auto it = overrides.constFind(ScrollPerScreenKeys::centerFocusedColumn());
    if (it != overrides.constEnd()) {
        const int mode = it->toInt();
        if (mode >= 0 && mode <= 2) {
            return static_cast<CenterFocusedColumn>(mode);
        }
    }
    return m_centerFocusedColumn;
}

QList<qreal> ScrollEngine::effectivePresetColumnWidths(const QString& screenId) const
{
    return effectivePresetColumnWidths(m_perScreenOverrides.value(screenId));
}

QList<qreal> ScrollEngine::effectivePresetColumnWidths(const QVariantMap& overrides) const
{
    return presetListFromOverride(overrides, ScrollPerScreenKeys::presetColumnWidths(), MinColumnWidthFraction,
                                  m_presetColumnWidths);
}

QList<qreal> ScrollEngine::effectivePresetWindowHeights(const QString& screenId) const
{
    return effectivePresetWindowHeights(m_perScreenOverrides.value(screenId));
}

QList<qreal> ScrollEngine::effectivePresetWindowHeights(const QVariantMap& overrides) const
{
    return presetListFromOverride(overrides, ScrollPerScreenKeys::presetWindowHeights(), MinWindowHeightFraction,
                                  m_presetWindowHeights);
}

ColumnWidth ScrollEngine::effectiveDefaultColumnWidth(const QString& screenId) const
{
    return effectiveDefaultColumnWidth(m_perScreenOverrides.value(screenId));
}

ColumnWidth ScrollEngine::effectiveDefaultColumnWidth(const QVariantMap& overrides) const
{
    return effectiveDefaultColumnWidth(overrides, effectivePresetColumnWidths(overrides));
}

ColumnWidth ScrollEngine::effectiveDefaultColumnWidth(const QVariantMap& overrides,
                                                      const QList<qreal>& presetWidths) const
{
    // Validate-then-fall-back, like its two siblings: an out-of-range rule
    // value is not silently reshaped into a legal one, because a rule author
    // who wrote 0.001 asked for something this engine cannot do and should
    // get the configured default rather than an arbitrary clamp result. (The
    // per-WINDOW open rule qBounds instead — that value reaches a single
    // column the user is watching open, so nudging it into range is the less
    // surprising answer there.)
    // Rule channel first (rule > per-screen setting > global): the bare
    // fraction key is written only by the rule cascade.
    const auto it = overrides.constFind(ScrollPerScreenKeys::defaultColumnWidth());
    if (it != overrides.constEnd()) {
        const qreal fraction = it->toDouble();
        if (fraction >= MinColumnWidthFraction && fraction <= 1.0) {
            return ColumnWidth::makeProportion(fraction);
        }
    }
    // Settings channel: the kind-aware trio from the per-screen override map.
    // Every layer writes the trio's keys INDEPENDENTLY, so a per-screen kind
    // beside an untouched value or preset spin is the ordinary case, not a
    // malformed pair. Each slot therefore falls back on its own to the cached
    // GLOBAL's matching slot; the per-screen KIND still decides. Falling back
    // to the whole global instead discarded the per-screen kind, and reading
    // an absent preset spin as 0 pinned the monitor to the first preset while
    // the UI showed the inherited index.
    const auto kindIt = overrides.constFind(ScrollPerScreenKeys::defaultColumnWidthKind());
    if (kindIt != overrides.constEnd()) {
        const int kind = kindIt->toInt();
        const auto valueIt = overrides.constFind(ScrollPerScreenKeys::defaultColumnWidthValue());
        const auto presetIt = overrides.constFind(ScrollPerScreenKeys::defaultColumnWidthPresetIndex());
        if (kind == static_cast<int>(DefaultWidthKind::Fixed)) {
            const qreal value = valueIt != overrides.constEnd()
                ? valueIt->toDouble()
                : (m_defaultColumnWidth.kind == ColumnWidth::Fixed ? qreal(m_defaultColumnWidth.fixedPx) : 0.0);
            if (value >= 1.0) {
                return ColumnWidth::makeFixed(qRound(value));
            }
        }
        if (kind == static_cast<int>(DefaultWidthKind::Preset)) {
            // The per-screen SPIN is an index into this screen's EFFECTIVE
            // vocabulary; resolve it to a value anchor here (the index clamp
            // is the deliberate stale-but-honest read — the preset list can
            // legitimately shrink under a stored spin). An absent spin
            // inherits the global slot, which is already a fraction —
            // relayout snaps it into this screen's vocabulary. The .value
            // fallback is a belt: the effective lists cannot be empty today.
            if (presetIt != overrides.constEnd()) {
                // The empty case exits first: qBound asserts on an inverted
                // range, which is what size() - 1 gives for an empty list.
                if (presetWidths.isEmpty()) {
                    return ColumnWidth::makePreset(0.5);
                }
                const int spin = qBound(0, presetIt->toInt(), int(presetWidths.size()) - 1);
                return ColumnWidth::makePreset(presetWidths.at(spin));
            }
            return m_defaultColumnWidth.kind == ColumnWidth::Preset
                ? m_defaultColumnWidth
                : ColumnWidth::makePreset(presetWidths.value(0, 0.5));
        }
        if (kind == static_cast<int>(DefaultWidthKind::Proportion)) {
            const qreal value = valueIt != overrides.constEnd()
                ? valueIt->toDouble()
                : (m_defaultColumnWidth.kind == ColumnWidth::Proportion ? m_defaultColumnWidth.proportion : 0.0);
            if (value >= MinColumnWidthFraction && value <= 1.0) {
                return ColumnWidth::makeProportion(value);
            }
        }
        // ClientDecides (and a kind whose resolved value is still out of
        // range) falls through to the global — the open path decides the
        // client-sized case via effectiveWidthClientDecides, and this
        // function only ever supplies the fallback width for it.
    }
    // The global default is a value anchor now — no effective-list clamp
    // needed; resolution snaps it into whatever vocabulary is live.
    return m_defaultColumnWidth;
}

bool ScrollEngine::effectiveWidthClientDecides(const QString& screenId) const
{
    const QVariantMap overrides = m_perScreenOverrides.value(screenId);
    const auto kindIt = overrides.constFind(ScrollPerScreenKeys::defaultColumnWidthKind());
    if (kindIt != overrides.constEnd()) {
        return kindIt->toInt() == static_cast<int>(DefaultWidthKind::ClientDecides);
    }
    return m_defaultWidthClientDecides;
}

WindowHeight ScrollEngine::effectiveDefaultWindowHeight(const QString& screenId, const QRect& workArea) const
{
    return effectiveDefaultWindowHeight(m_perScreenOverrides.value(screenId), workArea);
}

WindowHeight ScrollEngine::effectiveDefaultWindowHeight(const QVariantMap& overrides, const QRect& workArea) const
{
    return effectiveDefaultWindowHeight(overrides, workArea, effectivePresetWindowHeights(overrides));
}

WindowHeight ScrollEngine::effectiveDefaultWindowHeight(const QVariantMap& overrides, const QRect& workArea,
                                                        const QList<qreal>& presetHeights) const
{
    // Rule channel: a bare work-area fraction, committed as Fixed pixels
    // against the CURRENT work area (same resolution the adjust verbs use).
    const auto it = overrides.constFind(ScrollPerScreenKeys::defaultWindowHeight());
    if (it != overrides.constEnd() && workArea.height() > 0) {
        const qreal fraction = it->toDouble();
        if (fraction > 0.0 && fraction <= 1.0) {
            return WindowHeight::makeFixed(qMax(1, qRound(fraction * workArea.height())));
        }
    }
    // Settings channel: the kind trio, resolved per SLOT against the cached
    // global — see effectiveDefaultColumnWidth for why a partial trio is the
    // ordinary case rather than a malformed pair.
    const auto kindIt = overrides.constFind(ScrollPerScreenKeys::defaultWindowHeightKind());
    if (kindIt != overrides.constEnd()) {
        const int kind = kindIt->toInt();
        const auto valueIt = overrides.constFind(ScrollPerScreenKeys::defaultWindowHeightValue());
        const auto presetIt = overrides.constFind(ScrollPerScreenKeys::defaultWindowHeightPresetIndex());
        if (kind == static_cast<int>(DefaultHeightKind::Fixed)) {
            const qreal value = valueIt != overrides.constEnd()
                ? valueIt->toDouble()
                : (m_defaultWindowHeight.kind == WindowHeight::Fixed ? qreal(m_defaultWindowHeight.fixedPx) : 0.0);
            if (value >= 1.0) {
                return WindowHeight::makeFixed(qRound(value));
            }
        } else if (kind == static_cast<int>(DefaultHeightKind::Preset)) {
            // Same spin-to-value resolution as the width twin above.
            if (presetIt != overrides.constEnd()) {
                // Empty-first, for the width twin's qBound reason.
                if (presetHeights.isEmpty()) {
                    return WindowHeight::makePreset(0.5);
                }
                const int spin = qBound(0, presetIt->toInt(), int(presetHeights.size()) - 1);
                return WindowHeight::makePreset(presetHeights.at(spin));
            }
            return m_defaultWindowHeight.kind == WindowHeight::Preset
                ? m_defaultWindowHeight
                : WindowHeight::makePreset(presetHeights.value(0, 0.5));
        } else if (kind == static_cast<int>(DefaultHeightKind::Auto)) {
            return WindowHeight{};
        }
    }
    // Value anchor: no clamp, resolution snaps (see the width twin).
    return m_defaultWindowHeight;
}

ScrollInsertPosition ScrollEngine::effectiveInsertPosition(const QString& screenId) const
{
    return effectiveInsertPosition(m_perScreenOverrides.value(screenId));
}

ScrollInsertPosition ScrollEngine::effectiveInsertPosition(const QVariantMap& overrides) const
{
    const auto it = overrides.constFind(ScrollPerScreenKeys::insertPosition());
    if (it != overrides.constEnd()) {
        const int pos = it->toInt();
        if (pos >= static_cast<int>(ScrollInsertPosition::RightOfActive)
            && pos <= static_cast<int>(ScrollInsertPosition::IntoActiveColumn)) {
            return static_cast<ScrollInsertPosition>(pos);
        }
    }
    return m_insertPosition;
}

ColumnDisplay ScrollEngine::effectiveDefaultColumnDisplay(const QString& screenId) const
{
    return effectiveDefaultColumnDisplay(m_perScreenOverrides.value(screenId));
}

ColumnDisplay ScrollEngine::effectiveDefaultColumnDisplay(const QVariantMap& overrides) const
{
    // Validate-then-fall-back, same terms as the two siblings. Reading "any
    // value that is not 1" as Normal meant a garbage override (or a future
    // display kind this build does not know) silently overrode the user's
    // configured default with Normal instead of leaving it alone.
    const auto it = overrides.constFind(ScrollPerScreenKeys::defaultColumnDisplay());
    if (it != overrides.constEnd()) {
        const int display = it->toInt();
        if (display == static_cast<int>(ColumnDisplay::Normal) || display == static_cast<int>(ColumnDisplay::Tabbed)) {
            return static_cast<ColumnDisplay>(display);
        }
    }
    return m_defaultColumnDisplay;
}

TabIndicatorParams ScrollEngine::tabIndicatorParamsForScreen(const QString& screenId) const
{
    return effectiveTabIndicator(screenId);
}

TabIndicatorParams ScrollEngine::effectiveTabIndicator(const QString& screenId) const
{
    return effectiveTabIndicator(m_perScreenOverrides.value(screenId));
}

TabIndicatorParams ScrollEngine::effectiveTabIndicator(const QVariantMap& overrides) const
{
    TabIndicatorParams params = m_tabIndicator;
    if (overrides.isEmpty()) {
        return params;
    }
    namespace K = ScrollPerScreenKeys;
    // Per-property: an override map carrying only one key must leave the other
    // six on their configured values, the same contract the width trio has.
    // Each read is guarded by constFind rather than value(), because a default
    // -constructed QVariant would otherwise read as false / 0 and silently
    // override the configured value with a zero.
    const auto readBool = [&overrides](const QString& key, bool& out) {
        const auto it = overrides.constFind(key);
        if (it != overrides.constEnd()) {
            out = it->toBool();
        }
    };
    // Bounded, for the same public-API reason the length belt below states: an
    // embedder can hand this library an override map directly, and an
    // unbounded width or gap feeds the reservation arithmetic that decides how
    // much of the column the window gets. Validate-then-fall-back, so a
    // garbage override leaves the configured value alone.
    const auto readInt = [&overrides](const QString& key, int& out, int lo, int hi) {
        const auto it = overrides.constFind(key);
        if (it != overrides.constEnd()) {
            bool ok = false;
            const int v = it->toInt(&ok);
            if (ok && v >= lo && v <= hi) {
                out = v;
            }
        }
    };
    readBool(K::tabIndicatorEnabled(), params.enabled);
    readBool(K::tabIndicatorHideWhenSingleTab(), params.hideWhenSingleTab);
    readBool(K::tabIndicatorPlaceWithinColumn(), params.placeWithinColumn);
    readInt(K::tabIndicatorGap(), params.gap, kMinTabIndicatorGap, kMaxTabIndicatorGap);
    readInt(K::tabIndicatorWidth(), params.width, kMinTabIndicatorWidth, kMaxTabIndicatorWidth);
    const auto lengthIt = overrides.constFind(K::tabIndicatorLengthProportion());
    if (lengthIt != overrides.constEnd()) {
        bool ok = false;
        const qreal v = lengthIt->toDouble(&ok);
        // A belt at the library boundary. The rule cascade DOES range-check
        // this upstream (layoutregistry_contextresolve.cpp checks it against
        // the same Min/MaxTabIndicatorLengthRatio the descriptor validates),
        // but this library is public API and an embedder can hand it an
        // override map directly, where a zero or negative proportion would
        // resolve the indicator to a sliver while every setting reports it on.
        if (ok && v > 0.0) {
            params.lengthProportion = qMin(v, qreal(1.0));
        }
    }
    // Validate-then-fall-back, the terms effectiveDefaultColumnDisplay uses:
    // a garbage override must leave the configured position alone rather than
    // silently snapping the indicator to Left.
    const auto posIt = overrides.constFind(K::tabIndicatorPosition());
    if (posIt != overrides.constEnd()) {
        const int pos = posIt->toInt();
        if (pos >= static_cast<int>(TabIndicatorPosition::Left)
            && pos <= static_cast<int>(TabIndicatorPosition::Bottom)) {
            params.position = static_cast<TabIndicatorPosition>(pos);
        }
    }
    return params;
}

} // namespace PhosphorScrollEngine
