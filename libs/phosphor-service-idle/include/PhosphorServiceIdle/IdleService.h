// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceIdle/phosphorserviceidle_export.h>

#include <QObject>

namespace PhosphorServiceIdle {

/**
 * @brief Session idle-management host for Phosphor-based desktop shells.
 *
 * Watches the session for inactivity through a configurable multi-stage timeout
 * policy and inhibits idle on request. It is the policy layer over the raw
 * Wayland clients in `phosphor-wayland` (`IdleNotifier` for `ext-idle-notify-v1`
 * and `IdleInhibitor` for `zwp-idle-inhibit-v1`); it composes them rather than
 * binding the protocols itself.
 *
 * Phase 2.7 milestone 1 is the skeleton: the host constructs and reports whether
 * the compositor advertises the idle protocols this service needs. The stage
 * state machine, inhibition aggregation, and the full QML facade arrive in the
 * following milestones.
 */
class PHOSPHORSERVICEIDLE_EXPORT IdleService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ isSupported CONSTANT)

public:
    explicit IdleService(QObject* parent = nullptr);

    /// Whether the compositor advertises the idle-notification protocol this
    /// service builds on. When false the service constructs but stays inert.
    [[nodiscard]] bool isSupported() const;

private:
    Q_DISABLE_COPY_MOVE(IdleService)
};

} // namespace PhosphorServiceIdle
