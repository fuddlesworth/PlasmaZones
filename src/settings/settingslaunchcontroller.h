// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../core/single_instance_service.h"

#include <QObject>
#include <QString>
#include <memory>

namespace PlasmaZones {

class SettingsController;

/**
 * @brief Owns the settings app's D-Bus single-instance lifecycle.
 *
 * Mirror of `EditorLaunchController`: splits transport concerns (D-Bus name
 * ownership, forwarded launch dispatch) off of `SettingsController`, which
 * stays focused on its domain (settings data, page state, layouts, screens).
 *
 * Owns:
 *   - The `SingleInstanceService` RAII handle for `org.plasmazones.Settings.App`
 *     on the session bus.
 *   - `handleSetActivePage()` — the forwarded-launch entry point that a second
 *     launcher invokes via `SettingsAppAdaptor::setActivePage`.
 *
 * Holds a non-owning pointer to the real `SettingsController`. The controller
 * must outlive this object (main.cpp guarantees this by declaring the
 * controller first and the launcher second, so reverse destruction order
 * tears down the launcher while the controller is still alive).
 */
class SettingsLaunchController : public QObject
{
    Q_OBJECT

public:
    /**
     * @param controller The settings controller this launcher forwards to.
     *                   Must outlive this object.
     * @param parent     Optional QObject parent.
     */
    explicit SettingsLaunchController(SettingsController* controller, QObject* parent = nullptr);
    ~SettingsLaunchController() override;

    /**
     * @brief Claim the well-known D-Bus name and export this object as the
     * forwarded-launch receiver.
     *
     * @return true on success, false if another process holds the name or
     *         the D-Bus registration failed.
     */
    bool registerDBusService();

    /**
     * @brief Entry point for a forwarded `--page` request.
     *
     * Called by `SettingsAppAdaptor::setActivePage` when a second launcher
     * hands off its CLI arg over D-Bus. Delegates to
     * `SettingsController::setActivePage`. Does NOT try to raise the window:
     * programmatic focus-steal is unreliable on Wayland (see PR #314).
     */
    void handleSetActivePage(const QString& page);

private:
    SettingsController* m_controller; ///< Non-owning; must outlive this object.
    std::unique_ptr<SingleInstanceService> m_singleInstance;
};

} // namespace PlasmaZones
