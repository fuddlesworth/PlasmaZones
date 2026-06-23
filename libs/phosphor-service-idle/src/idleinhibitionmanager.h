// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Internal (not installed) reference-counted idle-inhibition aggregator. Callers
// acquire an inhibition (a video is playing, a long copy is running) and release
// it by cookie; idle stays inhibited while any cookie is held. The facade drives
// the state machine from `inhibitedChanged` (per U2 in the plan, inhibition pauses
// idle monitoring rather than holding a surface-bound zwp-idle-inhibit object).
// Pure logic, unit-tested without a compositor.

#include <QObject>
#include <QSet>

namespace PhosphorServiceIdle {

class IdleInhibitionManager : public QObject
{
    Q_OBJECT

public:
    explicit IdleInhibitionManager(QObject* parent = nullptr);
    ~IdleInhibitionManager() override;

    /// Acquire an inhibition. Returns a cookie to release it with; cookies are
    /// unique and never reused within a process lifetime (so a released cookie
    /// can never collide with a live one).
    int inhibit();

    /// Release a previously acquired inhibition. Returns true if @p cookie was
    /// held; a no-op returning false for an unknown or already-released cookie.
    bool release(int cookie);

    [[nodiscard]] bool isInhibited() const;
    /// Number of inhibitions currently held.
    [[nodiscard]] int activeCount() const;

Q_SIGNALS:
    /// Emitted only on the edges: false to true when the first cookie is acquired,
    /// true to false when the last is released.
    void inhibitedChanged(bool inhibited);

private:
    QSet<int> m_cookies;
    int m_nextCookie = 1; // 0 is reserved as an invalid cookie.
};

} // namespace PhosphorServiceIdle
