// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorCompositor/ICompositorBridge.h>

#include <QString>
#include <QStringList>

#include <map>
#include <memory>

/**
 * @brief In-memory ICompositorBridge for DecorationManager unit tests.
 *
 * Windows are FakeWindow records owned by the bridge; WindowHandle is a
 * stable pointer to the record. Every mutating call is appended to an
 * ordered call log so tests can assert exact call sequences (e.g.
 * "setNoBorder before moveResize with the pre-captured target").
 *
 * Known fidelity divergences from the real KWinCompositorBridge — bug
 * classes that live in these gaps are NOT covered by this fake:
 *  - findWindowById is exact-match only BY DEFAULT. The real effect lookup
 *    falls back to a fuzzy appId match (cross-session restore) that can
 *    resolve a SIBLING window of the same app, or null when ambiguous; set
 *    fuzzyFindByAppId to opt into that behavior (used to exercise
 *    DecorationManager::resolveExact's sibling-mismatch guard).
 *  - moveResize applies the frame immediately. Real Wayland clients ack the
 *    configure asynchronously, so frameGeometry() lags moveResizeGeometry().
 *  - setNoBorder leaves frame/moveResizeGeo untouched, while real KWin holds
 *    the CLIENT size constant so the frame shrinks/grows by the title-bar
 *    height. The AlreadyPlaced re-assert tests therefore pin call ORDER
 *    only — the under-fill bug class the re-assert exists for cannot
 *    reproduce here.
 *  - findAllWindowsById does NOT honor fuzzyFindByAppId — it stays
 *    exact-match even when the flag is set, so multi-match fuzzy scenarios
 *    (several windows sharing an appId) cannot be exercised through it.
 *  - applySnapGeometry is logged as a moveResize entry in callLog (it
 *    delegates to the same record mutation), so call-order assertions
 *    cannot distinguish the two paths.
 *  - stackingOrder() returns std::map key order (lexicographic by id), NOT
 *    bottom-to-top stacking as the interface documents. DecorationManager
 *    never calls it; a future consumer test must not rely on the ordering.
 *  - windowInfo() populates handle/windowId/frameGeometry/screenId/
 *    minimized/hasDecoration but leaves appId/windowClass/caption/icon/
 *    minSize/pid and the remaining boolean flags at their defaults;
 *    asQObject() returns nullptr (no D-Bus watcher parent in unit tests).
 *
 * Style note: `callLog` / `fuzzyFindByAppId` are deliberately bare public
 * data members (no m_ prefix, no accessors) — they are test-fixture knobs
 * the suites poke directly, and accessor ceremony would only obscure that.
 */
class FakeCompositorBridge : public PhosphorCompositor::ICompositorBridge
{
public:
    struct FakeWindow
    {
        QString id;
        QString screenId;
        QRectF frame;
        QRectF moveResizeGeo;
        bool noBorder = false;
        bool userCanSetNoBorder = true;
        bool hasDecoration = true;
        bool minimized = false;
    };

    FakeWindow* addWindow(const QString& id, const QString& screenId = QStringLiteral("screen-0"),
                          const QRectF& frame = QRectF(0, 0, 800, 600))
    {
        auto win = std::make_unique<FakeWindow>();
        win->id = id;
        win->screenId = screenId;
        win->frame = frame;
        win->moveResizeGeo = frame;
        FakeWindow* raw = win.get();
        m_windows[id] = std::move(win);
        return raw;
    }

    void removeWindow(const QString& id)
    {
        m_windows.erase(id);
    }

    FakeWindow* window(const QString& id) const
    {
        auto it = m_windows.find(id);
        return it != m_windows.end() ? it->second.get() : nullptr;
    }

    QStringList callLog;

    /// Opt-in fuzzy lookup mirroring the real effect's appId fallback
    /// (cross-session restore): when the exact id misses, return the first
    /// window whose appId (text before '|') matches.
    bool fuzzyFindByAppId = false;

    void clearLog()
    {
        callLog.clear();
    }

    // ── ICompositorBridge ──────────────────────────────────────────────

    PhosphorCompositor::WindowHandle findWindowById(const QString& windowId) const override
    {
        if (auto* w = window(windowId)) {
            return w;
        }
        if (fuzzyFindByAppId) {
            const QString appId = windowId.section(QLatin1Char('|'), 0, 0);
            for (const auto& [id, win] : m_windows) {
                if (id.section(QLatin1Char('|'), 0, 0) == appId) {
                    return win.get();
                }
            }
        }
        return nullptr;
    }

