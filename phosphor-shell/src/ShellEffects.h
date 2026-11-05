// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QRect>

QT_BEGIN_NAMESPACE
class QQuickItem;
QT_END_NAMESPACE

namespace PhosphorShellApp {

// Compositor-side effects the shell's surfaces ask for. Exposed to QML as
// the `ShellEffects` context property (the BarRegistry precedent).
//
// A Wayland client cannot read what is behind its own surface, so the
// phosphor-glass material (real backdrop blur under a navy tint, 05 §5)
// cannot be rendered client-side over live windows. What a client CAN do
// is ask the compositor to blur behind a region of its surface through the
// kde-blur protocol (org_kde_kwin_blur), which KWin honours for layer
// surfaces exactly as it does for Plasma's own panel. KWindowEffects'
// Wayland plugin speaks that protocol and re-sends the request whenever
// the wl_surface is recreated, so one call per window suffices.
//
// The band then paints the pack's navy tint at its opacity over the
// compositor's blur, which is the glass; the pack's glow and sweep are
// off on the bar by design (A2 §3.2), so nothing of the shader is lost.
class ShellEffects : public QObject
{
    Q_OBJECT

public:
    explicit ShellEffects(QObject* parent = nullptr);

    /// Whether the build carries a blur backend at all.
    [[nodiscard]] Q_INVOKABLE static bool blurAvailable();

    /// Request compositor blur behind `region` of the window `item` lives
    /// in (window-local logical pixels). An empty region disables it.
    /// Returns false when the item has no window yet or the backend is
    /// absent, so a caller can retry once the window exists.
    Q_INVOKABLE static bool setBlurBehind(QQuickItem* item, const QRect& region);
};

} // namespace PhosphorShellApp
