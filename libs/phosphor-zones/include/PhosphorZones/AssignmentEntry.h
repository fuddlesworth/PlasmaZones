// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayoutApi/LayoutId.h>

#include <QColor>
#include <QHash>
#include <QList>
#include <QString>
#include <QUuid>
#include <QVariantMap>
#include <QtGlobal>

#include <optional>

namespace PhosphorZones {

/**
 * @brief Key for layout assignment (screen + desktop + activity)
 *
 * RETAINED PUBLIC API with no in-tree consumer. The daemon now stores
 * assignments as context RULES (ContextRuleBridge), and the v3 group format
 * @c fromGroupName parses is drained by configmigration_v4finalize's own
 * parser — so nothing in this repo constructs one. It stays exported because
 * this is an installed LGPL library header whose whole point is third-party
 * linkage: the (screen, desktop, activity) tuple is still the assignment
 * identity, and removing an exported type is an ABI break for consumers we do
 * not control. Deliberately kept, not overlooked.
 */
struct LayoutAssignmentKey
{
    QString screenId; // Stable EDID-based identifier (or connector name fallback)
    int virtualDesktop = 0; // 0 = all desktops
    QString activity; // Empty = all activities

    bool operator==(const LayoutAssignmentKey& other) const
    {
        return screenId == other.screenId && virtualDesktop == other.virtualDesktop && activity == other.activity;
    }

    /**
     * @brief Parse a "<prefix><screenId>[:Desktop:N][:Activity:uuid]" group name.
     *
     * Expects suffixes in canonical order: :Desktop:N before :Activity:uuid.
     * Group names are always constructed internally in this order.
     *
     * @param groupName Full group name from the config backend.
     * @param prefix    Schema-defined prefix the caller supplies (the lib's own
     *                  wire format uses @c "Assignment:"). Passing it explicitly
     *                  keeps this header free of
     *                  any cpp-side constant dependency.
     * @return Key with populated fields; screenId is empty on parse failure.
     */
    static LayoutAssignmentKey fromGroupName(const QString& groupName, const QString& prefix)
    {
        LayoutAssignmentKey result;
        if (!groupName.startsWith(prefix))
            return result;
        QString remainder = groupName.mid(prefix.size());
        if (remainder.isEmpty())
            return result;

        constexpr QLatin1String kActivityTag(":Activity:");
        constexpr QLatin1String kDesktopTag(":Desktop:");
        int actIdx = remainder.indexOf(kActivityTag);
        if (actIdx >= 0) {
            const QString activity = remainder.mid(actIdx + kActivityTag.size());
            if (!activity.isEmpty())
                result.activity = activity;
            remainder = remainder.left(actIdx);
        }
        int deskIdx = remainder.indexOf(kDesktopTag);
        if (deskIdx >= 0) {
            bool ok = false;
            int desktop = remainder.mid(deskIdx + kDesktopTag.size()).toInt(&ok);
            if (ok && desktop > 0)
                result.virtualDesktop = desktop;
            remainder = remainder.left(deskIdx);
        }
        result.screenId = remainder;
        return result;
    }
};

inline size_t qHash(const LayoutAssignmentKey& key, size_t seed = 0)
{
    seed = ::qHash(key.screenId, seed);
    seed = ::qHash(key.virtualDesktop, seed);
    seed = ::qHash(key.activity, seed);
    return seed;
}

/**
 * @brief Explicit per-context assignment entry storing every mode's payload
 *
 * Each screen/desktop/activity context stores an explicit Mode plus all three
 * per-mode payloads: SnappingLayout (UUID), PhosphorTiles::TilingAlgorithm,
 * and ScrollingTemplateLayout (UUID). Toggling between modes only flips the
 * mode field — the other fields are preserved, eliminating the need for
 * shadow assignments.
 */
struct AssignmentEntry
{
    /// Per-context engine selection. The v4 rule store persists the WIRE
    /// STRINGS produced by `modeToWireString` ("snapping", "autotile",
    /// "scrolling") — `ContextRuleBridge::makeDisableRule` writes them
    /// and `disableRuleMode` reads them back. A consumer's legacy v3→v4
    /// config migration does still read the int side from the assignments
    /// group (`Assignment:<screen>…`, key `Mode` — NOT the `Display.*`
    /// per-mode disable-list group), so NEVER renumber existing values — a
    /// renumber would silently swap engines for v3 disable lists that
    /// haven't been migrated yet. Append new modes at the end.
    enum Mode {
        Snapping = 0,
        Autotile = 1,
        /// The niri-style scrolling engine (PhosphorScrollEngine): windows
        /// form columns on an endless horizontal strip per context.
        /// `ScreenModeRouter::engineFor` hands Scrolling screens to the
        /// live ScrollEngine; the (Mode, Family) settings table here still
        /// drives all downstream config routing. Scrolling consumes no manual
        /// layout at all: its sizing comes from a native ScrollingTemplate
        /// (scrollingTemplateLayout below), its activeLayoutId() stays the
        /// bare "scrolling:" sentinel, and the mode lookup is the
        /// discriminator.
        Scrolling = 2
    };
    Mode mode = Snapping;
    QString snappingLayout; // UUID string of manual layout
    QString tilingAlgorithm; // e.g. "dwindle", "wide", "tall"
    /// Id of the native scrolling template (ScrollingTemplate) a Scrolling
    /// context uses for its seed blueprint, default column width and preset
    /// vocabularies. Its own UUID namespace, disjoint from manual layout ids.
    /// Deliberately its own field, never a reuse of
    /// snappingLayout: the lossless mode-toggle contract preserves the
    /// snapping choice across mode flips, and the "scrolling:" sentinel in
    /// activeLayoutId() stays payload-free (rules match on the bare
    /// sentinel). Empty = no template; the engine falls back to the
    /// settings preset lists.
    QString scrollingTemplateLayout;

