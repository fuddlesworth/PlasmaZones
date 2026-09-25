// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/SystemUsage.h>

#include <QByteArray>
#include <QFile>
#include <QLoggingCategory>
#include <QList>
#include <QString>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {
Q_LOGGING_CATEGORY(lcUsage, "phosphorshell.systemusage")

// Round an already-computed percentage into 0..100. The callers reject
// non-finite parsed FIELDS and guard the divisors, but never test the
// computed percentage itself, so this isfinite check is the finiteness
// guard of record: static_cast<int> of a NaN is undefined behaviour.
int roundedPercent(double value)
{
    if (!std::isfinite(value)) {
        return 0;
    }
    if (value < 0) {
        return 0;
    }
    if (value > 100) {
        return 100;
    }
    return static_cast<int>(value + 0.5);
}

// Read a small /proc file whole. QFile::readAll on procfs returns the
// snapshot the kernel generates at open, which is what a sample wants.
bool readProcFile(const QString& path, QByteArray* out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    *out = file.readAll();
    return true;
}
} // namespace

namespace PhosphorShell {

bool SystemUsage::parseCpuTotals(const QByteArray& content, CpuTotals* out)
{
    if (!out) {
        return false;
    }

    const qsizetype newline = content.indexOf('\n');
    const QByteArray firstLine = content.left(newline < 0 ? content.size() : newline);
    if (!firstLine.startsWith("cpu ")) {
        return false;
    }

    // "cpu  user nice system idle iowait irq softirq steal guest guest_nice"
    const QList<QByteArray> fields = firstLine.simplified().split(' ');
    // fields[0] is the "cpu" label, so the four required counters
    // (user, nice, system, idle) put the minimum length at 5.
    if (fields.size() < 5) {
        return false;
    }

    // Stop at steal (index 8). The kernel already counts guest inside user
    // and guest_nice inside nice, so summing them again inflates the total
    // and understates busy% on any host running VMs.
    constexpr qsizetype kLastCountedField = 8;
    const qsizetype last = std::min<qsizetype>(fields.size() - 1, kLastCountedField);

    CpuTotals totals;
    for (qsizetype i = 1; i <= last; ++i) {
        bool ok = false;
        const double value = fields.at(i).toDouble(&ok);
        // toDouble accepts "nan"/"inf" with ok == true, so the finite check
        // is a separate condition rather than a redundant one.
        if (!ok || !std::isfinite(value)) {
            return false;
        }
        totals.total += value;
        // idle = idle + iowait (fields 4 and 5 counting the label).
        if (i == 4 || i == 5) {
            totals.idle += value;
        }
    }

    *out = totals;
    return true;
}

bool SystemUsage::parseMemoryTotals(const QByteArray& content, MemoryTotals* out)
{
    if (!out) {
        return false;
    }

    MemoryTotals totals;
    bool haveTotal = false;
    bool haveAvailable = false;

    const QList<QByteArray> lines = content.split('\n');
    for (const QByteArray& raw : lines) {
        const QByteArray line = raw.trimmed();
        const bool isTotal = line.startsWith("MemTotal:");
        const bool isAvailable = line.startsWith("MemAvailable:");
        if (!isTotal && !isAvailable) {
            continue;
        }
        // "MemTotal:  16311472 kB" — take the field after the label rather
        // than a fixed column, so extra inner spacing is tolerated.
        const QList<QByteArray> parts = line.simplified().split(' ');
        if (parts.size() < 2) {
            continue;
        }
        bool ok = false;
        const double value = parts.at(1).toDouble(&ok);
        if (!ok || !std::isfinite(value)) {
            continue;
        }
        if (isTotal) {
            totals.totalKb = value;
            haveTotal = true;
        } else {
            totals.availableKb = value;
            haveAvailable = true;
            break; // MemAvailable follows MemTotal; nothing else is needed.
        }
    }

    // MemAvailable is required: without it (kernel < 3.14, exotic build)
    // availableKb would stay 0 and report a bogus 100% used.
    if (!haveTotal || !haveAvailable || totals.totalKb <= 0) {
        return false;
    }

    *out = totals;
    return true;
}

int SystemUsage::cpuPercentBetween(const CpuTotals& prev, const CpuTotals& current)
{
    const double deltaTotal = current.total - prev.total;
    const double deltaIdle = current.idle - prev.idle;
    // A non-positive delta means the counters did not advance, or went
    // backwards. Either way there is no rate to report.
    if (!(deltaTotal > 0)) {
        return -1;
    }
    return roundedPercent((1.0 - deltaIdle / deltaTotal) * 100.0);
}

SystemUsage::SystemUsage(QObject* parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setInterval(m_interval);
    connect(m_timer, &QTimer::timeout, this, &SystemUsage::sample);
    if (m_enabled) {
        m_timer->start();
        // Baseline immediately so memory is populated on the first frame
        // and the CPU delta has a reference before the first tick.
        //
        // `enabled` defaults true, and QML applies a declared `enabled:
        // false` only after construction, so such a consumer still pays for
        // this one pair of /proc reads before the timer stops. That is
        // harmless (two small reads, no publish worth acting on) and the
        // later enable re-baselines through setEnabled anyway.
        sample();
    }
}

SystemUsage::~SystemUsage() = default;

bool SystemUsage::enabled() const
{
    return m_enabled;
}

void SystemUsage::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    if (m_enabled) {
        m_timer->start();
        // Drop the stale baseline: without this the first tick after a
        // pause reports the busy fraction averaged over the entire
        // disabled gap, presented as one interval's worth.
        restartBaseline();
    } else {
        m_timer->stop();
    }
    Q_EMIT enabledChanged();
}

