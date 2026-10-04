// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorIdentity/VirtualScreenId.h>

#include <QHash>
#include <QRectF>
#include <QSet>
#include <QString>

#include <optional>

namespace PlasmaZones {

/**
 * @brief The free geometry of windows that left a tiling desktop, and which
 *        of them are owed it back.
 *
 * A tiled window that moves to another desktop takes its tile frame with it.
 * Its free (pre-tile) rect is stashed here when it leaves, filed under the
 * screen it was measured on, because the rect is in that monitor's
 * coordinates. Where the window arrives decides what the rect is for:
 *  - on a tiling desktop the stash is folded back into the window's pre-tile
 *    record before its re-add, so a later float-back returns there
 *    (consumeForManagedArrival);
 *  - on a desktop that does not tile, the window is owed its free placement
 *    once, the first time it is in view there (takeOwedPlacement), and the
 *    entry stays for a later move back onto a tiling desktop.
 * A rect from another monitor is never handed back. Virtual screens of one
 * output share its coordinates and count as the same monitor.
 *
 * Kept free of KWin types so it is unit-testable on its own.
 */
class DesktopMoveStash
{
public:
    /// Stash @p rect, measured on @p screenId. An invalid rect stashes nothing.
    void stash(const QString& windowId, const QString& screenId, const QRectF& rect)
    {
        if (windowId.isEmpty() || !rect.isValid()) {
            return;
        }
        m_entries.insert(windowId, Entry{screenId, rect});
    }

    /// Whether @p windowId left its desktop at its tile frame, and so is owed
    /// a free placement where it lands. Recorded with or without a rect: a
    /// window with none still needs bringing onto its output.
    void setOwed(const QString& windowId, bool owed)
    {
        if (owed && !windowId.isEmpty()) {
            m_owed.insert(windowId);
        } else {
            m_owed.remove(windowId);
        }
    }

    bool isOwed(const QString& windowId) const
    {
        return m_owed.contains(windowId);
    }

    /// The window arrived on a tiling desktop on @p screenId: the entry and
    /// the owed placement are spent, and the rect comes back only on the
    /// monitor it was measured on.
    std::optional<QRectF> consumeForManagedArrival(const QString& windowId, const QString& screenId)
    {
        m_owed.remove(windowId);
        const auto it = m_entries.find(windowId);
        if (it == m_entries.end()) {
            return std::nullopt;
        }
        const Entry entry = *it;
        m_entries.erase(it);
        if (!PhosphorIdentity::VirtualScreenId::samePhysical(entry.screenId, screenId)) {
            return std::nullopt;
        }
        return entry.rect;
    }

    /// The window is first in view on a desktop that does not tile: spend the
    /// owed placement, answering whether one was owed in @p owed and the rect
    /// when it was measured on this monitor. The entry stays.
    std::optional<QRectF> takeOwedPlacement(const QString& windowId, const QString& screenId, bool* owed)
    {
        const bool wasOwed = m_owed.remove(windowId);
        if (owed) {
            *owed = wasOwed;
        }
        if (!wasOwed) {
            return std::nullopt;
        }
        const auto it = m_entries.constFind(windowId);
        if (it == m_entries.constEnd() || !PhosphorIdentity::VirtualScreenId::samePhysical(it->screenId, screenId)) {
            return std::nullopt;
        }
        return it->rect;
    }

    /// The window was closed.
    void forget(const QString& windowId)
    {
        m_entries.remove(windowId);
        m_owed.remove(windowId);
    }

    /// Daemon loss or bring-up.
    void clear()
    {
        m_entries.clear();
        m_owed.clear();
    }

private:
    struct Entry
    {
        QString screenId;
        QRectF rect;
    };
    QHash<QString, Entry> m_entries;
    QSet<QString> m_owed;
};

} // namespace PlasmaZones
