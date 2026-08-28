// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Per-context rule/template override resolution: the effective* readers that
// layer a screen's override map over the cached config defaults. Split out of
// engine_core.cpp at the file-size ceiling; the map's writers
// (applyPerScreenConfig / clearPerScreenConfig) stay there with the rest of
// the engine's state handling.

#include <PhosphorScrollEngine/ScrollEngine.h>

// Complete type needed for the qobject_cast in effectiveFocusNewWindows.
// ScrollEngine.h includes it already (for the kDragScroll* defaults), but it
// is included directly here rather than relied on transitively, so a future
// header trim cannot break the cast (the nounity build's role in catching
// exactly this).
#include <PhosphorScrollEngine/IScrollSettings.h>

#include "enginelimits.h"

#include <QList>
#include <QMetaType>
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
/// The length proportion's pair, named for the same reason its four siblings
/// above are rather than living as bare literals at the read: the floor is a
/// real decision (a proportion small enough to resolve to a sliver reads as a
/// broken indicator while every setting still reports it on), and hiding it in
/// a `> 0.0` test made it look like a null check. Mirrors the rules layer's
/// Min/MaxTabIndicatorLengthRatio by value, hand-copied on the same terms as
/// the bounds above.
constexpr qreal kMinTabIndicatorLengthProportion = 0.05;
constexpr qreal kMaxTabIndicatorLengthProportion = 1.0;

/// Validated int read out of an override map, and the reason every int
/// resolver in this file goes through it: QVariant::toInt() answers 0 for a
/// value it cannot convert, and 0 is a LEGAL value of every enum resolved
/// here (Never, Proportion, RightOfActive, Normal, Left, TreatAsNormal), so
/// an unchecked read cannot tell a rule that asked for the first enumerator
/// from an embedder's malformed value — and would silently override the
/// user's configured default with that enumerator. Reject-and-fall-through,
/// the same stance effectiveBoolOverride takes on the bool keys.
/// @return false when the key is absent or the value is not convertible,
/// which is the caller's fall-through-to-the-global signal.
bool overrideInt(const QVariantMap& overrides, const QString& key, int& out)
{
    const auto it = overrides.constFind(key);
    if (it == overrides.constEnd()) {
        return false;
    }
    // Bools refused explicitly, the effectiveBoolOverride stance in the
    // other direction: toInt(&ok) happily converts one (true -> 1), which
    // would pin a real enumerator for a payload aimed at the wrong key.
    if (it->typeId() == QMetaType::Bool) {
        return false;
    }
    bool ok = false;
    const int value = it->toInt(&ok);
    if (!ok) {
        return false;
    }
    out = value;
    return true;
}

/// The qreal twin of overrideInt, on the same terms: an unconvertible value
/// reads as 0.0, which every fraction range in this file happens to reject —
/// but only by accident of the ranges, so the type check is made explicit
/// here rather than left resting on that.
bool overrideDouble(const QVariantMap& overrides, const QString& key, qreal& out)
{
    const auto it = overrides.constFind(key);
    if (it == overrides.constEnd()) {
        return false;
    }
    // Same bool refusal as overrideInt: toDouble on a bool answers 1.0.
    if (it->typeId() == QMetaType::Bool) {
        return false;
    }
    bool ok = false;
    const qreal value = it->toDouble(&ok);
    if (!ok) {
        return false;
    }
    out = value;
    return true;
}

/// Whether @p kind names a DefaultWidthKind this build knows. Shared by the
/// width resolver and the ClientDecides gate so the two halves of one
/// setting cannot disagree about which kinds exist.
bool isKnownWidthKind(int kind)
{
    return kind == static_cast<int>(DefaultWidthKind::Proportion) || kind == static_cast<int>(DefaultWidthKind::Fixed)
        || kind == static_cast<int>(DefaultWidthKind::ClientDecides)
        || kind == static_cast<int>(DefaultWidthKind::Preset);
}

