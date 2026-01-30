// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modetracker.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include "../config/settings.h"
#include "../core/logging.h"

namespace PlasmaZones {

ModeTracker::ModeTracker(Settings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
}

ModeTracker::~ModeTracker() = default;

void ModeTracker::setCurrentMode(TilingMode mode)
{
    if (m_currentMode == mode) {
        return;
    }

    m_currentMode = mode;
    Q_EMIT currentModeChanged(mode);

    // Auto-save when mode changes
    save();
}

ModeTracker::TilingMode ModeTracker::toggleMode()
{
    TilingMode newMode = (m_currentMode == TilingMode::Manual)
        ? TilingMode::Autotile
        : TilingMode::Manual;

    setCurrentMode(newMode);

    // Emit toggle signal with relevant ID
    QString relevantId = (newMode == TilingMode::Autotile)
        ? m_lastAutotileAlgorithm
        : m_lastManualLayoutId;

    Q_EMIT modeToggled(newMode, relevantId);

    qCInfo(lcDaemon) << "Mode toggled to"
                     << (newMode == TilingMode::Autotile ? "Autotile" : "Manual")
                     << "- relevant ID:" << relevantId;

    return newMode;
}

void ModeTracker::recordManualLayout(const QString& layoutId)
{
    if (layoutId.isEmpty()) {
        return;
    }

    if (m_lastManualLayoutId != layoutId) {
        m_lastManualLayoutId = layoutId;
        Q_EMIT lastManualLayoutIdChanged(layoutId);
    }

    // Set mode to manual when recording a manual layout
    setCurrentMode(TilingMode::Manual);

    qCDebug(lcDaemon) << "Recorded manual layout:" << layoutId;
}

void ModeTracker::recordManualLayout(const QUuid& layoutId)
{
    // Use WithoutBraces for consistent UUID string format
    recordManualLayout(layoutId.toString(QUuid::WithoutBraces));
}

void ModeTracker::recordAutotileAlgorithm(const QString& algorithmId)
{
    if (algorithmId.isEmpty()) {
        return;
    }

    if (m_lastAutotileAlgorithm != algorithmId) {
        m_lastAutotileAlgorithm = algorithmId;
        Q_EMIT lastAutotileAlgorithmChanged(algorithmId);
    }

    // Set mode to autotile when recording an algorithm
    setCurrentMode(TilingMode::Autotile);

    qCDebug(lcDaemon) << "Recorded autotile algorithm:" << algorithmId;
}

void ModeTracker::load()
{
    if (!m_settings) {
        qCWarning(lcDaemon) << "ModeTracker::load() called without settings";
        return;
    }

    // Load from KConfig settings
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup group = config->group(QStringLiteral("ModeTracking"));

    int modeInt = group.readEntry(QStringLiteral("LastTilingMode"), 0);
    m_currentMode = (modeInt == 1) ? TilingMode::Autotile : TilingMode::Manual;

    m_lastManualLayoutId = group.readEntry(QStringLiteral("LastManualLayoutId"), QString());
    m_lastAutotileAlgorithm = group.readEntry(QStringLiteral("LastAutotileAlgorithm"), QStringLiteral("master-stack"));

    qCInfo(lcDaemon) << "ModeTracker loaded - mode:"
                     << (m_currentMode == TilingMode::Autotile ? "Autotile" : "Manual")
                     << "lastLayout:" << m_lastManualLayoutId
                     << "lastAlgorithm:" << m_lastAutotileAlgorithm;
}

void ModeTracker::save()
{
    if (!m_settings) {
        return;
    }

    // Save to KConfig settings
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup group = config->group(QStringLiteral("ModeTracking"));

    group.writeEntry(QStringLiteral("LastTilingMode"), static_cast<int>(m_currentMode));
    group.writeEntry(QStringLiteral("LastManualLayoutId"), m_lastManualLayoutId);
    group.writeEntry(QStringLiteral("LastAutotileAlgorithm"), m_lastAutotileAlgorithm);

    config->sync();

    qCDebug(lcDaemon) << "ModeTracker saved";
}

} // namespace PlasmaZones
