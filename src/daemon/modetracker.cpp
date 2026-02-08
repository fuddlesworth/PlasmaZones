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

void ModeTracker::recordManualLayout(const QString& layoutId)
{
    if (layoutId.isEmpty()) {
        return;
    }

    if (m_lastManualLayoutId != layoutId) {
        m_lastManualLayoutId = layoutId;
        Q_EMIT lastManualLayoutIdChanged(layoutId);
    }

    save();
    qCDebug(lcDaemon) << "Recorded manual layout:" << layoutId;
}

void ModeTracker::recordManualLayout(const QUuid& layoutId)
{
    recordManualLayout(layoutId.toString());
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

    qCDebug(lcDaemon) << "ModeTracker loaded lastLayout= " << m_lastManualLayoutId;
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

    config->sync();
    qCDebug(lcDaemon) << "ModeTracker saved";
}

} // namespace PlasmaZones
