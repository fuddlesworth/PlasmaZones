// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

class QWindow;

namespace PlasmaZones {

/**
 * @brief Force an already-visible top-level window to the front on Wayland.
 *
 * A plain `show() / raise() / requestActivate()` on an already-mapped xdg_toplevel
 * is a no-op on Wayland — KWin will not steal focus without a valid activation
 * token. Destroying the platform window and re-showing it forces a fresh
 * xdg_toplevel mapping, which the compositor treats as a new window presentation
 * and brings to the front. The QQuickWindow / QML scene graph survives because
 * only the native surface is destroyed, not the QWindow object.
 *
 * For not-yet-visible windows, this is equivalent to `show() / raise() /
 * requestActivate()` since there's no mapped surface to recreate.
 *
 * The destroy-and-remap is deferred via QTimer::singleShot(0) so it runs on the
 * caller's event loop after any pending repaints — calling it during a paint
 * or D-Bus dispatch will not re-enter platform code.
 */
PLASMAZONES_EXPORT void forceBringToFront(QWindow* window);

} // namespace PlasmaZones
