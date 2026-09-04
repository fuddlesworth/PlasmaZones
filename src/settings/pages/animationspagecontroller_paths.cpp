// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// AnimationsPageController path-discovery helpers — small Q_INVOKABLE readers
// (`eventPathAcceptsWindowRules`, `sectionForPath`, `eventLabel`,
// `parentChain`) and the translated segment-label table (`segmentLabel`) that
// turn event-profile paths into UI taxonomy. Same class as
// animationspagecontroller.cpp, separate TU.

#include "animationspagecontroller.h"
#include "animations_controller_detail.h"

#include "phosphor_i18n.h"

#include <PhosphorAnimation/ProfilePaths.h>

#include <QLatin1Char>
#include <QLatin1String>

namespace PlasmaZones {

bool AnimationsPageController::eventPathAcceptsWindowRules(const QString& path) const
{
    return PhosphorAnimation::ProfilePaths::eventPathResolvesPerWindow(path);
}

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

QString AnimationsPageController::segmentLabel(const QString& segment)
{
    // Every segment of the built-in taxonomy (PhosphorAnimation::ProfilePaths)
    // plus the merged "overlays" UI section, translated. The English source is
    // the title-cased form humanizeSegment would produce, so the two agree for
    // a translator-less run; a segment outside this table (a plugin-added path)
    // falls back to that mechanical form rather than to the raw token.
    //
    // PATH SEGMENTS ONLY. This table once also carried the event-CLASS tokens
    // (fade, geometry, strip, tab, window-morph), which no caller can reach:
    // the two are eventLabel (a path's last segment) and eventSections (a
    // section key from sectionForPath), and neither ever yields a class token.
    // They were shipped to translators as strings that render nowhere. The
    // class tokens the user does see come from ShaderBrowserPage's own
    // _typeCatalog, which spells them differently on purpose.
    if (segment == QLatin1String("accordionCollapse")) {
        return PhosphorI18n::tr("Accordion Collapse", "animation event or section");
    }
    if (segment == QLatin1String("accordionExpand")) {
        return PhosphorI18n::tr("Accordion Expand", "animation event or section");
    }
    if (segment == QLatin1String("appearance")) {
        return PhosphorI18n::tr("Appearance", "animation event or section");
    }
    if (segment == QLatin1String("appletPopup")) {
        return PhosphorI18n::tr("Applet Popup", "animation event or section");
    }
    if (segment == QLatin1String("badgeHide")) {
        return PhosphorI18n::tr("Badge Hide", "animation event or section");
    }
    if (segment == QLatin1String("badgePulse")) {
        return PhosphorI18n::tr("Badge Pulse", "animation event or section");
    }
    if (segment == QLatin1String("badgeShow")) {
        return PhosphorI18n::tr("Badge Show", "animation event or section");
    }
    if (segment == QLatin1String("border")) {
        return PhosphorI18n::tr("Border", "animation event or section");
    }
    if (segment == QLatin1String("cheatsheet")) {
        return PhosphorI18n::tr("Cheatsheet", "animation event or section");
    }
    if (segment == QLatin1String("click")) {
        return PhosphorI18n::tr("Click", "animation event or section");
    }
    if (segment == QLatin1String("close")) {
        return PhosphorI18n::tr("Close", "animation event or section");
    }
    if (segment == QLatin1String("cursor")) {
        return PhosphorI18n::tr("Cursor", "animation event or section");
    }
    if (segment == QLatin1String("desktop")) {
        return PhosphorI18n::tr("Desktop", "animation event or section");
    }
    if (segment == QLatin1String("dim")) {
        return PhosphorI18n::tr("Dim", "animation event or section");
    }
    if (segment == QLatin1String("editor")) {
        return PhosphorI18n::tr("Editor", "animation event or section");
    }
    if (segment == QLatin1String("fadeIn")) {
        return PhosphorI18n::tr("Fade In", "animation event or section");
    }
    if (segment == QLatin1String("fadeOut")) {
        return PhosphorI18n::tr("Fade Out", "animation event or section");
    }
    if (segment == QLatin1String("fast")) {
        return PhosphorI18n::tr("Fast", "animation event or section");
    }
    if (segment == QLatin1String("focus")) {
        return PhosphorI18n::tr("Focus", "animation event or section");
    }
    if (segment == QLatin1String("global")) {
        return PhosphorI18n::tr("Global", "animation event or section");
    }
    if (segment == QLatin1String("hide")) {
        return PhosphorI18n::tr("Hide", "animation event or section");
    }
    if (segment == QLatin1String("hover")) {
        return PhosphorI18n::tr("Hover", "animation event or section");
    }
    if (segment == QLatin1String("layoutPicker")) {
        return PhosphorI18n::tr("Layout Picker", "animation event or section");
    }
    if (segment == QLatin1String("layoutSwitch")) {
        return PhosphorI18n::tr("Layout Switch", "animation event or section");
    }
    if (segment == QLatin1String("maximize")) {
        return PhosphorI18n::tr("Maximize", "animation event or section");
    }
    if (segment == QLatin1String("minimize")) {
        return PhosphorI18n::tr("Minimize", "animation event or section");
    }
    if (segment == QLatin1String("move")) {
        return PhosphorI18n::tr("Move", "animation event or section");
    }
    if (segment == QLatin1String("movement")) {
        return PhosphorI18n::tr("Movement", "animation event or section");
    }
    if (segment == QLatin1String("open")) {
        return PhosphorI18n::tr("Open", "animation event or section");
    }
    if (segment == QLatin1String("osd")) {
        return PhosphorI18n::tr("OSD", "animation event or section");
    }
    if (segment == QLatin1String("overlays")) {
        return PhosphorI18n::tr("Overlays", "animation event or section");
    }
    if (segment == QLatin1String("panel")) {
        return PhosphorI18n::tr("Panel", "animation event or section");
    }
    if (segment == QLatin1String("peek")) {
        return PhosphorI18n::tr("Peek", "animation event or section");
    }
    if (segment == QLatin1String("pop")) {
        return PhosphorI18n::tr("Pop", "animation event or section");
    }
    if (segment == QLatin1String("popup")) {
        return PhosphorI18n::tr("Popup", "animation event or section");
    }
    if (segment == QLatin1String("press")) {
        return PhosphorI18n::tr("Press", "animation event or section");
    }
    if (segment == QLatin1String("progress")) {
        return PhosphorI18n::tr("Progress", "animation event or section");
    }
    if (segment == QLatin1String("pulse")) {
        return PhosphorI18n::tr("Pulse", "animation event or section");
    }
    if (segment == QLatin1String("reorder")) {
        return PhosphorI18n::tr("Reorder", "animation event or section");
    }
    if (segment == QLatin1String("scrolling")) {
        return PhosphorI18n::tr("Scrolling", "animation event or section");
    }
    if (segment == QLatin1String("shell")) {
        return PhosphorI18n::tr("Shell", "animation event or section");
    }
    if (segment == QLatin1String("show")) {
        return PhosphorI18n::tr("Show", "animation event or section");
    }
    if (segment == QLatin1String("slideIn")) {
        return PhosphorI18n::tr("Slide In", "animation event or section");
    }
    if (segment == QLatin1String("slideOut")) {
        return PhosphorI18n::tr("Slide Out", "animation event or section");
    }
    if (segment == QLatin1String("slow")) {
        return PhosphorI18n::tr("Slow", "animation event or section");
    }
    if (segment == QLatin1String("placeIn")) {
        return PhosphorI18n::tr("Placed", "animation event or section");
    }
    if (segment == QLatin1String("placeOut")) {
        return PhosphorI18n::tr("Released", "animation event or section");
    }
    if (segment == QLatin1String("snapAssist")) {
        return PhosphorI18n::tr("Snap Assist", "animation event or section");
    }
    if (segment == QLatin1String("snapIn")) {
        return PhosphorI18n::tr("Snap In", "animation event or section");
    }
    if (segment == QLatin1String("snapOut")) {
        return PhosphorI18n::tr("Snap Out", "animation event or section");
    }
    if (segment == QLatin1String("snapResize")) {
        return PhosphorI18n::tr("Snap Resize", "animation event or section");
    }
    if (segment == QLatin1String("switch")) {
        return PhosphorI18n::tr("Switch", "animation event or section");
    }
    if (segment == QLatin1String("tabSwitch")) {
        return PhosphorI18n::tr("Tab Switch", "animation event or section");
    }
    if (segment == QLatin1String("tint")) {
        return PhosphorI18n::tr("Tint", "animation event or section");
    }
    if (segment == QLatin1String("toggleOff")) {
        return PhosphorI18n::tr("Toggle Off", "animation event or section");
    }
    if (segment == QLatin1String("toggleOn")) {
        return PhosphorI18n::tr("Toggle On", "animation event or section");
    }
    if (segment == QLatin1String("view")) {
        return PhosphorI18n::tr("View", "animation event or section");
    }
    if (segment == QLatin1String("widget")) {
        return PhosphorI18n::tr("Widget", "animation event or section");
    }
    if (segment == QLatin1String("window")) {
        return PhosphorI18n::tr("Window", "animation event or section");
    }
    if (segment == QLatin1String("zoneHighlight")) {
        return PhosphorI18n::tr("Zone Highlight", "animation event or section");
    }
    if (segment == QLatin1String("zoneOverlayFlash")) {
        return PhosphorI18n::tr("Zone Overlay Flash", "animation event or section");
    }
    if (segment == QLatin1String("zoneSelector")) {
        return PhosphorI18n::tr("Zone Selector", "animation event or section");
    }
    return animations_controller_detail::humanizeSegment(segment);
}

QString AnimationsPageController::eventLabel(const QString& path) const
{
    if (path.isEmpty())
        return {};
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    const QString segment = dot < 0 ? path : path.mid(dot + 1);
    return segmentLabel(segment);
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
