// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/// @file PhosphorWayland.h
/// Convenience umbrella header — includes all public PhosphorWayland headers.
///
/// Shader-domain headers (BaseUniforms, IUniformExtension,
/// ShaderIncludeResolver, IWallpaperProvider, ShaderRegistry) live in
/// the sibling `phosphor-shaders` library; `#include <PhosphorShaders/...>`
/// to reach them. What remains here is the Wayland layer-shell integration.

#pragma once

#include <PhosphorWayland/IdleNotifier>
#include <PhosphorWayland/LayerSurface>
#include <PhosphorWayland/SinglePixelBuffer>
#include <PhosphorWayland/ToplevelDrag>
