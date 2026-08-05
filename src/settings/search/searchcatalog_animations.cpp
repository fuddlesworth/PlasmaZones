// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "searchcatalog_p.h"

#include "phosphor_i18n.h"

namespace PlasmaZones {

using SearchCatalogDetail::addSetting;

void seedAnimationEventAnchors(PhosphorControl::SearchController* search)
{
    // Animation events (reveal-tagged on the event list's outer delegates)
    // Windows (appearance) page under Transitions.
    addSetting(search, QStringLiteral("animations-windows"), QStringLiteral("window.appearance.open"),
               PhosphorI18n::tr("Opened"));
    addSetting(search, QStringLiteral("animations-windows"), QStringLiteral("window.appearance.close"),
               PhosphorI18n::tr("Closed"));
    addSetting(search, QStringLiteral("animations-windows"), QStringLiteral("window.appearance.minimize"),
               PhosphorI18n::tr("Minimized"));
    addSetting(search, QStringLiteral("animations-windows"), QStringLiteral("window.appearance.focus"),
               PhosphorI18n::tr("Focused"));
    // Window Dragging page under Motion — the held-drag leaf lives on its
    // own page (see AnimationsWindowDraggingPage.qml).
    addSetting(search, QStringLiteral("animations-window-dragging"), QStringLiteral("window.movement.move"),
               PhosphorI18n::tr("Dragged"));
    // Windows (movement) page under Motion.
    addSetting(search, QStringLiteral("animations-window-motion"), QStringLiteral("window.movement.maximize"),
               PhosphorI18n::tr("Maximized"));
    addSetting(search, QStringLiteral("animations-window-motion"), QStringLiteral("window.movement.snapIn"),
               PhosphorI18n::tr("Snapped Into Zone"));
    addSetting(search, QStringLiteral("animations-window-motion"), QStringLiteral("window.movement.snapOut"),
               PhosphorI18n::tr("Snapped Out of Zone"));
    addSetting(search, QStringLiteral("animations-window-motion"), QStringLiteral("window.movement.layoutSwitch"),
               PhosphorI18n::tr("Layout Switched"));
    // OSDs page.
    addSetting(search, QStringLiteral("animations-osds"), QStringLiteral("osd.show"), PhosphorI18n::tr("Shown"));
    addSetting(search, QStringLiteral("animations-osds"), QStringLiteral("osd.hide"), PhosphorI18n::tr("Hidden"));
    addSetting(search, QStringLiteral("animations-osds"), QStringLiteral("osd.pop"), PhosphorI18n::tr("Emphasized"));
    // Overlays page.
    addSetting(search, QStringLiteral("animations-overlays"), QStringLiteral("popup.zoneSelector.show"),
               PhosphorI18n::tr("Zone Selector Shown"));
    addSetting(search, QStringLiteral("animations-overlays"), QStringLiteral("popup.zoneSelector.hide"),
               PhosphorI18n::tr("Zone Selector Hidden"));
    addSetting(search, QStringLiteral("animations-overlays"), QStringLiteral("popup.layoutPicker.show"),
               PhosphorI18n::tr("Layout Picker Shown"));
    addSetting(search, QStringLiteral("animations-overlays"), QStringLiteral("popup.layoutPicker.hide"),
               PhosphorI18n::tr("Layout Picker Hidden"));
    addSetting(search, QStringLiteral("animations-overlays"), QStringLiteral("popup.snapAssist.show"),
               PhosphorI18n::tr("Snap Assist Shown"));
    addSetting(search, QStringLiteral("animations-overlays"), QStringLiteral("popup.snapAssist.hide"),
               PhosphorI18n::tr("Snap Assist Hidden"));
    addSetting(search, QStringLiteral("animations-overlays"), QStringLiteral("popup.cheatsheet.show"),
               PhosphorI18n::tr("Shortcut Cheatsheet Shown"));
    addSetting(search, QStringLiteral("animations-overlays"), QStringLiteral("popup.cheatsheet.hide"),
               PhosphorI18n::tr("Shortcut Cheatsheet Hidden"));
    // Desktop page.
    addSetting(search, QStringLiteral("animations-desktops"), QStringLiteral("desktop.switch"),
               PhosphorI18n::tr("Desktop Switched"));
    // Keywords on this one row, unlike its siblings: the label shares no token
    // with what users actually type for it. "show desktop" is the name of the
    // KWin action, and "peek" is what the settings page calls the state, but
    // neither appears in "Peeked at Desktop" as a searchable stem. The page
    // carries the same keywords, so without these the terms land on the page
    // rather than deep-linking to the row.
    addSetting(search, QStringLiteral("animations-desktops"), QStringLiteral("desktop.peek"),
               PhosphorI18n::tr("Peeked at Desktop"), {PhosphorI18n::tr("peek"), PhosphorI18n::tr("show desktop")});
    addSetting(search, QStringLiteral("animations-side-panels"), QStringLiteral("panel.slideIn"),
               PhosphorI18n::tr("Slide In"));
    addSetting(search, QStringLiteral("animations-side-panels"), QStringLiteral("panel.slideOut"),
               PhosphorI18n::tr("Slide Out"));
    addSetting(search, QStringLiteral("animations-side-panels"), QStringLiteral("panel.fadeIn"),
               PhosphorI18n::tr("Fade In"));
    addSetting(search, QStringLiteral("animations-side-panels"), QStringLiteral("panel.fadeOut"),
               PhosphorI18n::tr("Fade Out"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.hover"), PhosphorI18n::tr("Hover"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.press"), PhosphorI18n::tr("Press"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.toggleOn"),
               PhosphorI18n::tr("Toggle On"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.toggleOff"),
               PhosphorI18n::tr("Toggle Off"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.badgeShow"),
               PhosphorI18n::tr("Show (badge)"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.badgeHide"),
               PhosphorI18n::tr("Hide (badge)"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.badgePulse"),
               PhosphorI18n::tr("Pulse (badge)"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.tint"), PhosphorI18n::tr("Tint"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.dim"), PhosphorI18n::tr("Dim"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.fadeIn"),
               PhosphorI18n::tr("Fade In"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.fadeOut"),
               PhosphorI18n::tr("Fade Out"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.reorder"),
               PhosphorI18n::tr("Reorder"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.accordionExpand"),
               PhosphorI18n::tr("Expand (accordion)"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.accordionCollapse"),
               PhosphorI18n::tr("Collapse (accordion)"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.progress"),
               PhosphorI18n::tr("Progress"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.zoneHighlight"),
               PhosphorI18n::tr("Zone Highlight"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.zoneHighlight.pop"),
               PhosphorI18n::tr("Zone Highlight Pop"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.zoneHighlight.border"),
               PhosphorI18n::tr("Zone Highlight Border"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("widget.zoneOverlayFlash"),
               PhosphorI18n::tr("Zone Overlay Layout-Switch Flash"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("cursor.hover"),
               PhosphorI18n::tr("Cursor Hover"));
    addSetting(search, QStringLiteral("animations-widgets"), QStringLiteral("cursor.click"),
               PhosphorI18n::tr("Cursor Click"));
    addSetting(search, QStringLiteral("animations-editor"), QStringLiteral("editor.snapIn"),
               PhosphorI18n::tr("Snap Into Zone (Fill Preview)"));
    addSetting(search, QStringLiteral("animations-editor"), QStringLiteral("editor.snapOut"),
               PhosphorI18n::tr("Snap Out of Zone"));
    addSetting(search, QStringLiteral("animations-editor"), QStringLiteral("editor.snapResize"),
               PhosphorI18n::tr("Snap Resize (Drag Preview)"));
}

} // namespace PlasmaZones
