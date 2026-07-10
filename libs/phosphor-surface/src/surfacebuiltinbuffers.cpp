// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStandardPaths>

namespace PhosphorSurfaceShaders {

bool SurfaceShaderRegistry::isBuiltinBufferShader(const QString& token)
{
    return token.startsWith(QLatin1String("builtin:"));
}

QString SurfaceShaderRegistry::resolveBuiltinBufferShader(const QString& token, const QString& effectDir)
{
    // Fixed token→file whitelist. The mapped names are registry-owned
    // constants, never author input, so resolving them against the shared
    // dirs does not reopen the pack-dir traversal guard this function
    // bypasses.
    static const QHash<QString, QString> kBuiltinBufferShaders = {
        {QStringLiteral("builtin:gaussian-h"), QStringLiteral("gaussian_h.frag")},
        {QStringLiteral("builtin:gaussian-v"), QStringLiteral("gaussian_v.frag")},
    };
    const QString fileName = kBuiltinBufferShaders.value(token);
    if (fileName.isEmpty()) {
        return QString();
    }
    // System packs live at <root>/<pack> with the shared dir as a sibling of
    // the pack dir; probe there first so a development tree (or a custom
    // search path) resolves against its own shared dir rather than an
    // installed copy.
    if (!effectDir.isEmpty()) {
        const QString sibling = QDir(effectDir).absoluteFilePath(QStringLiteral("../shared/") + fileName);
        if (QFile::exists(sibling)) {
            return QFileInfo(sibling).canonicalFilePath();
        }
    }
    // User packs (~/.local/share/plasmazones/surface/<pack>) have no shared
    // sibling; fall back to the installed data dirs, mirroring how
    // SurfaceShaderItem::surfaceIncludePaths() locates the shared includes.
    // Canonicalize like the sibling branch so both paths return the same
    // form (symlinks resolved).
    const QString located = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                   QStringLiteral("plasmazones/surface/shared/") + fileName);
    return located.isEmpty() ? QString() : QFileInfo(located).canonicalFilePath();
}

} // namespace PhosphorSurfaceShaders
