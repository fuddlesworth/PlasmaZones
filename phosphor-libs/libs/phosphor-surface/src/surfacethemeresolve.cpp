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

    // Border colours. A neutral frame-contrast line wins over the system accent
    // when both are engaged. activeColor / inactiveColor are set unconditionally
    // (no packHasHalo-style declared-param guard): translateSurfaceParams only
    // emits a lane for params the pack declares, so a key inserted for a pack
    // that lacks it is dropped. The halo branch below guards because it has to
    // READ the existing colour to preserve its alpha; this branch writes fresh.
    if (effectiveFlag(QLatin1String("useThemeNeutral")).value_or(false)) {
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
    if (effectiveFlag(QLatin1String("useThemeTint")).value_or(false)) {
        for (const QLatin1String haloId : {QLatin1String("shadowColor"), QLatin1String("glowColor")}) {
            const QString key(haloId);
            bool packHasHalo = false;
            QVariant current;
            for (const auto& param : effect.parameters) {
                if (param.id == haloId) {
                    packHasHalo = true;
                    current = friendlyParams.contains(key) ? friendlyParams.value(key) : param.defaultValue;
                    break;
                }
            }
            if (packHasHalo) {
                QColor cur = current.value<QColor>();
                if (!cur.isValid()) {
                    cur = QColor(current.toString());
                }
                QColor tint = theme.background;
                tint.setAlphaF(cur.isValid() ? cur.alphaF() : 0.5);
                friendlyParams.insert(key, tint);
            }
        }
    }
}

} // namespace PhosphorSurfaceShaders
