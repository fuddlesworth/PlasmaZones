// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/SystemClock.h>

#include <QDateTime>
#include <QTimer>

namespace PhosphorShell {

SystemClock::SystemClock(QObject* parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    // Single-shot at every precision: each tick is aligned to the next
    // wall-clock boundary and re-armed in onTimerFired(), so the clock
    // never drifts away from the boundary the way a fixed-interval
    // repeating timer would.
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &SystemClock::onTimerFired);
    update();
    reconfigureTimer();
}

SystemClock::~SystemClock() = default;

bool SystemClock::enabled() const
{
    return m_enabled;
}

void SystemClock::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    Q_EMIT enabledChanged();
    if (m_enabled)
        update();
    reconfigureTimer();
}

SystemClock::Precision SystemClock::precision() const
{
    return m_precision;
}

void SystemClock::setPrecision(Precision precision)
{
    if (m_precision == precision)
        return;
    m_precision = precision;
    Q_EMIT precisionChanged();
    // Field values do not depend on precision (update() always populates
    // all three) — only the tick cadence changes, so just re-arm.
    reconfigureTimer();
}

int SystemClock::hours() const
{
    return m_hours;
}

int SystemClock::minutes() const
{
    return m_minutes;
}

int SystemClock::seconds() const
{
    return m_seconds;
}

QDate SystemClock::date() const
{
    return m_date;
}

void SystemClock::update()
{
    // Single wall-clock read — taking QTime and QDate separately would
    // let the two land on different sides of a midnight boundary.
    const QDateTime nowDt = QDateTime::currentDateTime();
    const QTime now = nowDt.time();
    const QDate today = nowDt.date();

    bool anyTimeChanged = false;
    if (m_hours != now.hour()) {
        m_hours = now.hour();
        anyTimeChanged = true;
    }
    if (m_minutes != now.minute()) {
        m_minutes = now.minute();
        anyTimeChanged = true;
    }
    if (m_seconds != now.second()) {
        m_seconds = now.second();
        anyTimeChanged = true;
    }
    if (anyTimeChanged)
        Q_EMIT timeChanged();

    if (m_date != today) {
        m_date = today;
        Q_EMIT dateChanged();
    }
}

void SystemClock::reconfigureTimer()
{
    if (!m_enabled) {
        m_timer->stop();
        return;
    }
    const QTime now = QTime::currentTime();
    int intervalMs = 1000;
    switch (m_precision) {
    case Hours:
        intervalMs = ((59 - now.minute()) * 60 + (60 - now.second())) * 1000 - now.msec();
        break;
    case Minutes:
        intervalMs = (60 - now.second()) * 1000 - now.msec();
        break;
    case Seconds:
        intervalMs = 1000 - now.msec();
        break;
    }
    // A near-boundary computation can yield a tiny or zero interval;
    // never arm below 1 ms — the following tick re-aligns cleanly.
    m_timer->start(qMax(intervalMs, 1));
}

void SystemClock::onTimerFired()
{
    update();
    // The timer is single-shot — re-arm for the next aligned boundary.
    reconfigureTimer();
}

} // namespace PhosphorShell
