// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "idlenotifiersource.h"

namespace PhosphorServiceIdle {

IdleNotifierSource::IdleNotifierSource(QObject* parent)
    : IIdleSource(parent)
{
    connect(&m_notifier, &PhosphorWayland::IdleNotifier::idled, this, [this] {
        Q_EMIT idled();
    });
    connect(&m_notifier, &PhosphorWayland::IdleNotifier::resumed, this, [this] {
        Q_EMIT resumed();
    });
}

void IdleNotifierSource::setTimeout(std::chrono::milliseconds timeout)
{
    m_notifier.setTimeout(timeout);
}

std::chrono::milliseconds IdleNotifierSource::timeout() const
{
    return m_notifier.timeout();
}

} // namespace PhosphorServiceIdle
