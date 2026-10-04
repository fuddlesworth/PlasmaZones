// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QList>
#include <QString>

#include <optional>

namespace PlasmaZones {

/**
 * @brief The windows parked for a snap restore on desktop arrival, each with
 *        the cause it was parked for.
 *
 * A window the daemon moved to a desktop nobody is looking at is placed when
 * that desktop comes into view, because its suspended client would not ack a
 * resize sent now. Why it was moved decides what that placement may do (F415):
 * the continuation of an open (a RouteToDesktop rule) still owes the window
 * its whole first placement, while every other move (the desktop shortcut, a
 * cross-mode handoff, the shell) only re-applies the zone the window already
 * holds there and places nothing new.
 *
 * Kept free of KWin types so it is unit-testable on its own.
 */
class DesktopArrivalParks
{
public:
    enum class Cause {
        OpenContinuation, ///< the arrival finishes an open: the full restore chain
        ReapplyOnly, ///< the arrival re-applies a held zone and nothing else
    };

    /// Park @p windowId. A second arm keeps the stronger cause: an open
    /// continuation is never downgraded by a later re-apply.
    void arm(const QString& windowId, Cause cause)
    {
        if (windowId.isEmpty()) {
            return;
        }
        auto it = m_parks.find(windowId);
        if (it == m_parks.end()) {
            m_parks.insert(windowId, cause);
        } else if (cause == Cause::OpenContinuation) {
            *it = cause;
        }
    }

    void cancel(const QString& windowId)
    {
        m_parks.remove(windowId);
    }

    /// Spend @p windowId's park, answering the cause it was parked for.
    std::optional<Cause> take(const QString& windowId)
    {
        auto it = m_parks.find(windowId);
        if (it == m_parks.end()) {
            return std::nullopt;
        }
        const Cause cause = *it;
        m_parks.erase(it);
        return cause;
    }

    bool contains(const QString& windowId) const
    {
        return m_parks.contains(windowId);
    }

    bool isEmpty() const
    {
        return m_parks.isEmpty();
    }

    QList<QString> ids() const
    {
        return m_parks.keys();
    }

    void clear()
    {
        m_parks.clear();
    }

private:
    QHash<QString, Cause> m_parks;
};

} // namespace PlasmaZones
