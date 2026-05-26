// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorTheme/phosphortheme_export.h>

#include <QColor>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace PhosphorTheme {

// The set of token names a service must publish. Snake_case mirrors the
// canonical Phosphor palette (published at
// https://phosphor-works.github.io/palette/) and matugen's JSON output,
// so token strings round-trip without alias translation.
//
// This is the contract, shells, examples, and tests reference token names
// from here, not raw strings. New tokens go here first; QML / matugen
// templates / tests catch up afterwards.
struct PHOSPHORTHEME_EXPORT TokenNames
{
    // Surfaces (M3 surface ramp).
    static constexpr auto Background = "background";
    static constexpr auto Surface = "surface";
    static constexpr auto SurfaceContainer = "surface_container";
    static constexpr auto SurfaceContainerHigh = "surface_container_high";
    static constexpr auto SurfaceVariant = "surface_variant";
    static constexpr auto OnSurface = "on_surface";
    static constexpr auto OnSurfaceVariant = "on_surface_variant";

    // Accents (M3 primary / secondary / tertiary).
    static constexpr auto Primary = "primary";
    static constexpr auto OnPrimary = "on_primary";
    static constexpr auto PrimaryContainer = "primary_container";
    static constexpr auto OnPrimaryContainer = "on_primary_container";
    static constexpr auto Secondary = "secondary";
    static constexpr auto OnSecondary = "on_secondary";
    static constexpr auto SecondaryContainer = "secondary_container";
    static constexpr auto Tertiary = "tertiary";
    static constexpr auto OnTertiary = "on_tertiary";
    static constexpr auto TertiaryContainer = "tertiary_container";

    // Error.
    static constexpr auto Error = "error";
    static constexpr auto OnError = "on_error";
    static constexpr auto ErrorContainer = "error_container";

    // Outline.
    static constexpr auto Outline = "outline";
    static constexpr auto OutlineVariant = "outline_variant";

    // Status (drawn from the ANSI 16 ramp).
    static constexpr auto Success = "success";
    static constexpr auto SuccessBright = "success_bright";
    static constexpr auto Warning = "warning";
    static constexpr auto WarningBright = "warning_bright";
    static constexpr auto ErrorBright = "error_bright";
    static constexpr auto Info = "info";
    static constexpr auto InfoBright = "info_bright";

    // Brand-gradient stops, drive the connected-corner / accent gradients
    // throughout the shell. These are not Material 3 standard tokens; they
    // come from the [extensions.brand] block of the canonical Phosphor
    // palette (https://phosphor-works.github.io/palette/).
    static constexpr auto BrandStop0 = "brand_stop_0";
    static constexpr auto BrandStop1 = "brand_stop_1";
    static constexpr auto BrandStop2 = "brand_stop_2";
    static constexpr auto BrandStop3 = "brand_stop_3";
};

// Service contract for the active theme palette.
//
// A shell instantiates exactly one IThemeService (the default concrete
// implementation is `PaletteStore`) and exposes it to QML. Alternate
// implementations (test mocks, remote-driven services) plug in by
// satisfying this interface and registering the instance with the QML
// engine before `Phosphor.Theme` is imported.
//
// The interface stays pure-virtual to keep tests cheap: no QObject base,
// no signal/slot machinery exposed at the abstract layer. Concrete
// implementations bring their own QObject if they need notifications.
class PHOSPHORTHEME_EXPORT IThemeService
{
public:
    virtual ~IThemeService() = default;

    // Read the full token map (`token name` → QColor) for the active
    // palette. Stable order is not promised; callers iterate by key.
    [[nodiscard]] virtual QVariantMap palette() const = 0;

    // Look up a single token by name (one of TokenNames::*). Returns an
    // invalid color if the token isn't in the active palette, caller's
    // problem to handle, since "token missing" is a programming error.
    [[nodiscard]] virtual QColor token(const QString& name) const = 0;

    // Load a palette from a JSON byte array. Tokens missing from the JSON
    // are left at their current value (does NOT reset to default). Returns
    // true on parse success; false leaves the active palette untouched.
    virtual bool loadFromJson(const QByteArray& json) = 0;

    // Apply a parsed token map directly. Same merge semantics as
    // loadFromJson but skips the JSON round-trip, used by MatugenRunner
    // and any other in-process source that already has QColor values.
    // Empty maps are a no-op.
    virtual void applyTokens(const QVariantMap& tokens) = 0;

    // Load a palette from a JSON file path. Sets up a filesystem watch on
    // the path so subsequent on-disk edits hot-reload the palette. Returns
    // true on read+parse success.
    virtual bool loadFromFile(const QString& path) = 0;

    // Reset the palette to the built-in canonical defaults (Phosphor dark).
    // Stops any active filesystem watch.
    virtual void resetToDefaults() = 0;
};

} // namespace PhosphorTheme
