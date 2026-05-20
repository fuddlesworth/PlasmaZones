// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Border decoration — title-bar hiding + border refresh for scroll mode.
// Split out of scrollhandler.cpp to keep that translation unit under the
// 800-line limit; mirrors the autotilehandler/ sub-file layout.

#include "../scrollhandler.h"
#include "../plasmazoneseffect.h"

#include <effect/effectwindow.h>
#include <window.h>

#include <QLoggingCategory>
#include <QStringList>

#include <utility>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

void ScrollHandler::setWindowBorderless(KWin::EffectWindow* w, const QString& windowId, bool borderless)
{
    if (borderless) {
        if (!w) {
            return;
        }
        // CSD windows (GTK/Electron) carry no server-side decoration —
        // hasDecoration() is false — so there is no title bar to hide.
        if (!w->hasDecoration()) {
            return;
        }
        KWin::Window* kw = w->window();
        if (!kw) {
            return;
        }
        // Verify identity before hiding the title bar: hide-loop callers
        // resolve w via findWindowById(), whose appId fuzzy fallback can return
        // a *different* live window of the same app when the exact one is gone.
        // setNoBorder(true) on that window would wrongly hide a title bar this
        // windowId never owned.
        if (m_effect->getWindowId(w) != windowId) {
            return;
        }
        if (!m_borderlessWindows.contains(windowId)) {
            m_borderlessWindows.insert(windowId);
            kw->setNoBorder(true);
            qCDebug(lcEffect) << "Scroll: hid title bar for" << windowId;
        }
    } else {
        if (!m_borderlessWindows.contains(windowId)) {
            return; // not borderless to begin with
        }
        // Decide what to do about the tracking entry based on identity:
        //   - w is null: window is genuinely gone; drop the entry so a closed
        //     window cannot leak into m_borderlessWindows.
        //   - w resolves to the SAME windowId: drop the entry AND restore the
        //     title bar — the normal case.
        //   - w resolves to a DIFFERENT live window of the same app (the
        //     findWindowById appId fuzzy fallback): leave the entry in place
        //     so the original window's tracked-borderless state survives, and
        //     do nothing to the wrong window's decoration. The user can hit
        //     this path on rapid window-of-same-class churn; clearing the
        //     entry here would make the still-alive original permanently
        //     stuck in borderless with nothing to restore it.
        if (!w) {
            m_borderlessWindows.remove(windowId);
            return;
        }
        if (m_effect->getWindowId(w) != windowId) {
            return; // wrong window — leave both entry and decoration alone
        }
        m_borderlessWindows.remove(windowId);
        if (KWin::Window* kw = w->window()) {
            kw->setNoBorder(false);
            qCDebug(lcEffect) << "Scroll: restored title bar for" << windowId;
        }
    }
}

void ScrollHandler::hideTitleBarsForTrackedWindows()
{
    // setWindowBorderless self-gates — already-borderless and CSD windows are
    // skipped — so this is safe to call after any tracked-set change. Minimized
    // windows are skipped: onWindowMinimizedChanged restores their title bar
    // and this must not re-hide it (it re-hides on restore).
    for (const QString& windowId : std::as_const(m_notifiedWindows)) {
        KWin::EffectWindow* w = m_effect->findWindowById(windowId);
        if (w && !w->isMinimized()) {
            setWindowBorderless(w, windowId, true);
        }
    }
}

void ScrollHandler::refreshDecorations()
{
    // Hide title bars for every tracked scroll window when the setting is on.
    if (m_border.hideTitleBars) {
        hideTitleBarsForTrackedWindows();
    }
    m_effect->updateAllBorders();
}

void ScrollHandler::clearDecoration(const QString& windowId, KWin::EffectWindow* w)
{
    // The window is no longer scroll-managed — restore any title bar scroll hid
    // and drop its border. Another placement mode (or none) now owns it.
    setWindowBorderless(w, windowId, false);
    m_effect->removeWindowBorder(windowId);
}

bool ScrollHandler::updateHideTitleBarsSetting(bool enabled)
{
    if (m_border.hideTitleBars == enabled) {
        return false;
    }
    m_border.hideTitleBars = enabled;
    if (enabled) {
        // Turning ON — hide the title bar of every tracked scroll window.
        hideTitleBarsForTrackedWindows();
    } else {
        // Turning OFF — restore every title bar scroll hid. Snapshot first:
        // setWindowBorderless mutates m_borderlessWindows under the loop.
        const QStringList borderless(m_borderlessWindows.cbegin(), m_borderlessWindows.cend());
        for (const QString& windowId : borderless) {
            setWindowBorderless(m_effect->findWindowById(windowId), windowId, false);
        }
    }
    return true;
}

} // namespace PlasmaZones
