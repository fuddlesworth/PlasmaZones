// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// AnimationsPageController path-discovery helpers — small Q_INVOKABLE
// readers (`sectionForPath`, `eventLabel`, `parentChain`)
// that translate event-profile paths into UI taxonomy. Split out so
// animationspagecontroller.cpp stays under the project's 800-line cap.
// Same class, separate TU, no API change.

#include "animationspagecontroller.h"
#include "animations_controller_detail.h"

#include <PhosphorAnimation/ProfilePaths.h>

#include <QLatin1Char>
#include <QLatin1String>

namespace PlasmaZones {

QString AnimationsPageController::sectionForPath(const QString& path) const
{
    if (path.isEmpty())
        return {};
    const int dot = path.indexOf(QLatin1Char('.'));
    const QString topLevel = dot < 0 ? path : path.left(dot);

    // Merge osd.*, popup.*, and panel.* into the "overlays" UI section.
    if (topLevel == QLatin1String("osd") || topLevel == QLatin1String("popup") || topLevel == QLatin1String("panel"))
        return QStringLiteral("overlays");

    // Merge cursor.* into the "widget" UI section.
    if (topLevel == QLatin1String("cursor"))
        return QStringLiteral("widget");

    return topLevel;
}

QString AnimationsPageController::eventLabel(const QString& path) const
{
    if (path.isEmpty())
        return {};
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    const QString segment = dot < 0 ? path : path.mid(dot + 1);
    return animations_controller_detail::humanizeSegment(segment);
}

QStringList AnimationsPageController::parentChain(const QString& path) const
{
    QStringList chain;
    QString cur = path;
    while (!cur.isEmpty()) {
        chain.append(cur);
        cur = PhosphorAnimation::ProfilePaths::parentPath(cur);
    }
    return chain;
}

} // namespace PlasmaZones