    QString activeLayoutId() const
    {
        // Scrolling has no layout entity, so the id is the bare sentinel
        // "scrolling:". This is LOAD-BEARING: the assignment cascade's
        // visitors reject entries with an empty activeLayoutId(), and a
        // fresh mode-only Scrolling entry (empty preserved snappingLayout)
        // would otherwise silently fail to assign. The gap / lock /
        // overlay / tiling-param / scrolling-param resolvers stamp this
        // sentinel into query.activeLayout, so `ActiveLayout Equals
        // "scrolling:"` matches THERE (the assignment and context-default
        // resolvers deliberately leave activeLayout unstamped — recursion).
        // There is no per-layout identity to distinguish beyond the mode.
        if (mode == Scrolling) {
            return QString(PhosphorLayout::LayoutId::ScrollingId);
        }
        if (mode == Autotile) {
            // Autotile mode always produces a non-empty id so the cascade
            // visitors in LayoutRegistry accept it (they reject on
            // activeLayoutId().isEmpty()). For a mode-only entry (empty
            // tilingAlgorithm — what the KCM writes for "autotile, use
            // default algorithm"), makeAutotileId returns the bare
            // prefix @c "autotile:"; downstream callers use
            // @ref PhosphorLayout::LayoutId::isAutotile to detect mode
            // and @ref PhosphorLayout::LayoutId::extractAlgorithmId to
            // get the algorithm (empty = engine default).
            return PhosphorLayout::LayoutId::makeAutotileId(tilingAlgorithm);
        }
        return snappingLayout;
    }
    /// True when the entry carries a concrete layout/algorithm PAYLOAD.
    /// This is NOT the cascade's visibility predicate — that is
    /// activeLayoutId() non-empty, which a payload-less Scrolling entry
    /// satisfies through the "scrolling:" sentinel while isValid() stays
    /// false. scrollingTemplateLayout is deliberately EXCLUDED: a template
    /// never feeds activeLayoutId(), so counting it would make an entry
    /// "valid" that still resolves to nothing on a Snapping/Autotile
    /// context. Kept for tests/tooling; no production caller branches on it.
    bool isValid() const
    {
        return !snappingLayout.isEmpty() || !tilingAlgorithm.isEmpty();
    }
    bool operator==(const AssignmentEntry& other) const
    {
        return mode == other.mode && snappingLayout == other.snappingLayout && tilingAlgorithm == other.tilingAlgorithm
            && scrollingTemplateLayout == other.scrollingTemplateLayout;
    }

