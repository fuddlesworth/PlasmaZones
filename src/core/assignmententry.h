// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "constants.h"
#include <QString>

namespace PlasmaZones {

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
     * @brief Parse an "Assignment:screenId[:Desktop:N][:Activity:uuid]" group name
     * @return Key with populated fields; screenId is empty on parse failure
     */
    static LayoutAssignmentKey fromGroupName(const QString& groupName)
    {
        LayoutAssignmentKey result;
        const QLatin1String prefix("Assignment:");
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
 * and TilingAlgorithm. Toggling between modes only flips the mode field —
 * the other field is preserved, eliminating the need for shadow assignments.
 */
struct AssignmentEntry
{
    enum Mode {
        Snapping = 0,
        Autotile = 1
    };
    Mode mode = Snapping;
    QString snappingLayout; // UUID string of manual layout
    QString tilingAlgorithm; // e.g. "dwindle", "wide", "tall"

    QString activeLayoutId() const
    {
        if (mode == Autotile) {
            // Return autotile prefix even with empty algorithm — signals "autotile mode,
            // use default algorithm." Callers use LayoutId::isAutotile() to detect mode
            // and extractAlgorithmId() to get the algorithm (empty = use default).
            return LayoutId::makeAutotileId(tilingAlgorithm);
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
        if (LayoutId::isAutotile(layoutId)) {
            entry.mode = Autotile;
            entry.tilingAlgorithm = LayoutId::extractAlgorithmId(layoutId);
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
        if (LayoutId::isAutotile(layoutId)) {
            entry.mode = Autotile;
            entry.tilingAlgorithm = LayoutId::extractAlgorithmId(layoutId);
        } else {
            entry.mode = Snapping;
            entry.snappingLayout = layoutId;
        }
        return entry;
    }
};

} // namespace PlasmaZones
