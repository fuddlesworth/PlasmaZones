// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Forwarder. Zone-shader UBO layout types now live in PhosphorRendering so
// libraries and tools (tools/shader-render) can share them without reaching
// into the daemon tree. This header is kept so existing PlasmaZones::* call
// sites continue to compile.

#pragma once

#include <PhosphorRendering/ZoneShaderCommon.h>

namespace PlasmaZones {

using PhosphorRendering::MaxZones;
using PhosphorRendering::ZoneColor;
using PhosphorRendering::ZoneData;
using PhosphorRendering::ZoneDataSnapshot;
using PhosphorRendering::ZoneRect;
using PhosphorRendering::ZoneShaderUniforms;
using PhosphorShell::kShaderTimeWrap;

} // namespace PlasmaZones
