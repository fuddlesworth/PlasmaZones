// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceClipboard/phosphorserviceclipboard_export.h>

#include <QObject>

namespace PhosphorServiceClipboard {

/**
 * @brief Clipboard-history host for Phosphor-based desktop shells.
 *
 * Watches the session clipboard through `phosphor-wayland`'s `ClipboardDevice`
 * (a `wlr-data-control` client), keeps a de-duplicated, capped, on-disk history,
 * and can re-apply any entry. It is the policy / history / persistence layer over
 * the raw device; it composes the device rather than binding the protocol itself,
 * so its public surface is a clean Qt/QML type with no Wayland types leaking out.
 *
 * Phase 2.8 milestone 1 / 2 is the skeleton: the host constructs and reports
 * whether the compositor advertises the data-control protocol this service needs.
 * The history model, persistence, and the full QML facade arrive in the following
 * milestones.
 */
class PHOSPHORSERVICECLIPBOARD_EXPORT ClipboardService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ isSupported CONSTANT)

public:
    explicit ClipboardService(QObject* parent = nullptr);

    /// Whether the compositor advertises the `wlr-data-control` protocol this
    /// service builds on. When false the service constructs but stays inert.
    [[nodiscard]] bool isSupported() const;

private:
    Q_DISABLE_COPY_MOVE(ClipboardService)
};

} // namespace PhosphorServiceClipboard
