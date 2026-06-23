// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Canonical Phosphor default palette, dark variant.
//
// Source: https://phosphor-works.github.io/palette/ (CC-BY-SA 4.0).
// Material 3 + ANSI 16 + brand-gradient extensions on deep navy.
//
// This must compile without runtime dependencies, it's the bootstrap
// the shell falls back to when no palette JSON is on disk.

#include "defaultpalette.h"

#include <PhosphorTheme/IThemeService.h>

#include <QColor>
#include <QLatin1String>

namespace PhosphorTheme::detail {

QVariantMap defaultDarkPalette()
{
    QVariantMap p;

    // Surfaces, void → navy → abyss → variants. on_surface_variant is the
    // muted text color used for chrome that shouldn't compete with content.
    p.insert(QLatin1String(TokenNames::Background), QColor(QLatin1String("#050916")));
    p.insert(QLatin1String(TokenNames::Surface), QColor(QLatin1String("#0B1730")));
    p.insert(QLatin1String(TokenNames::SurfaceContainer), QColor(QLatin1String("#070F22")));
    p.insert(QLatin1String(TokenNames::SurfaceContainerHigh), QColor(QLatin1String("#101A36")));
    p.insert(QLatin1String(TokenNames::SurfaceVariant), QColor(QLatin1String("#1E293B")));
    p.insert(QLatin1String(TokenNames::OnSurface), QColor(QLatin1String("#E6EDFF")));
    p.insert(QLatin1String(TokenNames::OnSurfaceVariant), QColor(QLatin1String("#94A3B8")));

    // Primary (blue-500).
    p.insert(QLatin1String(TokenNames::Primary), QColor(QLatin1String("#3B82F6")));
    p.insert(QLatin1String(TokenNames::OnPrimary), QColor(QLatin1String("#F0F9FF")));
    p.insert(QLatin1String(TokenNames::PrimaryContainer), QColor(QLatin1String("#1E3A8A")));
    p.insert(QLatin1String(TokenNames::OnPrimaryContainer), QColor(QLatin1String("#DBEAFE")));

    // Secondary (purple-500).
    p.insert(QLatin1String(TokenNames::Secondary), QColor(QLatin1String("#A855F7")));
    p.insert(QLatin1String(TokenNames::OnSecondary), QColor(QLatin1String("#FAF5FF")));
    p.insert(QLatin1String(TokenNames::SecondaryContainer), QColor(QLatin1String("#581C87")));

    // Tertiary (cyan-400).
    p.insert(QLatin1String(TokenNames::Tertiary), QColor(QLatin1String("#22D3EE")));
    p.insert(QLatin1String(TokenNames::OnTertiary), QColor(QLatin1String("#ECFEFF")));
    p.insert(QLatin1String(TokenNames::TertiaryContainer), QColor(QLatin1String("#164E63")));

    // Error (rose-500).
    p.insert(QLatin1String(TokenNames::Error), QColor(QLatin1String("#F43F5E")));
    p.insert(QLatin1String(TokenNames::OnError), QColor(QLatin1String("#FFF1F2")));
    p.insert(QLatin1String(TokenNames::ErrorContainer), QColor(QLatin1String("#881337")));

    // Outline, blue against navy for active focus rings, dimmer
    // primary_container for inactive separators.
    p.insert(QLatin1String(TokenNames::Outline), QColor(QLatin1String("#3B82F6")));
    p.insert(QLatin1String(TokenNames::OutlineVariant), QColor(QLatin1String("#1E3A8A")));

    // Status, ANSI 16 standard + bright variants.
    p.insert(QLatin1String(TokenNames::Success), QColor(QLatin1String("#10B981")));
    p.insert(QLatin1String(TokenNames::SuccessBright), QColor(QLatin1String("#34D399")));
    p.insert(QLatin1String(TokenNames::Warning), QColor(QLatin1String("#FBBF24")));
    p.insert(QLatin1String(TokenNames::WarningBright), QColor(QLatin1String("#FCD34D")));
    p.insert(QLatin1String(TokenNames::ErrorBright), QColor(QLatin1String("#FB7185")));
    p.insert(QLatin1String(TokenNames::Info), QColor(QLatin1String("#22D3EE")));
    p.insert(QLatin1String(TokenNames::InfoBright), QColor(QLatin1String("#67E8F9")));

    // Brand-gradient stops, cyan → blue → purple → rose, the signature
    // four-color sweep used on connected-corner accents and shader
    // backgrounds.
    p.insert(QLatin1String(TokenNames::BrandStop0), QColor(QLatin1String("#22D3EE")));
    p.insert(QLatin1String(TokenNames::BrandStop1), QColor(QLatin1String("#3B82F6")));
    p.insert(QLatin1String(TokenNames::BrandStop2), QColor(QLatin1String("#A855F7")));
    p.insert(QLatin1String(TokenNames::BrandStop3), QColor(QLatin1String("#F43F5E")));

    return p;
}

} // namespace PhosphorTheme::detail
