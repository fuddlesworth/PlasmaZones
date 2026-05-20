// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayoutApi/LayoutId.h>

#include <QHash>
#include <QString>

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
 * @brief Explicit per-context assignment entry storing the per-mode payload fields
 *
 * Each screen/desktop/activity context stores an explicit Mode plus the
 * SnappingLayout (UUID), TilingAlgorithm, and scrollSetting payloads. Toggling
 * between modes only flips the mode field — the other payload fields are
 * preserved, eliminating the need for shadow assignments.
 */
struct AssignmentEntry
{
    enum Mode {
        Snapping = 0,
        Autotile = 1,
        Scroll = 2
    };
    Mode mode = Snapping;
    QString snappingLayout; // UUID string of manual layout
    QString tilingAlgorithm; // e.g. "dwindle", "wide", "tall"
    QString scrollSetting; // scroll-mode tuning id ("" = engine defaults)

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
        if (mode == Scroll) {
            // Mirrors autotile: a mode-only scroll entry (empty scrollSetting)
            // serialises to the bare prefix "scroll:", which the cascade
            // accepts as non-empty so modeForScreen reports Scroll.
            return PhosphorLayout::LayoutId::makeScrollId(scrollSetting);
        }
        return snappingLayout;
    }
    bool isValid() const
    {
        // Validity is mode-driven: a Snapping entry needs a non-empty
        // snappingLayout, an Autotile entry can have an empty tilingAlgorithm
        // (use the engine default — see makeAutotileId comment above), and a
        // Scroll entry can have an empty scrollSetting (use defaults — same
        // rationale). The previous "any field non-empty" check was both lax
        // (a Snapping mode with a stray tilingAlgorithm reported valid) and
        // strict (a legitimate "scroll, use defaults" with all fields empty
        // reported invalid).
        switch (mode) {
        case Autotile:
        case Scroll:
            return true; // mode-only is acceptable; emptyness means "defaults"
        case Snapping:
            return !snappingLayout.isEmpty();
        }
        return false;
    }
    bool operator==(const AssignmentEntry& other) const
    {
        return mode == other.mode && snappingLayout == other.snappingLayout && tilingAlgorithm == other.tilingAlgorithm
            && scrollSetting == other.scrollSetting;
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
        } else if (PhosphorLayout::LayoutId::isScroll(layoutId)) {
            entry.mode = Scroll;
            entry.scrollSetting = PhosphorLayout::LayoutId::extractScrollSettingId(layoutId);
        } else {
            entry.mode = Snapping;
            entry.snappingLayout = layoutId;
        }
        return entry;
    }
    /** @brief Create a fresh AssignmentEntry from a layoutId string */
    static AssignmentEntry fromLayoutId(const QString& layoutId)
    {
        // Forward to the two-arg overload with a default-constructed existing
        // — same mode classification, single source of truth.
        return fromLayoutId(layoutId, AssignmentEntry{});
    }
};

} // namespace PhosphorZones
