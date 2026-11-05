// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PresetPalettes.h"

#include <PhosphorTheme/IThemeService.h>
#include <PhosphorTheme/PaletteStore.h>

#include <QColor>
#include <QLatin1String>

namespace PhosphorThemeDemo {

using PhosphorTheme::TokenNames;

namespace {

// Helper to keep the palette literals readable. Each preset reads as
// a single curly block instead of three dozen `p.insert(...)` calls.
struct ColorEntry
{
    const char* name;
    const char* hex;
};

QVariantMap buildPalette(std::initializer_list<ColorEntry> entries)
{
    QVariantMap p;
    for (const auto& e : entries) {
        p.insert(QLatin1String(e.name), QColor(QLatin1String(e.hex)));
    }
    return p;
}

} // namespace

PresetPalettes::PresetPalettes(QObject* parent)
    : QObject(parent)
{
}

QVariantMap PresetPalettes::dark() const
{
    // The canonical Phosphor dark is the built-in default. Read it from
    // the lib's stateless accessor so we never duplicate the hex values
    // here, and so this getter stays free of QFileSystemWatcher / inotify
    // allocations (which a full PaletteStore would incur on every call).
    return PhosphorTheme::PaletteStore::defaultPalette();
}

QVariantMap PresetPalettes::light() const
{
    return buildPalette({
        {TokenNames::Background, "#FAFAFE"},
        {TokenNames::Surface, "#F4F4FB"},
        {TokenNames::SurfaceContainer, "#ECECF5"},
        {TokenNames::SurfaceContainerHigh, "#E0E0EE"},
        {TokenNames::SurfaceVariant, "#D8D8E6"},
        {TokenNames::OnSurface, "#1A1A2E"},
        {TokenNames::OnSurfaceVariant, "#5C5C7A"},
        {TokenNames::Primary, "#2563EB"},
        {TokenNames::OnPrimary, "#FFFFFF"},
        {TokenNames::PrimaryContainer, "#DBEAFE"},
        {TokenNames::OnPrimaryContainer, "#1E3A8A"},
        {TokenNames::Secondary, "#9333EA"},
        {TokenNames::OnSecondary, "#FFFFFF"},
        {TokenNames::SecondaryContainer, "#F3E8FF"},
        {TokenNames::Tertiary, "#0891B2"},
        {TokenNames::OnTertiary, "#FFFFFF"},
        {TokenNames::TertiaryContainer, "#CFFAFE"},
        {TokenNames::Error, "#DC2626"},
        {TokenNames::OnError, "#FFFFFF"},
        {TokenNames::ErrorContainer, "#FEE2E2"},
        {TokenNames::Outline, "#2563EB"},
        {TokenNames::OutlineVariant, "#C7D2FE"},
        {TokenNames::Success, "#059669"},
        {TokenNames::SuccessBright, "#10B981"},
        {TokenNames::Warning, "#D97706"},
        {TokenNames::WarningBright, "#F59E0B"},
        {TokenNames::ErrorBright, "#EF4444"},
        {TokenNames::Info, "#0891B2"},
        {TokenNames::InfoBright, "#06B6D4"},
        {TokenNames::BrandStop0, "#06B6D4"},
        {TokenNames::BrandStop1, "#2563EB"},
        {TokenNames::BrandStop2, "#9333EA"},
        {TokenNames::BrandStop3, "#DC2626"},
    });
}

QVariantMap PresetPalettes::sunset() const
{
    return buildPalette({
        {TokenNames::Background, "#1A0F1A"},
        {TokenNames::Surface, "#2D1B26"},
        {TokenNames::SurfaceContainer, "#1F1018"},
        {TokenNames::SurfaceContainerHigh, "#3A2733"},
        {TokenNames::SurfaceVariant, "#4A3340"},
        {TokenNames::OnSurface, "#FFE8DC"},
        {TokenNames::OnSurfaceVariant, "#C9A99A"},
        {TokenNames::Primary, "#FB923C"},
        {TokenNames::OnPrimary, "#1A0F0A"},
        {TokenNames::PrimaryContainer, "#9A3412"},
        {TokenNames::OnPrimaryContainer, "#FED7AA"},
        {TokenNames::Secondary, "#F472B6"},
        {TokenNames::OnSecondary, "#1A0810"},
        {TokenNames::SecondaryContainer, "#831843"},
        {TokenNames::Tertiary, "#FBBF24"},
        {TokenNames::OnTertiary, "#1A0F0A"},
        {TokenNames::TertiaryContainer, "#78350F"},
        {TokenNames::Error, "#EF4444"},
        {TokenNames::OnError, "#1A0808"},
        {TokenNames::ErrorContainer, "#7F1D1D"},
        {TokenNames::Outline, "#FB923C"},
        {TokenNames::OutlineVariant, "#9A3412"},
        {TokenNames::Success, "#84CC16"},
        {TokenNames::SuccessBright, "#A3E635"},
        {TokenNames::Warning, "#FBBF24"},
        {TokenNames::WarningBright, "#FCD34D"},
        {TokenNames::ErrorBright, "#F87171"},
        {TokenNames::Info, "#FB923C"},
        {TokenNames::InfoBright, "#FDBA74"},
        {TokenNames::BrandStop0, "#FBBF24"},
        {TokenNames::BrandStop1, "#FB923C"},
        {TokenNames::BrandStop2, "#F472B6"},
        {TokenNames::BrandStop3, "#EF4444"},
    });
}

QVariantMap PresetPalettes::forest() const
{
    return buildPalette({
        {TokenNames::Background, "#0A1F0F"},
        {TokenNames::Surface, "#142718"},
        {TokenNames::SurfaceContainer, "#0E1F12"},
        {TokenNames::SurfaceContainerHigh, "#1A3322"},
        {TokenNames::SurfaceVariant, "#1F4030"},
        {TokenNames::OnSurface, "#DDFFEB"},
        {TokenNames::OnSurfaceVariant, "#94B89F"},
        {TokenNames::Primary, "#22C55E"},
        {TokenNames::OnPrimary, "#051006"},
        {TokenNames::PrimaryContainer, "#14532D"},
        {TokenNames::OnPrimaryContainer, "#BBF7D0"},
        {TokenNames::Secondary, "#84CC16"},
        {TokenNames::OnSecondary, "#050B02"},
        {TokenNames::SecondaryContainer, "#365314"},
        {TokenNames::Tertiary, "#06B6D4"},
        {TokenNames::OnTertiary, "#051A1F"},
        {TokenNames::TertiaryContainer, "#164E63"},
        {TokenNames::Error, "#EF4444"},
        {TokenNames::OnError, "#FFFFFF"},
        {TokenNames::ErrorContainer, "#7F1D1D"},
        {TokenNames::Outline, "#22C55E"},
        {TokenNames::OutlineVariant, "#14532D"},
        {TokenNames::Success, "#22C55E"},
        {TokenNames::SuccessBright, "#4ADE80"},
        {TokenNames::Warning, "#EAB308"},
        {TokenNames::WarningBright, "#FACC15"},
        {TokenNames::ErrorBright, "#F87171"},
        {TokenNames::Info, "#06B6D4"},
        {TokenNames::InfoBright, "#22D3EE"},
        {TokenNames::BrandStop0, "#06B6D4"},
        {TokenNames::BrandStop1, "#22C55E"},
        {TokenNames::BrandStop2, "#84CC16"},
        {TokenNames::BrandStop3, "#EAB308"},
    });
}

QStringList PresetPalettes::names() const
{
    return {QStringLiteral("dark"), QStringLiteral("light"), QStringLiteral("sunset"), QStringLiteral("forest")};
}

QVariantMap PresetPalettes::byName(const QString& name) const
{
    if (name == QLatin1String("dark"))
        return dark();
    if (name == QLatin1String("light"))
        return light();
    if (name == QLatin1String("sunset"))
        return sunset();
    if (name == QLatin1String("forest"))
        return forest();
    return {};
}

} // namespace PhosphorThemeDemo