/// Whether @p kind names a DefaultHeightKind this build knows. The height
/// twin of isKnownWidthKind, and shared by the same two halves for the same
/// reason.
bool isKnownHeightKind(int kind)
{
    return kind == static_cast<int>(DefaultHeightKind::Auto) || kind == static_cast<int>(DefaultHeightKind::Fixed)
        || kind == static_cast<int>(DefaultHeightKind::Preset)
        || kind == static_cast<int>(DefaultHeightKind::ClientDecides);
}

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
    return effectiveCenterFocusedColumn(overridesForScreen(screenId));
}

CenterFocusedColumn ScrollEngine::effectiveCenterFocusedColumn(const QVariantMap& overrides) const
{
    int mode = 0;
    if (overrideInt(overrides, ScrollPerScreenKeys::centerFocusedColumn(), mode) && mode >= 0 && mode <= 2) {
        return static_cast<CenterFocusedColumn>(mode);
    }
    return m_centerFocusedColumn;
}

StripAxis ScrollEngine::effectiveStripAxis(const QVariantMap& overrides, const QRect& workArea) const
{
    // Reads the TRI-STATE intent (0 auto, 1 horizontal, 2 vertical) and
    // returns the RESOLVED two-valued axis. Returning the resolved type is
    // what makes it structurally impossible for Auto to escape into layout
    // math — there is no third value for a caller to mishandle.
    //
    // The two numberings deliberately disagree (config Horizontal is 1, the
    // protocol's is 0), so this is a switch and never a cast.
    int intent = 0;
    if (!overrideInt(overrides, ScrollPerScreenKeys::stripAxis(), intent)) {
        intent = m_stripAxis;
    }
    switch (intent) {
    case 1:
        return StripAxis::horizontal();
    case 2:
        return StripAxis::vertical();
    default:
        break;
    }
    // Auto, and anything unrecognised: derive from the work area. An
    // out-of-range intent degrades to Auto rather than to a fixed axis,
    // because Auto is the setting's own default and the one answer that is
    // never wrong for the shape of the screen.
    return resolveStripAxis(workArea);
}

// ── behaviour toggles ──
// One shape for all eight callers (the five behaviour toggles plus the three
// tab-indicator bools): a rule-written per-screen key wins, an absent key
// falls back to the member the global config seeded. The value is taken only
// when it is a real bool — a hand-edited string would otherwise coerce to
// false and silently disable a behaviour, the same reject-and-fall-through
// stance the context resolver takes on the way in.
bool ScrollEngine::effectiveBoolOverride(const QVariantMap& overrides, const QString& key, bool fallback)
{
    const auto it = overrides.constFind(key);
    if (it != overrides.constEnd() && it->typeId() == QMetaType::Bool) {
        return it->toBool();
    }
    return fallback;
}

bool ScrollEngine::effectiveAlwaysCenterSingleColumn(const QVariantMap& overrides) const
{
    return effectiveBoolOverride(overrides, ScrollPerScreenKeys::alwaysCenterSingleColumn(),
                                 m_alwaysCenterSingleColumn);
}

bool ScrollEngine::effectiveRespectMinimumSize(const QVariantMap& overrides) const
{
    return effectiveBoolOverride(overrides, ScrollPerScreenKeys::respectMinimumSize(), m_respectMinimumSize);
}

bool ScrollEngine::effectiveCropStraddlers(const QVariantMap& overrides) const
{
    return effectiveBoolOverride(overrides, ScrollPerScreenKeys::cropStraddlers(), m_cropStraddlers);
}

bool ScrollEngine::effectiveFocusNewWindows(const QString& screenId) const
{
    bool fallback = true;
    if (auto* settings = qobject_cast<PhosphorEngine::IScrollSettings*>(engineSettings())) {
        fallback = settings->scrollingFocusNewWindows();
    }
    return effectiveBoolOverride(overridesForScreen(screenId), ScrollPerScreenKeys::focusNewWindows(), fallback);
}

