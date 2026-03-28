// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stub for xdg_popup_interface referenced by the wlr-layer-shell protocol
// code (for the get_popup request). PlasmaZones never uses layer shell
// popups, but the generated protocol code references this symbol.

#include <wayland-util.h>

// Minimal stub — only needs to exist for the linker.
// If we ever need layer-shell popups, we'd generate the full xdg-shell
// protocol code instead.
const struct wl_interface xdg_popup_interface = {
    "xdg_popup", 1, 0, NULL, 0, NULL,
};
