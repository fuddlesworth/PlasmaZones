// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>

namespace PlasmaZones {

/// The settings app's copy of the daemon's getScreenStates answer, and the
/// bookkeeping that keeps it current. SettingsController makes the D-Bus
/// call. This class decides what the replies do, so it unit-tests without a
/// daemon: a burst of schedule() calls folds into one refreshDue(), each
/// request is numbered so a reply to an older one still in flight is
/// dropped, a failed reply keeps the last answer, and statesChanged() fires
/// only when an accepted answer differs from the stored one.
class ScreenStatesCache : public QObject
{
    Q_OBJECT
public:
    explicit ScreenStatesCache(int coalesceMs, QObject* parent = nullptr);

    QVariantList states() const
    {
        return m_states;
    }

    /// Store @p states as the current answer without emitting
    /// statesChanged(), for the blocking read at construction.
    void seed(QVariantList states);

    /// Restart the coalescing timer. refreshDue() fires once it runs out
    /// with no further call.
    void schedule();

    /// Number a new request. Only the reply to the newest number is taken.
    quint64 beginRequest();

    /// Take the reply to @p request. A reply to an older request is dropped.
    /// @p ok false (the call failed) keeps the last answer, since an empty
    /// list would read as "no screen has any state". Returns true when
    /// @p states replaced the stored answer, which is exactly when
    /// statesChanged() was emitted.
    bool finishRequest(quint64 request, bool ok, QVariantList states);

Q_SIGNALS:
    /// The coalescing timer ran out. The owner asks the daemon now.
    void refreshDue();
    void statesChanged();

private:
    QVariantList m_states;
    QTimer m_timer;
    quint64 m_latestRequest = 0;
};

} // namespace PlasmaZones
