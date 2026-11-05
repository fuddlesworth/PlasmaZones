// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/ShaderPackPaths.h>

#include <QLoggingCategory>

namespace PhosphorShaders {

QString resolveWithinPack(const QDir& packDir, const QString& declaredName, PhosphorFsLoader::AbsolutePathPolicy policy,
                          const QLoggingCategory& log)
{
    // An empty declared name is ABSENT, not an escape. `toString(default)` hands
    // back an explicit `""` rather than the default (an empty string IS a
    // string), so without this the guard refused it and warned "declared a path
    // outside its own directory: ''", which points the pack author at the wrong
    // problem.
    if (declaredName.isEmpty()) {
        return {};
    }
    const auto resolved = PhosphorFsLoader::resolveWithinDirectory(declaredName, packDir.absolutePath(), policy);
    if (!resolved) {
        qCWarning(log) << "Shader pack declared a path outside its own directory:" << declaredName << "in"
                       << packDir.absolutePath() << "— ignoring";
        return {};
    }
    return *resolved;
}

} // namespace PhosphorShaders
