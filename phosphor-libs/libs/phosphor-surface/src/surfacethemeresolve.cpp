// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSurface/SurfaceThemeResolve.h>

#include <PhosphorSurface/SurfaceShaderEffect.h>

#include <QLatin1String>
#include <QVariant>

#include <optional>

namespace PhosphorSurfaceShaders {

namespace {

/// Linear interpolation between two colours in [0,1] RGB, opaque result. Mirrors
/// Kirigami.ColorUtils.linearInterpolation (PopupFrame's built-in border used it).
QColor lerpColor(const QColor& a, const QColor& b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t, 1.0);
}

} // namespace

void resolveThemeParamColors(const SurfaceShaderEffect& effect, QVariantMap& friendlyParams,
                             const SurfaceThemeColors& theme)
{
    // Effective value of a bool param: the caller's override, else the pack's
    // declared default; nullopt when the pack has no such param.
    const auto effectiveFlag = [&](QLatin1String id) -> std::optional<bool> {
        for (const auto& param : effect.parameters) {
            if (param.id == id) {
                const QString key(id);
                return friendlyParams.contains(key) ? friendlyParams.value(key).toBool() : param.defaultValue.toBool();
            }
        }
        return std::nullopt;
    };
    // Effective value of a real-valued param: the caller's override, else the
    // pack's declared default, else @p fallback.
    const auto effectiveReal = [&](QLatin1String id, double fallback) -> double {
        for (const auto& param : effect.parameters) {
            if (param.id == id) {
                const QString key(id);
                return friendlyParams.contains(key) ? friendlyParams.value(key).toDouble()
                                                    : param.defaultValue.toDouble();
            }
        }
        return fallback;
    };
    // Replace only a declared colour's RGB. Its alpha remains the pack/user's
    // intensity, including zero, and an omitted override uses the pack default.
    const auto tintDeclaredColor = [&](QLatin1String id, const QColor& tint) {
        for (const auto& param : effect.parameters) {
            if (param.id != id) {
                continue;
            }
            const QString key(id);
            const QVariant current = friendlyParams.value(key, param.defaultValue);
            QColor color = current.value<QColor>();
            if (!color.isValid()) {
                color = QColor(current.toString());
            }
            QColor resolved = tint;
            resolved.setAlphaF(color.isValid() ? color.alphaF() : 0.5);
            friendlyParams.insert(key, resolved);
            break;
        }
    };
    const bool useWindowAccent =
        theme.windowAccent.isValid() && effectiveFlag(QLatin1String("useWindowAccent")).value_or(false);

    // Window identity takes precedence when requested and available. Otherwise
    // a neutral frame-contrast line wins over the system accent. The existing
    // neutral/system modes write fresh opaque colours; identity preserves each
    // declared colour's alpha so focused and unfocused opacities remain distinct.
    if (useWindowAccent) {
        tintDeclaredColor(QLatin1String("activeColor"), theme.windowAccent);
        tintDeclaredColor(QLatin1String("inactiveColor"), theme.windowAccent);
    } else if (effectiveFlag(QLatin1String("useThemeNeutral")).value_or(false)) {
        const QColor neutral = lerpColor(theme.background, theme.foreground,
                                         qBound(0.0, effectiveReal(QLatin1String("frameContrast"), 0.2), 1.0));
        friendlyParams.insert(QStringLiteral("activeColor"), neutral);
        friendlyParams.insert(QStringLiteral("inactiveColor"), neutral);
    } else if (effectiveFlag(QLatin1String("useSystemAccent")).value_or(false)) {
        QColor accent = theme.accent;
        accent.setAlphaF(1.0);
        QColor inactive = theme.inactive;
        inactive.setAlphaF(1.0);
        friendlyParams.insert(QStringLiteral("activeColor"), accent);
        friendlyParams.insert(QStringLiteral("inactiveColor"), inactive);
    }

    // Halo (glow / shadow) tint: colour the halo with the theme background so it
    // tracks light / dark instead of a fixed colour. The background is low-chroma,
    // so this reads as a soft theme-matched shadow rather than an additive colour
    // smear. The pack's own colour alpha (its intensity knob) is preserved.
    const bool useThemeTint = effectiveFlag(QLatin1String("useThemeTint")).value_or(false);
    if (useThemeTint) {
        tintDeclaredColor(QLatin1String("shadowColor"), theme.background);
    }
    if (useWindowAccent) {
        tintDeclaredColor(QLatin1String("glowColor"), theme.windowAccent);
    } else if (useThemeTint) {
        tintDeclaredColor(QLatin1String("glowColor"), theme.background);
    }
}

} // namespace PhosphorSurfaceShaders