bool ScrollEngine::effectiveSmartGaps(const QVariantMap& overrides) const
{
    return effectiveBoolOverride(overrides, ScrollPerScreenKeys::smartGaps(), m_smartGaps);
}

PhosphorEngine::StickyWindowHandling ScrollEngine::effectiveStickyWindowHandling(const QString& screenId) const
{
    return effectiveStickyWindowHandling(overridesForScreen(screenId));
}

PhosphorEngine::StickyWindowHandling ScrollEngine::effectiveStickyWindowHandling(const QVariantMap& overrides) const
{
    int mode = 0;
    // Same guarded cast as the config read in loadSettings: an out-of-range
    // int must not become an undefined enumerator.
    if (overrideInt(overrides, ScrollPerScreenKeys::stickyWindowHandling(), mode)
        && mode >= static_cast<int>(PhosphorEngine::StickyWindowHandling::TreatAsNormal)
        && mode <= static_cast<int>(PhosphorEngine::StickyWindowHandling::IgnoreAll)) {
        return static_cast<PhosphorEngine::StickyWindowHandling>(mode);
    }
    return m_stickyWindowHandling;
}

QList<qreal> ScrollEngine::effectivePresetColumnWidths(const QString& screenId) const
{
    return effectivePresetColumnWidths(overridesForScreen(screenId));
}

QList<qreal> ScrollEngine::effectivePresetColumnWidths(const QVariantMap& overrides) const
{
    return presetListFromOverride(overrides, ScrollPerScreenKeys::presetColumnWidths(), MinColumnWidthFraction,
                                  m_presetColumnWidths);
}

QList<qreal> ScrollEngine::effectivePresetWindowHeights(const QString& screenId) const
{
    return effectivePresetWindowHeights(overridesForScreen(screenId));
}

QList<qreal> ScrollEngine::effectivePresetWindowHeights(const QVariantMap& overrides) const
{
    return presetListFromOverride(overrides, ScrollPerScreenKeys::presetWindowHeights(), MinWindowHeightFraction,
                                  m_presetWindowHeights);
}

ScrollBlueprintProgress ScrollEngine::blueprintProgressForScreen(const QString& screenId) const
{
    ScrollBlueprintProgress progress;
    // Ownership gate, so the documented "a screen this engine does not own
    // reports {0, 0}" holds for a direct caller too. The in-tree path is
    // gated a layer up in ScrollingAdaptor, but this is exported library
    // surface: an embedder asking about a screen the engine has since given
    // up would otherwise get the progress its surviving overrides still
    // resolve to.
    if (screenId.isEmpty() || !m_scrollingScreens.contains(screenId)) {
        return progress;
    }
    const QVariantMap overrides = overridesForScreen(screenId);
    const auto blueprintIt = overrides.constFind(ScrollPerScreenKeys::templateColumns());
    if (blueprintIt == overrides.constEnd()) {
        return progress;
    }
    // Capped the same way the consumption site indexes, so `total` counts
    // entries that can actually be spent rather than everything an embedder
    // supplied. A readout claiming twenty starting columns while the engine
    // will only ever consume sixteen would be a lie in the direction that
    // matters (the user waits for columns that never come).
    //
    // Entries are counted as POSITIONS, not as carriers of usable overrides,
    // which is exactly how the consumption site spends them: an entry whose
    // width and display are both absent or malformed still names a starting
    // column, and that column opens on the resolved defaults. Counting only
    // "contributing" entries here would make total and used disagree with the
    // cursor the moment a blueprint carried one, which is the divergence this
    // pair exists to avoid.
    progress.total = qMin(int(blueprintIt->toList().size()), kMaxTemplateEntries);
    if (progress.total == 0) {
        return progress;
    }
    // The CURRENT context's state, matching visibleStripJson. A screen with no
    // state yet has spent nothing, which is the honest answer rather than an
    // absence.
    const ScrollState* state = m_states.stateForKey(currentKeyForScreen(screenId));
    if (!state) {
        return progress;
    }
    // The same floor the consumption site applies, for the same reason:
    // columns that arrived through a non-consuming path still stand for the
    // entries their positions cover, so a readout keyed on the bare cursor
    // would under-report a restored strip.
    const int spent = qMax(state->blueprintCursor(), int(state->strip().columns().size()));
    progress.used = qMin(spent, progress.total);
    return progress;
}