    /** @brief Update an existing AssignmentEntry from a layoutId, preserving the "other" field.
     *  Mode IS set from the layoutId type — batch operations from the KCM
     *  send a single layout ID per context, and that ID determines the active mode.
     *  The non-matching field is preserved for easy mode toggling.
     */
    static AssignmentEntry fromLayoutId(const QString& layoutId, const AssignmentEntry& existing)
    {
        AssignmentEntry entry = existing;
        if (PhosphorLayout::LayoutId::isAutotile(layoutId)) {
            entry.mode = Autotile;
            entry.tilingAlgorithm = PhosphorLayout::LayoutId::extractAlgorithmId(layoutId);
        } else if (PhosphorLayout::LayoutId::isScrolling(layoutId)) {
            // The "scrolling:" sentinel carries no layout entity — flip the
            // mode and preserve all three layout fields (the lossless-toggle
            // contract), so a get→set round-trip cannot degrade a Scrolling
            // assignment into Snapping-pointing-at-a-bogus-id.
            entry.mode = Scrolling;
        } else {
            entry.mode = Snapping;
            // Normalize a UUID-shaped id to its canonical braced spelling at
            // this ONE classification choke point, so every caller (the D-Bus
            // setAssignmentEntry and the four setAll*Assignments verbs, the
            // batch rebuilds, the controllers) stores one spelling. A braceless
            // or upper-case bus-supplied uuid stored verbatim defeats the
            // exact-string compare that purgeLayoutIdFromAssignments does on
            // layout delete, and the byte-wise action compare in
            // upsertAssignmentRule's no-op guard. Anything that is not a UUID
            // passes through untouched — the snapping slot is not required to
            // hold one.
            const QUuid parsed = QUuid::fromString(layoutId);
            entry.snappingLayout = parsed.isNull() ? layoutId : parsed.toString();
        }
        return entry;
    }
    /** @brief Create a fresh AssignmentEntry from a layoutId string.
     *  Exactly the two-arg overload applied to a default-constructed entry —
     *  one classification cascade, so a new mode cannot be added to one
     *  overload and forgotten in the other. */
    static AssignmentEntry fromLayoutId(const QString& layoutId)
    {
        return fromLayoutId(layoutId, AssignmentEntry{});
    }
};

/**
 * @brief Per-context gap override resolved from rules.
 *
 * Unlike @ref AssignmentEntry (which is engine-mode/layout centric and gated
 * on a SetEngineMode action), gap overrides are independent per-property
 * window-rule actions resolved per slot. Each field is set only when a
 * matching context rule fills the corresponding gap slot, so an unset field
 * falls through to the next precedence layer (per-screen → layout → global).
 * The daemon maps a populated override into a PerScreenKeys-shaped map
 * for @c GeometryUtils::getEffectiveOuterGaps / getEffectiveInnerGap.
 */
struct ContextGapOverride
{
    std::optional<int> innerGap;
    std::optional<int> outerGap;
    std::optional<bool> usePerSideOuterGap;
    std::optional<int> outerGapTop;
    std::optional<int> outerGapBottom;
    std::optional<int> outerGapLeft;
    std::optional<int> outerGapRight;

    bool isEmpty() const
    {
        return !innerGap && !outerGap && !usePerSideOuterGap && !outerGapTop && !outerGapBottom && !outerGapLeft
            && !outerGapRight;
    }
};

/**
 * @brief Per-context overlay-property overrides resolved from window-rule actions.
 *
 * Each field is set only when a matching context rule fills the corresponding
 * overlay slot; an unset field falls through to the active layout's own value
 * (shader / style) or the global config value (appearance). Consumed daemon-side
 * by the overlay service — see @c LayoutRegistry::resolveContextOverlay.
 *
 * @c style is the @c OverlayDisplayMode int (0 = ZoneRectangles, 1 =
 * LayoutPreview); the resolver maps the wire token ("rectangles" / "preview")
 * to the int so consumers compare against the same enum the layout exposes.
 * @c shaderParams holds the overridden shader's uniform values (translated by
 * the overlay service); it is only meaningful when @c shaderId is set and is
 * empty when the rule overrides only the shader id (shader defaults apply).
 *
 * The appearance fields (colours / opacities / border dimensions / zone-number
 * visibility) layer over the global @c Snapping.Zones.* config: each is filled
 * only by its @c SetOverlay* context action, and an unset field means "use the
 * global config value" — config stays authoritative, the rule only overrides.
 * Colours are concrete (no accent sentinel). Opacities are [0, 1].
 */
struct ContextOverlayOverride
{
    std::optional<QString> shaderId;
    QVariantMap shaderParams;
    std::optional<int> style;
    std::optional<QColor> highlightColor;
    std::optional<QColor> inactiveColor;
    std::optional<QColor> borderColor;
    std::optional<double> activeOpacity;
    std::optional<double> inactiveOpacity;
    std::optional<int> borderWidth;
    std::optional<int> borderRadius;
    std::optional<bool> showZoneNumbers;

