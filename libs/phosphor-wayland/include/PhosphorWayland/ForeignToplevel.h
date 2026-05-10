// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorwayland_export.h>

#include <QList>
#include <QObject>
#include <QPointer>

#include <memory>

QT_BEGIN_NAMESPACE
class QScreen;
class QWindow;
QT_END_NAMESPACE

namespace PhosphorWayland {

class ForeignToplevel;

/**
 * @brief Client-side wrapper around `zwlr_foreign_toplevel_manager_v1`.
 *
 * Lists every toplevel window from every Wayland client connected to the
 * compositor and lets the consumer subscribe to add / remove events. Owns
 * one ForeignToplevel per window. Suitable for taskbars, window-lists,
 * window switchers, IME candidate lists, etc.
 *
 * Construct one per process — the manager binds the protocol global on
 * the first instance and Wayland keeps a per-binding event stream, so a
 * second binding would receive a duplicate stream of toplevels.
 *
 * Threading: every method MUST be called from the GUI thread.
 *
 * Lifecycle: ForeignToplevel objects are owned by the manager and live
 * until the compositor sends `closed` (or the manager is destroyed). The
 * `toplevelRemoved` signal fires immediately before the ForeignToplevel
 * is `deleteLater()`'d. Don't dereference a ForeignToplevel from a slot
 * fired by `toplevelRemoved` — capture by ID instead.
 */
class PHOSPHORWAYLAND_EXPORT ForeignToplevelManager : public QObject
{
    Q_OBJECT

public:
    explicit ForeignToplevelManager(QObject* parent = nullptr);
    ~ForeignToplevelManager() override;

    /// True iff the compositor advertises `zwlr_foreign_toplevel_manager_v1`.
    /// Check this before constructing — the constructor still succeeds on
    /// unsupported compositors but signals nothing.
    static bool isSupported();

    /// Snapshot of the current toplevel set. Pointers stay live until the
    /// matching `toplevelRemoved` signal fires; never null inside the list.
    [[nodiscard]] QList<ForeignToplevel*> toplevels() const;

    /// Stop receiving events. After this the manager will not emit
    /// `toplevelAdded`. Existing ForeignToplevel handles remain valid until
    /// the compositor closes them or this object is destroyed. Idempotent.
    void stop();

Q_SIGNALS:
    void toplevelAdded(PhosphorWayland::ForeignToplevel* toplevel);
    void toplevelRemoved(PhosphorWayland::ForeignToplevel* toplevel);

private:
    friend class ForeignToplevel;
    class Private;
    std::unique_ptr<Private> d;
};

/**
 * @brief A single toplevel window owned by some other Wayland client.
 *
 * Surfaces the protocol's title / app_id / state / output / parent events
 * as Q_PROPERTYs and the activate / close / set_maximized / set_minimized
 * / set_fullscreen / set_rectangle requests as Q_INVOKABLE methods. Owned
 * by ForeignToplevelManager; never construct directly.
 *
 * State changes are coalesced — the protocol sends a `done` event after
 * a batch of property updates, and we only fire the change signals at
 * that point. This matches xdg-shell's `configure` semantics and lets
 * QML bindings re-evaluate once per logical update instead of N times
 * per frame for active windows that get rapid updates.
 */
class PHOSPHORWAYLAND_EXPORT ForeignToplevel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString appId READ appId NOTIFY appIdChanged)
    Q_PROPERTY(bool maximized READ isMaximized NOTIFY stateChanged)
    Q_PROPERTY(bool minimized READ isMinimized NOTIFY stateChanged)
    Q_PROPERTY(bool activated READ isActivated NOTIFY stateChanged)
    Q_PROPERTY(bool fullscreen READ isFullscreen NOTIFY stateChanged)
    Q_PROPERTY(QList<QScreen*> outputs READ outputs NOTIFY outputsChanged)
    Q_PROPERTY(PhosphorWayland::ForeignToplevel* parentToplevel READ parentToplevel NOTIFY parentToplevelChanged)
    Q_PROPERTY(bool closed READ isClosed NOTIFY closedChanged)

public:
    ~ForeignToplevel() override;

    [[nodiscard]] QString title() const;
    [[nodiscard]] QString appId() const;
    [[nodiscard]] bool isMaximized() const;
    [[nodiscard]] bool isMinimized() const;
    [[nodiscard]] bool isActivated() const;
    [[nodiscard]] bool isFullscreen() const;
    [[nodiscard]] QList<QScreen*> outputs() const;
    [[nodiscard]] ForeignToplevel* parentToplevel() const;
    [[nodiscard]] bool isClosed() const;

    /// Request the compositor focus this toplevel. Always uses the first
    /// seat returned by Qt's QtWaylandClient::QWaylandDisplay (typically
    /// the only seat on single-user systems). Multi-seat selection is not
    /// currently exposed — adding a `QObject* seat` parameter would be a
    /// future API extension if a real consumer needs it.
    Q_INVOKABLE void activate();

    /// Request the compositor close this toplevel. Equivalent to clicking
    /// the X button — the owning client decides whether to honour it
    /// (some show "save changes?" dialogs first).
    Q_INVOKABLE void close();

    Q_INVOKABLE void setMaximized(bool maximized);
    Q_INVOKABLE void setMinimized(bool minimized);
    /// Toggle fullscreen on the given output (null = compositor's choice).
    /// `output` is silently ignored when `fullscreen=false` — the protocol
    /// has separate set/unset requests and `unset_fullscreen` carries no
    /// output argument. Requires zwlr_foreign_toplevel_manager_v1 version
    /// >= 2; on older compositors this is silently a no-op.
    Q_INVOKABLE void setFullscreen(bool fullscreen, QScreen* output = nullptr);

    /// Tell the compositor where on screen this toplevel's representation
    /// lives (e.g. its taskbar entry). The compositor uses this to anchor
    /// minimize / unminimize animations. `surface` must be a QWindow that's
    /// currently mapped to a wl_surface; the rectangle is surface-local
    /// (not screen-local). QML callers wanting to anchor to a QQuickItem
    /// should pass `item.Window.window` and pre-map the rect via mapToScene().
    Q_INVOKABLE void setRectangle(QWindow* surface, const QRect& rect);

Q_SIGNALS:
    void titleChanged();
    void appIdChanged();
    void stateChanged();
    void outputsChanged();
    void parentToplevelChanged();
    void closedChanged();

private:
    friend class ForeignToplevelManager;
    class Private;
    explicit ForeignToplevel(std::unique_ptr<Private> p);
    std::unique_ptr<Private> d;
};

} // namespace PhosphorWayland

Q_DECLARE_METATYPE(PhosphorWayland::ForeignToplevel*)
