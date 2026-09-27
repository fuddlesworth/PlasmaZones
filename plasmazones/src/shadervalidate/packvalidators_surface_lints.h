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
/// Reads @p meta, @p eff, @p packDir AND THE FILESYSTEM. It writes to no stream, counts no
/// errors and mutates none of its arguments, and that is what makes the split a seam rather
/// than a cut — see the file comment. It is not otherwise pure, and two earlier versions of
/// this line understated how far from pure: the first called it pure outright, the second
/// listed only half the I/O. The full list is that it stats texture, preview, fragment,
/// vertex, BUFFER-SHADER and undeclared-sibling-surface.vert paths; canonicalises the pack
/// directory, the resolved builtin path AND every shared root; resolves builtin buffer tokens
/// through the registry, which probes QStandardPaths; and probes QStandardPaths directly of
/// its own accord through packSharedRoots. Its answer therefore depends on what is installed
/// on the machine, which is exactly what one of its own lints reports. Do not memoise or
/// reorder it on the strength of a purity claim.
///
/// @p eff must already have had its paths confined to the pack directory. The caller does
/// that before calling, and refuses the pack outright on an escape, so a path reaching a
/// lint here is one it is safe to have resolved.
QStringList surfaceMetadataLints(const QJsonObject& meta, const PhosphorSurfaceShaders::SurfaceShaderEffect& eff,
                                 const QString& packDir);

} // namespace PlasmaZones::ShaderValidate
