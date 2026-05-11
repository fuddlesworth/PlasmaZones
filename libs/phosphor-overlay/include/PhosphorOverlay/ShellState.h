// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Per-screen state owned by ShellHost.
//
// Holds the shell-side bookkeeping for one screen: the layer-shell
// wl_surface, its QQuickWindow, the QScreen pointer the shell was
// constructed against, and the slot-Item map keyed by slot name.
//
// Consumer-specific per-screen state (e.g. content geometry,
// per-content sentinels) lives in the consumer's parallel per-screen
// map; ShellState only owns what the library mechanism owns.
//
// Slot Items are looked up at shell-create time by the consumer via
// the post-create callback and stored here so the library can use the
// names to compute the "is any slot live?" predicate ShellHost needs
// for surface mapping (added in the subsequent sync-state migration
// commit).

#include <PhosphorOverlay/SlotEntry.h>
#include <PhosphorOverlay/phosphoroverlay_export.h>

#include <QHash>
#include <QString>

class QQuickWindow;
class QScreen;

namespace PhosphorLayer {
class Surface;
}

namespace PhosphorOverlay {

struct PHOSPHOROVERLAY_EXPORT ShellState
{
    /// The layer-shell wl_surface backing this screen's overlay shell.
    /// Lifetime is managed by the Qt parent chain (the Surface is a
    /// QObject parented inside the SurfaceManager / consumer-owned
    /// engine), but ShellHost::destroyShell schedules `deleteLater()`
    /// explicitly so the wl_surface unmaps deterministically rather
    /// than at random parent-dtor time. nullptr means the shell has
    /// not been created yet (or was torn down and not yet re-created).
    PhosphorLayer::Surface* shellSurface = nullptr;

    /// The QQuickWindow hosting the shell's QML scene tree. Cached at
    /// create-time to avoid repeated `shellSurface->window()` calls in
    /// the hot per-show path.
    QQuickWindow* shellWindow = nullptr;

    /// The physical QScreen the shell was constructed against. Used by
    /// hot-plug rekey logic to confirm the same monitor underlies the
    /// new key.
    QScreen* physScreen = nullptr;

    /// Per-content slot entries keyed by slot name (e.g. "osd",
    /// "snapAssist", "layoutPicker", "zoneSelector", "mainOverlay").
    /// Each entry holds the slot's QQuickItem (borrowed; owned by the
    /// shell QQuickWindow's scene graph) plus the @c PhosphorLayer::Role
    /// the slot's SurfaceAnimator show/hide leg targets. Populated by
    /// the consumer's post-create callback and consumed by
    /// @c ShellHost::hideSlot to drive the animator without the consumer
    /// re-specifying the role at each call.
    QHash<QString, SlotEntry> slots;
};

} // namespace PhosphorOverlay
