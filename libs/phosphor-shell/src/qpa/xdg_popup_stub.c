// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Stub for xdg_popup_interface referenced by the wlr-layer-shell protocol
// code (for the get_popup request). PhosphorShell never uses layer shell
// popups, but the generated protocol code references this symbol.

#include <wayland-util.h>

// Minimal stub — only needs to exist for the linker. The generated
// wlr-layer-shell protocol code references this symbol for the
// get_popup request, but PhosphorShell never calls get_popup.
//
// Weak attribute prevents duplicate symbol errors if another linked library
// (e.g. Qt's own Wayland modules) also defines xdg_popup_interface.
// This stub is Linux/Wayland-only — MSVC/Windows is not a target platform.
//
// SAFETY: The NULL method/event arrays mean any runtime attempt to use this
// interface (e.g. via zwlr_layer_surface_v1_get_popup) will crash immediately.
// This is intentional — PhosphorShell never creates layer-shell popups, so
// reaching this code path indicates a logic error. If layer-shell popups are
// ever needed, replace this stub with a real xdg_popup_interface binding
// (from wayland-protocols' xdg-shell).
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
const struct wl_interface xdg_popup_interface = {
    "xdg_popup", 1, 0, NULL, 0, NULL,
};
