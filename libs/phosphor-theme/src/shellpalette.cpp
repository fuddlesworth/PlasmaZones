// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorTheme/ShellPalette.h>
#include <array>
#include <algorithm>
namespace PhosphorTheme {
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
