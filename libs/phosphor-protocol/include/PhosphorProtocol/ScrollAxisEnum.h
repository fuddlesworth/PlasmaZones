// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

namespace PhosphorProtocol {

/// Which way a scrolling strip runs on one screen.
///
/// MAIN axis = the strip: columns lie along it and the view scrolls along it.
/// CROSS axis = the stack: the tiles of a column divide it.
/// Horizontal is the historical layout, main = x and cross = y.
///
/// Wire/config encoding is the underlying int; append only. Horizontal MUST
/// stay 0, and that is load-bearing in more than one place at once: a
/// value-initialized wire struct, an absent JSON key read through toInt(0),
/// and a screen absent from the effect's vertical-axis list all have to mean
/// "as it behaved before vertical strips existed".
///
/// This is the RESOLVED vocabulary, deliberately two-valued. The SETTING is a
/// separate three-valued config enum (Auto / Horizontal / Vertical) whose
/// numbering does not match this one, so that Auto can be its zero. Never
/// static_cast between the two — same trap, and same handling, as
/// DefaultWidthKind vs ColumnWidth::Kind (PhosphorScrollEngine/ScrollTypes.h).
///
/// Deliberately free of QtDBus / Q_DECLARE_METATYPE, and of every Qt include,
/// so the GPL KWin effect can include it on the paint path without pulling in
/// marshalling machinery (same reasoning as WindowTypeEnum.h).
enum class ScrollAxis : int {
    Horizontal = 0,
    Vertical = 1,
};

/// Inclusive bounds of the valid ScrollAxis underlying values — used to
/// range-check an int that crossed a process boundary before casting it back.
inline constexpr int scrollAxisMinValue = static_cast<int>(ScrollAxis::Horizontal);
inline constexpr int scrollAxisMaxValue = static_cast<int>(ScrollAxis::Vertical);

/// True if @p value is a valid ScrollAxis underlying value.
inline constexpr bool isValidScrollAxis(int value)
{
    return value >= scrollAxisMinValue && value <= scrollAxisMaxValue;
}

/// Cast an int that crossed a process boundary to a ScrollAxis; out-of-range
/// values (version skew, a malformed caller) fall back to Horizontal, which is
/// the same answer an absent value gives.
inline constexpr ScrollAxis scrollAxisFromInt(int value)
{
    return isValidScrollAxis(value) ? static_cast<ScrollAxis>(value) : ScrollAxis::Horizontal;
}

/// The Auto rule, resolving a work area's dimensions to a strip axis: a work
/// area taller than it is wide runs the strip vertically.
///
/// Strictly greater on height, with no threshold knob. An aspect-ratio
/// threshold is a setting nobody can reason about, where "taller than wide" is
/// a rule you can state in one sentence. A square work area resolves
/// HORIZONTAL, and so does a degenerate one, which is both the historical
/// behaviour and the only safe answer when the geometry says nothing.
///
/// Lives here, on ints rather than a QRect, because more than one process has
/// to answer the SAME question: the daemon's engine resolves it per layout
/// pass, and the editor resolves it to draw a template preview for a screen it
/// is not laying out. Two copies of a one-line rule is exactly how the two
/// answers drift apart.
inline constexpr ScrollAxis autoScrollAxisFor(int workAreaWidth, int workAreaHeight)
{
    return workAreaHeight > workAreaWidth ? ScrollAxis::Vertical : ScrollAxis::Horizontal;
}

} // namespace PhosphorProtocol
