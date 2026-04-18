// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QLatin1String>
#include <QString>
#include <QStringLiteral>
#include <QStringView>

namespace PhosphorLayout {

/// How zone numbers are displayed in algorithm previews.
///
/// Internal typed enumeration used throughout the C++ codebase. String
/// encodings are still used at the wire/parse boundaries (JS script
/// front-matter, D-Bus @c AlgorithmInfoEntry, JSON serialisation, QML
/// property strings) — convert via @c zoneNumberDisplayToString and
/// @c zoneNumberDisplayFromString at those boundaries.
enum class ZoneNumberDisplay {
    RendererDecides, ///< Empty on the wire — algorithm defers to renderer default.
    All, ///< Every zone shows its number.
    Last, ///< Only the trailing zone is numbered.
    FirstAndLast, ///< First and last zones are numbered.
    None, ///< No zones are numbered.
};

/// Encode a ZoneNumberDisplay as its canonical wire string. @c
/// RendererDecides maps to an empty string so callers can check
/// @c QString::isEmpty() before emitting the field.
inline QString zoneNumberDisplayToString(ZoneNumberDisplay value)
{
    switch (value) {
    case ZoneNumberDisplay::All:
        return QStringLiteral("all");
    case ZoneNumberDisplay::Last:
        return QStringLiteral("last");
    case ZoneNumberDisplay::FirstAndLast:
        return QStringLiteral("firstAndLast");
    case ZoneNumberDisplay::None:
        return QStringLiteral("none");
    case ZoneNumberDisplay::RendererDecides:
        break;
    }
    return QString();
}

/// Decode a wire string back into a ZoneNumberDisplay. Forgiving:
/// unknown or empty strings map to @c RendererDecides so callers need
/// not pre-validate JSON / D-Bus input.
inline ZoneNumberDisplay zoneNumberDisplayFromString(QStringView text)
{
    if (text == QLatin1String("all")) {
        return ZoneNumberDisplay::All;
    }
    if (text == QLatin1String("last")) {
        return ZoneNumberDisplay::Last;
    }
    if (text == QLatin1String("firstAndLast")) {
        return ZoneNumberDisplay::FirstAndLast;
    }
    if (text == QLatin1String("none")) {
        return ZoneNumberDisplay::None;
    }
    return ZoneNumberDisplay::RendererDecides;
}

/// Capability + display metadata for a single autotile algorithm.
///
/// Embedded inside @c LayoutPreview as an optional field — only autotile
/// previews carry it. Manual zone-based previews leave the optional empty.
///
/// The fields here are limited to what a layout-picker UI needs to know
/// about an algorithm to render its row correctly (icons, "supports master
/// count" parameter editor, system-vs-user lock badge). Tuning parameters
/// that affect the algorithm's actual computation (split ratio, master
/// count) live in per-algorithm settings, not here.
struct AlgorithmMetadata
{
    /// True when the algorithm honours an explicit master-window count.
    /// Picker shows a count editor only when this is set.
    bool supportsMasterCount = false;

    /// True when the algorithm honours an explicit master/secondary split
    /// ratio. Picker shows a ratio slider only when this is set.
    bool supportsSplitRatio = false;

    /// True when the algorithm can produce overlapping zones (e.g.
    /// "stack" mode where the master area and stack area visually overlap).
    /// Picker may render an overlap badge.
    bool producesOverlappingZones = false;

    /// True when the algorithm declares custom @param annotations beyond
    /// the standard split/master/gap knobs. Picker offers a per-algorithm
    /// custom-params editor when set.
    bool supportsCustomParams = false;

    /// True when the algorithm carries persistent per-screen state across
    /// sessions (BSP-style trees that remember user splits). Picker may
    /// surface a "remembers your splits" indicator.
    bool supportsMemory = false;

    /// True when the algorithm is loaded from a JS script file rather than
    /// a built-in C++ implementation. Affects how the picker renders the
    /// system-vs-user badge (see @c isUserScript).
    bool isScripted = false;

    /// True when the script lives in the user's local algorithms directory
    /// (vs system-installed). Only meaningful when @c isScripted is true.
    /// Drives the lock-icon vs user-icon badge in the picker.
    bool isUserScript = false;

    /// How zone numbers are displayed in previews. Typed internally;
    /// converted to / from the wire-format string at JSON, D-Bus, and QML
    /// boundaries via @c zoneNumberDisplayToString /
    /// @c zoneNumberDisplayFromString.
    ZoneNumberDisplay zoneNumberDisplay = ZoneNumberDisplay::RendererDecides;
};

} // namespace PhosphorLayout
