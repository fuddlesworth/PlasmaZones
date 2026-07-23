// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zoneshadernoderhi.h"

#include <PhosphorRendering/ShaderCompiler.h>

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QStandardPaths>

namespace PlasmaZones {

PhosphorRendering::WarmShaderBakeResult
warmShaderBakeCacheForPaths(const QString& vertexPath, const QString& fragmentPath, const QStringList& includePaths,
                            const QString& paramPreamble, const QString& entryPrologue,
                            const QList<PhosphorShaders::EntryCandidate>& entryCandidates)
{
    if (vertexPath.isEmpty() || fragmentPath.isEmpty()) {
        PhosphorRendering::WarmShaderBakeResult result;
        result.errorMessage = QStringLiteral("Vertex or fragment path is empty");
        return result;
    }

    const QStringList expandedPaths = expandShaderIncludePaths(includePaths);

    return PhosphorRendering::warmShaderBakeCacheForPaths(vertexPath, fragmentPath, expandedPaths, paramPreamble,
                                                          entryPrologue, entryCandidates);
}

} // namespace PlasmaZones
