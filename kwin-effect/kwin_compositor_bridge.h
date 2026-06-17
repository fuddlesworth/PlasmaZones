// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorCompositor/ICompositorBridge.h>

namespace KWin {
class EffectWindow;
}

namespace PlasmaZones {

// Targeted using-declarations, not a namespace-wide directive: headers must
// not leak the whole PhosphorCompositor namespace into every includer.
using PhosphorCompositor::ICompositorBridge;
using PhosphorCompositor::WindowHandle;
using PhosphorCompositor::WindowInfo;

class PlasmaZonesEffect;

/**
 * @brief KWin implementation of ICompositorBridge
 *
 * Adapts KWin::EffectWindow* and KWin effect APIs to the compositor-agnostic
 * ICompositorBridge interface. WindowHandle is a KWin::EffectWindow* cast to void*.
 *
 * Delegates to PlasmaZonesEffect methods (which own the actual KWin state).
 */
class KWinCompositorBridge : public ICompositorBridge
{
public:
    // Reference, not pointer: the bridge is a member of the effect and can
    // never outlive it, so non-null is a structural guarantee rather than a
    // debug-only Q_ASSERT.
    explicit KWinCompositorBridge(PlasmaZonesEffect& effect);

    // Public: used by KWin-specific handler code
    static KWin::EffectWindow* toEffectWindow(WindowHandle w)
    {
        return static_cast<KWin::EffectWindow*>(w);
    }
    static WindowHandle fromEffectWindow(KWin::EffectWindow* w)
    {
        return static_cast<WindowHandle>(w);
    }

    // ═══════════════════════════════════════════════════════════════════
    // ICompositorBridge implementation
    // ═══════════════════════════════════════════════════════════════════

    WindowHandle findWindowById(const QString& windowId) const override;
    QVector<WindowHandle> findAllWindowsById(const QString& windowId) const override;
    QVector<WindowHandle> stackingOrder() const override;

    QString windowId(WindowHandle w) const override;
    QString windowScreenId(WindowHandle w) const override;

    QRectF frameGeometry(WindowHandle w) const override;
    QSizeF minSize(WindowHandle w) const override;
    bool isMinimized(WindowHandle w) const override;
    bool isOnCurrentDesktop(WindowHandle w) const override;
    bool isOnCurrentActivity(WindowHandle w) const override;
    bool hasDecoration(WindowHandle w) const override;
    bool userCanSetNoBorder(WindowHandle w) const override;
    bool isNoBorder(WindowHandle w) const override;
    QRectF moveResizeGeometry(WindowHandle w) const override;
    WindowInfo windowInfo(WindowHandle w) const override;

    bool shouldHandleWindow(WindowHandle w) const override;
    bool isTileableWindow(WindowHandle w) const override;

    void moveResize(WindowHandle w, const QRectF& geometry) override;
    void setNoBorder(WindowHandle w, bool noBorder) override;
    void setMaximized(WindowHandle w, bool maximized) override;
    void activateWindow(WindowHandle w) override;
    void raiseWindow(WindowHandle w) override;
    void applyWindowGeometry(WindowHandle w, const QRectF& geometry, bool skipAnimation) override;

    QObject* asQObject() override;
    bool isDaemonReady() const override;

    void invalidateScreenIdCache() override;

private:
    PlasmaZonesEffect& m_effect;
};

} // namespace PlasmaZones
