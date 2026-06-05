// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Internal (not installed) IIdleSource backed by the foundation
// ext-idle-notify-v1 client. The production state machine's factory creates
// these; they translate PhosphorWayland::IdleNotifier's idled / resumed signals
// into the seam the machine consumes.

#include "iidlesource.h"

#include <PhosphorWayland/IdleNotifier.h>

namespace PhosphorServiceIdle {

class IdleNotifierSource : public IIdleSource
{
public:
    explicit IdleNotifierSource(QObject* parent = nullptr);

    void setTimeout(std::chrono::milliseconds timeout) override;
    [[nodiscard]] std::chrono::milliseconds timeout() const override;

private:
    PhosphorWayland::IdleNotifier m_notifier;
};

} // namespace PhosphorServiceIdle