    bool isEmpty() const
    {
        // shaderParams is only ever populated alongside shaderId, but check it too so
        // isEmpty() stays honest if a future writer sets the map without the gate.
        return !shaderId && shaderParams.isEmpty() && !style && !highlightColor && !inactiveColor && !borderColor
            && !activeOpacity && !inactiveOpacity && !borderWidth && !borderRadius && !showZoneNumbers;
    }
};

/**
 * @brief Per-context autotile parameter overrides resolved from context rules.
 *
 * Each field is set only when a matching context rule fills the corresponding
 * slot (SetMaxWindows / SetSplitRatio / SetMasterCount / SetInsertPosition /
 * SetOverflowBehavior / SetDragBehavior / SetAlgorithmParam); an unset field
 * means "use the config value". Consumed daemon-side: the values are layered onto
 * the per-screen autotile override map (config stays the base, the rule wins where
 * present). @c dragBehavior is consumed by the drag adaptor rather than the
 * override map. Resolved by @c LayoutRegistry::resolveContextTilingParams.
 */
struct ContextTilingParams
{
    std::optional<int> maxWindows;
    std::optional<double> splitRatio;
    std::optional<int> masterCount;
    /// The AutotileInsertPosition int (0 = End, 1 = AfterFocused, 2 = AsMaster);
    /// the resolver maps the wire token to this int so the daemon stores the same
    /// value the per-screen config store uses.
    std::optional<int> insertPosition;
    /// The AutotileOverflowBehavior int (0 = Float, 1 = Unlimited).
    std::optional<int> overflowBehavior;
    /// The AutotileDragBehavior int (0 = Float, 1 = Reorder). Consumed by the drag
    /// adaptor (not the tile-engine override map) unlike the other params.
    std::optional<int> dragBehavior;
    /// A SetAlgorithmParam override: the target algorithm id and the custom-param
    /// values to layer over that algorithm's config. Empty target = no override.
    /// The daemon applies @c algorithmParams only when @c algorithmParamTarget is
    /// the screen's effective algorithm.
    QString algorithmParamTarget;
    QVariantMap algorithmParams;

    bool isEmpty() const
    {
        // algorithmParams is only ever populated alongside algorithmParamTarget, but
        // check it too so isEmpty() stays honest if a future writer sets it without the
        // target.
        return !maxWindows && !splitRatio && !masterCount && !insertPosition && !overflowBehavior && !dragBehavior
            && algorithmParamTarget.isEmpty() && algorithmParams.isEmpty();
    }
};

/**
 * @brief Per-context scrolling parameter overrides resolved from context rules.
 *
 * Each field is set only when a matching context rule fills the corresponding
 * slot (SetScrollDefaultColumnWidth / SetCenterFocusedColumn /
 * SetScrollDefaultColumnDisplay / SetScrollInsertPosition /
 * SetScrollDefaultWindowHeight, plus the thirteen SetTabIndicator* slots
 * documented in their own block below); an unset field means "use the config
 * value".
 * Consumed daemon-side: the values are layered onto the scrolling engine's
 * per-screen parameters (config stays the base, the rule wins where present),
 * the same way @ref ContextTilingParams is layered onto the autotile override
 * map. Resolved by @c LayoutRegistry::resolveContextScrollingParams.
 */
struct ContextScrollingParams
{
    /// Width a newly-opened column takes, as a fraction of the work area
    /// (0.05-1.0). The wire value is the fraction; the editor shows a percent.
    std::optional<double> defaultColumnWidth;
    /// When the viewport re-centres on the focused column (0 = never,
    /// 1 = always, 2 = on overflow); the resolver maps the wire token to this
    /// int so the daemon stores the same value the config store uses.
    std::optional<int> centerFocusedColumn;
    /// How a newly-opened column lays its windows out (0 = normal, 1 = tabbed).
    std::optional<int> defaultColumnDisplay;
    /// Where a fresh-opened window's column enters the strip
    /// (ScrollInsertPosition ints, right-of-active 0 … into-active-column 4).
    std::optional<int> insertPosition;
    /// Height a newly-opened window takes, as a fraction of the work-area
    /// height (0.05-1.0); the engine commits it as fixed pixels at relayout.
    std::optional<double> defaultWindowHeight;

