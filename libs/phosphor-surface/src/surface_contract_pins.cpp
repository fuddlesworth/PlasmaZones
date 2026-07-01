// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Compile-time pins for the SURFACE shader UBO contract. Including the header
// in a compiled TU forces its std140 sizeof/offsetof static_asserts to fire as
// part of the library build, so the C++ SurfaceUniforms mirror can never drift
// from the daemon UBO branch of data/surface/shared/surface_uniforms.glsl
// without failing the build — even before the daemon RHI runtime (which also
// includes it) lands. Mirrors phosphor-animation/src/contract_pins.cpp.

#include <PhosphorSurface/SurfaceShaderUniforms.h>
