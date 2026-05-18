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
};

/// The number of distinct `Field` enumerators. `Field` is a contiguous range
/// `[0, FieldCount)`; bump this whenever an enumerator is added — round-trip
/// tests iterate the range using it as the upper bound.
inline constexpr int FieldCount = static_cast<int>(Field::Activity) + 1;

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
    GreaterThan = 7, ///< numeric compare (Pid / VirtualDesktop)
    LessThan = 8, ///< numeric compare (Pid / VirtualDesktop)
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
        return true;
    case Field::Pid:
    case Field::VirtualDesktop:
    case Field::WindowType:
    case Field::IsSticky:
    case Field::IsFullscreen:
    case Field::IsMinimized:
        return false;
    }
    return false;
}

/// True if @p field carries a numeric value (Pid / VirtualDesktop).
inline bool fieldIsNumeric(Field field)
{
    return field == Field::Pid || field == Field::VirtualDesktop;
}

/// True if @p field carries a boolean value (the three window-state flags).
inline bool fieldIsBool(Field field)
{
    return field == Field::IsSticky || field == Field::IsFullscreen || field == Field::IsMinimized;
}

} // namespace PhosphorWindowRule
