// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QLatin1StringView>
#include <QString>
#include <QStringView>

#include <optional>

namespace PhosphorRules {

/// Match fields. Each names one attribute of a WindowQuery — either a
/// window property (absent during a windowless context query) or a context
/// attribute (present during context resolution, except the optional
/// tiledWindowCount which is absent when the count is unknown). A predicate over
/// an absent field evaluates false, which is what makes window-property rules
/// naturally inert during context resolution without special-casing.
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
    // ── PlasmaZones extension fields [30, 33] ────────────────────────────
    IsFloating = 30, ///< floated out of tiling (snap or autotile)
    IsSnapped = 31, ///< occupies a snap zone
    Zone = 32, ///< the snap zone's UUID the window occupies
    IsTiled = 33, ///< managed by the autotile engine (distinct from IsSnapped)
    // ── Context placement-mode field [34] ────────────────────────────────
    Mode = 34, ///< context — current placement mode (snapping / tiling)
    // ── Context tiling-environment field [35] ────────────────────────────
    TiledWindowCount = 35, ///< context — tiled windows on this screen + desktop
    // ── Window capability fields [36, 37] ────────────────────────────────
    IsMovable = 36, ///< window can be moved
    IsMaximizable = 37, ///< window can be maximized
    // ── Context screen-orientation field [38] ────────────────────────────
    ScreenOrientation = 38, ///< context — "portrait" / "landscape" of the resolving screen
    // ── Context active-layout field [39] ─────────────────────────────────
    ActiveLayout = 39, ///< context — the layout id currently resolved for the screen (snap UUID or "autotile:<algo>")
};

/// The number of distinct `Field` enumerators. `Field` is a contiguous range
/// `[0, FieldCount)`; bump this whenever an enumerator is added — round-trip
/// tests iterate the range using it as the upper bound.
inline constexpr int FieldCount = static_cast<int>(Field::ActiveLayout) + 1;

// ── Field descriptor table ──────────────────────────────────────────────────
// Single source of truth for every field's wire string, value-kind, and
// source classification. Adding a new field: append an enum value above,
// bump FieldCount, and add one row here — the classifier functions below
// derive everything from this table.

/// Value-kind of a field's payload in a WindowQuery.
enum class FieldType : int {
    String, ///< QString-valued (operators: Equals/Contains/StartsWith/EndsWith/Regex)
    Bool, ///< bool-valued (operator: Equals)
    Int, ///< int-valued (operators: Equals/GreaterThan/LessThan)
    Enum, ///< enum-as-int (WindowType) — Equals only
};

