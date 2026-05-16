// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QDate>
#include <QObject>

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace PhosphorShell {

/// Real-time clock for status bar widgets.
///
/// `hours`, `minutes` and `seconds` are always populated; `precision`
/// governs only how often the clock re-samples the wall clock (every
/// second, minute, or hour) — a coarser precision simply means the
/// finer fields refresh less often, never that they are invalid. The
/// timer is stopped entirely when `enabled` is false, so inactive
/// clocks consume zero CPU.
///
/// Usage from QML:
///
///     import Phosphor.Shell
///     SystemClock {
///         id: clock
///         precision: SystemClock.Minutes
///     }
///     Text { text: clock.hours + ":" + clock.minutes }
class PHOSPHORSHELL_EXPORT SystemClock : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(Precision precision READ precision WRITE setPrecision NOTIFY precisionChanged)
    Q_PROPERTY(int hours READ hours NOTIFY timeChanged)
    Q_PROPERTY(int minutes READ minutes NOTIFY timeChanged)
    Q_PROPERTY(int seconds READ seconds NOTIFY timeChanged)
    Q_PROPERTY(QDate date READ date NOTIFY dateChanged)

public:
    enum Precision {
        Hours,
        Minutes,
        Seconds
    };
    Q_ENUM(Precision)

    explicit SystemClock(QObject* parent = nullptr);
    ~SystemClock() override;

    [[nodiscard]] bool enabled() const;
    void setEnabled(bool enabled);

    [[nodiscard]] Precision precision() const;
    void setPrecision(Precision precision);

    [[nodiscard]] int hours() const;
    [[nodiscard]] int minutes() const;
    [[nodiscard]] int seconds() const;
    [[nodiscard]] QDate date() const;

Q_SIGNALS:
    void enabledChanged();
    void precisionChanged();
    void timeChanged();
    void dateChanged();

private:
    void update();
    void reconfigureTimer();
    void onTimerFired();

    QTimer* m_timer = nullptr;
    Precision m_precision = Seconds;
    bool m_enabled = true;
    int m_hours = -1;
    int m_minutes = -1;
    int m_seconds = -1;
    QDate m_date;
};

} // namespace PhosphorShell
