// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Shared include for the generated wlr-layer-shell protocol header.
// The generated C header uses "namespace" as a parameter name in
// zwlr_layer_shell_v1_get_layer_surface(). This is a C identifier that
// collides with the C++ reserved word. The #define renames it to
// "namespace_" so the header compiles in C++ mode. This is the same
// workaround used by LayerShellQt and other C++ Wayland clients.
//
// Centralized here so every C++ TU that needs the protocol header
// includes it through one place — no duplicated #define/#undef blocks.
extern "C" {
#define namespace namespace_
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace
}
