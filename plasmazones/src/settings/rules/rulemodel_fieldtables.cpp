// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The static label tables of RuleModel: the rule-section names and the
// match-field names. Pure enum-to-prose switches with no dependency on the
// label-composition helpers, which is why they split cleanly out of
// rulemodel_labels.cpp when that file reached its size ceiling. The per-leaf
// and per-action label composition, the two summaries built from it, and the
// unknown-action-type fallback all stay there, next to the helpers they use.
//
// Same class as rulemodel.cpp, separate translation unit, no API change.

#include "rulemodel.h"

#include "phosphor_i18n.h"

#include <PhosphorRules/MatchTypes.h>

namespace PlasmaZones {

// The `case Field::X` labels below need the enum name unqualified. Class scope
// does not supply it (the declaration spells the parameter PhosphorRules::Field),
// so the using-declaration is load-bearing, not tidiness.
using PhosphorRules::Field;

QString RuleModel::sectionLabel(Section section)
{
    switch (section) {
    case Section::Monitor:
        return PhosphorI18n::tr("Monitor & Layout");
    case Section::Application:
        return PhosphorI18n::tr("Applications");
    case Section::Activity:
        return PhosphorI18n::tr("Activities");
    case Section::Animation:
        return PhosphorI18n::tr("Animations");
    case Section::Advanced:
        return PhosphorI18n::tr("Advanced / Custom");
    case Section::System:
        return PhosphorI18n::tr("System");
    }
    return QString();
}

QString RuleModel::fieldLabel(PhosphorRules::Field field)
{
    switch (field) {
    case Field::AppId:
        return PhosphorI18n::tr("Application");
    case Field::WindowClass:
        return PhosphorI18n::tr("Window class");
    case Field::DesktopFile:
        return PhosphorI18n::tr("Desktop file");
    case Field::WindowRole:
        return PhosphorI18n::tr("Window role");
    case Field::Pid:
        return PhosphorI18n::tr("Process ID");
    case Field::Title:
        return PhosphorI18n::tr("Title");
    case Field::WindowType:
        return PhosphorI18n::tr("Window type");
    case Field::IsSticky:
        return PhosphorI18n::tr("Sticky");
    case Field::IsFullscreen:
        return PhosphorI18n::tr("Fullscreen");
    case Field::IsMaximized:
        return PhosphorI18n::tr("Maximized");
    case Field::IsMinimized:
        return PhosphorI18n::tr("Minimized");
    case Field::IsFocused:
        return PhosphorI18n::tr("Focused");
    case Field::ScreenId:
        return PhosphorI18n::tr("Monitor");
    case Field::VirtualDesktop:
        return PhosphorI18n::tr("Desktop");
    case Field::Activity:
        return PhosphorI18n::tr("Activity");
    case Field::IsTransient:
        return PhosphorI18n::tr("Transient");
    case Field::IsNotification:
        return PhosphorI18n::tr("Notification");
    case Field::Width:
        return PhosphorI18n::tr("Width");
    case Field::Height:
        return PhosphorI18n::tr("Height");
    case Field::KeepAbove:
        return PhosphorI18n::tr("Keep above");
    case Field::KeepBelow:
        return PhosphorI18n::tr("Keep below");
    case Field::SkipTaskbar:
        return PhosphorI18n::tr("Skip taskbar");
    case Field::SkipPager:
        return PhosphorI18n::tr("Skip pager");
    case Field::SkipSwitcher:
        return PhosphorI18n::tr("Skip switcher");
    case Field::IsModal:
        return PhosphorI18n::tr("Modal");
    case Field::HasDecoration:
        return PhosphorI18n::tr("Decorated");
    case Field::IsResizable:
        return PhosphorI18n::tr("Resizable");
    case Field::IsMovable:
        return PhosphorI18n::tr("Movable");
    case Field::IsMaximizable:
        return PhosphorI18n::tr("Maximizable");
    case Field::PositionX:
        return PhosphorI18n::tr("Position X");
    case Field::PositionY:
        return PhosphorI18n::tr("Position Y");
    case Field::CaptionNormal:
        return PhosphorI18n::tr("Title (no suffix)");
    case Field::IsFloating:
        return PhosphorI18n::tr("Floating");
    case Field::IsSnapped:
        return PhosphorI18n::tr("Snapped");
    case Field::IsTiled:
        return PhosphorI18n::tr("Tiled");
    case Field::Zone:
        return PhosphorI18n::tr("Zone");
    case Field::Mode:
        return PhosphorI18n::tr("Mode");
    case Field::TiledWindowCount:
        return PhosphorI18n::tr("Tiled window count");
    case Field::ScreenOrientation:
        return PhosphorI18n::tr("Screen orientation");
    case Field::ActiveLayout:
        return PhosphorI18n::tr("Active layout");
    case Field::ColorScheme:
        return PhosphorI18n::tr("Color scheme");
    }
    return QString();
}

} // namespace PlasmaZones