void SystemUsage::restartBaseline()
{
    m_haveCpuBaseline = false;
    sample();
}

int SystemUsage::interval() const
{
    return m_interval;
}

void SystemUsage::setInterval(int intervalMs)
{
    // Clamp rather than ignore, so the property agrees with the binding that
    // wrote it instead of silently reading back the old value.
    const int effective = std::max(intervalMs, MinIntervalMs);
    if (m_interval == effective) {
        // Nothing changes, so say nothing: a binding that re-evaluates to the
        // same sub-floor value would otherwise warn on every write.
        return;
    }
    if (intervalMs < MinIntervalMs) {
        qCWarning(lcUsage) << "interval" << intervalMs << "ms is below the" << MinIntervalMs << "ms floor; clamping";
    }
    m_interval = effective;
    m_timer->setInterval(m_interval);
    Q_EMIT intervalChanged();
}

int SystemUsage::cpuPercent() const
{
    return m_cpuPercent;
}

int SystemUsage::memoryPercent() const
{
    return m_memoryPercent;
}

void SystemUsage::sample()
{
    sampleCpu();
    sampleMemory();
}

void SystemUsage::sampleCpu()
{
    QByteArray content;
    if (!readProcFile(QStringLiteral("/proc/stat"), &content)) {
        if (!m_warnedCpuRead) {
            m_warnedCpuRead = true;
            qCWarning(lcUsage) << "cannot read /proc/stat; CPU usage stays at its last value";
        }
        // Drop the baseline for the same reason a pause does: the counters
        // keep advancing while we cannot read them, so the first successful
        // sample after the outage would otherwise publish the busy fraction
        // averaged over the whole gap as if it covered one interval. The
        // deliberate cost is that a source failing on alternate ticks never
        // yields a delta at all, freezing the readout rather than publishing
        // a wrong one.
        m_haveCpuBaseline = false;
        return;
    }
    m_warnedCpuRead = false;

    CpuTotals current;
    if (!parseCpuTotals(content, &current)) {
        if (!m_warnedCpuParse) {
            m_warnedCpuParse = true;
            qCWarning(lcUsage) << "unexpected /proc/stat layout; CPU usage stays at its last value";
        }
        m_haveCpuBaseline = false;
        return;
    }
    m_warnedCpuParse = false;

    if (m_haveCpuBaseline) {
        // -1 means the pair yielded no usable rate; re-baseline silently and
        // let the next tick produce one.
        const int percent = cpuPercentBetween(m_prev, current);
        if (percent >= 0 && percent != m_cpuPercent) {
            m_cpuPercent = percent;
            Q_EMIT cpuPercentChanged();
        }
    }
    m_prev = current;
    m_haveCpuBaseline = true;
}

void SystemUsage::sampleMemory()
{
    QByteArray content;
    if (!readProcFile(QStringLiteral("/proc/meminfo"), &content)) {
        if (!m_warnedMemoryRead) {
            m_warnedMemoryRead = true;
            qCWarning(lcUsage) << "cannot read /proc/meminfo; memory usage stays at its last value";
        }
        return;
    }
    m_warnedMemoryRead = false;

    MemoryTotals totals;
    if (!parseMemoryTotals(content, &totals)) {
        if (!m_warnedMemoryParse) {
            m_warnedMemoryParse = true;
            qCWarning(lcUsage) << "unexpected /proc/meminfo layout; memory usage stays at its last value";
        }
        return;
    }
    m_warnedMemoryParse = false;

    const int percent = roundedPercent((1.0 - totals.availableKb / totals.totalKb) * 100.0);
    if (percent != m_memoryPercent) {
        m_memoryPercent = percent;
        Q_EMIT memoryPercentChanged();
    }
}

} // namespace PhosphorShell
