// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Internal (not installed) multi-stage idle state machine. It composes a ladder
// of single-timeout idle sources (one per stage) into a single monotonic state:
// as inactivity grows, each stage's source fires `idled()` in timeout order and
// advances the current stage; the first `resumed()` from any source resets the
// whole ladder to active. Pure logic over IIdleSource, so it is unit-tested with
// a fake source and no live compositor.

#include "iidlesource.h"

#include <QList>
#include <QObject>
#include <QString>

#include <chrono>
#include <vector>

namespace PhosphorServiceIdle {

/// One step in the idle ladder: a display name and the inactivity timeout at
/// which it fires.
struct IdleStage
{
    QString name;
    std::chrono::milliseconds timeout;

    bool operator==(const IdleStage& other) const = default;
};

class IdleStateMachine : public QObject
{
    Q_OBJECT

public:
    /// @p factory creates a bare idle source per stage; the machine arms each
    /// with its stage timeout. Injected so tests pass a factory of fakes.
    explicit IdleStateMachine(IdleSourceFactory factory, QObject* parent = nullptr);
    ~IdleStateMachine() override;

    /// Replace the stage ladder. Stages with a non-positive timeout are dropped
    /// (they could never arm), then sorted by ascending timeout so the
    /// current-stage index is always monotonic in inactivity. A no-op when the
    /// resulting ladder is unchanged; otherwise rebuilds the sources (resetting
    /// the machine to active) and emits `stagesChanged`.
    void setStages(const QList<IdleStage>& stages);
    [[nodiscard]] QList<IdleStage> stages() const;

    /// Did every stage of the current ladder actually arm? False for an EMPTY ladder too:
    /// nothing is watching, so nothing can report. A caller that armed a ladder and gets
    /// false back knows its idle detection is dead and can rebuild rather than wait forever
    /// for an edge that will never come.
    [[nodiscard]] bool isArmed() const;

    /// 0 when active; otherwise the 1-based index of the deepest stage currently
    /// fired.
    [[nodiscard]] int currentStage() const;
    [[nodiscard]] bool isIdle() const;
    /// Name of the current stage, or an empty string when active.
    [[nodiscard]] QString currentStageName() const;

    /// Arm or disarm idle monitoring. While disabled the sources are torn down
    /// (so the compositor delivers no idle notifications) and the machine reports
    /// active; re-enabling re-arms them. The facade disables monitoring while idle
    /// is inhibited.
    void setMonitoringEnabled(bool enabled);
    [[nodiscard]] bool isMonitoringEnabled() const;

Q_SIGNALS:
    /// The configured stage ladder changed (dropped/sorted form).
    void stagesChanged();
    void currentStageChanged();
    /// Entered idle stage @p stage (1-based).
    void idled(int stage);
    /// Returned to active from any idle stage.
    void resumed();

private:
    void rebuild();
    void onSourceIdled(int stageIndex);
    void onSourceResumed();

    IdleSourceFactory m_factory;
    QList<IdleStage> m_stages;
    std::vector<IIdleSource::Ptr> m_sources;
    int m_currentStage = 0;
    bool m_monitoringEnabled = true;
};

} // namespace PhosphorServiceIdle
