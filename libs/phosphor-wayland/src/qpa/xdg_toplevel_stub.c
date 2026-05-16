// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <wayland-util.h>

// Minimal stub — the generated xdg-toplevel-drag protocol code references
// xdg_toplevel_interface for the attach request, but PhosphorWayland never
// calls attach (capability query only). See xdg_popup_stub.c for the same
// pattern.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
const struct wl_interface xdg_toplevel_interface = {
    "xdg_toplevel", 1, 0, NULL, 0, NULL,
};
