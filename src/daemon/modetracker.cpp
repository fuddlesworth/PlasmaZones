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
    // Immediate save: mode changes are infrequent (user-initiated) and the data is
    // critical for correct session restore, so we write synchronously rather than
    // debouncing. KSharedConfig internally buffers writes.
    save();
    Q_EMIT currentModeChanged(mode);
    qCInfo(lcDaemon) << "Mode changed to" << (mode == TilingMode::Autotile ? "Autotile" : "Manual");
}

// toggleMode() emits two signals with distinct purposes:
//   - modeToggled:       action-oriented, indicates the user explicitly toggled
//   - currentModeChanged: state-oriented (emitted by setCurrentMode), reports the new state
// Slots connected to currentModeChanged must NOT call toggleMode() recursively,
// as this would cause infinite re-entrance through setCurrentMode -> emit -> toggle.
// The m_toggling guard prevents this at runtime.
TilingMode ModeTracker::toggleMode()
{
    if (m_toggling) {
        return m_currentMode;
    }
    m_toggling = true;
    TilingMode newMode = (m_currentMode == TilingMode::Manual) ? TilingMode::Autotile : TilingMode::Manual;
    setCurrentMode(newMode);
    Q_EMIT modeToggled(newMode);
    m_toggling = false;
    return newMode;
}

void ModeTracker::recordManualLayout(const QString& layoutId)
{
    if (layoutId.isEmpty()) {
        return;
    }

    if (m_lastManualLayoutId != layoutId) {
        m_lastManualLayoutId = layoutId;
        // Immediate save: mode changes are infrequent (user-initiated) and the data is
        // critical for correct session restore, so we write synchronously rather than
        // debouncing. KSharedConfig internally buffers writes.
        save();
        qCInfo(lcDaemon) << "Recorded manual layout=" << layoutId;
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
        // Immediate save: mode changes are infrequent (user-initiated) and the data is
        // critical for correct session restore, so we write synchronously rather than
        // debouncing. KSharedConfig internally buffers writes.
        save();
        qCInfo(lcDaemon) << "Recorded autotile algorithm=" << algorithmId;
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
    m_lastAutotileAlgorithm =
        group.readEntry(QStringLiteral("LastAutotileAlgorithm"), QString(DBus::AutotileAlgorithm::BSP));

    qCDebug(lcDaemon) << "ModeTracker loaded mode=" << static_cast<int>(m_currentMode)
                      << "lastLayout=" << m_lastManualLayoutId << "lastAlgorithm=" << m_lastAutotileAlgorithm;
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
