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

#include <PhosphorOverlay/phosphoroverlay_export.h>

#include <QHash>
#include <QPointer>
#include <QString>

class QQuickItem;
class QQuickWindow;
class QScreen;

namespace PhosphorLayer {
class Surface;
}

namespace PhosphorOverlay {

struct PHOSPHOROVERLAY_EXPORT ShellState
{
    /// The layer-shell wl_surface backing this screen's overlay shell.
    /// Owned by the SurfaceManager; ShellHost holds a borrowed pointer.
    /// nullptr means the shell has not been created yet (or was torn
    /// down and not yet re-created).
    PhosphorLayer::Surface* shellSurface = nullptr;

    /// The QQuickWindow hosting the shell's QML scene tree. Cached at
    /// create-time to avoid repeated `shellSurface->window()` calls in
    /// the hot per-show path.
    QQuickWindow* shellWindow = nullptr;

    /// The physical QScreen the shell was constructed against. Used by
    /// hot-plug rekey logic to confirm the same monitor underlies the
    /// new key.
    QScreen* physScreen = nullptr;

    /// Slot Items keyed by slot name (e.g. "osd", "snapAssist",
    /// "layoutPicker", "zoneSelector", "mainOverlay"). Populated by the
    /// post-create callback. Borrowed — owned by the shell QQuickWindow's
    /// scene graph, torn down implicitly when the shell surface is
    /// deleted.
    QHash<QString, QPointer<QQuickItem>> slots;
};

} // namespace PhosphorOverlay
