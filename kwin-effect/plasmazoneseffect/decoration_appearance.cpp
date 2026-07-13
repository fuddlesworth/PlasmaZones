// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Plain window-appearance resolution for the decoration layers: the
// per-window merge of the config-backed appearance defaults (border,
// opacity+tint, title bar — each gated by its own scope token) with the
// per-slot rule overrides resolveWindowAppearance produced. Split out of
// decorations.cpp to keep that TU under the 800-line limit, mirroring the
// surface_backdrop.cpp split; updateWindowDecoration remains the sole
// consumer path.

#include "../plasmazoneseffect.h"

#include "../autotilehandler.h"
#include "shader_resolve.h"
#include "window_query.h"

#include <effect/effectwindow.h>

#include <PhosphorCompositor/DecorationDefaults.h>
#include <PhosphorProtocol/WindowTypeEnum.h>
#include <PhosphorRules/RuleAction.h>

#include <QColor>
#include <QString>

#include <optional>

namespace PlasmaZones {

namespace {

// Resolve a config-default border-colour string against the live system colour
// its `accent` sentinel maps to. Mirrors resolveWindowAppearance's rule colour
// path: the sentinel yields @p systemColor (nullopt when that colour is not yet
// known), an empty string contributes nothing, anything else parses as hex.
std::optional<QColor> resolveDefaultBorderColor(const QString& value, const QColor& systemColor)
{
    if (value.isEmpty()) {
        return std::nullopt;
    }
    if (value == PhosphorRules::BorderColorToken::Accent) {
        return systemColor.isValid() ? std::optional<QColor>(systemColor) : std::nullopt;
    }
    const QColor color(value);
    return color.isValid() ? std::optional<QColor>(color) : std::nullopt;
}

} // namespace

bool PlasmaZonesEffect::windowMatchesAppearanceScope(const QString& scope, KWin::EffectWindow* w,
                                                     const QString& windowId) const
{
    if (!w) {
        return false;
    }
    namespace WAS = PhosphorCompositor::WindowAppearanceScope;
    if (scope == WAS::All) {
        return true;
    }
    if (scope == WAS::Tiled) {
        // A window occupying a snap zone OR managed by the autotile engine. Use
        // each engine's render-marked set (populated synchronously on commit),
        // NOT the NavigationHandler zone cache: markWindowSnapped builds the border
        // synchronously before the async windowStateChanged that fills that cache
        // lands, so reading it here would miss a just-snapped window's default
        // border until the next full sweep. The autotile half is already symmetric.
        return isWindowMarkedSnapped(windowId) || m_autotileHandler->isTiledWindow(windowId);
    }
    if (scope == WAS::Normal) {
        return windowTypeFor(w) == PhosphorProtocol::WindowType::Normal && !windowIsTransient(w);
    }
    // Unknown / empty token: contribute no default (the settings side validates
    // the token to one of the three above).
    return false;
}

ResolvedWindowAppearance PlasmaZonesEffect::resolveEffectiveWindowAppearance(KWin::EffectWindow* w,
                                                                             const QString& windowId) const
{
    // Start from the user rule appearance (per-slot optionals). resolveWindowAppearance
    // returns nullopt when no rule fills any slot, including the empty-rule-set case;
    // an empty struct then carries only the config-default fills below.
    std::optional<ResolvedWindowAppearance> ruleOvr;
    if (w && !m_shaderManager.animationRuleSet().isEmpty()) {
        ruleOvr = resolveWindowAppearance(resolveRuleActions(w, windowId), m_borderAccentColor, m_borderInactiveColor);
    }
    ResolvedWindowAppearance out = ruleOvr.value_or(ResolvedWindowAppearance{});

    const WindowAppearanceDefault& def = m_windowAppearanceDefault;

    // Border slots: fill each slot the rules left unset from the config default,
    // but only when the window matches the border scope. An engaged rule slot
    // (even a rule value of false / 0) is left untouched — rules win per slot.
    if (windowMatchesAppearanceScope(def.borderScope, w, windowId)) {
        if (!out.showBorder) {
            out.showBorder = def.showBorder;
        }
        if (!out.borderWidth) {
            out.borderWidth = def.borderWidth;
        }
        if (!out.borderRadius) {
            out.borderRadius = def.borderRadius;
        }
        if (!out.activeColor) {
            out.activeColor = resolveDefaultBorderColor(def.activeColor, m_borderAccentColor);
        }
        if (!out.inactiveColor) {
            out.inactiveColor = resolveDefaultBorderColor(def.inactiveColor, m_borderInactiveColor);
        }
    }
    // Mirror the rule path: an unset inactive colour falls back to the active one
    // so an active-only default keeps its border colour when the window unfocuses.
    if (!out.inactiveColor) {
        out.inactiveColor = out.activeColor;
    }

    // Plain opacity+tint layer slots: same per-slot fill as the border above,
    // gated by the layer's own scope. An engaged rule slot (including a
    // SetOpacityTintVisible=false veto) is left untouched. `opacity` has no
    // rule slot, so the config value always fills it here when in scope.
    if (windowMatchesAppearanceScope(def.opacityTintScope, w, windowId)) {
        if (!out.showOpacityTint) {
            out.showOpacityTint = def.showOpacityTint;
        }
        if (!out.opacity) {
            out.opacity = def.opacity;
        }
        if (!out.tintStrength) {
            out.tintStrength = def.tintStrength;
        }
        if (!out.tintColor) {
            out.tintColor = resolveDefaultBorderColor(def.tintColor, m_borderAccentColor);
        }
    }

    // Title-bar slot: the config default only ever contributes a HIDE (true).
    // A config value of false means "no opinion" (show the native title bar),
    // NOT a force-show veto — that veto semantic (engaged false) is reserved for
    // an explicit SetHideTitleBar=false rule, so leave the slot unset when the
    // default is off. Fill only when the default hides AND the window is in scope.
    if (!out.hideTitleBar && def.hideTitleBar && windowMatchesAppearanceScope(def.titleBarScope, w, windowId)) {
        out.hideTitleBar = true;
    }
    return out;
}

} // namespace PlasmaZones
