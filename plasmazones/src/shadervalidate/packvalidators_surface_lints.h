// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The surface arm's metadata-lint collector, split out of packvalidator_surface.cpp when
// that file reached the 1150-line ceiling. Its own header rather than an entry in
// packvalidators.h, because that header declares the four per-model ENTRY POINTS that
// main.cpp dispatches to and this is an internal helper of one of them.

#pragma once

#include <QStringList>

class QJsonObject;
class QString;

namespace PhosphorSurfaceShaders {
class SurfaceShaderEffect;
}

namespace PlasmaZones::ShaderValidate {

/// Everything wrong with a surface pack's metadata.json that is decidable WITHOUT
/// compiling a stage, as one human-readable line per problem.
///
/// Pure: reads @p meta, @p eff and @p packDir, writes to no stream, counts no errors and
/// mutates nothing. The caller flushes the list and turns each entry into one error. That
/// purity is what makes the split a seam rather than a cut — see the file comment.
///
/// @p eff must already have had its paths confined to the pack directory. The caller does
/// that before calling, and refuses the pack outright on an escape, so a path reaching a
/// lint here is one it is safe to have resolved.
QStringList surfaceMetadataLints(const QJsonObject& meta, const PhosphorSurfaceShaders::SurfaceShaderEffect& eff,
                                 const QString& packDir);

} // namespace PlasmaZones::ShaderValidate
