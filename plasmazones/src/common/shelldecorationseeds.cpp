// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "shelldecorationseeds.h"
#include "config/configdefaults.h"
#include "config/settings.h"

#include <PhosphorSurface/DecorationSupportedPaths.h>
#include <PhosphorTheme/AppearanceWatcher.h>
#include <PhosphorTheme/ShellPalette.h>

namespace PlasmaZones {
namespace {
QString withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(alpha);
    return color.name(QColor::HexArgb);
}
}

PhosphorSurfaceShaders::DecorationProfileTree shellDecorationSeedTree(const QVariantMap& appearance,
                                                                      bool desktopStyleActive)
{
    using namespace PhosphorSurfaceShaders;
    auto seeds = ConfigDefaults::decorationProfileTree();
    const auto palette = PhosphorTheme::ShellPalette::fromSettings(appearance);
    const int radius = appearance.value(QStringLiteral("radius")).toInt();
    if (desktopStyleActive) {
        DecorationProfile window;
        window.chain = QStringList{QStringLiteral("border"), QStringLiteral("top-rail"), QStringLiteral("shadow")};
        const QColor accent = palette.windowColor(0);
        QVariantMap params{{QStringLiteral("border"),
                            QVariantMap{{QStringLiteral("borderWidth"), 1},
                                        {QStringLiteral("cornerRadius"), radius},
                                        {QStringLiteral("edgeSoftness"), .7},
                                        {QStringLiteral("squareWhenMaximized"), true},
                                        {QStringLiteral("useSystemAccent"), false},
                                        {QStringLiteral("useThemeNeutral"), false},
                                        {QStringLiteral("useWindowAccent"), true},
                                        {QStringLiteral("activeColor"), withAlpha(accent, .70)},
                                        {QStringLiteral("inactiveColor"), withAlpha(accent, .40)}}},
                           {QStringLiteral("top-rail"),
                            QVariantMap{{QStringLiteral("activeHeight"), 2},
                                        {QStringLiteral("inactiveHeight"), 1},
                                        {QStringLiteral("cornerRadius"), radius},
                                        {QStringLiteral("squareWhenMaximized"), true},
                                        {QStringLiteral("railInset"), 0},
                                        {QStringLiteral("edgeSoftness"), .7},
                                        {QStringLiteral("activeColor"), withAlpha(accent, .90)},
                                        {QStringLiteral("activeCenterColor"), withAlpha(palette.text, .90)},
                                        {QStringLiteral("inactiveColor"), withAlpha(accent, .45)}}},
                           {QStringLiteral("shadow"),
                            QVariantMap{{QStringLiteral("shadowSize"), 36},
                                        {QStringLiteral("hideWhenMaximized"), true},
                                        {QStringLiteral("shadowStrength"), .85},
                                        {QStringLiteral("offsetX"), 0},
                                        {QStringLiteral("offsetY"), 12},
                                        {QStringLiteral("cornerRadius"), radius},
                                        {QStringLiteral("shadowColor"), withAlpha(palette.recess.darker(150), .60)},
                                        {QStringLiteral("useThemeTint"), false}}}};
        if (appearance.value(QStringLiteral("glow")).toBool()) {
            window.chain->append(QStringLiteral("glow"));
            params.insert(QStringLiteral("glow"),
                          QVariantMap{{QStringLiteral("glowSize"), 36},
                                      {QStringLiteral("hideWhenMaximized"), true},
                                      {QStringLiteral("glowStrength"), .14},
                                      {QStringLiteral("cornerRadius"), radius},
                                      {QStringLiteral("glowColor"), accent.name(QColor::HexArgb)},
                                      {QStringLiteral("useThemeTint"), false},
                                      {QStringLiteral("useWindowAccent"), true}});
        }
        window.parameters = params;
        seeds.setOverride(QStringLiteral("window"), window);
    }

    const auto effect = appearance.value(QStringLiteral("surfaceEffect")).toString();
    if (!appearance.value(QStringLiteral("surfacePacks")).toBool()
        || (effect != QLatin1String("glass") && effect != QLatin1String("motes")))
        return seeds;
    const QString pack = QStringLiteral("phosphor-") + effect;
    for (const auto& path : decorationShellPhosphorLeafPaths()) {
        DecorationProfile surface;
        surface.chain = QStringList{pack};
        const bool translucent = path == decorationShellPhosphorBarPath() || path == decorationShellPhosphorPickerPath()
            || path == decorationShellPhosphorPopoutPath();
        surface.parameters = QVariantMap{{pack,
                                          QVariantMap{{QStringLiteral("colorCyan"), palette.stops[0].name()},
                                                      {QStringLiteral("colorBlue"), palette.stops[1].name()},
                                                      {QStringLiteral("colorPurple"), palette.stops[2].name()},
                                                      {QStringLiteral("colorRose"), palette.stops[3].name()},
                                                      {QStringLiteral("colorTint"), palette.surface.name()},
                                                      {QStringLiteral("cornerRadius"), radius},
                                                      {QStringLiteral("contentOpacity"), translucent ? .94 : 1.0}}}};
        seeds.setOverride(path, surface);
    }
    return seeds;
}

void bindShellDecorationSeeds(Settings& settings, PhosphorTheme::AppearanceWatcher& appearance)
{
    const auto refresh = [&settings, &appearance] {
        settings.setDecorationSeedTree(shellDecorationSeedTree(appearance.values(), appearance.desktopStyleActive()));
    };
    QObject::connect(&appearance, &PhosphorTheme::AppearanceWatcher::changed, &settings, refresh);
    refresh();
}

void installShellDecorationSeeds(Settings& settings)
{
    auto* appearance = new PhosphorTheme::AppearanceWatcher(&settings);
    bindShellDecorationSeeds(settings, *appearance);
}
}