    /// The tab indicator's overrides, niri's `tab-indicator` layout block.
    /// Split the way IScrollSettings splits the family: the GEOMETRY fields
    /// are layered onto the scrolling engine's per-screen override map, while
    /// the PAINT fields never reach that library and are applied to the
    /// overlay daemon-side. Each is independently optional so a context rule
    /// that sets one property leaves the other twelve alone.
    std::optional<bool> tabIndicatorEnabled;
    std::optional<bool> tabIndicatorHideWhenSingleTab;
    std::optional<bool> tabIndicatorPlaceWithinColumn;
    std::optional<int> tabIndicatorGap; ///< px; NEGATIVE draws the indicator over the window
    std::optional<int> tabIndicatorWidth; ///< px thickness
    std::optional<double> tabIndicatorLength; ///< fraction of the column extent
    std::optional<int> tabIndicatorPosition; ///< TabIndicatorPosition ints, left 0 … bottom 3
    std::optional<int> tabIndicatorStyle; ///< 0 = title chips, 1 = segment bar
    std::optional<int> tabIndicatorGapsBetweenTabs; ///< px
    std::optional<int> tabIndicatorCornerRadius; ///< px; -1 is the "fully rounded" sentinel
    std::optional<QString> tabIndicatorActiveColor;
    std::optional<QString> tabIndicatorInactiveColor;
    std::optional<QString> tabIndicatorUrgentColor;

    /// The drop indicator's overrides. ALL PAINT — unlike the tab indicator
    /// there is no geometry half, because the indicator's rect comes from the
    /// engine's own layout math and cannot be positioned independently of
    /// where the drop lands. So none of these reaches the scrolling engine's
    /// per-screen override map; they go straight to the overlay.
    std::optional<bool> dropIndicatorEnabled;
    std::optional<QString> dropIndicatorColor;
    std::optional<QString> dropIndicatorBorderColor;
    std::optional<double> dropIndicatorOpacity; ///< fill only; the border is always opaque
    std::optional<int> dropIndicatorBorderWidth; ///< px; 0 is a fill with no edge
    std::optional<int> dropIndicatorBorderRadius; ///< px; 0 is square, no sentinel

    /// True when at least one drop-indicator slot resolved. Same purpose as
    /// the tab-indicator predicate below.
    bool hasDropIndicatorOverrides() const
    {
        return dropIndicatorEnabled || dropIndicatorColor || dropIndicatorBorderColor || dropIndicatorOpacity
            || dropIndicatorBorderWidth || dropIndicatorBorderRadius;
    }

    /// True when at least one tab-indicator slot resolved, so the daemon can
    /// skip the whole indicator-override path when it is false rather than
    /// testing thirteen optionals.
    bool hasTabIndicatorOverrides() const
    {
        return tabIndicatorEnabled || tabIndicatorHideWhenSingleTab || tabIndicatorPlaceWithinColumn || tabIndicatorGap
            || tabIndicatorWidth || tabIndicatorLength || tabIndicatorPosition || tabIndicatorStyle
            || tabIndicatorGapsBetweenTabs || tabIndicatorCornerRadius || tabIndicatorActiveColor
            || tabIndicatorInactiveColor || tabIndicatorUrgentColor;
    }

