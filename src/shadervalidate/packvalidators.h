// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The three per-authoring-model pack validators for plasmazones-shader-validate.
// Each reproduces the matching runtime's GLSL assembly (entry scaffold +
// generated p_<id> preamble + include expansion) and bakes every stage through
// headless glslang. main.cpp owns argument parsing and dispatch; these do the
// per-pack work. Each returns the number of errors found for one pack dir.

#pragma once

class QString;
class QTextStream;

namespace PlasmaZones::ShaderValidate {

// zone/overlay packs (data/overlays/*): ShaderRegistry metadata + the zone entry
// scaffold (pZone/pImage); validates frag, multipass buffers, and vertex stage.
int validatePack(const QString& packDir, QTextStream& out);

// animation/transition packs (data/animations/*): AnimationShaderEffect + the
// animation entry scaffold (pTransition / pIn+pOut) + paramPreamble.
int validateAnimationPack(const QString& packDir, QTextStream& out);

// surface/decoration packs (data/surface/*): SurfaceShaderEffect + paramPreamble;
// validates effect.frag, buffer passes, and the shared vertex stage.
int validateSurfacePack(const QString& packDir, QTextStream& out);

} // namespace PlasmaZones::ShaderValidate
