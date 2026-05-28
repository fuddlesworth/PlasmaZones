// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// WindowRuleController label-lookup setters + wiring-completion gate.
// Split out so windowrulecontroller.cpp stays under the project's 800-line
// cap. The setters here are tiny pass-throughs (forward the resolver into
// WindowRuleModel + tick the lookup-wired bitmask), and they form a
// coherent sub-surface — every method either sets a label resolver or
// reports completion via `lookupsReady`. Same class, separate TU, no
// API change.

#include "windowrulecontroller.h"

namespace PlasmaZones {

void WindowRuleController::setScreenLookup(WindowRuleModel::LabelLookup fn)
{
    m_model.setScreenLabelLookup(std::move(fn));
    markLookupWired(LookupScreen);
}

void WindowRuleController::setActivityLookup(WindowRuleModel::LabelLookup fn)
{
    m_model.setActivityLabelLookup(std::move(fn));
    markLookupWired(LookupActivity);
}

void WindowRuleController::setLayoutLookup(WindowRuleModel::LabelLookup fn)
{
    // Back-compat: wire the same resolver into both split lookups. New
    // callers should prefer setSnappingLayoutLookup +
    // setTilingAlgorithmLookup so a UUID-shaped algorithm token (or
    // vice versa) doesn't silently cross-resolve.
    m_snappingLayoutLookup = fn;
    m_tilingAlgorithmLookup = std::move(fn);
    m_model.setLayoutLabelLookup(m_snappingLayoutLookup);
    markLookupWired(LookupSnappingLayout);
    markLookupWired(LookupTilingAlgorithm);
}

void WindowRuleController::setSnappingLayoutLookup(WindowRuleModel::LabelLookup fn)
{
    m_snappingLayoutLookup = std::move(fn);
    m_model.setSnappingLayoutLabelLookup(m_snappingLayoutLookup);
    markLookupWired(LookupSnappingLayout);
}

void WindowRuleController::setTilingAlgorithmLookup(WindowRuleModel::LabelLookup fn)
{
    m_tilingAlgorithmLookup = std::move(fn);
    m_model.setTilingAlgorithmLabelLookup(m_tilingAlgorithmLookup);
    markLookupWired(LookupTilingAlgorithm);
}

void WindowRuleController::markLookupWired(LookupBit bit)
{
    m_wiredLookups |= static_cast<unsigned>(bit);
    if (!m_lookupsReadyEmitted && (m_wiredLookups & AllLookups) == AllLookups) {
        m_lookupsReadyEmitted = true;
        Q_EMIT lookupsReady();
    }
}

} // namespace PlasmaZones
