// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zoneshadernoderhi.h"

#include <PhosphorRendering/ShaderCompiler.h>

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

inline void appendUniquePath(QStringList& paths, const QString& path)
{
    if (!path.isEmpty() && QDir(path).exists() && !paths.contains(path)) {
        paths.append(path);
    }
}

inline QStringList trustedShaderRoots()
{
    return QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/shaders"),
                                     QStandardPaths::LocateDirectory);
}

inline QStringList expandShaderIncludePaths(const QStringList& inputPaths)
{
    QStringList expanded;

    // Trusted PlasmaZones shader roots first. This ensures shared/common.glsl
    // resolves consistently for bundled, dev-prefix, system, and user shader roots.
    for (const QString& root : trustedShaderRoots()) {
        appendUniquePath(expanded, root + QStringLiteral("/shared"));
        appendUniquePath(expanded, root);
    }

    // Preserve caller-provided include paths, but do not infer arbitrary parent
    // directories from them.
    for (const QString& path : inputPaths) {
        const QFileInfo info(path);
        const QString dir = info.isFile() ? info.absolutePath() : info.absoluteFilePath();

        appendUniquePath(expanded, dir + QStringLiteral("/shared"));
        appendUniquePath(expanded, dir);
    }

    return expanded;
}

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