/// Whether a field is a window property or a context attribute.
enum class FieldSource : int {
    Window, ///< absent during windowless context queries
    Context, ///< resolvable during a windowless context query (screen / desktop /
             ///< activity / mode are always set; tiledWindowCount is optional and
             ///< absent when the count is unknown)
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
    {Field::AppId, QLatin1StringView("appId"), FieldType::String, FieldSource::Window},
    {Field::WindowClass, QLatin1StringView("windowClass"), FieldType::String, FieldSource::Window},
    {Field::DesktopFile, QLatin1StringView("desktopFile"), FieldType::String, FieldSource::Window},
    {Field::WindowRole, QLatin1StringView("windowRole"), FieldType::String, FieldSource::Window},
    {Field::Pid, QLatin1StringView("pid"), FieldType::Int, FieldSource::Window},
    {Field::Title, QLatin1StringView("title"), FieldType::String, FieldSource::Window},
    {Field::WindowType, QLatin1StringView("windowType"), FieldType::Enum, FieldSource::Window},
    {Field::IsSticky, QLatin1StringView("isSticky"), FieldType::Bool, FieldSource::Window},
    {Field::IsFullscreen, QLatin1StringView("isFullscreen"), FieldType::Bool, FieldSource::Window},
    {Field::IsMinimized, QLatin1StringView("isMinimized"), FieldType::Bool, FieldSource::Window},
    {Field::ScreenId, QLatin1StringView("screenId"), FieldType::String, FieldSource::Context},
    {Field::VirtualDesktop, QLatin1StringView("virtualDesktop"), FieldType::Int, FieldSource::Context},
    {Field::Activity, QLatin1StringView("activity"), FieldType::String, FieldSource::Context},
    {Field::IsMaximized, QLatin1StringView("isMaximized"), FieldType::Bool, FieldSource::Window},
    {Field::IsFocused, QLatin1StringView("isFocused"), FieldType::Bool, FieldSource::Window},
    {Field::IsTransient, QLatin1StringView("isTransient"), FieldType::Bool, FieldSource::Window},
    {Field::IsNotification, QLatin1StringView("isNotification"), FieldType::Bool, FieldSource::Window},
    {Field::Width, QLatin1StringView("width"), FieldType::Int, FieldSource::Window},
    {Field::Height, QLatin1StringView("height"), FieldType::Int, FieldSource::Window},
    {Field::KeepAbove, QLatin1StringView("keepAbove"), FieldType::Bool, FieldSource::Window},
    {Field::KeepBelow, QLatin1StringView("keepBelow"), FieldType::Bool, FieldSource::Window},
    {Field::SkipTaskbar, QLatin1StringView("skipTaskbar"), FieldType::Bool, FieldSource::Window},
    {Field::SkipPager, QLatin1StringView("skipPager"), FieldType::Bool, FieldSource::Window},
    {Field::SkipSwitcher, QLatin1StringView("skipSwitcher"), FieldType::Bool, FieldSource::Window},
    {Field::IsModal, QLatin1StringView("isModal"), FieldType::Bool, FieldSource::Window},
    {Field::HasDecoration, QLatin1StringView("hasDecoration"), FieldType::Bool, FieldSource::Window},
    {Field::IsResizable, QLatin1StringView("isResizable"), FieldType::Bool, FieldSource::Window},
    {Field::PositionX, QLatin1StringView("positionX"), FieldType::Int, FieldSource::Window},
    {Field::PositionY, QLatin1StringView("positionY"), FieldType::Int, FieldSource::Window},
    {Field::CaptionNormal, QLatin1StringView("captionNormal"), FieldType::String, FieldSource::Window},
    // [30, 33] — PlasmaZones extension fields
    {Field::IsFloating, QLatin1StringView("isFloating"), FieldType::Bool, FieldSource::Window},
    {Field::IsSnapped, QLatin1StringView("isSnapped"), FieldType::Bool, FieldSource::Window},
    {Field::Zone, QLatin1StringView("zone"), FieldType::String, FieldSource::Window},
    {Field::IsTiled, QLatin1StringView("isTiled"), FieldType::Bool, FieldSource::Window},
    // [34] — Context placement-mode field. String-valued (wire tokens
    // "snapping" / "tiling") so an `Equals` leaf compares the
    // token directly, and Context-sourced so it is present during windowless
    // context resolution — which is what lets a per-mode rule participate in
    // the gap cascade and pass the context-action compatibility check.
    {Field::Mode, QLatin1StringView("mode"), FieldType::String, FieldSource::Context},
    // [35] — Tiled-window count for the screen + desktop being resolved. Int-
    // valued (Equals / GreaterThan / LessThan) and Context-sourced so it is
    // present during windowless context resolution — which is what lets a
    // "switch algorithm when a second window opens" rule participate in the
    // tiling-algorithm slot of the assignment cascade. Absent (predicate
    // false) when the resolving screen is not actively tiling.
    {Field::TiledWindowCount, QLatin1StringView("tiledWindowCount"), FieldType::Int, FieldSource::Context},
    // [36, 37] — Window capability flags (can the window be moved / maximized).
    // Window-sourced like IsResizable, so inert during windowless context queries.
    {Field::IsMovable, QLatin1StringView("isMovable"), FieldType::Bool, FieldSource::Window},
    {Field::IsMaximizable, QLatin1StringView("isMaximizable"), FieldType::Bool, FieldSource::Window},
    // [38] — Screen orientation ("portrait" / "landscape") of the screen being
    // resolved. String-valued (Equals against the token) and Context-sourced so
    // it is present during windowless context resolution — which lets an
    // orientation rule drive the layout/algorithm assignment for a rotated
    // monitor. Empty (predicate false) when no geometry provider is wired.
    {Field::ScreenOrientation, QLatin1StringView("screenOrientation"), FieldType::String, FieldSource::Context},
    // [39] — The layout id currently resolved for the screen (snap UUID or
    // "autotile:<algo>"). String-valued (Equals against the id) and Context-
    // sourced. Populated only by the daemon-facing resolvers (gap / lock /
    // overlay), NOT the assignment cascade — reading the active layout while
    // resolving it would recurse. Empty (predicate false) where unpopulated.
    {Field::ActiveLayout, QLatin1StringView("activeLayout"), FieldType::String, FieldSource::Context},
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
/// (screen / virtual desktop / activity / placement mode / tiled-window count)
/// rather than a property of the window itself.
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
    GreaterThan =
        6, ///< numeric compare (Pid / VirtualDesktop / Width / Height / PositionX / PositionY / TiledWindowCount)
    LessThan =
        7, ///< numeric compare (Pid / VirtualDesktop / Width / Height / PositionX / PositionY / TiledWindowCount)
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

} // namespace PhosphorRules
