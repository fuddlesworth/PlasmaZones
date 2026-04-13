// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wayland_raise.h"

#include <QPointer>
#include <QTimer>
#include <QWindow>

namespace PlasmaZones {

void forceBringToFront(QWindow* window)
{
    if (!window) {
        return;
    }

    const QWindow::Visibility vis = window->visibility();

    auto showWithVisibility = [](QWindow* w, QWindow::Visibility v) {
        switch (v) {
        case QWindow::FullScreen:
            w->showFullScreen();
            break;
        case QWindow::Maximized:
            w->showMaximized();
            break;
        case QWindow::Minimized:
            // Minimized windows can't be brought to front — un-minimize instead.
            w->showNormal();
            break;
        case QWindow::Hidden:
        case QWindow::AutomaticVisibility:
        case QWindow::Windowed:
        default:
            w->show();
            break;
        }
    };

    if (!window->isVisible()) {
        showWithVisibility(window, vis == QWindow::Hidden ? QWindow::AutomaticVisibility : vis);
        window->raise();
        window->requestActivate();
        return;
    }

    // Defer the destroy-and-remap so we don't tear down the platform surface
    // from inside a paint event or D-Bus call dispatch.
    QPointer<QWindow> safeWindow(window);
    QTimer::singleShot(0, window, [safeWindow, vis, showWithVisibility]() {
        if (!safeWindow) {
            return;
        }
        safeWindow->destroy();
        showWithVisibility(safeWindow.data(), vis);
        safeWindow->raise();
        safeWindow->requestActivate();
    });
}

} // namespace PlasmaZones
