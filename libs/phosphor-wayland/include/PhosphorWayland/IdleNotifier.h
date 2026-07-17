// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorwayland_export.h>

#include <QObject>

#include <chrono>
#include <memory>

namespace PhosphorWayland {

class PHOSPHORWAYLAND_EXPORT IdleNotifier : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool idle READ isIdle NOTIFY idleChanged)

public:
    explicit IdleNotifier(QObject* parent = nullptr);
    ~IdleNotifier() override;

    void setTimeout(std::chrono::milliseconds timeout);
    std::chrono::milliseconds timeout() const;

    bool isIdle() const;

    static bool isSupported();

    /// Whether an ext-idle-notification object is actually ARMED right now.
    ///
    /// Distinct from isSupported(), and the difference is the whole point: the compositor
    /// can advertise the protocol while arming still fails, because arming needs a seat and
    /// the seat's input devices may not have been advertised yet (a daemon that starts
    /// before the compositor finishes bringing the seat up). Arming then silently does
    /// nothing, isSupported() keeps saying true, and idle detection is dead for the session
    /// with one warning to show for it. A caller that wants to retry has to be able to ask.
    [[nodiscard]] bool isArmed() const;

Q_SIGNALS:
    void timeoutChanged();
    void idleChanged();
    void idled();
    void resumed();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorWayland
