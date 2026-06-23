// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorEngine/PlacementEngineBase.h>

namespace PhosphorEngine {

PlacementEngineBase::PlacementEngineBase(QObject* parent)
    : QObject(parent)
{
}

PlacementEngineBase::~PlacementEngineBase() = default;

int PlacementEngineBase::pruneStaleWindows(const QSet<QString>& aliveWindowIds)
{
    // The base keeps no per-window state since the unmanaged-geometry store was
    // collapsed into the unified WindowPlacementStore. Engines override this and
    // prune their own state (window→state maps, overflow, float markers).
    Q_UNUSED(aliveWindowIds)
    return 0;
}

void PlacementEngineBase::setEngineSettings(QObject* settings)
{
    if (Q_UNLIKELY(!settings)) {
        qWarning("PlacementEngineBase::setEngineSettings called with nullptr");
        return;
    }
    m_engineSettings = settings;
}

} // namespace PhosphorEngine
