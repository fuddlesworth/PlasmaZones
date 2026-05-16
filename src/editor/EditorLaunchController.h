// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../core/single_instance_service.h"

#include <QObject>
#include <QString>
#include <memory>

namespace PlasmaZones {

class EditorController;

/**
 * @brief Owns the editor's D-Bus single-instance lifecycle.
 *
 * Split out of EditorController to keep the domain model (zones, layouts,
 * shaders, undo, selection) separate from transport/launch concerns. This
 * class owns:
 *
 *   - The `SingleInstanceService` RAII handle for `org.plasmazones.Editor.App`
 *     on the session bus.
 *   - `applyLaunchArgs()` — the CLI-arg → controller-state translation,
 *     shared between the initial launch (main.cpp) and forwarded launches
 *     (`handleLaunchRequest()` via D-Bus).
 *   - The `handleLaunchRequest()` Q_SCRIPTABLE slot that a second launcher
 *     invokes over D-Bus.
 *
 * Holds a non-owning pointer to the real `EditorController`. The controller
 * must outlive this object (main.cpp guarantees this — it constructs the
 * controller first, then the launch controller).
 *
 * Deliberately does NOT try to raise/activate the editor window from
 * `handleLaunchRequest`: see the comment on the slot for why.
 */
class EditorLaunchController : public QObject
{
    Q_OBJECT

public:
    /**
     * @param controller The editor controller this launcher forwards args to.
     *                   Must outlive this object.
     * @param parent     Optional QObject parent.
     */
    explicit EditorLaunchController(EditorController* controller, QObject* parent = nullptr);
    ~EditorLaunchController() override;

    /**
     * @brief Claim the well-known D-Bus name and export this object as the
     * launch request receiver.
     *
     * Must be called from main.cpp early enough that the heavy daemon calls
     * inside `applyLaunchArgs` (shader queries, layout load) run *after*
     * the name is owned — otherwise a rapid-fire second launcher can still
     * race its own startup work before discovering there's a running
     * instance to forward to.
     *
     * @return true on success, false if another process holds the name or
     *         the D-Bus registration failed.
     */
    bool registerDBusService();

    /**
     * @brief Apply command-line launch arguments to the underlying controller.
     *
     * Shared entry point for initial launches (called from main.cpp) and
     * forwarded launches (called from `handleLaunchRequest()`). The two
     * paths cannot drift because they route through this method.
     *
     * Preview mode is set *unconditionally* so state from a previous
     * forwarded launch cannot leak into a subsequent non-preview launch on
     * the same running instance.
     */
    void applyLaunchArgs(const QString& screenId, const QString& layoutId, bool createNew, bool preview);

    /**
     * @brief Entry point for a forwarded launch.
     *
     * Called by `EditorAppAdaptor::handleLaunchRequest` when a second
     * launcher hands off its CLI args over D-Bus. Also invocable directly
     * from C++ for testing. Applies args but deliberately does NOT try to
     * raise the window: neither the Wayland destroy-and-remap dance nor
     * XDG activation token forwarding reliably convinces KWin to bring an
     * already-mapped fullscreen xdg_toplevel to the front from a
     * programmatic caller. A forwarded launch whose args don't change
     * anything is a no-op — the user has to focus the existing window
     * themselves.
     */
    void handleLaunchRequest(const QString& screenId, const QString& layoutId, bool createNew, bool preview);

private:
    EditorController* m_controller; ///< Non-owning; must outlive this object.
    std::unique_ptr<SingleInstanceService> m_singleInstance;
};

} // namespace PlasmaZones
