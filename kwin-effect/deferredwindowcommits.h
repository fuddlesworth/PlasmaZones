// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <functional>
#include <utility>

namespace PlasmaZones {

/**
 * @brief Per-window deferred single-shot commits with consume-erase semantics.
 *
 * The shared plumbing behind the tiling handlers' debounced/deferred
 * per-window state machines (AutotileHandler's minimize→float debounce and
 * both engines' unminimize→unfloat grace): one pending single-shot QTimer
 * per windowId, cancellable until it fires. What stays engine-specific is
 * the @c fire callback — every revalidation and commit decision lives with
 * the caller; this class only owns the timer bookkeeping.
 *
 * Lifetime: timers are parented to @p owner and their timeouts use @p owner
 * as the connection context, so destroying the owner (which also destroys
 * this member) cancels everything in flight — no callback ever runs against
 * a torn-down handler. The timeout consumes the pending entry BEFORE
 * invoking @c fire, so a commit that re-schedules for the same window (or a
 * cancel() called from inside the callback) never touches a stale entry.
 */
class DeferredWindowCommits
{
public:
    /// @param owner Timer parent and timeout connection context — the
    ///        handler that owns this member.
    explicit DeferredWindowCommits(QObject* owner)
        : m_owner(owner)
    {
    }

    DeferredWindowCommits(const DeferredWindowCommits&) = delete;
    DeferredWindowCommits& operator=(const DeferredWindowCommits&) = delete;

    /// True while a commit is pending for @p windowId.
    bool contains(const QString& windowId) const
    {
        return m_pending.contains(windowId);
    }

    /// Schedule @p fire to run after @p intervalMs, replacing nothing —
    /// callers gate on contains() when an existing pending commit should
    /// win. The pending entry is consumed (erased, timer released) before
    /// @p fire runs, so every early-return inside the callback leaves no
    /// bookkeeping behind.
    void schedule(const QString& windowId, int intervalMs, std::function<void()> fire)
    {
        auto* timer = new QTimer(m_owner);
        timer->setSingleShot(true);
        timer->setInterval(intervalMs);
        QObject::connect(timer, &QTimer::timeout, m_owner, [this, windowId, fire = std::move(fire)]() {
            // Consume the hash entry first; we own the timer's deleteLater
            // regardless of which branch the callback takes.
            auto it = m_pending.find(windowId);
            if (it == m_pending.end()) {
                return; // Already cancelled.
            }
            QPointer<QTimer> owned = it.value();
            m_pending.erase(it);
            if (owned) {
                owned->deleteLater();
            }
            fire();
        });
        m_pending.insert(windowId, timer);
        timer->start();
    }

    /// Cancel the pending commit for @p windowId. No-op if none is pending.
    void cancel(const QString& windowId)
    {
        auto it = m_pending.find(windowId);
        if (it == m_pending.end()) {
            return;
        }
        if (QTimer* pending = it.value()) {
            pending->stop();
            pending->deleteLater();
        }
        m_pending.erase(it);
    }

    /// Cancel every pending commit (bulk teardown / engine disable).
    void cancelAll()
    {
        // keys() snapshot: cancel() erases while we iterate.
        const QStringList ids = m_pending.keys();
        for (const QString& windowId : ids) {
            cancel(windowId);
        }
    }

private:
    QObject* m_owner;
    QHash<QString, QPointer<QTimer>> m_pending;
};

} // namespace PlasmaZones
