// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWayland/ToplevelDrag.h>
#include "qpa/layershellintegration.h"
#include "qpa/xdg_toplevel_drag_protocol.h"

namespace PhosphorWayland {

bool isToplevelDragSupported()
{
    auto* integration = LayerShellIntegration::instance();
    return integration && integration->toplevelDragManager();
}

} // namespace PhosphorWayland