ColumnWidth ScrollEngine::effectiveDefaultColumnWidth(const QString& screenId) const
{
    return effectiveDefaultColumnWidth(overridesForScreen(screenId));
}

ColumnWidth ScrollEngine::effectiveDefaultColumnWidth(const QVariantMap& overrides) const
{
    return effectiveDefaultColumnWidth(overrides, effectivePresetColumnWidths(overrides));
}

std::optional<qreal> ScrollEngine::ruleColumnWidthFraction(const QVariantMap& overrides)
{
    qreal fraction = 0.0;
    if (overrideDouble(overrides, ScrollPerScreenKeys::defaultColumnWidth(), fraction)
        && fraction >= MinColumnWidthFraction && fraction <= 1.0) {
        return fraction;
    }
    return std::nullopt;
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
    // fraction key is written only by the rule cascade. Through the shared
    // helper, so the open path's ClientDecides gate can ask the SAME question
    // this resolver answers rather than testing the key's presence.
    if (const auto ruleFraction = ruleColumnWidthFraction(overrides)) {
        return ColumnWidth::makeProportion(*ruleFraction);
    }
    // Settings channel: the kind-aware trio from the per-screen override map.
    // Every layer writes the trio's keys INDEPENDENTLY, so a per-screen kind
    // beside an untouched value or preset spin is the ordinary case, not a
    // malformed pair. Each slot therefore falls back on its own to the cached
    // GLOBAL's matching slot; the per-screen KIND still decides. Falling back
    // to the whole global instead discarded the per-screen kind, and reading
    // an absent preset spin as 0 pinned the monitor to the first preset while
    // the UI showed the inherited index.
    int kind = 0;
    if (overrideInt(overrides, ScrollPerScreenKeys::defaultColumnWidthKind(), kind)) {
        // An unconvertible VALUE or SPIN reads exactly like an absent one:
        // both mean "this layer wrote no such slot", and both inherit the
        // cached global's matching slot rather than committing a zero.
        qreal slotValue = 0.0;
        const bool hasValue = overrideDouble(overrides, ScrollPerScreenKeys::defaultColumnWidthValue(), slotValue);
        int spin = 0;
        const bool hasSpin = overrideInt(overrides, ScrollPerScreenKeys::defaultColumnWidthPresetIndex(), spin);
        if (kind == static_cast<int>(DefaultWidthKind::Fixed)) {
            const qreal value = hasValue
                ? slotValue
                : (m_defaultColumnWidth.kind == ColumnWidth::Fixed ? qreal(m_defaultColumnWidth.fixedPx) : 0.0);
            if (value >= 1.0) {
                // Bounded before the round, like every other numeric slot
                // resolved out of this map: the value arrives unvalidated
                // from applyPerScreenConfig, and qRound of a double past
                // int's range is undefined.
                return ColumnWidth::makeFixed(qRound(qBound(1.0, value, kMaxFixedExtentPx)));
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
            if (hasSpin) {
                // The empty case exits first: qBound asserts on an inverted
                // range, which is what size() - 1 gives for an empty list.
                if (presetWidths.isEmpty()) {
                    return ColumnWidth::makePreset(0.5);
                }
                return ColumnWidth::makePreset(presetWidths.at(qBound(0, spin, int(presetWidths.size()) - 1)));
            }
            return m_defaultColumnWidth.kind == ColumnWidth::Preset
                ? m_defaultColumnWidth
                : ColumnWidth::makePreset(presetWidths.value(0, 0.5));
        }
        if (kind == static_cast<int>(DefaultWidthKind::Proportion)) {
            const qreal value = hasValue
                ? slotValue
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
    return effectiveWidthClientDecides(overridesForScreen(screenId));
}

std::optional<ColumnWidth> ScrollEngine::resetDefaultColumnWidthFor(const QVariantMap& overrides,
                                                                    const ScrollLayoutParams& params) const
{
    // A rule's width outranks the kind (effectiveDefaultColumnWidth folds it
    // in first), so only a ClientDecides verdict with NO rule width means
    // "there is no default width to go back to".
    if (effectiveWidthClientDecides(overrides) && !ruleColumnWidthFraction(overrides).has_value()) {
        return std::nullopt;
    }
    return params.defaultColumnWidth;
}

bool ScrollEngine::effectiveWidthClientDecides(const QVariantMap& overrides) const
{
    // Kind VALIDATION, not just a read: effectiveDefaultColumnWidth falls
    // through to the global on a kind it does not recognise, so this half of
    // the same setting must fall through too. Answering "not ClientDecides"
    // for an unrecognised kind would pin the open path to a width the
    // resolver never supplied — the two halves would resolve from different
    // premises for exactly the malformed maps the validation exists for.
    int kind = 0;
    if (overrideInt(overrides, ScrollPerScreenKeys::defaultColumnWidthKind(), kind) && isKnownWidthKind(kind)) {
        return kind == static_cast<int>(DefaultWidthKind::ClientDecides);
    }
    return m_defaultWidthClientDecides;
}

WindowHeight ScrollEngine::effectiveDefaultWindowHeight(const QString& screenId, const QRect& workArea,
                                                        StripAxis axis) const
{
    return effectiveDefaultWindowHeight(overridesForScreen(screenId), workArea, axis);
}

WindowHeight ScrollEngine::effectiveDefaultWindowHeight(const QVariantMap& overrides, const QRect& workArea,
                                                        StripAxis axis) const
{
    return effectiveDefaultWindowHeight(overrides, workArea, axis, effectivePresetWindowHeights(overrides));
}

WindowHeight ScrollEngine::effectiveDefaultWindowHeight(const QVariantMap& overrides, const QRect& workArea,
                                                        StripAxis axis, const QList<qreal>& presetHeights) const
{
    // Rule channel: a bare fraction of the work area's CROSS extent, committed
    // as Fixed pixels against the CURRENT work area (same resolution the
    // adjust verbs use). Cross, not physical height: a window height divides
    // the within-column stack, which runs across the strip whichever way the
    // strip itself runs.
    // Through the shared helper, so the open path's ClientDecides gate asks
    // the SAME question this resolver answers (the width twin documents why a
    // presence test is the wrong question).
    const int crossExtent = axis.crossSize(workArea);
    if (crossExtent > 0) {
        if (const auto ruleFraction = ruleWindowHeightFraction(overrides)) {
            return WindowHeight::makeFixed(qMax(1, qRound(*ruleFraction * crossExtent)));
        }
    }
    // Settings channel: the kind trio, resolved per SLOT against the cached
    // global — see effectiveDefaultColumnWidth for why a partial trio is the
    // ordinary case rather than a malformed pair.
    int kind = 0;
    if (overrideInt(overrides, ScrollPerScreenKeys::defaultWindowHeightKind(), kind)) {
        // Absent and unconvertible slots both inherit the global's matching
        // slot — the width twin above documents why.
        qreal slotValue = 0.0;
        const bool hasValue = overrideDouble(overrides, ScrollPerScreenKeys::defaultWindowHeightValue(), slotValue);
        int spin = 0;
        const bool hasSpin = overrideInt(overrides, ScrollPerScreenKeys::defaultWindowHeightPresetIndex(), spin);
        if (kind == static_cast<int>(DefaultHeightKind::Fixed)) {
            const qreal value = hasValue
                ? slotValue
                : (m_defaultWindowHeight.kind == WindowHeight::Fixed ? qreal(m_defaultWindowHeight.fixedPx) : 0.0);
            if (value >= 1.0) {
                // Bounded before the round, for the width twin's reason.
                return WindowHeight::makeFixed(qRound(qBound(1.0, value, kMaxFixedExtentPx)));
            }
        } else if (kind == static_cast<int>(DefaultHeightKind::Preset)) {
            // Same spin-to-value resolution as the width twin above.
            if (hasSpin) {
                // Empty-first, for the width twin's qBound reason.
                if (presetHeights.isEmpty()) {
                    return WindowHeight::makePreset(0.5);
                }
                return WindowHeight::makePreset(presetHeights.at(qBound(0, spin, int(presetHeights.size()) - 1)));
            }
            return m_defaultWindowHeight.kind == WindowHeight::Preset
                ? m_defaultWindowHeight
                : WindowHeight::makePreset(presetHeights.value(0, 0.5));
        } else if (kind == static_cast<int>(DefaultHeightKind::Auto)) {
            return WindowHeight{};
        }
        // ClientDecides (and a kind whose resolved value is still out of
        // range) falls through to the global — the open path decides the
        // client-sized case via effectiveHeightClientDecides, and this
        // function only ever supplies the fallback height for it.
    }
    // Value anchor: no clamp, resolution snaps (see the width twin).
    return m_defaultWindowHeight;
}

std::optional<qreal> ScrollEngine::ruleWindowHeightFraction(const QVariantMap& overrides)
{
    qreal fraction = 0.0;
    if (overrideDouble(overrides, ScrollPerScreenKeys::defaultWindowHeight(), fraction) && fraction > 0.0
        && fraction <= 1.0) {
        return fraction;
    }
    return std::nullopt;
}

bool ScrollEngine::effectiveHeightClientDecides(const QString& screenId) const
{
    return effectiveHeightClientDecides(overridesForScreen(screenId));
}

bool ScrollEngine::effectiveHeightClientDecides(const QVariantMap& overrides) const
{
    // Kind VALIDATION, not just a read — the width twin carries the full
    // reasoning: the resolver falls through to the global on a kind it does
    // not recognise, so this half of the same setting must fall through too.
    int kind = 0;
    if (overrideInt(overrides, ScrollPerScreenKeys::defaultWindowHeightKind(), kind) && isKnownHeightKind(kind)) {
        return kind == static_cast<int>(DefaultHeightKind::ClientDecides);
    }
    return m_defaultHeightClientDecides;
}

std::optional<WindowHeight> ScrollEngine::resetDefaultWindowHeightFor(const QVariantMap& overrides) const
{
    // A rule's height outranks the kind (effectiveDefaultWindowHeight folds
    // it in first), so only a ClientDecides verdict with NO rule height means
    // "there is no default height to go back to".
    if (effectiveHeightClientDecides(overrides) && !ruleWindowHeightFraction(overrides).has_value()) {
        return std::nullopt;
    }
    // The even split, not params.defaultWindowHeight: this verb's promise is
    // "every window back to sharing its column", which is what the retile
    // copy says and what the arm it replaces always did. A Fixed or Preset
    // DEFAULT height is an OPEN-time shape, and re-imposing it here would
    // stack every window in a column at the same pixel height and let the
    // relayout renormalize them back apart.
    return WindowHeight::makeAuto();
}

ScrollInsertPosition ScrollEngine::effectiveInsertPosition(const QString& screenId) const
{
    return effectiveInsertPosition(overridesForScreen(screenId));
}

ScrollInsertPosition ScrollEngine::effectiveInsertPosition(const QVariantMap& overrides) const
{
    int pos = 0;
    if (overrideInt(overrides, ScrollPerScreenKeys::insertPosition(), pos)
        && pos >= static_cast<int>(ScrollInsertPosition::RightOfActive)
        && pos <= static_cast<int>(ScrollInsertPosition::IntoActiveColumn)) {
        return static_cast<ScrollInsertPosition>(pos);
    }
    return m_insertPosition;
}

ColumnDisplay ScrollEngine::effectiveDefaultColumnDisplay(const QString& screenId) const
{
    return effectiveDefaultColumnDisplay(overridesForScreen(screenId));
}

ColumnDisplay ScrollEngine::effectiveDefaultColumnDisplay(const QVariantMap& overrides) const
{
    // Validate-then-fall-back, same terms as the two siblings. Reading "any
    // value that is not 1" as Normal meant a garbage override (or a future
    // display kind this build does not know) silently overrode the user's
    // configured default with Normal instead of leaving it alone.
    int display = 0;
    if (overrideInt(overrides, ScrollPerScreenKeys::defaultColumnDisplay(), display)
        && (display == static_cast<int>(ColumnDisplay::Normal) || display == static_cast<int>(ColumnDisplay::Tabbed))) {
        return static_cast<ColumnDisplay>(display);
    }
    return m_defaultColumnDisplay;
}

TabIndicatorParams ScrollEngine::tabIndicatorParamsForScreen(const QString& screenId) const
{
    return effectiveTabIndicator(screenId);
}

TabIndicatorParams ScrollEngine::effectiveTabIndicator(const QString& screenId) const
{
    return effectiveTabIndicator(overridesForScreen(screenId));
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
    // Each read is guarded rather than taken through value(), because a
    // default-constructed QVariant would otherwise read as false / 0 and
    // silently override the configured value with a zero.
    //
    // The three bools go through effectiveBoolOverride, the same reader the
    // five behaviour toggles use: it takes the value only when it IS a bool,
    // so a hand-edited string cannot coerce to false and turn the indicator
    // off while every setting still reports it on. Two resolvers over one map
    // disagreeing about reject-vs-coerce is a bug in itself.
    params.enabled = effectiveBoolOverride(overrides, K::tabIndicatorEnabled(), params.enabled);
    params.hideWhenSingleTab =
        effectiveBoolOverride(overrides, K::tabIndicatorHideWhenSingleTab(), params.hideWhenSingleTab);
    params.placeWithinColumn =
        effectiveBoolOverride(overrides, K::tabIndicatorPlaceWithinColumn(), params.placeWithinColumn);
    // Bounded, for the same public-API reason the length belt below states: an
    // embedder can hand this library an override map directly, and an
    // unbounded width or gap feeds the reservation arithmetic that decides how
    // much of the column the window gets. Validate-then-fall-back, so a
    // garbage override leaves the configured value alone.
    const auto readInt = [&overrides](const QString& key, int& out, int lo, int hi) {
        int v = 0;
        if (overrideInt(overrides, key, v) && v >= lo && v <= hi) {
            out = v;
        }
    };
    readInt(K::tabIndicatorGap(), params.gap, kMinTabIndicatorGap, kMaxTabIndicatorGap);
    readInt(K::tabIndicatorWidth(), params.width, kMinTabIndicatorWidth, kMaxTabIndicatorWidth);
    // Validate-then-fall-back like the two ints above, the one stance the
    // whole function takes: an out-of-range proportion leaves the configured
    // value alone rather than being clamped, so an embedder handing the
    // library a garbage override (the rule cascade range-checks this upstream
    // in layoutregistry_contextresolve.cpp, but the map is public API) gets
    // the same answer for the length as for the gap and width.
    qreal length = 0.0;
    if (overrideDouble(overrides, K::tabIndicatorLengthProportion(), length)
        && length >= kMinTabIndicatorLengthProportion && length <= kMaxTabIndicatorLengthProportion) {
        params.lengthProportion = length;
    }
    // Validate-then-fall-back, the terms effectiveDefaultColumnDisplay uses:
    // a garbage override must leave the configured position alone rather than
    // silently snapping the indicator to Left.
    int pos = 0;
    if (overrideInt(overrides, K::tabIndicatorPosition(), pos) && pos >= static_cast<int>(TabIndicatorPosition::Left)
        && pos <= static_cast<int>(TabIndicatorPosition::Bottom)) {
        params.position = static_cast<TabIndicatorPosition>(pos);
    }
    return params;
}

} // namespace PhosphorScrollEngine
