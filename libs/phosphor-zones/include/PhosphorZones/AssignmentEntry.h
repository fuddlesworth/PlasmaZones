// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayoutApi/LayoutId.h>

#include <QHash>
#include <QList>
#include <QString>
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
     * @param prefix    Schema-defined prefix (the lib's own wire format
     *                  uses @c "Assignment:" — defined as a private
     *                  constant in @c layoutregistry_persistence.cpp).
     *                  Passing it explicitly keeps this header free of
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
    /// Per-context engine selection. The integer values are persisted to
    /// the rule store via `ContextRuleBridge::makeDisableRule` / read back
    /// via `disableRuleMode`, and they appear in the v4 windowrules.json
    /// schema as the `DisableEngine` action's `mode` wire token (see
    /// `modeToWireString` / `modeFromWireString` below). NEVER renumber
    /// existing values — older rule stores would silently swap engines.
    /// Append new modes at the end.
    enum Mode {
        Snapping = 0,
        Autotile = 1,
        /// Reserved engine slot for a future scrolling-workspace engine.
        /// The settings UI exposes the mode and persists per-mode disable
        /// lists / config groups, but the router currently has no engine
        /// to hand windows to — see `ScreenModeRouter::routerFor` for the
        /// passthrough fallback that lets KWin place the window naturally
        /// rather than blocking on a missing engine. A real engine
        /// implementer adds an adapter to the router and removes the
        /// passthrough; no daemon-internal switch needs to be edited
        /// because the (Mode, Family) settings table here drives all
        /// downstream config routing.
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
    // a case here — `Q_UNREACHABLE` produces a -Wswitch diagnostic at the
    // missing-case site, or (b) callers fabricated an out-of-range value
    // via cast. Either way returning an empty token is a silent
    // data-loss hazard: every `writeDisableEntries` site would build a
    // disable rule with `mode:""`, which round-trips through
    // `disableRuleMode` as `nullopt`, making the rule invisible to the
    // next read.
    Q_UNREACHABLE();
    return QString();
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
