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
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, &SystemClock::update);
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
    reconfigureTimer();
    if (m_enabled)
        update();
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
    reconfigureTimer();
    update();
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
    const QTime now = QTime::currentTime();
    const QDate today = QDate::currentDate();
    bool timeChanged = false;

    const int h = now.hour();
    const int m = now.minute();
    const int s = now.second();

    if (m_hours != h) {
        m_hours = h;
        timeChanged = true;
    }
    if (m_precision >= Minutes && m_minutes != m) {
        m_minutes = m;
        timeChanged = true;
    }
    if (m_precision >= Seconds && m_seconds != s) {
        m_seconds = s;
        timeChanged = true;
    }

    if (timeChanged)
        Q_EMIT this->timeChanged();

    if (m_date != today) {
        m_date = today;
        Q_EMIT dateChanged();
    }

    if (m_enabled && (m_precision == Hours || m_precision == Minutes))
        reconfigureTimer();
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
    case Hours: {
        int msUntilNextHour = ((59 - now.minute()) * 60 + (60 - now.second())) * 1000 - now.msec();
        intervalMs = qMax(msUntilNextHour, 1000);
        m_timer->setSingleShot(true);
        m_timer->start(intervalMs);
        return;
    }
    case Minutes: {
        int msUntilNextMinute = (60 - now.second()) * 1000 - now.msec();
        intervalMs = qMax(msUntilNextMinute, 500);
        m_timer->setSingleShot(true);
        m_timer->start(intervalMs);
        return;
    }
    case Seconds:
        intervalMs = 1000;
        break;
    }
    m_timer->setSingleShot(false);
    m_timer->start(intervalMs);
}

} // namespace PhosphorShell
