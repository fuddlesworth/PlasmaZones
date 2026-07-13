// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceIdle/IdleService.h>

#include "idleinhibitionmanager.h"
#include "idlenotifiersource.h"
#include "idlestatemachine.h"

#include <PhosphorWayland/IdleNotifier.h>

#include <QVariantMap>

#include <chrono>
#include <memory>

namespace PhosphorServiceIdle {

namespace {

QList<IdleStage> toStages(const QVariantList& list)
{
    QList<IdleStage> stages;
    stages.reserve(list.size());
    for (const QVariant& entry : list) {
        const QVariantMap map = entry.toMap();
        const int ms = map.value(StageKey::TimeoutMs).toInt();
        if (ms <= 0)
            continue; // a non-positive timeout has no meaning; drop it.
        stages.push_back(IdleStage{map.value(StageKey::Name).toString(), std::chrono::milliseconds(ms)});
    }
    return stages;
}

QVariantList fromStages(const QList<IdleStage>& stages)
{
    QVariantList list;
    list.reserve(stages.size());
    for (const IdleStage& stage : stages) {
        QVariantMap map;
        map.insert(StageKey::Name, stage.name);
        map.insert(StageKey::TimeoutMs, static_cast<int>(stage.timeout.count()));
        list.push_back(map);
    }
    return list;
}

} // namespace

class IdleService::Private
{
public:
    // The production factory hands the state machine an IdleNotifierSource per
    // stage (an ext-idle-notify-v1 client). Tests of the machine inject fakes
    // directly; the facade always uses the real source.
    IdleStateMachine machine;
    IdleInhibitionManager inhibition;

    Private()
        : machine([]() -> IIdleSource::Ptr {
            return std::make_unique<IdleNotifierSource>();
        })
    {
    }
};

IdleService::IdleService(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    connect(&d->machine, &IdleStateMachine::stagesChanged, this, &IdleService::stagesChanged);
    connect(&d->machine, &IdleStateMachine::currentStageChanged, this, &IdleService::currentStageChanged);
    connect(&d->machine, &IdleStateMachine::idled, this, &IdleService::idled);
    connect(&d->machine, &IdleStateMachine::resumed, this, &IdleService::resumed);

    // Inhibition pauses idle monitoring: while any cookie is held the ladder is
    // disarmed and reports active.
    connect(&d->inhibition, &IdleInhibitionManager::inhibitedChanged, this, [this](bool inhibited) {
        d->machine.setMonitoringEnabled(!inhibited);
        Q_EMIT inhibitedChanged();
    });
}

IdleService::~IdleService() = default;

bool IdleService::isSupported() const
{
    // Idle detection is the core of the service; the compositor must advertise
    // ext-idle-notify-v1 (surfaced by PhosphorWayland::IdleNotifier) for the
    // service to do anything.
    return PhosphorWayland::IdleNotifier::isSupported();
}

QVariantList IdleService::stages() const
{
    return fromStages(d->machine.stages());
}

void IdleService::setStages(const QVariantList& stages)
{
    // The machine emits stagesChanged (forwarded above) only when the ladder
    // actually changes, so this does not emit directly.
    d->machine.setStages(toStages(stages));
}

bool IdleService::isArmed() const
{
    return d->machine.isArmed();
}

int IdleService::currentStage() const
{
    return d->machine.currentStage();
}

QString IdleService::currentStageName() const
{
    return d->machine.currentStageName();
}

bool IdleService::isIdle() const
{
    return d->machine.isIdle();
}

bool IdleService::isInhibited() const
{
    return d->inhibition.isInhibited();
}

int IdleService::inhibit()
{
    return d->inhibition.inhibit();
}

bool IdleService::release(int cookie)
{
    return d->inhibition.release(cookie);
}

} // namespace PhosphorServiceIdle
