// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QByteArray>
#include <QObject>

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace PhosphorShell {

/// CPU and memory utilisation for status bar widgets.
///
/// Samples `/proc/stat` and `/proc/meminfo` in-process on a timer, so a
/// bar readout costs no subprocess per tick. CPU is the busy fraction
/// between consecutive samples (the kernel exports cumulative jiffies, so
/// a single sample cannot yield a rate). The constructor takes the first
/// baseline, so `cpuPercent` is already meaningful on the first timer
/// tick. Memory is `1 - MemAvailable / MemTotal`.
///
/// Both values are whole percents clamped to 0..100. A malformed or
/// unexpected `/proc` layout (exotic architecture, namespaced procfs, a
/// kernel without `MemAvailable`) leaves the previous value in place and
/// warns, once per failure kind per episode, rather than publishing a
/// bogus reading. The two differ on recovery: memory republishes on the
/// next good sample, while CPU also re-baselines, so its first sample
/// after a failure publishes no rate and the readout holds one tick
/// longer. That is deliberate, since the alternative is reporting the
/// whole outage as if it were one interval.
///
/// Usage from QML:
///
///     import Phosphor.Shell
///     SystemUsage {
///         id: usage
///         interval: 2000
///         enabled: root.visible
///     }
///     Text { text: "CPU " + usage.cpuPercent + "%" }
class PHOSPHORSHELL_EXPORT SystemUsage : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(int interval READ interval WRITE setInterval NOTIFY intervalChanged)
    Q_PROPERTY(int cpuPercent READ cpuPercent NOTIFY cpuPercentChanged)
    Q_PROPERTY(int memoryPercent READ memoryPercent NOTIFY memoryPercentChanged)

public:
    /// Shortest accepted sampling period. Below roughly this the sampler
    /// costs more CPU than it measures and the reading becomes largely a
    /// measurement of itself.
    static constexpr int MinIntervalMs = 100;

    /// Cumulative busy / idle jiffies parsed from a `/proc/stat` body.
    struct CpuTotals
    {
        double total = 0;
        double idle = 0;
    };

    /// Total / available kilobytes parsed from a `/proc/meminfo` body.
    struct MemoryTotals
    {
        double totalKb = 0;
        double availableKb = 0;
    };

    explicit SystemUsage(QObject* parent = nullptr);
    ~SystemUsage() override;

    [[nodiscard]] bool enabled() const;
    /// Enabling re-baselines and samples immediately, so the first reading
    /// after a pause covers one interval rather than the whole gap.
    void setEnabled(bool enabled);

    /// Sampling period in milliseconds, clamped to at least MinIntervalMs.
    /// A shorter request is clamped (and warned about) rather than ignored,
    /// so the property and the binding that wrote it agree.
    [[nodiscard]] int interval() const;
    void setInterval(int intervalMs);

    [[nodiscard]] int cpuPercent() const;
    [[nodiscard]] int memoryPercent() const;

    /// Parse the aggregate "cpu" line of a `/proc/stat` body. Returns false
    /// for a missing/short/non-numeric line, leaving `out` untouched.
    ///
    /// Static and body-taking so the arithmetic is testable without a
    /// filesystem: this is the logic the bar widget used to carry in QML.
    [[nodiscard]] static bool parseCpuTotals(const QByteArray& content, CpuTotals* out);

    /// Parse MemTotal / MemAvailable from a `/proc/meminfo` body. Returns
    /// false unless both are present and numeric, leaving `out` untouched.
    [[nodiscard]] static bool parseMemoryTotals(const QByteArray& content, MemoryTotals* out);

    /// Busy percentage between two cumulative snapshots, or -1 when the pair
    /// yields no usable rate: counters that did not advance, or that went
    /// backwards (a suspend/resume snapshot, a namespace reset, a wrap). The
    /// caller publishes nothing for -1 and re-baselines instead.
    ///
    /// Static and snapshot-taking for the same reason as the parsers: it
    /// makes the delta arithmetic pinnable without waiting on real jiffies,
    /// which advance too slowly to exercise from a test.
    [[nodiscard]] static int cpuPercentBetween(const CpuTotals& prev, const CpuTotals& current);

Q_SIGNALS:
    void enabledChanged();
    void intervalChanged();
    void cpuPercentChanged();
    void memoryPercentChanged();

private:
    void sample();
    void sampleCpu();
    void sampleMemory();
    void restartBaseline();

    QTimer* m_timer = nullptr;
    bool m_enabled = true;
    int m_interval = 2000;
    int m_cpuPercent = 0;
    int m_memoryPercent = 0;
    // Previous cumulative jiffy totals; the CPU rate is their delta.
    // Doubles because the totals exceed 32-bit range on a multi-core box
    // within days at 100 Hz.
    CpuTotals m_prev;
    bool m_haveCpuBaseline = false;
    // One warning per failure kind, cleared on a successful sample so a
    // recurrence after recovery is reported again rather than silenced for
    // the life of the process.
    bool m_warnedCpuRead = false;
    bool m_warnedCpuParse = false;
    bool m_warnedMemoryRead = false;
    bool m_warnedMemoryParse = false;
};

} // namespace PhosphorShell
