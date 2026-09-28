// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorTheme/ShellPalette.h>
#include <array>
#include <algorithm>
#include <cmath>
namespace PhosphorTheme {
namespace {
qreal luminance(const QColor& color)
{
    const auto channel = [](qreal c) {
        return c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4);
    };
    return .2126 * channel(color.redF()) + .7152 * channel(color.greenF()) + .0722 * channel(color.blueF());
}
QColor readable(const QColor& seed, const QList<QColor>& backgrounds, bool light)
{
    const qreal hue = std::max<qreal>(qreal(0), seed.hslHueF());
    const qreal saturation = std::min<qreal>(qreal(.72), seed.hslSaturationF());
    qreal level =
        light ? std::min<qreal>(qreal(.38), seed.lightnessF()) : std::max<qreal>(qreal(.72), seed.lightnessF());
    for (int step = 0; step < 40; ++step) {
        const auto candidate = QColor::fromHslF(hue, saturation, std::clamp(level, qreal(0), qreal(1)));
        const auto value = luminance(candidate);
        bool passes = true;
        for (const auto& background : backgrounds) {
            const auto other = luminance(background);
            passes &= (std::max(value, other) + .05) / (std::min(value, other) + .05) >= 4.5;
        }
        if (passes)
            return candidate;
        level += light ? -.02 : .02;
    }
    return light ? QColor(Qt::black) : QColor(Qt::white);
}
void tintWallpaper(ShellPalette& p, const QVariantMap& settings)
{
    const auto colors = settings.value(QStringLiteral("wallpaperColors")).toList();
    if (colors.size() != 4)
        return;
    const QColor seed(colors.first().toString());
    if (!seed.isValid())
        return;
    const bool light = settings.value(QStringLiteral("material")) == QStringLiteral("light");
    const auto hue = std::max<qreal>(qreal(0), seed.hslHueF());
    const auto saturation = std::min<qreal>(qreal(.38), seed.hslSaturationF() * 1.15);
    const auto tone = [hue, saturation](qreal level, qreal scale = 1) {
        return QColor::fromHslF(hue, saturation * scale, level);
    };
    p.surface = tone(light ? .952 : .135);
    p.card = tone(light ? .885 : .205, .86);
    p.recess = tone(light ? .916 : .085, .8);
    const QList<QColor> backgrounds{p.surface, p.card, p.recess};
    p.text = readable(tone(light ? .145 : .945, .6), backgrounds, light);
    p.muted = readable(tone(light ? .355 : .715, .65), backgrounds, light);
    p.outline = tone(light ? .28 : .76, .7);
    p.outline.setAlpha(light ? 48 : 43);
    p.opacity = settings.value(QStringLiteral("material")) == QStringLiteral("solid") ? 1 : light ? .96 : .95;
    p.stops.clear();
    for (const auto& color : colors)
        p.stops.append(readable(QColor(color.toString()), backgrounds, light));
}
}

ShellPalette ShellPalette::fromSettings(const QVariantMap& settings)
{
    const QString material = settings.value(QStringLiteral("material")).toString();
    const QString palette = settings.value(QStringLiteral("palette")).toString();
    const bool light = material == QStringLiteral("light");
    const bool ember = palette == QStringLiteral("ember");
    const bool wallpaper = palette == QStringLiteral("wallpaper");
    ShellPalette p;
    p.surface = QColor(light           ? QStringLiteral("#eef2fa")
                           : ember     ? QStringLiteral("#272620")
                           : wallpaper ? QStringLiteral("#222338")
                                       : QStringLiteral("#101d32"));
    p.card = QColor(light           ? QStringLiteral("#dce3ef")
                        : ember     ? QStringLiteral("#403c32")
                        : wallpaper ? QStringLiteral("#39394f")
                                    : QStringLiteral("#23314a"));
    p.recess = QColor(light           ? QStringLiteral("#e1e8f4")
                          : ember     ? QStringLiteral("#1c1e1b")
                          : wallpaper ? QStringLiteral("#181b2b")
                                      : QStringLiteral("#0b1528"));
    p.text = QColor(light ? QStringLiteral("#172740") : ember ? QStringLiteral("#eee9dc") : QStringLiteral("#e8eef9"));
    p.muted = QColor(light ? QStringLiteral("#4d607a") : ember ? QStringLiteral("#b6b1a4") : QStringLiteral("#97a8c0"));
    p.outline = QColor(light ? QStringLiteral("#28234064") : QStringLiteral("#1cb4c8f1"));
    p.opacity = light                         ? 0.93
        : material == QStringLiteral("glass") ? (palette == QStringLiteral("spectrum") ? 0.95 : 0.93)
                                              : 1;
    const QStringList colors = ember ? (light ? QStringList{QStringLiteral("#856127"), QStringLiteral("#8b5a34"),
                                                            QStringLiteral("#9d4e2f"), QStringLiteral("#984350")}
                                              : QStringList{QStringLiteral("#e8c988"), QStringLiteral("#d4b08a"),
                                                            QStringLiteral("#d3906c"), QStringLiteral("#d27b83")})
        : wallpaper                  ? (light ? QStringList{QStringLiteral("#49739d"), QStringLiteral("#6c63aa"),
                                                            QStringLiteral("#a2577d"), QStringLiteral("#955a39")}
                                              : QStringList{QStringLiteral("#93b6db"), QStringLiteral("#aeace6"),
                                                            QStringLiteral("#db9ab4"), QStringLiteral("#eabb9c")})
                                     : (light ? QStringList{QStringLiteral("#137b91"), QStringLiteral("#3567b3"),
                                                            QStringLiteral("#8154a8"), QStringLiteral("#a34e76")}
                                              : QStringList{QStringLiteral("#41d4e8"), QStringLiteral("#6e9cfd"),
                                                            QStringLiteral("#b68aee"), QStringLiteral("#f390b3")});
    for (const auto& color : colors)
        p.stops.append(QColor(color));
    if (wallpaper)
        tintWallpaper(p, settings);
    return p;
}
QVariantMap ShellPalette::toVariant() const
{
    QVariantList colors;
    for (const auto& color : stops)
        colors.append(color);
    return {{QStringLiteral("surface"), surface}, {QStringLiteral("card"), card},
            {QStringLiteral("recess"), recess},   {QStringLiteral("text"), text},
            {QStringLiteral("muted"), muted},     {QStringLiteral("outline"), outline},
            {QStringLiteral("opacity"), opacity}, {QStringLiteral("stops"), colors}};
}
QColor ShellPalette::windowColor(int index) const
{
    constexpr std::array<int, 4> order{0, 2, 3, 1};
    return stops.at(order.at(std::max(0, index) % order.size()));
}
}
