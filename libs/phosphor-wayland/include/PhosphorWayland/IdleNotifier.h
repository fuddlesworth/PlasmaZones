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
