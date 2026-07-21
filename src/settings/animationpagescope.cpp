// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "animationpagescope.h"

#include <PhosphorAnimation/ProfilePaths.h>

#include <QHash>
#include <QLatin1Char>
#include <QPair>
#include <QSet>

namespace PlasmaZones {

AnimationPageScope animationPageScope(const QString& page)
{
    if (page == QLatin1String("animations-general"))
        return {AnimationPageScope::ConfigOnly, {}, {}};
    // Roots mirror the surfacePath prefixes the QML pages bind and the
    // ProfilePaths taxonomy. Keep in lockstep with the *Page.qml event lists.
    static const QHash<QString, QPair<QStringList, QStringList>> kEventRoots{
        {QStringLiteral("animations-windows"), {{QStringLiteral("window.appearance")}, {}}},
        {QStringLiteral("animations-window-motion"),
         {{QStringLiteral("window.movement")}, {QStringLiteral("window.movement.move")}}},
        {QStringLiteral("animations-window-dragging"), {{QStringLiteral("window.movement.move")}, {}}},
        {QStringLiteral("animations-osds"), {{QStringLiteral("osd")}, {}}},
        {QStringLiteral("animations-overlays"), {{QStringLiteral("popup")}, {}}},
        {QStringLiteral("animations-desktops"), {{QStringLiteral("desktop")}, {}}},
        {QStringLiteral("animations-side-panels"), {{QStringLiteral("panel")}, {}}},
        {QStringLiteral("animations-widgets"), {{QStringLiteral("widget")}, {}}},
        {QStringLiteral("animations-editor"), {{QStringLiteral("editor")}, {}}},
    };
    const auto it = kEventRoots.constFind(page);
    if (it != kEventRoots.cend())
        return {AnimationPageScope::EventSubtree, it->first, it->second};
    // animations-presets / animations-motionsets / animations-shaders, and
    // the condensed animations-simple page (it spans several roots plus the
    // General keys, so no single subtree fits).
    return {AnimationPageScope::WholeTree, {}, {}};
}

bool animationPathUnderAny(const QString& path, const QStringList& roots)
{
    for (const QString& root : roots) {
        if (path == root || path.startsWith(root + QLatin1Char('.')))
            return true;
    }
    return false;
}

bool animationPathInScope(const QString& path, const AnimationPageScope& scope)
{
    return animationPathUnderAny(path, scope.include) && !animationPathUnderAny(path, scope.exclude);
}

QStringList animationScopedBuiltInPaths(const AnimationPageScope& scope)
{
    QStringList out;
    for (const QString& path : PhosphorAnimation::ProfilePaths::allBuiltInPaths()) {
        if (animationPathInScope(path, scope))
            out.append(path);
    }
    return out;
}

bool shaderTreeScopeDiffers(const PhosphorAnimationShaders::ShaderProfileTree& current,
                            const PhosphorAnimationShaders::ShaderProfileTree& baseline,
                            const AnimationPageScope& scope)
{
    // Walks the union of both trees' overridden paths (so an override present
    // in one and absent in the other counts) filtered to the scope — this also
    // catches plugin-added paths that allBuiltInPaths() would miss.
    QSet<QString> paths;
    for (const QString& p : current.overriddenPaths())
        if (animationPathInScope(p, scope))
            paths.insert(p);
    for (const QString& p : baseline.overriddenPaths())
        if (animationPathInScope(p, scope))
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
