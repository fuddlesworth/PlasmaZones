// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorEngine/LayerFocusSwitch.h>

namespace PhosphorEngine {

namespace {

QString pickTarget(const LayerSwitchSide& side)
{
    const auto eligible = [&side](const QString& id) {
        return !side.isEligible || side.isEligible(id);
    };
    if (!side.candidate.isEmpty() && eligible(side.candidate)) {
        return side.candidate;
    }
    // Fallback order is the caller's pool order (typically the sorted
    // floating set — arbitrary, carrying no recency meaning; no engine has
    // a frontmost-float notion to prefer). The emptiness guard mirrors the
    // candidate check: a blank pool entry sorts first, and returning it
    // would read as a no_target refusal despite valid entries behind it.
    for (const QString& id : side.fallbacks) {
        if (!id.isEmpty() && eligible(id)) {
            return id;
        }
    }
    return {};
}

} // namespace

LayerSwitchResult resolveLayerFocusSwitch(bool floatingHasFocus, const LayerSwitchSide& tiledSide,
                                          const LayerSwitchSide& floatingSide)
{
    LayerSwitchResult result;
    result.toTiled = floatingHasFocus;
    const LayerSwitchSide& targetSide = floatingHasFocus ? tiledSide : floatingSide;
    const LayerSwitchSide& sourceSide = floatingHasFocus ? floatingSide : tiledSide;
    result.source = sourceSide.focusForFeedback;
    result.target = pickTarget(targetSide);
    if (result.target.isEmpty()) {
        result.reason = QStringLiteral("no_target");
        return result;
    }
    result.success = true;
    result.reason = floatingHasFocus ? QStringLiteral("tiled") : QStringLiteral("floating");
    return result;
}

} // namespace PhosphorEngine
