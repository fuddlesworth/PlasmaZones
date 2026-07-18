// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "decorationpagescope.h"

#include <QHash>
#include <QLatin1Char>
#include <QSet>

namespace PlasmaZones {

QString decorationSurfaceRoot(const QString& page)
{
    static const QHash<QString, QString> roots{
        {QStringLiteral("decorations-windows"), QStringLiteral("window")},
        {QStringLiteral("decorations-osds"), QStringLiteral("osd")},
        {QStringLiteral("decorations-popups"), QStringLiteral("popup")},
    };
    return roots.value(page);
}

bool decorationPathInRoot(const QString& path, const QString& root)
{
    return path == root || path.startsWith(root + QLatin1Char('.'));
}

bool decorationRootDiffers(const PhosphorSurfaceShaders::DecorationProfileTree& current,
                           const PhosphorSurfaceShaders::DecorationProfileTree& baseline, const QString& root)
{
    QSet<QString> paths;
    for (const QString& p : current.overriddenPaths())
        if (decorationPathInRoot(p, root))
            paths.insert(p);
    for (const QString& p : baseline.overriddenPaths())
        if (decorationPathInRoot(p, root))
            paths.insert(p);
    for (const QString& p : paths) {
        if (current.hasOverride(p) != baseline.hasOverride(p))
            return true;
        if (current.directOverride(p) != baseline.directOverride(p))
            return true;
    }
    return false;
}

} // namespace PlasmaZones
