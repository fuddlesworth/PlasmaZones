// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "phosphorscreens_export.h"

#include <QObject>

class QScreen;

namespace Phosphor::Screens {

/**
 * @brief Pluggable producer of panel-reservation offsets per screen.
 *
 * Panels (taskbars, docks, status bars) reserve part of each screen's
 * geometry. Different desktops report this differently: KDE Plasma exposes
 * it via `org.kde.plasmashell` D-Bus, GNOME via `org.gnome.Mutter`, sway /
 * Hyprland via wlr-foreign-toplevel, and so on. PhosphorScreens delegates
 * the source-specific details to an `IPanelSource` so the manager core
 * stays compositor-agnostic.
 *
 * Implementations are owned by the consumer (typically the daemon).
 * Lifetime: must outlive the ScreenManager that holds the pointer.
 *
 * Threading: ScreenManager calls `currentOffsets`, `ready`, and
 * `requestRequery` from the GUI thread; signals must be emitted on the
 * GUI thread (use `QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection)`
 * if the implementation queries from a worker).
 */
class PHOSPHORSCREENS_EXPORT IPanelSource : public QObject
{
    Q_OBJECT
public:
    /// Per-edge reserved geometry, in physical pixels relative to the
    /// screen's top-left. Zero on every edge means "no panel reserves
    /// space here" (or "we don't know yet").
    struct Offsets
    {
        int top = 0;
        int bottom = 0;
        int left = 0;
        int right = 0;

        bool isZero() const noexcept
        {
            return top == 0 && bottom == 0 && left == 0 && right == 0;
        }
        bool operator==(const Offsets&) const = default;
    };

    explicit IPanelSource(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    ~IPanelSource() override = default;

    /// Begin watching. Implementations may emit @ref panelOffsetsChanged
    /// any time after this returns; ScreenManager reads `currentOffsets`
    /// on each emission to refresh its cache.
    virtual void start() = 0;

    /// Stop watching. Subsequent `currentOffsets` queries should still
    /// return the last-known values; only the change channel is closed.
    virtual void stop() = 0;

    /// Snapshot for a given screen. Returns zero offsets if the source
    /// has no information for this screen yet.
    virtual Offsets currentOffsets(QScreen* screen) const = 0;

    /// Has *any* successful query landed? Drives ScreenManager's
    /// `panelGeometryReady` one-shot signal — consumers that compute
    /// initial zone geometry at daemon startup gate on this so they
    /// don't lay out windows against the unreserved screen rect.
    virtual bool ready() const = 0;

    /// Best-effort: ask the backend to re-query immediately, optionally
    /// after a short delay (e.g. to let a panel-editor UI close fully
    /// before settling). Implementations that don't support push-style
    /// refresh can no-op; @ref requeryCompleted is still expected to
    /// fire at most once per call so callers can chain UI updates.
    virtual void requestRequery(int delayMs = 0) = 0;

Q_SIGNALS:
    /// Offsets for @p screen changed. Emit only when the new values
    /// differ from the last known values (no spam on repeat queries).
    void panelOffsetsChanged(QScreen* screen);

    /// A `requestRequery` cycle completed. Forwarded by ScreenManager as
    /// `delayedPanelRequeryCompleted`. Always fires at most once per
    /// `requestRequery` call, even if the underlying query coalesces
    /// with one already in flight.
    void requeryCompleted();
};

} // namespace Phosphor::Screens
