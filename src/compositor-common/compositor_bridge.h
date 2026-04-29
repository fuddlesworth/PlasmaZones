// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QIcon>
#include <QObject>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace PlasmaZones {

/**
 * @brief Opaque handle to a compositor window
 *
 * Each compositor plugin provides its own concrete type:
 * - KWin: wraps KWin::EffectWindow*
 * - Wayfire: wraps wayfire_view
 *
 * The handle is a type-erased pointer. Concrete implementations
 * static_cast back to their compositor-specific pointer type.
 * nullptr is the sentinel for "no window" / "not found".
 */
using WindowHandle = void*;

/**
 * @brief Compositor-agnostic window property snapshot
 *
 * Used for operations that need multiple properties at once
 * (e.g., snap assist candidate building, window list serialization).
 * Avoids repeated virtual calls for bulk operations.
 */
struct WindowInfo
{
    WindowHandle handle = nullptr;
    QString windowId;
    QString appId;
    QString windowClass; ///< WM_CLASS / app_id as reported by compositor (may differ from appId for XWayland)
    QString screenId;
    QString caption;
    QIcon icon;
    QRectF frameGeometry;
    QSizeF minSize;
    bool isMinimized = false;
    bool isFullScreen = false;
    bool isOnCurrentDesktop = true;
    bool isOnCurrentActivity = true;
    bool hasDecoration = false;
    bool isNormalWindow = true;
    bool keepAbove = false;
    qint64 pid = 0;
};

/**
 * @brief Abstract interface bridging compositor-agnostic logic to compositor-specific APIs
 *
 * This interface allows shared code (autotile state management, D-Bus signal dispatch,
 * snap assist candidate building, etc.) to operate on windows without knowing whether
 * the compositor is KWin, Wayfire, or something else.
 *
 * Each compositor plugin implements this interface. The shared code calls these methods
 * instead of directly accessing compositor-specific APIs.
 *
 * Design principles:
 * - Methods take WindowHandle (void*) — plugins static_cast to their native type
 * - Bulk operations use WindowInfo snapshots to avoid virtual call overhead
 * - Only methods actually needed by shared code are included (no speculative API)
 * - D-Bus helpers (fireAndForget, asyncCall) are free functions, not on this interface
 */
class ICompositorBridge
{
public:
    ICompositorBridge() = default;
    virtual ~ICompositorBridge() = default;

    ICompositorBridge(const ICompositorBridge&) = delete;
    ICompositorBridge& operator=(const ICompositorBridge&) = delete;
    ICompositorBridge(ICompositorBridge&&) = delete;
    ICompositorBridge& operator=(ICompositorBridge&&) = delete;

    // ═══════════════════════════════════════════════════════════════════
    // Window Lookup
    // ═══════════════════════════════════════════════════════════════════

    /// Find a window by its full window ID ("appId|instanceId")
    virtual WindowHandle findWindowById(const QString& windowId) const = 0;

    /// Find all windows matching a window ID (exact + appId fallback for disambiguation)
    virtual QVector<WindowHandle> findAllWindowsById(const QString& windowId) const = 0;

    /// Get all managed windows in stacking order (bottom to top)
    virtual QVector<WindowHandle> stackingOrder() const = 0;

    // ═══════════════════════════════════════════════════════════════════
    // Window Identity
    // ═══════════════════════════════════════════════════════════════════

    /// Get the full window ID for a window ("appId|instanceId")
    virtual QString windowId(WindowHandle w) const = 0;

    /// Get the screen ID where a window is located
    virtual QString windowScreenId(WindowHandle w) const = 0;

    // ═══════════════════════════════════════════════════════════════════
    // Window Properties
    // ═══════════════════════════════════════════════════════════════════

    virtual QRectF frameGeometry(WindowHandle w) const = 0;
    virtual QSizeF minSize(WindowHandle w) const = 0;
    virtual bool isMinimized(WindowHandle w) const = 0;
    virtual bool isOnCurrentDesktop(WindowHandle w) const = 0;
    virtual bool isOnCurrentActivity(WindowHandle w) const = 0;
    virtual bool hasDecoration(WindowHandle w) const = 0;

    /// Fill a WindowInfo snapshot (for bulk operations)
    virtual WindowInfo windowInfo(WindowHandle w) const = 0;

    // ═══════════════════════════════════════════════════════════════════
    // Window Filtering
    // ═══════════════════════════════════════════════════════════════════

    /// Should this window be managed by PlasmaZones at all?
    virtual bool shouldHandleWindow(WindowHandle w) const = 0;

    /// Is this window eligible for autotile (stricter than shouldHandle)?
    virtual bool isTileableWindow(WindowHandle w) const = 0;

    // ═══════════════════════════════════════════════════════════════════
    // Window Actions
    // ═══════════════════════════════════════════════════════════════════

    /// Move/resize a window to the given geometry
    virtual void moveResize(WindowHandle w, const QRectF& geometry) = 0;

    /// Set/remove server-side decoration (title bar)
    virtual void setNoBorder(WindowHandle w, bool noBorder) = 0;

    /// Maximize or restore a window
    virtual void setMaximized(WindowHandle w, bool maximized) = 0;

    /// Give input focus to a window
    virtual void activateWindow(WindowHandle w) = 0;

    /// Raise a window in the stacking order
    virtual void raiseWindow(WindowHandle w) = 0;

    /// Apply snap geometry with optional animation
    virtual void applySnapGeometry(WindowHandle w, const QRectF& geometry, bool skipAnimation = false) = 0;

    // ═══════════════════════════════════════════════════════════════════
    // D-Bus Integration (convenience wrappers using compositor as parent)
    // ═══════════════════════════════════════════════════════════════════

    /// Get the QObject parent for D-Bus watcher ownership
    virtual QObject* asQObject() = 0;

    /// Check if the daemon D-Bus service is registered and ready
    virtual bool isDaemonReady() const = 0;

    // ═══════════════════════════════════════════════════════════════════
    // Screen Management
    // ═══════════════════════════════════════════════════════════════════

    /// Invalidate cached screen IDs (call on screen add/remove/reconfigure)
    virtual void invalidateScreenIdCache() = 0;
};

} // namespace PlasmaZones
