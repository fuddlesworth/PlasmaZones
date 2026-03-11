// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../autotileadaptor.h"

#include "autotile/AutotileConfig.h"
#include "autotile/AutotileEngine.h"

#include "core/constants.h"
#include "core/logging.h"

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════
// Helper Methods
// ═══════════════════════════════════════════════════════════════════════════

bool AutotileAdaptor::ensureEngine(const char* methodName) const
{
    if (!m_engine) {
        qCWarning(lcDbusAutotile) << "Cannot" << methodName << "- engine not available";
        return false;
    }
    return true;
}

bool AutotileAdaptor::ensureEngineAndConfig(const char* methodName) const
{
    if (!m_engine) {
        qCWarning(lcDbusAutotile) << "Cannot" << methodName << "- engine not available";
        return false;
    }
    if (!m_engine->config()) {
        qCWarning(lcDbusAutotile) << "Cannot" << methodName << "- config not available";
        return false;
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Property Accessors - Config values with DRY macros
// ═══════════════════════════════════════════════════════════════════════════

double AutotileAdaptor::masterRatio() const
{
    if (!m_engine || !m_engine->config()) {
        return AutotileDefaults::DefaultSplitRatio;
    }
    return m_engine->config()->splitRatio;
}

// NOTE: D-Bus property changes (masterRatio, masterCount, etc.) update runtime state
// only. They are NOT written back to KConfig/Settings. Per-screen state (including
// splitRatio) is persisted separately via TilingState save/load on daemon shutdown.
void AutotileAdaptor::setMasterRatio(double ratio)
{
    if (!ensureEngineAndConfig("setMasterRatio")) {
        return;
    }
    ratio = qBound(AutotileDefaults::MinSplitRatio, ratio, AutotileDefaults::MaxSplitRatio);
    if (!qFuzzyCompare(m_engine->config()->splitRatio, ratio)) {
        // Update config AND all per-screen TilingState objects (which algorithms use)
        m_engine->setGlobalSplitRatio(ratio);
        Q_EMIT configChanged();
    }
}

int AutotileAdaptor::masterCount() const
{
    if (!m_engine || !m_engine->config()) {
        return AutotileDefaults::DefaultMasterCount;
    }
    return m_engine->config()->masterCount;
}

void AutotileAdaptor::setMasterCount(int count)
{
    if (!ensureEngineAndConfig("setMasterCount")) {
        return;
    }
    count = qBound(AutotileDefaults::MinMasterCount, count, AutotileDefaults::MaxMasterCount);
    if (m_engine->config()->masterCount != count) {
        // Update config AND all per-screen TilingState objects (which algorithms use)
        m_engine->setGlobalMasterCount(count);
        Q_EMIT configChanged();
    }
}

int AutotileAdaptor::innerGap() const
{
    if (!m_engine || !m_engine->config()) {
        return AutotileDefaults::DefaultGap;
    }
    return m_engine->config()->innerGap;
}

void AutotileAdaptor::setInnerGap(int gap)
{
    if (!ensureEngineAndConfig("setInnerGap")) {
        return;
    }
    const int oldGap = m_engine->config()->innerGap;
    m_engine->setInnerGap(gap);
    if (m_engine->config()->innerGap != oldGap) {
        Q_EMIT configChanged();
    }
}

int AutotileAdaptor::outerGap() const
{
    if (!m_engine || !m_engine->config()) {
        return AutotileDefaults::DefaultGap;
    }
    return m_engine->config()->outerGap;
}

void AutotileAdaptor::setOuterGap(int gap)
{
    if (!ensureEngineAndConfig("setOuterGap")) {
        return;
    }
    const int oldGap = m_engine->config()->outerGap;
    m_engine->setOuterGap(gap);
    if (m_engine->config()->outerGap != oldGap) {
        Q_EMIT configChanged();
    }
}

bool AutotileAdaptor::smartGaps() const
{
    if (!m_engine || !m_engine->config()) {
        return AutotileDefaults::DefaultSmartGaps;
    }
    return m_engine->config()->smartGaps;
}

void AutotileAdaptor::setSmartGaps(bool enabled)
{
    if (!ensureEngineAndConfig("setSmartGaps")) {
        return;
    }
    const bool oldSmartGaps = m_engine->config()->smartGaps;
    m_engine->setSmartGaps(enabled);
    if (m_engine->config()->smartGaps != oldSmartGaps) {
        Q_EMIT configChanged();
    }
}

bool AutotileAdaptor::focusNewWindows() const
{
    if (!m_engine || !m_engine->config()) {
        return AutotileDefaults::DefaultFocusNewWindows;
    }
    return m_engine->config()->focusNewWindows;
}

void AutotileAdaptor::setFocusNewWindows(bool enabled)
{
    if (!ensureEngineAndConfig("setFocusNewWindows")) {
        return;
    }
    const bool oldFocusNewWindows = m_engine->config()->focusNewWindows;
    m_engine->setFocusNewWindows(enabled);
    if (m_engine->config()->focusNewWindows != oldFocusNewWindows) {
        Q_EMIT configChanged();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Ratio/Count Adjustment
// ═══════════════════════════════════════════════════════════════════════════

void AutotileAdaptor::increaseMasterRatio(double delta)
{
    if (!ensureEngine("increaseMasterRatio")) {
        return;
    }
    // Validate delta is positive and reasonable
    if (delta <= 0.0 || delta > 1.0) {
        qCWarning(lcDbusAutotile) << "increaseMasterRatio: invalid delta" << delta << "(must be > 0 and <= 1.0)";
        return;
    }
    qCDebug(lcDbusAutotile) << "increaseMasterRatio: delta=" << delta;
    // Note: This modifies per-screen TilingState, not global config.
    // tilingChanged signal is emitted by engine after retile.
    m_engine->increaseMasterRatio(delta);
}

void AutotileAdaptor::decreaseMasterRatio(double delta)
{
    if (!ensureEngine("decreaseMasterRatio")) {
        return;
    }
    // Validate delta is positive and reasonable
    if (delta <= 0.0 || delta > 1.0) {
        qCWarning(lcDbusAutotile) << "decreaseMasterRatio: invalid delta" << delta << "(must be > 0 and <= 1.0)";
        return;
    }
    qCDebug(lcDbusAutotile) << "decreaseMasterRatio: delta=" << delta;
    // Note: This modifies per-screen TilingState, not global config.
    // tilingChanged signal is emitted by engine after retile.
    m_engine->decreaseMasterRatio(delta);
}

void AutotileAdaptor::increaseMasterCount()
{
    if (!ensureEngine("increaseMasterCount")) {
        return;
    }
    qCDebug(lcDbusAutotile) << "increaseMasterCount";
    // Note: This modifies per-screen TilingState, not global config.
    // tilingChanged signal is emitted by engine after retile.
    m_engine->increaseMasterCount();
}

void AutotileAdaptor::decreaseMasterCount()
{
    if (!ensureEngine("decreaseMasterCount")) {
        return;
    }
    qCDebug(lcDbusAutotile) << "decreaseMasterCount";
    // Note: This modifies per-screen TilingState, not global config.
    // tilingChanged signal is emitted by engine after retile.
    m_engine->decreaseMasterCount();
}

} // namespace PlasmaZones
