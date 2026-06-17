// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QLatin1StringView>
#include <QString>
#include <QStringView>

#include <optional>

namespace PhosphorWindowRule {

/// Match fields. Each names one attribute of a WindowQuery — either a
/// window property (absent during a windowless context query) or a context
/// attribute (always present). A predicate over an absent window field
/// evaluates false, which is what makes window-property rules naturally
/// inert during context resolution without special-casing.
enum class Field : int {
    // Window identity
    AppId = 0,
    WindowClass = 1,
    DesktopFile = 2,
    WindowRole = 3,
    Pid = 4,
    // Window content
    Title = 5,
    // Window state
    WindowType = 6,
    IsSticky = 7,
    IsFullscreen = 8,
    IsMinimized = 9,
    // Context
    ScreenId = 10,
    VirtualDesktop = 11,
    Activity = 12,
    // Window state (appended — keeping enum values stable across versions)
    IsMaximized = 13,
    IsFocused = 14, ///< true when the window is the focused / active window
    IsTransient = 15, ///< dialog/utility/popup/menu/tooltip/splash family, or has a transient parent
    IsNotification = 16, ///< notification / critical-notification / on-screen-display surface
    // Window geometry (numeric)
    Width = 17, ///< frame width in px
    Height = 18, ///< frame height in px
    // Window stacking / accessory state (appended — append-only for value stability)
    KeepAbove = 19, ///< window set to stay above others (always on top)
    KeepBelow = 20, ///< window set to stay below others
    SkipTaskbar = 21, ///< hidden from the taskbar
    SkipPager = 22, ///< hidden from the pager
    SkipSwitcher = 23, ///< hidden from the window switcher (Alt+Tab)
    IsModal = 24, ///< modal dialog
    HasDecoration = 25, ///< has a server-side title-bar / border
    IsResizable = 26, ///< window can be resized
    // Window geometry (numeric) — position
    PositionX = 27, ///< frame left edge X in px
    PositionY = 28, ///< frame top edge Y in px
    // Window content
    CaptionNormal = 29, ///< title without the WM-added application-name suffix
    // PlasmaZones placement state (snap-mode semantics; see WindowQuery population).
    // IsFloating covers both snap- and autotile-floated windows; IsSnapped / Zone
    // reflect snap-mode zone membership only — autotile tiles carry no persistent
    // zone UUID, so they are neither IsSnapped nor matched by Zone.
    IsFloating = 30, ///< window floated out of tiling (snap or autotile)
    IsSnapped = 31, ///< window occupies a snap zone
    Zone = 32, ///< the snap zone's UUID the window occupies
};

/// The number of distinct `Field` enumerators. `Field` is a contiguous range
/// `[0, FieldCount)`; bump this whenever an enumerator is added — round-trip
/// tests iterate the range using it as the upper bound.
inline constexpr int FieldCount = static_cast<int>(Field::Zone) + 1;

/// Match operators. Not every operator is valid against every field —
/// validity is enforced by Predicate::isValid(), not the parser.
enum class Operator : int {
    Equals = 0, ///< case-insensitive for strings, numeric for ints, bool for flags
    Contains = 1, ///< substring, case-insensitive (strings)
    StartsWith = 2, ///< prefix, case-insensitive (strings)
    EndsWith = 3, ///< suffix, case-insensitive (strings)
    Regex = 4, ///< QRegularExpression, precompiled & cached per predicate
    AppIdMatches = 5, ///< segment-aware reverse-DNS match (AppId only)
    In = 6, ///< value is a set; membership test
    GreaterThan = 7, ///< numeric compare (Pid / VirtualDesktop / Width / Height)
    LessThan = 8, ///< numeric compare (Pid / VirtualDesktop / Width / Height)
};

/// The number of distinct `Operator` enumerators. `Operator` is a contiguous
/// range `[0, OperatorCount)`; bump this whenever an enumerator is added.
inline constexpr int OperatorCount = static_cast<int>(Operator::LessThan) + 1;

/// Canonical lowercase wire string for a Field.
inline QString fieldToString(Field field)
{
    switch (field) {
    case Field::AppId:
        return QStringLiteral("appId");
    case Field::WindowClass:
        return QStringLiteral("windowClass");
    case Field::DesktopFile:
        return QStringLiteral("desktopFile");
    case Field::WindowRole:
        return QStringLiteral("windowRole");
    case Field::Pid:
        return QStringLiteral("pid");
    case Field::Title:
        return QStringLiteral("title");
    case Field::WindowType:
        return QStringLiteral("windowType");
    case Field::IsSticky:
        return QStringLiteral("isSticky");
    case Field::IsFullscreen:
        return QStringLiteral("isFullscreen");
    case Field::IsMinimized:
        return QStringLiteral("isMinimized");
    case Field::ScreenId:
        return QStringLiteral("screenId");
    case Field::VirtualDesktop:
        return QStringLiteral("virtualDesktop");
    case Field::Activity:
        return QStringLiteral("activity");
    case Field::IsMaximized:
        return QStringLiteral("isMaximized");
    case Field::IsFocused:
        return QStringLiteral("isFocused");
    case Field::IsTransient:
        return QStringLiteral("isTransient");
    case Field::IsNotification:
        return QStringLiteral("isNotification");
    case Field::Width:
        return QStringLiteral("width");
    case Field::Height:
        return QStringLiteral("height");
    case Field::KeepAbove:
        return QStringLiteral("keepAbove");
    case Field::KeepBelow:
        return QStringLiteral("keepBelow");
    case Field::SkipTaskbar:
        return QStringLiteral("skipTaskbar");
    case Field::SkipPager:
        return QStringLiteral("skipPager");
    case Field::SkipSwitcher:
        return QStringLiteral("skipSwitcher");
    case Field::IsModal:
        return QStringLiteral("isModal");
    case Field::HasDecoration:
        return QStringLiteral("hasDecoration");
    case Field::IsResizable:
        return QStringLiteral("isResizable");
    case Field::PositionX:
        return QStringLiteral("positionX");
    case Field::PositionY:
        return QStringLiteral("positionY");
    case Field::CaptionNormal:
        return QStringLiteral("captionNormal");
    case Field::IsFloating:
        return QStringLiteral("isFloating");
    case Field::IsSnapped:
        return QStringLiteral("isSnapped");
    case Field::Zone:
        return QStringLiteral("zone");
    }
    return QStringLiteral("appId");
}

/// Strict parse: an unknown token returns nullopt so the loader can drop the
/// malformed predicate rather than silently coercing typos to a default.
inline std::optional<Field> fieldFromString(QStringView s)
{
    static constexpr std::pair<QLatin1StringView, Field> kTable[] = {
        {QLatin1StringView("appId"), Field::AppId},
        {QLatin1StringView("windowClass"), Field::WindowClass},
        {QLatin1StringView("desktopFile"), Field::DesktopFile},
        {QLatin1StringView("windowRole"), Field::WindowRole},
        {QLatin1StringView("pid"), Field::Pid},
        {QLatin1StringView("title"), Field::Title},
        {QLatin1StringView("windowType"), Field::WindowType},
        {QLatin1StringView("isSticky"), Field::IsSticky},
        {QLatin1StringView("isFullscreen"), Field::IsFullscreen},
        {QLatin1StringView("isMinimized"), Field::IsMinimized},
        {QLatin1StringView("screenId"), Field::ScreenId},
        {QLatin1StringView("virtualDesktop"), Field::VirtualDesktop},
        {QLatin1StringView("activity"), Field::Activity},
        {QLatin1StringView("isMaximized"), Field::IsMaximized},
        {QLatin1StringView("isFocused"), Field::IsFocused},
        {QLatin1StringView("isTransient"), Field::IsTransient},
        {QLatin1StringView("isNotification"), Field::IsNotification},
        {QLatin1StringView("width"), Field::Width},
        {QLatin1StringView("height"), Field::Height},
        {QLatin1StringView("keepAbove"), Field::KeepAbove},
        {QLatin1StringView("keepBelow"), Field::KeepBelow},
        {QLatin1StringView("skipTaskbar"), Field::SkipTaskbar},
        {QLatin1StringView("skipPager"), Field::SkipPager},
        {QLatin1StringView("skipSwitcher"), Field::SkipSwitcher},
        {QLatin1StringView("isModal"), Field::IsModal},
        {QLatin1StringView("hasDecoration"), Field::HasDecoration},
        {QLatin1StringView("isResizable"), Field::IsResizable},
        {QLatin1StringView("positionX"), Field::PositionX},
        {QLatin1StringView("positionY"), Field::PositionY},
        {QLatin1StringView("captionNormal"), Field::CaptionNormal},
        {QLatin1StringView("isFloating"), Field::IsFloating},
        {QLatin1StringView("isSnapped"), Field::IsSnapped},
        {QLatin1StringView("zone"), Field::Zone},
    };
    for (const auto& [token, field] : kTable) {
        if (s.compare(token, Qt::CaseInsensitive) == 0) {
            return field;
        }
    }
    return std::nullopt;
}

/// Canonical lowercase wire string for an Operator.
inline QString operatorToString(Operator op)
{
    switch (op) {
    case Operator::Equals:
        return QStringLiteral("equals");
    case Operator::Contains:
        return QStringLiteral("contains");
    case Operator::StartsWith:
        return QStringLiteral("startsWith");
    case Operator::EndsWith:
        return QStringLiteral("endsWith");
    case Operator::Regex:
        return QStringLiteral("regex");
    case Operator::AppIdMatches:
        return QStringLiteral("appIdMatches");
    case Operator::In:
        return QStringLiteral("in");
    case Operator::GreaterThan:
        return QStringLiteral("greaterThan");
    case Operator::LessThan:
        return QStringLiteral("lessThan");
    }
    return QStringLiteral("equals");
}

/// Strict parse: an unknown token returns nullopt.
inline std::optional<Operator> operatorFromString(QStringView s)
{
    static constexpr std::pair<QLatin1StringView, Operator> kTable[] = {
        {QLatin1StringView("equals"), Operator::Equals},
        {QLatin1StringView("contains"), Operator::Contains},
        {QLatin1StringView("startsWith"), Operator::StartsWith},
        {QLatin1StringView("endsWith"), Operator::EndsWith},
        {QLatin1StringView("regex"), Operator::Regex},
        {QLatin1StringView("appIdMatches"), Operator::AppIdMatches},
        {QLatin1StringView("in"), Operator::In},
        {QLatin1StringView("greaterThan"), Operator::GreaterThan},
        {QLatin1StringView("lessThan"), Operator::LessThan},
    };
    for (const auto& [token, op] : kTable) {
        if (s.compare(token, Qt::CaseInsensitive) == 0) {
            return op;
        }
    }
    return std::nullopt;
}

/// True if @p field carries a string value (the four string operators and
/// Regex apply to these; numeric/bool fields do not).
inline bool fieldIsString(Field field)
{
    switch (field) {
    case Field::AppId:
    case Field::WindowClass:
    case Field::DesktopFile:
    case Field::WindowRole:
    case Field::Title:
    case Field::ScreenId:
    case Field::Activity:
    case Field::CaptionNormal:
    case Field::Zone:
        return true;
    case Field::Pid:
    case Field::VirtualDesktop:
    case Field::WindowType:
    case Field::IsSticky:
    case Field::IsFullscreen:
    case Field::IsMinimized:
    case Field::IsMaximized:
    case Field::IsFocused:
    case Field::IsTransient:
    case Field::IsNotification:
    case Field::Width:
    case Field::Height:
    case Field::KeepAbove:
    case Field::KeepBelow:
    case Field::SkipTaskbar:
    case Field::SkipPager:
    case Field::SkipSwitcher:
    case Field::IsModal:
    case Field::HasDecoration:
    case Field::IsResizable:
    case Field::PositionX:
    case Field::PositionY:
    case Field::IsFloating:
    case Field::IsSnapped:
        return false;
    }
    return false;
}

/// True if @p field carries a numeric value (Pid / VirtualDesktop / Width / Height).
inline bool fieldIsNumeric(Field field)
{
    return field == Field::Pid || field == Field::VirtualDesktop || field == Field::Width || field == Field::Height
        || field == Field::PositionX || field == Field::PositionY;
}

/// True if @p field carries a boolean value (window-state flags).
inline bool fieldIsBool(Field field)
{
    return field == Field::IsSticky || field == Field::IsFullscreen || field == Field::IsMinimized
        || field == Field::IsMaximized || field == Field::IsFocused || field == Field::IsTransient
        || field == Field::IsNotification || field == Field::KeepAbove || field == Field::KeepBelow
        || field == Field::SkipTaskbar || field == Field::SkipPager || field == Field::SkipSwitcher
        || field == Field::IsModal || field == Field::HasDecoration || field == Field::IsResizable
        || field == Field::IsFloating || field == Field::IsSnapped;
}

/// True if @p field describes the **context** a window appears in
/// (screen / virtual desktop / activity) rather than a property of the
/// window itself. Context fields are populated on every `WindowQuery`,
/// including the windowless queries the context-mode evaluator issues;
/// non-context (window-property) fields are absent on those queries and
/// any predicate over them evaluates false there. Action/match
/// compatibility (a `SetEngineMode` rule whose match references only
/// window properties never fires during context resolution) is computed
/// off this classification.
inline bool fieldIsContext(Field field)
{
    return field == Field::ScreenId || field == Field::VirtualDesktop || field == Field::Activity;
}

} // namespace PhosphorWindowRule
