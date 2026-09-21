// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorEngine/PlacementEngineBase.h>

#include <PhosphorEngine/LayerFocusSwitch.h>
#include <PhosphorEngine/PerScreenStates.h>

#include <QLoggingCategory>

// Filterable like every other diagnostic in this library (the bare qWarning
// it replaces could not be silenced per-category).
Q_LOGGING_CATEGORY(lcPlacementEngineBase, "org.phosphor.engine.placementbase")

namespace PhosphorEngine {

// Defined here rather than in a file of its own: PerScreenStates is a
// header-only template with no TU to host it. INSIDE the namespace, matching
// the exported declaration in that header — a definition at global scope
// declares a different function, and the template's use of it would resolve to
// the namespaced declaration nothing then defines.
Q_LOGGING_CATEGORY(lcPerScreenStates, "org.phosphor.engine.perscreenstates")

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

void PlacementEngineBase::announceLayerSwitch(const LayerSwitchResult& result, const QString& action,
                                              const QString& screenId)
{
    if (!result.success) {
        Q_EMIT navigationFeedback(false, action, result.reason, result.source, QString(), screenId);
        return;
    }
    Q_EMIT activateWindowRequested(result.target);
    Q_EMIT navigationFeedback(true, action, result.reason, result.source, result.target, screenId);
}

void PlacementEngineBase::setEngineSettings(QObject* settings)
{
    if (Q_UNLIKELY(!settings)) {
        qCWarning(lcPlacementEngineBase, "PlacementEngineBase::setEngineSettings called with nullptr");
        return;
    }
    m_engineSettings = settings;
}

} // namespace PhosphorEngine