    QVector<PhosphorCompositor::WindowHandle> findAllWindowsById(const QString& windowId) const override
    {
        QVector<PhosphorCompositor::WindowHandle> result;
        if (auto* w = window(windowId)) {
            result.append(w);
        }
        return result;
    }

    QVector<PhosphorCompositor::WindowHandle> stackingOrder() const override
    {
        QVector<PhosphorCompositor::WindowHandle> result;
        for (const auto& [id, win] : m_windows) {
            result.append(win.get());
        }
        return result;
    }

    QString windowId(PhosphorCompositor::WindowHandle w) const override
    {
        return w ? toWin(w)->id : QString();
    }

    QString windowScreenId(PhosphorCompositor::WindowHandle w) const override
    {
        return w ? toWin(w)->screenId : QString();
    }

    QRectF frameGeometry(PhosphorCompositor::WindowHandle w) const override
    {
        return w ? toWin(w)->frame : QRectF();
    }

    QSizeF minSize(PhosphorCompositor::WindowHandle) const override
    {
        return QSizeF();
    }

    bool isMinimized(PhosphorCompositor::WindowHandle w) const override
    {
        return w && toWin(w)->minimized;
    }

    bool isOnCurrentDesktop(PhosphorCompositor::WindowHandle) const override
    {
        return true;
    }

    bool isOnCurrentActivity(PhosphorCompositor::WindowHandle) const override
    {
        return true;
    }

    bool hasDecoration(PhosphorCompositor::WindowHandle w) const override
    {
        return w && toWin(w)->hasDecoration;
    }

    bool userCanSetNoBorder(PhosphorCompositor::WindowHandle w) const override
    {
        return w && toWin(w)->userCanSetNoBorder;
    }

    bool isNoBorder(PhosphorCompositor::WindowHandle w) const override
    {
        return w && toWin(w)->noBorder;
    }

    QRectF moveResizeGeometry(PhosphorCompositor::WindowHandle w) const override
    {
        return w ? toWin(w)->moveResizeGeo : QRectF();
    }

    PhosphorCompositor::WindowInfo windowInfo(PhosphorCompositor::WindowHandle w) const override
    {
        PhosphorCompositor::WindowInfo info;
        if (!w) {
            return info;
        }
        const FakeWindow* fw = toWin(w);
        info.handle = w;
        info.windowId = fw->id;
        info.screenId = fw->screenId;
        info.frameGeometry = fw->frame;
        info.isMinimized = fw->minimized;
        info.hasDecoration = fw->hasDecoration;
        return info;
    }

    bool shouldHandleWindow(PhosphorCompositor::WindowHandle w) const override
    {
        return w != nullptr;
    }

    bool isTileableWindow(PhosphorCompositor::WindowHandle w) const override
    {
        return w != nullptr;
    }

    void moveResize(PhosphorCompositor::WindowHandle w, const QRectF& geometry) override
    {
        if (!w) {
            return;
        }
        FakeWindow* fw = toWin(w);
        fw->moveResizeGeo = geometry;
        fw->frame = geometry; // fake immediate configure ack
        callLog.append(QStringLiteral("moveResize(%1,%2x%3)").arg(fw->id).arg(geometry.width()).arg(geometry.height()));
    }

    void setNoBorder(PhosphorCompositor::WindowHandle w, bool noBorder) override
    {
        if (!w) {
            return;
        }
        FakeWindow* fw = toWin(w);
        fw->noBorder = noBorder;
        callLog.append(QStringLiteral("setNoBorder(%1,%2)")
                           .arg(fw->id, noBorder ? QStringLiteral("true") : QStringLiteral("false")));
    }

    void setMaximized(PhosphorCompositor::WindowHandle, bool) override
    {
    }

    void activateWindow(PhosphorCompositor::WindowHandle) override
    {
    }

    void raiseWindow(PhosphorCompositor::WindowHandle) override
    {
    }

    void applySnapGeometry(PhosphorCompositor::WindowHandle w, const QRectF& geometry, bool) override
    {
        moveResize(w, geometry);
    }

    QObject* asQObject() override
    {
        return nullptr;
    }

    bool isDaemonReady() const override
    {
        return true;
    }

    void invalidateScreenIdCache() override
    {
    }

private:
    static FakeWindow* toWin(PhosphorCompositor::WindowHandle w)
    {
        return static_cast<FakeWindow*>(w);
    }

    std::map<QString, std::unique_ptr<FakeWindow>> m_windows;
};
