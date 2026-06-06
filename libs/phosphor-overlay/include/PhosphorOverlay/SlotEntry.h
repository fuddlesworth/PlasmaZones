// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// A single content slot living on a per-screen shell. Holds the slot's
// QQuickItem (looked up by the consumer at shell-create time via the
// post-create callback) and the PhosphorLayer::Role the slot's
// SurfaceAnimator show/hide leg targets.
//
// The role is per-slot because every consumer's slots have a stable
// animator scope - Phosphor's OSD slot always animates as PhosphorRoles::Osd, the
// zone-selector slot as PhosphorRoles::ZoneSelector, and so on. Pinning the
// role into the slot entry lets the library drive
// SurfaceAnimator::beginShow / beginHide without the consumer
// re-specifying the role at every show/hide call site.

#include <PhosphorOverlay/phosphoroverlay_export.h>

#include <PhosphorLayer/Role.h>

#include <QPointer>

class QQuickItem;

namespace PhosphorOverlay {

struct PHOSPHOROVERLAY_EXPORT SlotEntry
{
    /// The slot's QQuickItem. Borrowed - owned by the shell window's
    /// scene graph; QPointer auto-clears if the underlying Item is
    /// destroyed (typically: shell torn down).
    QPointer<QQuickItem> item;

    /// The PhosphorLayer::Role identifying this slot's animator scope.
    /// Used as the `role` argument to
    /// PhosphorAnimationLayer::SurfaceAnimator::beginShow / beginHide.
    PhosphorLayer::Role role;
};

} // namespace PhosphorOverlay
