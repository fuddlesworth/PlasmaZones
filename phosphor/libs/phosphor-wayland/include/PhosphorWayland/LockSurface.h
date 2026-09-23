// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorwayland_export.h>

#include <QObject>
#include <QPointer>
#include <QScreen>
#include <QWindow>

namespace PhosphorWayland {

/// Property keys used by LockSurface ↔ QPA plugin communication.
namespace LockSurfaceProps {
inline constexpr const char* IsSessionLock = "_ps_session_lock";
inline constexpr const char* Surface = "_ps_session_lock_surface";
} // namespace LockSurfaceProps

/**
 * @brief Marks a QWindow as an `ext_session_lock_surface_v1` for one output.
 *
 * Pure Qt API, no Wayland types exposed. Call `get()` on a not-yet-shown
 * window and set its screen; when the window is shown the QPA plugin creates
 * the lock surface against the `ext_session_lock_v1` that `SessionLock`
 * currently holds, on the wl_output behind `screen()`. The compositor then
 * sends the surface's exact size in a configure, which the plugin acks and
 * applies to the window before its first frame; `configured` turns true once
 * that has happened.
 *
 * A lock surface can only exist while a lock is held or in flight
 * (`SessionLock::canCreateSurfaces()`): shown outside that window, the QWindow
 * gets no shell role and stays unmapped, which is the protocol's own answer.
 * The compositor unmaps every lock surface when the lock is released; the
 * window should then be hidden (destroying the surface) rather than reused.
 *
 * One per window, parented to it. Threading: GUI thread only.
 */
class PHOSPHORWAYLAND_EXPORT LockSurface : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(LockSurface)

    Q_PROPERTY(QScreen* screen READ screen WRITE setScreen NOTIFY screenChanged)
    Q_PROPERTY(bool configured READ isConfigured NOTIFY configuredChanged)

public:
    ~LockSurface() override;

    /// The output this lock surface covers. Immutable once the window is
    /// shown (the wl_output is bound at surface creation). A null screen
    /// resolves to the primary one.
    void setScreen(QScreen* screen);
    [[nodiscard]] QScreen* screen() const;

    /// True once the compositor has sized the surface and the window has
    /// taken that size.
    [[nodiscard]] bool isConfigured() const;

    /// Get or create the LockSurface for @p window. Must be called BEFORE the
    /// window's first show(); returns nullptr when the window is already
    /// visible (its platform window has a different role by then).
    static LockSurface* get(QWindow* window);

    /// The existing LockSurface for @p window, or nullptr.
    static LockSurface* find(QWindow* window);

    /// True iff the compositor advertises `ext_session_lock_manager_v1`.
    static bool isSupported();

Q_SIGNALS:
    void screenChanged();
    void configuredChanged();

private:
    friend class SessionLockWindow;
    explicit LockSurface(QWindow* window);
    void setConfigured(bool configured);

    QPointer<QWindow> m_window;
    QPointer<QScreen> m_screen;
    bool m_configured = false;
};

} // namespace PhosphorWayland

Q_DECLARE_METATYPE(PhosphorWayland::LockSurface*)
