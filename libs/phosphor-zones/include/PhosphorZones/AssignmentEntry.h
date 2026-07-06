// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayoutApi/LayoutId.h>

#include <QColor>
#include <QHash>
#include <QList>
#include <QString>
#include <QVariantMap>
#include <QtGlobal>

#include <optional>

namespace PhosphorZones {

/**
 * @brief Key for layout assignment (screen + desktop + activity)
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

        int actIdx = remainder.indexOf(QLatin1String(":Activity:"));
        if (actIdx >= 0) {
            const QString activity = remainder.mid(actIdx + 10);
            if (!activity.isEmpty())
                result.activity = activity;
            remainder = remainder.left(actIdx);
        }
        int deskIdx = remainder.indexOf(QLatin1String(":Desktop:"));
        if (deskIdx >= 0) {
            bool ok = false;
            int desktop = remainder.mid(deskIdx + 9).toInt(&ok);
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
 * @brief Explicit per-context assignment entry storing both mode fields
 *
 * Each screen/desktop/activity context stores an explicit Mode, SnappingLayout (UUID),
 * and PhosphorTiles::TilingAlgorithm. Toggling between modes only flips the mode field —
 * the other field is preserved, eliminating the need for shadow assignments.
 */
struct AssignmentEntry
{
    /// Per-context engine selection. The v4 rule store persists the WIRE
    /// STRINGS produced by `modeToWireString` ("snapping", "autotile",
    /// "scrolling") — `ContextRuleBridge::makeDisableRule` writes them
    /// and `disableRuleMode` reads them back. The legacy v3→v4 config
    /// migration (configmigration.cpp) does still read the int side via
    /// `Display.<screen>:Mode`, so NEVER renumber existing values — a
    /// renumber would silently swap engines for v3 disable lists that
    /// haven't been migrated yet. Append new modes at the end.
    enum Mode {
        Snapping = 0,
        Autotile = 1,
        /// Reserved engine slot for a future scrolling-workspace engine.
        /// The settings UI exposes the mode and persists per-mode disable
        /// lists / config groups, but the router currently has no engine
        /// to hand windows to — see `ScreenModeRouter::engineFor` for the
        /// passthrough fallback (returns nullptr for Scrolling) that lets
        /// KWin place the window naturally rather than blocking on a
        /// missing engine. A real engine implementer adds an adapter to
        /// the router and removes the passthrough; no daemon-internal
        /// switch needs to be edited because the (Mode, Family) settings
        /// table here drives all downstream config routing.
        Scrolling = 2
    };
    Mode mode = Snapping;
    QString snappingLayout; // UUID string of manual layout
    QString tilingAlgorithm; // e.g. "dwindle", "wide", "tall"

    QString activeLayoutId() const
    {
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
    bool isValid() const
    {
        return !snappingLayout.isEmpty() || !tilingAlgorithm.isEmpty();
    }
    bool operator==(const AssignmentEntry& other) const
    {
        return mode == other.mode && snappingLayout == other.snappingLayout && tilingAlgorithm == other.tilingAlgorithm;
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
        } else {
            entry.mode = Snapping;
            entry.snappingLayout = layoutId;
        }
        return entry;
    }
    /** @brief Create a fresh AssignmentEntry from a layoutId string */
    static AssignmentEntry fromLayoutId(const QString& layoutId)
    {
        AssignmentEntry entry;
        if (PhosphorLayout::LayoutId::isAutotile(layoutId)) {
            entry.mode = Autotile;
            entry.tilingAlgorithm = PhosphorLayout::LayoutId::extractAlgorithmId(layoutId);
        } else {
            entry.mode = Snapping;
            entry.snappingLayout = layoutId;
        }
        return entry;
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
 * The daemon maps a populated override into a PerScreenSnappingKey-shaped map
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
        return !shaderId && !style && !highlightColor && !inactiveColor && !borderColor && !activeOpacity
            && !inactiveOpacity && !borderWidth && !borderRadius && !showZoneNumbers;
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
        return !maxWindows && !splitRatio && !masterCount && !insertPosition && !overflowBehavior && !dragBehavior
            && algorithmParamTarget.isEmpty();
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
    // validator in `libs/phosphor-rules/src/ruleaction.cpp`), so a
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
 * one-liner — both Settings::saveAll / resetAll and the KCM page builders
 * loop over this instead of hand-coding the {Snapping, Autotile, ...} pair
 * literally. Adding a Mode in the enum and extending this list is all that
 * is required to fan out every (Mode, Family)-keyed routine.
 */
inline QList<AssignmentEntry::Mode> allModes()
{
    return {AssignmentEntry::Snapping, AssignmentEntry::Autotile, AssignmentEntry::Scrolling};
}

} // namespace PhosphorZones
