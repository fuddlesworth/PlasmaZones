// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// RuleController label-lookup setters. These are tiny pass-throughs that
// forward the resolver into RuleModel, and they form a coherent sub-surface.
// Same class as rulecontroller.cpp, separate TU, no API change.

#include "rulecontroller.h"

namespace PlasmaZones {

void RuleController::setScreenLookup(RuleModel::LabelLookup fn)
{
    m_model.setScreenLabelLookup(std::move(fn));
}

void RuleController::setActivityLookup(RuleModel::LabelLookup fn)
{
    m_model.setActivityLabelLookup(std::move(fn));
}

void RuleController::setZoneLookup(RuleModel::LabelLookup fn)
{
    m_model.setZoneLabelLookup(std::move(fn));
}

void RuleController::setVirtualDesktopLookup(RuleModel::LabelLookup fn)
{
    m_model.setVirtualDesktopLabelLookup(std::move(fn));
}

void RuleController::setSnappingLayoutLookup(RuleModel::LabelLookup fn)
{
    m_snappingLayoutLookup = std::move(fn);
    m_model.setSnappingLayoutLabelLookup(m_snappingLayoutLookup);
}

void RuleController::setTilingAlgorithmLookup(RuleModel::LabelLookup fn)
{
    m_tilingAlgorithmLookup = std::move(fn);
    m_model.setTilingAlgorithmLabelLookup(m_tilingAlgorithmLookup);
}

void RuleController::setShaderEffectLookup(RuleModel::LabelLookup fn)
{
    // Shader/curve are action-summary enhancements wired separately from the
    // match/placement resolvers — a rule whose resolver isn't installed yet
    // falls back to the raw id, then refreshes once it lands.
    m_model.setShaderEffectLabelLookup(std::move(fn));
}

void RuleController::setDecorationPackLookup(RuleModel::LabelLookup fn)
{
    // Same summary-enhancement wiring as setShaderEffectLookup, resolved
    // against the surface shader registry (decoration packs).
    m_model.setDecorationPackLabelLookup(std::move(fn));
}

void RuleController::setOverlayShaderLookup(RuleModel::LabelLookup fn)
{
    // Same separate-wiring rationale as setShaderEffectLookup above; the overlay
    // shader registry is distinct from the animation one.
    m_model.setOverlayShaderLabelLookup(std::move(fn));
}

void RuleController::setCurveLabelResolver(const QJSValue& resolver)
{
    // `m_curveResolver` is the whole of the resolver state — the lookup below
    // reads it live on every call and never captures it. So this assignment IS
    // the install, the swap AND the clear: passing a non-callable value leaves
    // the lookup wired but makes it return the raw wire string, which is the
    // documented "raw value shown" behaviour. There is deliberately no
    // `else { setCurveLabelLookup({}); }` branch — clearing the lookup would be
    // a second way to spell the same outcome, and one the live re-read below
    // already covers.
    m_curveResolver = resolver;
    if (resolver.isCallable()) {
        m_model.setCurveLabelLookup([this](const QString& wire) -> QString {
            // Re-read m_curveResolver live: a later setCurveLabelResolver()
            // swaps the closure without reinstalling this lambda.
            if (!m_curveResolver.isCallable()) {
                return wire;
            }
            QJSValue result = m_curveResolver.call(QJSValueList{QJSValue(wire)});
            if (result.isError() || result.isUndefined() || result.isNull()) {
                return wire;
            }
            const QString label = result.toString();
            return label.isEmpty() ? wire : label;
        });
    }
    // The resolver arrives from QML page init, which can run after the rule
    // set is already loaded — refresh so visible Curve labels pick it up.
    m_model.refreshLabels();
}

} // namespace PlasmaZones
