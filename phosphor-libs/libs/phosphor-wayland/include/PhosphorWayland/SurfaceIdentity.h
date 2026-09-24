// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorwayland_export.h>

#include <QtGlobal>

class QWindow;

namespace PhosphorWayland {

/// The Wayland protocol object id of @p window's @c wl_surface, or 0 when the
/// window has no realised Wayland surface (not yet shown, or a non-Wayland
/// platform).
///
/// The id is the one thing about a surface that BOTH sides of the protocol can
/// name: a client's `wl_proxy_get_id` and a compositor's `wl_resource_get_id`
/// return the same number for the same object. That makes it the handle a
/// client uses to tell a compositor-side component WHICH of its surfaces it is
/// talking about — nothing else about a layer surface (its class, its title,
/// its role, its geometry) survives the trip in a form a compositor exposes
/// per surface.
///
/// The id is unique only among a client's LIVE objects; Wayland reuses ids
/// after an object is destroyed. A holder of one must therefore drop it when
/// the surface goes, not keep it as a permanent name.
[[nodiscard]] PHOSPHORWAYLAND_EXPORT quint32 surfaceObjectId(QWindow* window);

} // namespace PhosphorWayland
