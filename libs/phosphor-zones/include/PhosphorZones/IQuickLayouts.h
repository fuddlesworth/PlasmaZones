// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// IQuickLayouts — numbered (1–9) layout shortcut slots.
//
// Split out of ILayoutManager so the shortcut-manager path and KCM's
// quick-slot editor can depend on this narrow 5-method surface instead
// of the full layout manager.

#include <phosphorzones_export.h>

#include <QHash>
#include <QString>

namespace PhosphorZones {

class Layout;

/**
 * @brief Numbered quick-layout slots (1–9) accessible via keyboard shortcuts.
 *
 * Slots store layout IDs (UUID string for manual layouts, or the
 * `"autotile:<algorithmId>"` form for autotile algorithms).
 * applyQuickLayout resolves the stored id and invokes the assignment
 * path on the target screen.
 */
class PHOSPHORZONES_EXPORT IQuickLayouts
{
public:
    IQuickLayouts() = default;
    virtual ~IQuickLayouts();

    /// Resolve a slot number to its stored layout. Borrowed pointer —
    /// owned by the layout registry. Returns nullptr if the slot is
    /// unset or stores an autotile id (which is not a Layout*).
    virtual Layout* layoutForShortcut(int number) const = 0;

    /// Apply the layout in @p number to @p screenId (resolves against
    /// current desktop / activity context).
    virtual void applyQuickLayout(int number, const QString& screenId) = 0;

    /// Store @p layoutId in slot @p number. Empty @p layoutId clears
    /// the slot. Persisted to settings.
    virtual void setQuickLayoutSlot(int number, const QString& layoutId) = 0;

    /// Batch setter — saves once at the end.
    virtual void setAllQuickLayoutSlots(const QHash<int, QString>& slots) = 0;

    /// Current slot-to-id mapping.
    virtual QHash<int, QString> quickLayoutSlots() const = 0;

protected:
    IQuickLayouts(const IQuickLayouts&) = default;
    IQuickLayouts& operator=(const IQuickLayouts&) = default;
};

} // namespace PhosphorZones
