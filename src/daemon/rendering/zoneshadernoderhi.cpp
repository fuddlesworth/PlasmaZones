// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zoneshadernoderhi.h"

#include <PhosphorRendering/ShaderCompiler.h>

#include <QStandardPaths>

namespace PlasmaZones {

WarmShaderBakeResult warmShaderBakeCacheForPaths(const QString& vertexPath, const QString& fragmentPath,
                                                 const QStringList& includePaths)
{
    if (vertexPath.isEmpty() || fragmentPath.isEmpty()) {
        WarmShaderBakeResult result;
        result.errorMessage = QStringLiteral("Vertex or fragment path is empty");
        return result;
    }

    // Caller-provided include paths are authoritative when supplied (the daemon
    // hands us ShaderRegistry::searchPaths(), which exactly matches what the
    // render path uses). When omitted, fall back to the well-known system data
    // dirs — this avoids the previous heuristic of "parent of the shader's
    // parent dir", which silently broke for shaders not nested two levels
    // deep under a recognised root.
    QStringList paths = includePaths;
    if (paths.isEmpty()) {
        const QStringList systemShaderDirs =
            QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/shaders"),
                                      QStandardPaths::LocateDirectory);
        for (const QString& dir : systemShaderDirs) {
            if (!paths.contains(dir))
                paths.append(dir);
        }
    }

    return PhosphorRendering::warmShaderBakeCacheForPaths(vertexPath, fragmentPath, paths);
}

} // namespace PlasmaZones