    bool isEmpty() const
    {
        return !defaultColumnWidth && !centerFocusedColumn && !defaultColumnDisplay && !insertPosition
            && !defaultWindowHeight && !hasTabIndicatorOverrides() && !hasDropIndicatorOverrides();
    }
};

/**
 * @brief Canonical wire-string for an @ref AssignmentEntry::Mode.
 *
 * The wire vocabulary lives next to the enum so every persister/consumer
 * (rule store via `ContextRuleBridge::makeDisableRule`, KCM debugging,
 * D-Bus enums) reads from one source of truth. Adding a new mode means
 * extending this switch and `modeFromWireString` together. NEVER rename
 * an existing token — the rule store records them verbatim and a rename
 * would orphan every persisted disable rule.
 */
inline QString modeToWireString(AssignmentEntry::Mode mode)
{
    switch (mode) {
    case AssignmentEntry::Snapping:
        return QStringLiteral("snapping");
    case AssignmentEntry::Autotile:
        return QStringLiteral("autotile");
    case AssignmentEntry::Scrolling:
        return QStringLiteral("scrolling");
    }
    // Switch is exhaustive over `Mode`. A `static_cast<Mode>(99)` reaching
    // this line means either (a) a future Mode enum value was added without
    // a case here — the real guard against that is Qt's -Wswitch diagnostic
    // at the missing-case site (NOTE: this project's CMake does NOT
    // promote it to -Werror, so it's a warning at build time, not an
    // error), or (b) callers fabricated an out-of-range value via cast.
    // `Q_UNREACHABLE_RETURN` expands to `[[unreachable]] + return`
    // on modern compilers, so the sentinel below carries through release
    // builds even when the optimizer assumes the function never reaches
    // here.
    //
    // The sentinel `"invalid"` is rejected by the `DisableEngine`
    // descriptor's closed-vocabulary validator
    // (`engineModeOptions().contains(...)`), so a malformed disable rule
    // fails load loudly. NOTE: `SetEngineMode`'s validator only checks
    // `hasNonEmptyString` (open-vocabulary by design — see its descriptor
    // validator in `libs/phosphor-rules/src/ruleaction_builtins_engine.cpp`), so a
    // malformed assignment rule survives load but is silently coerced
    // back to Snapping at consumption via
    // `entryFromRuleMatchActions → modeFromWireString → nullopt`. The
    // sentinel makes the corruption visible to operators inspecting
    // rules.json by eye, but is not a load-time gate for the
    // assignment path.
    Q_UNREACHABLE_RETURN(QStringLiteral("invalid"));
}

/**
 * @brief Inverse of @ref modeToWireString.
 *
 * Returns @c std::nullopt for an unrecognised token — callers must treat
 * that as a load failure (drop the rule / use the default). NEVER coerce
 * an unknown token to a default mode: a typo would silently re-route a
 * disable rule from "this engine only" to "all engines off".
 */
inline std::optional<AssignmentEntry::Mode> modeFromWireString(const QString& wire)
{
    if (wire == QLatin1String("snapping")) {
        return AssignmentEntry::Snapping;
    }
    if (wire == QLatin1String("autotile")) {
        return AssignmentEntry::Autotile;
    }
    if (wire == QLatin1String("scrolling")) {
        return AssignmentEntry::Scrolling;
    }
    return std::nullopt;
}

/**
 * @brief Iteration order for every @ref AssignmentEntry::Mode value.
 *
 * The order doubles as the UI tab order (Snapping first, Autotile second,
 * Scrolling last). Returns a `QList<Mode>` so range-for over modes is a
 * one-liner — a consumer's bulk save/reset paths and its settings-page
 * builders loop over this instead of hand-coding the {Snapping, Autotile, ...}
 * pair literally. Adding a Mode in the enum and extending this list is all that
 * is required to fan out every (Mode, Family)-keyed routine.
 */
inline QList<AssignmentEntry::Mode> allModes()
{
    return {AssignmentEntry::Snapping, AssignmentEntry::Autotile, AssignmentEntry::Scrolling};
}

} // namespace PhosphorZones
