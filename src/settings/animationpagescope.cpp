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
        return {AnimationPageScope::EventSubtree, it->first, it->second, false};
    // The condensed simple page spans several roots AND hosts the global
    // timing / window-filter cards, so it scopes to the union of the event
    // roots its cards bind plus the General keys. It must NOT fall through to
    // WholeTree: that would make a Reset here clear every override across
    // osd/popup/panel/widget/editor as well, none of which this page shows.
    // Keep the roots in lockstep with AnimationsSimplePage.qml's eventModel.
    // window.movement covers window.movement.move as a descendant, and the
    // page shows both, so there is no carve-out here.
    if (page == QLatin1String("animations-simple")) {
        return {AnimationPageScope::EventSubtree,
                {QStringLiteral("window.appearance.open"), QStringLiteral("window.appearance.close"),
                 QStringLiteral("window.appearance.minimize"), QStringLiteral("window.movement"),
                 QStringLiteral("desktop.switch")},
                {},
                /*includeGeneralKeys=*/true};
    }
    // animations-presets / animations-motionsets / animations-shaders: library
    // leaves that genuinely act on the whole editable tree.
    return {AnimationPageScope::WholeTree, {}, {}, false};
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
    // Memoised per scope. isPageDirty is a hot path and a collapsed-parent
    // query recurses over every animation leaf, each of which would otherwise
    // re-scan the whole built-in path list. Safe to cache because the inputs
    // are both immutable at runtime: allBuiltInPaths() is a compile-time list
    // (itself a function-local static), and a scope's include/exclude roots
    // are fixed per page id by animationPageScope. Keyed on the roots rather
    // than the page id so callers that build a scope by hand still hit it.
    //
    // GUI-THREAD ONLY. Static initialisation is thread-safe but the QHash
    // insert below is not, and every caller today is on the GUI thread. Anyone
    // calling this off-thread must add a lock rather than assume it is safe.
    static QHash<QString, QStringList> cache;
    const QString key =
        scope.include.join(QLatin1Char('\x1f')) + QLatin1Char('\x1e') + scope.exclude.join(QLatin1Char('\x1f'));
    const auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();

    QStringList out;
    for (const QString& path : PhosphorAnimation::ProfilePaths::allBuiltInPaths()) {
        if (animationPathInScope(path, scope))
            out.append(path);
    }
    cache.insert(key, out);
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
