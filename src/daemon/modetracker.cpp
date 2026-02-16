// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modetracker.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include "../config/settings.h"
#include "../core/constants.h"
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
    save();
    Q_EMIT currentModeChanged(mode);
    qCInfo(lcDaemon) << "Mode changed to:" << (mode == TilingMode::Autotile ? "Autotile" : "Manual");
}

TilingMode ModeTracker::toggleMode()
{
    TilingMode newMode = (m_currentMode == TilingMode::Manual) ? TilingMode::Autotile : TilingMode::Manual;
    setCurrentMode(newMode);
    Q_EMIT modeToggled(newMode);
    return newMode;
}

void ModeTracker::recordManualLayout(const QString& layoutId)
{
    if (layoutId.isEmpty()) {
        return;
    }

    if (m_lastManualLayoutId != layoutId) {
        m_lastManualLayoutId = layoutId;
        save();
        qCInfo(lcDaemon) << "Recorded manual layout:" << layoutId;
    }
}

void ModeTracker::recordManualLayout(const QUuid& layoutId)
{
    recordManualLayout(layoutId.toString());
}

void ModeTracker::recordAutotileAlgorithm(const QString& algorithmId)
{
    if (algorithmId.isEmpty()) {
        return;
    }

    if (m_lastAutotileAlgorithm != algorithmId) {
        m_lastAutotileAlgorithm = algorithmId;
        Q_EMIT lastAutotileAlgorithmChanged(algorithmId);
        save();
        qCInfo(lcDaemon) << "Recorded autotile algorithm:" << algorithmId;
    }
}

void ModeTracker::load()
{
    if (!m_settings) {
        qCWarning(lcDaemon) << "ModeTracker::load() called without settings";
        return;
    }

    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup group = config->group(QStringLiteral("ModeTracking"));

    m_lastManualLayoutId = group.readEntry(QStringLiteral("LastManualLayoutId"), QString());
    const int rawMode = group.readEntry(QStringLiteral("LastTilingMode"), 0);
    m_currentMode = (rawMode == 1) ? TilingMode::Autotile : TilingMode::Manual;
    m_lastAutotileAlgorithm = group.readEntry(QStringLiteral("LastAutotileAlgorithm"),
                                               QStringLiteral("master-stack"));

    qCDebug(lcDaemon) << "ModeTracker loaded mode=" << static_cast<int>(m_currentMode)
                       << "lastLayout=" << m_lastManualLayoutId
                       << "lastAlgorithm=" << m_lastAutotileAlgorithm;
}

void ModeTracker::save()
{
    if (!m_settings) {
        qCWarning(lcDaemon) << "ModeTracker::save() called without settings";
        return;
    }

    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup group = config->group(QStringLiteral("ModeTracking"));

    group.writeEntry(QStringLiteral("LastManualLayoutId"), m_lastManualLayoutId);
    group.writeEntry(QStringLiteral("LastTilingMode"), static_cast<int>(m_currentMode));
    group.writeEntry(QStringLiteral("LastAutotileAlgorithm"), m_lastAutotileAlgorithm);

    config->sync();
    qCDebug(lcDaemon) << "ModeTracker saved";
}

} // namespace PlasmaZones
