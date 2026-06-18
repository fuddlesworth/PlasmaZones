// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QLatin1StringView>
#include <QString>
#include <QStringView>

#include <optional>

namespace PhosphorWindowRules {

/// Match fields. Each names one attribute of a WindowQuery — either a
/// window property (absent during a windowless context query) or a context
/// attribute (always present). A predicate over an absent window field
/// evaluates false, which is what makes window-property rules naturally
/// inert during context resolution without special-casing.
enum class Field : int {
    // ── Window properties [0, 29] ────────────────────────────────────────
    AppId = 0,
    WindowClass = 1,
    DesktopFile = 2,
    WindowRole = 3,
    Pid = 4,
    Title = 5,
    WindowType = 6,
    IsSticky = 7,
    IsFullscreen = 8,
    IsMinimized = 9,
    ScreenId = 10, ///< context — always present
    VirtualDesktop = 11, ///< context — always present
    Activity = 12, ///< context — always present
    IsMaximized = 13,
    IsFocused = 14,
    IsTransient = 15,
    IsNotification = 16,
    Width = 17,
    Height = 18,
    KeepAbove = 19,
    KeepBelow = 20,
    SkipTaskbar = 21,
    SkipPager = 22,
    SkipSwitcher = 23,
    IsModal = 24,
    HasDecoration = 25,
    IsResizable = 26,
    PositionX = 27,
    PositionY = 28,
    CaptionNormal = 29,
    // ── PlasmaZones extension fields [30, 32] ────────────────────────────
    IsFloating = 30, ///< floated out of tiling (snap or autotile)
    IsSnapped = 31, ///< occupies a snap zone
    Zone = 32, ///< the snap zone's UUID the window occupies
};

/// The number of distinct `Field` enumerators. `Field` is a contiguous range
/// `[0, FieldCount)`; bump this whenever an enumerator is added — round-trip
/// tests iterate the range using it as the upper bound.
inline constexpr int FieldCount = static_cast<int>(Field::Zone) + 1;

// ── Field descriptor table ──────────────────────────────────────────────────
// Single source of truth for every field's wire string, value-kind, and
// source classification. Adding a new field: append an enum value above,
// bump FieldCount, and add one row here — the classifier functions below
// derive everything from this table.

/// Value-kind of a field's payload in a WindowQuery.
enum class FieldType : int {
    String, ///< QString-valued (operators: Equals/Contains/StartsWith/EndsWith/Regex/In)
    Bool, ///< bool-valued (operator: Equals)
    Int, ///< int-valued (operators: Equals/GreaterThan/LessThan)
    Enum, ///< enum-as-int (WindowType) — Equals/In only
};

/// Whether a field is a window property or a context attribute.
enum class FieldSource : int {
    Window, ///< absent during windowless context queries
    Context, ///< always present (screen / desktop / activity)
};

/// Compile-time descriptor for one Field.
struct FieldDescriptor
{
    Field field;
    QLatin1StringView wire;
    FieldType type;
    FieldSource source;
};

/// The master field table — indexed by `static_cast<int>(field)`.
inline constexpr FieldDescriptor kFieldTable[] = {
    // [0, 29] — Generic window properties
    {Field::AppId,          QLatin1StringView("appId"),          FieldType::String, FieldSource::Window},
    {Field::WindowClass,    QLatin1StringView("windowClass"),    FieldType::String, FieldSource::Window},
    {Field::DesktopFile,    QLatin1StringView("desktopFile"),    FieldType::String, FieldSource::Window},
    {Field::WindowRole,     QLatin1StringView("windowRole"),     FieldType::String, FieldSource::Window},
    {Field::Pid,            QLatin1StringView("pid"),            FieldType::Int,    FieldSource::Window},
    {Field::Title,          QLatin1StringView("title"),          FieldType::String, FieldSource::Window},
    {Field::WindowType,     QLatin1StringView("windowType"),     FieldType::Enum,   FieldSource::Window},
    {Field::IsSticky,       QLatin1StringView("isSticky"),       FieldType::Bool,   FieldSource::Window},
    {Field::IsFullscreen,   QLatin1StringView("isFullscreen"),   FieldType::Bool,   FieldSource::Window},
    {Field::IsMinimized,    QLatin1StringView("isMinimized"),    FieldType::Bool,   FieldSource::Window},
    {Field::ScreenId,       QLatin1StringView("screenId"),       FieldType::String, FieldSource::Context},
    {Field::VirtualDesktop, QLatin1StringView("virtualDesktop"), FieldType::Int,    FieldSource::Context},
    {Field::Activity,       QLatin1StringView("activity"),       FieldType::String, FieldSource::Context},
    {Field::IsMaximized,    QLatin1StringView("isMaximized"),    FieldType::Bool,   FieldSource::Window},
    {Field::IsFocused,      QLatin1StringView("isFocused"),      FieldType::Bool,   FieldSource::Window},
    {Field::IsTransient,    QLatin1StringView("isTransient"),    FieldType::Bool,   FieldSource::Window},
    {Field::IsNotification, QLatin1StringView("isNotification"), FieldType::Bool,   FieldSource::Window},
    {Field::Width,          QLatin1StringView("width"),          FieldType::Int,    FieldSource::Window},
    {Field::Height,         QLatin1StringView("height"),         FieldType::Int,    FieldSource::Window},
    {Field::KeepAbove,      QLatin1StringView("keepAbove"),      FieldType::Bool,   FieldSource::Window},
    {Field::KeepBelow,      QLatin1StringView("keepBelow"),      FieldType::Bool,   FieldSource::Window},
    {Field::SkipTaskbar,    QLatin1StringView("skipTaskbar"),    FieldType::Bool,   FieldSource::Window},
    {Field::SkipPager,      QLatin1StringView("skipPager"),      FieldType::Bool,   FieldSource::Window},
    {Field::SkipSwitcher,   QLatin1StringView("skipSwitcher"),   FieldType::Bool,   FieldSource::Window},
    {Field::IsModal,        QLatin1StringView("isModal"),        FieldType::Bool,   FieldSource::Window},
    {Field::HasDecoration,  QLatin1StringView("hasDecoration"),  FieldType::Bool,   FieldSource::Window},
    {Field::IsResizable,    QLatin1StringView("isResizable"),    FieldType::Bool,   FieldSource::Window},
    {Field::PositionX,      QLatin1StringView("positionX"),      FieldType::Int,    FieldSource::Window},
    {Field::PositionY,      QLatin1StringView("positionY"),      FieldType::Int,    FieldSource::Window},
    {Field::CaptionNormal,  QLatin1StringView("captionNormal"),  FieldType::String, FieldSource::Window},
    // [30, 32] — PlasmaZones extension fields
    {Field::IsFloating,     QLatin1StringView("isFloating"),     FieldType::Bool,   FieldSource::Window},
    {Field::IsSnapped,      QLatin1StringView("isSnapped"),      FieldType::Bool,   FieldSource::Window},
    {Field::Zone,           QLatin1StringView("zone"),           FieldType::String, FieldSource::Window},
};
static_assert(sizeof(kFieldTable) / sizeof(kFieldTable[0]) == static_cast<unsigned>(FieldCount),
              "kFieldTable must have one entry per Field");

consteval bool verifyFieldTableOrder()
{
    for (int i = 0; i < FieldCount; ++i) {
        if (static_cast<int>(kFieldTable[i].field) != i)
            return false;
    }
    return true;
}
static_assert(verifyFieldTableOrder(), "kFieldTable entries must be in Field enum order");

// ── Classifier functions (table-derived) ────────────────────────────────────

/// Canonical lowercase wire string for a Field.
inline QString fieldToString(Field field)
{
    const int idx = static_cast<int>(field);
    if (idx >= 0 && idx < FieldCount) {
        return QString(kFieldTable[idx].wire);
    }
    return QStringLiteral("appId");
}

/// Strict parse: an unknown token returns nullopt so the loader can drop the
/// malformed predicate rather than silently coercing typos to a default.
inline std::optional<Field> fieldFromString(QStringView s)
{
    for (const auto& d : kFieldTable) {
        if (s.compare(d.wire, Qt::CaseInsensitive) == 0) {
            return d.field;
        }
    }
    return std::nullopt;
}

/// True if @p field carries a string value (the four string operators and
/// Regex apply to these; numeric/bool fields do not).
inline bool fieldIsString(Field field)
{
    const int idx = static_cast<int>(field);
    return idx >= 0 && idx < FieldCount && kFieldTable[idx].type == FieldType::String;
}

/// True if @p field carries a numeric value.
inline bool fieldIsNumeric(Field field)
{
    const int idx = static_cast<int>(field);
    return idx >= 0 && idx < FieldCount && kFieldTable[idx].type == FieldType::Int;
}

/// True if @p field carries a boolean value (window-state flags).
inline bool fieldIsBool(Field field)
{
    const int idx = static_cast<int>(field);
    return idx >= 0 && idx < FieldCount && kFieldTable[idx].type == FieldType::Bool;
}

/// True if @p field describes the **context** a window appears in
/// (screen / virtual desktop / activity) rather than a property of the
/// window itself.
inline bool fieldIsContext(Field field)
{
    const int idx = static_cast<int>(field);
    return idx >= 0 && idx < FieldCount && kFieldTable[idx].source == FieldSource::Context;
}

// ─────────────────────────────────────────────────────────────────────────────

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
    GreaterThan = 7, ///< numeric compare (Pid / VirtualDesktop / Width / Height / PositionX / PositionY)
    LessThan = 8, ///< numeric compare (Pid / VirtualDesktop / Width / Height / PositionX / PositionY)
};

/// The number of distinct `Operator` enumerators. `Operator` is a contiguous
/// range `[0, OperatorCount)`; bump this whenever an enumerator is added.
inline constexpr int OperatorCount = static_cast<int>(Operator::LessThan) + 1;

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

} // namespace PhosphorWindowRules
