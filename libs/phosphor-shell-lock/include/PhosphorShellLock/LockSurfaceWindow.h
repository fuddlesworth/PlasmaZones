// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QQuickWindow>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QScreen;
class QShowEvent;
QT_END_NAMESPACE

namespace PhosphorWayland {
class LockSurface;
}

namespace PhosphorShellLock {

/**
 * @brief `Phosphor.Lock.LockSurface`: a QML window that is one output's
 *        `ext_session_lock_surface_v1`.
 *
 *     PerScreen {
 *         model: PhosphorShell.screens
 *         delegate: LockSurface {
 *             required property var phosphorScreen
 *             screen: phosphorScreen
 *             visible: lockController.surfacesWanted
 *             LockScreen { anchors.fill: parent }
 *         }
 *     }
 *
 * A QQuickWindow marked through `PhosphorWayland::LockSurface` at
 * construction, so the QPA plugin gives it the lock-surface role on
 * `screen` when it is shown. The compositor sizes it (it always covers its
 * output) and grants it keyboard focus by the protocol's own rules, which is
 * why the content needs no focus request.
 *
 * It refuses to exist while no session lock is held: `PhosphorWayland::
 * SessionLock::canCreateSurfaces()` is false outside `lock()` .. release,
 * and a show in that state is refused (`refused()`, then the window hides
 * itself) rather than mapped as some other kind of window. The lock's
 * release unmaps every lock surface at the compositor, so the composer
 * binds `visible` to the lock state and lets the window go with it.
 */
class LockSurfaceWindow : public QQuickWindow
{
    Q_OBJECT
    QML_NAMED_ELEMENT(LockSurface)
    Q_PROPERTY(QScreen* screen READ screen WRITE setScreen NOTIFY screenChanged)
    Q_PROPERTY(bool configured READ isConfigured NOTIFY configuredChanged)

public:
    explicit LockSurfaceWindow(QWindow* parent = nullptr);
    ~LockSurfaceWindow() override;

    /// The output this surface covers. Set before the first show; a null
    /// screen resolves to the primary one.
    [[nodiscard]] QScreen* screen() const;
    void setScreen(QScreen* screen);

    /// True once the compositor has sized the surface.
    [[nodiscard]] bool isConfigured() const;

    /// Whether a show right now would be honoured: a session lock is held
    /// or in flight.
    Q_INVOKABLE [[nodiscard]] bool canExist() const;

Q_SIGNALS:
    void configuredChanged();
    /// A show was refused because no session lock is held; the window hides
    /// itself right after.
    void refused();

protected:
    void showEvent(QShowEvent* event) override;

private:
    PhosphorWayland::LockSurface* m_surface = nullptr;
};

} // namespace PhosphorShellLock
