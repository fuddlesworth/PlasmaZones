// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorlayoutapi_export.h>

#include <QString>

namespace PhosphorLayout {

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
struct PHOSPHORLAYOUTAPI_EXPORT AlgorithmMetadata
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
    bool memory = false;

    /// True when the algorithm is loaded from a JS script file rather than
    /// a built-in C++ implementation. Affects how the picker renders the
    /// system-vs-user badge (see @c isUserScript).
    bool isScripted = false;

    /// True when the script lives in the user's local algorithms directory
    /// (vs system-installed). Only meaningful when @c isScripted is true.
    /// Drives the lock-icon vs user-icon badge in the picker.
    bool isUserScript = false;

    /// How zone numbers are displayed in previews — algorithm-specific
    /// hint passed through to the renderer. Values in current use:
    /// "all" (every zone numbered), "last" (only the trailing slot
    /// numbered), "" (renderer decides).
    QString zoneNumberDisplay;

    /// Whether this algorithm should show as a "system" entry (lock icon)
    /// in the picker. Built-in C++ algorithms are always system entries.
    /// Scripted algorithms are system entries only when system-installed
    /// (not user-script).
    bool isSystemEntry() const
    {
        if (!isScripted) {
            return true; // built-in C++
        }
        return !isUserScript;
    }
};

} // namespace PhosphorLayout
