// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Compile-time pins for the POINTER shader UBO contract. Including the header
// in a compiled TU forces its std140 sizeof/offsetof static_asserts to fire as
// part of the library build, so the C++ PointerUniformsTail mirror can never
// drift from the UBO branch of data/pointer/shared/pointer_uniforms.glsl
// without failing the build. Mirrors phosphor-surface/src/surface_contract_pins.cpp.

#include <PhosphorPointer/PointerShaderUniforms.h>
