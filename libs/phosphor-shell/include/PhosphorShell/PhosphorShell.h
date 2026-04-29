// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/// @file PhosphorShell.h
/// Convenience umbrella header — includes all public PhosphorShell headers.
///
/// Phase B1 of docs/phosphor-architecture-refactor-plan.md migrated the
/// shader-domain headers (BaseUniforms, IUniformExtension,
/// ShaderIncludeResolver, IWallpaperProvider, ShaderRegistry →
/// ShaderPackRegistry) to the sibling `phosphor-shaders` library.
/// Consumers wanting those types should `#include
/// <PhosphorShaders/...>` directly. What remains here is the Wayland
/// layer-shell integration.

#pragma once

#include <PhosphorShell/LayerSurface>
