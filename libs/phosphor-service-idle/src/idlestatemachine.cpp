// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "idlestatemachine.h"

#include <algorithm>
#include <utility>

namespace PhosphorServiceIdle {

IdleStateMachine::IdleStateMachine(IdleSourceFactory factory, QObject* parent)
    : QObject(parent)
    , m_factory(std::move(factory))
{
}

IdleStateMachine::~IdleStateMachine() = default;

void IdleStateMachine::setStages(const QList<IdleStage>& stages)
{
    // Drop stages that could never arm (the facade already filters these, but the
    // machine is the reusable unit and a direct C++ caller might not).
    QList<IdleStage> next;
    next.reserve(stages.size());
    for (const IdleStage& stage : stages) {
        if (stage.timeout.count() > 0)
            next.push_back(stage);
    }
    // Sort by ascending timeout so the current-stage index is monotonic in
    // inactivity: source i (and thus stage i+1) always fires before source i+1.
    std::stable_sort(next.begin(), next.end(), [](const IdleStage& a, const IdleStage& b) {
        return a.timeout < b.timeout;
    });

    // A redundant reconfigure must not tear down the live compositor sources or
    // emit a spurious change.
    if (next == m_stages)
        return;

    m_stages = std::move(next);
    rebuild();
    Q_EMIT stagesChanged();
}

QList<IdleStage> IdleStateMachine::stages() const
{
    return m_stages;
}

int IdleStateMachine::currentStage() const
{
    return m_currentStage;
}

bool IdleStateMachine::isIdle() const
{
    return m_currentStage > 0;
}

QString IdleStateMachine::currentStageName() const
{
    if (m_currentStage <= 0 || m_currentStage > m_stages.size())
        return {};
    return m_stages.at(m_currentStage - 1).name;
}

void IdleStateMachine::setMonitoringEnabled(bool enabled)
{
    if (enabled == m_monitoringEnabled)
        return;
    m_monitoringEnabled = enabled;
    // rebuild() arms the sources when enabled, tears them down and resets to
    // active when disabled.
    rebuild();
}

bool IdleStateMachine::isMonitoringEnabled() const
{
    return m_monitoringEnabled;
}

void IdleStateMachine::rebuild()
{
    // Destroying the old sources disconnects their signals (they are unparented
    // unique_ptrs, so this also tears down their compositor objects).
    m_sources.clear();

    // While monitoring is disabled (idle is inhibited) no sources are armed, so
    // the compositor delivers no idle notifications.
    if (m_monitoringEnabled) {
        for (int i = 0; i < m_stages.size(); ++i) {
            IIdleSource::Ptr source = m_factory();
            if (!source)
                continue;
            source->setTimeout(m_stages.at(i).timeout);
            connect(source.get(), &IIdleSource::idled, this, [this, i] {
                onSourceIdled(i);
            });
            connect(source.get(), &IIdleSource::resumed, this, [this] {
                onSourceResumed();
            });
            m_sources.push_back(std::move(source));
        }
    }

    // Rebuilding clears any prior idle state: the new sources all start active.
    if (m_currentStage != 0) {
        m_currentStage = 0;
        Q_EMIT resumed();
        Q_EMIT currentStageChanged();
    }
}

void IdleStateMachine::onSourceIdled(int stageIndex)
{
    // Stage indices are 0-based; the reported stage is 1-based (0 == active).
    const int newStage = stageIndex + 1;
    if (newStage <= m_currentStage)
        return; // a shallower or equal stage; the deepest fired wins.
    m_currentStage = newStage;
    Q_EMIT idled(newStage);
    Q_EMIT currentStageChanged();
}

void IdleStateMachine::onSourceResumed()
{
    // Any source resuming means activity happened, which resets the whole ladder.
    // The first resume wins; later resumes from other fired sources are no-ops.
    if (m_currentStage == 0)
        return;
    m_currentStage = 0;
    Q_EMIT resumed();
    Q_EMIT currentStageChanged();
}

} // namespace PhosphorServiceIdle
