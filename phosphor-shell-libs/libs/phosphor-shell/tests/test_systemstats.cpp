// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorShell/SystemStats.h>
#include <PhosphorShell/SystemStatsReader.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <limits>
using namespace PhosphorShell;
class TestSystemStats : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void memoryBreakdown();
    void malformedMemory();
    void ratesRejectResetAndInvalidTime();
    void collectorRebaselines();
    void networkDisconnectAndCounterReset();
    void gpuAvailability();
    void sharedSamplingAndPause();
};
namespace {
void write(const QString& root, const QString& path, const QByteArray& contents)
{
    const QString target = root + path;
    QDir().mkpath(QFileInfo(target).path());
    QFile f(target);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(contents), contents.size());
}
}
void TestSystemStats::memoryBreakdown()
{
    const auto m = SystemStatsReader::memoryFrom(
        "MemTotal: 64000 kB\nMemFree: 16000 kB\nMemAvailable: 40000 kB\nSwapTotal: 8000 kB\nSwapFree: 6000 kB\n");
    QCOMPARE(m.value(QStringLiteral("total")).toDouble(), 64000.0 * 1024);
    QCOMPARE(m.value(QStringLiteral("used")).toDouble(), 24000.0 * 1024);
    QCOMPARE(m.value(QStringLiteral("cached")).toDouble(), 24000.0 * 1024);
    QCOMPARE(m.value(QStringLiteral("free")).toDouble(), 16000.0 * 1024);
    QCOMPARE(m.value(QStringLiteral("usage")).toDouble(), 37.5);
    QCOMPARE(m.value(QStringLiteral("swapUsed")).toDouble(), 2000.0 * 1024);
}
void TestSystemStats::malformedMemory()
{
    for (const QByteArray& body :
         {QByteArray{}, QByteArray{"MemTotal: 64 kB\n"}, QByteArray{"MemTotal: 0 kB\nMemAvailable: 0 kB\n"},
          QByteArray{"MemTotal: 64 kB\nMemAvailable: 65 kB\n"}, QByteArray{"MemTotal: nan kB\nMemAvailable: 30 kB\n"},
          QByteArray{"MemTotal: 64 kB\nMemAvailable: -1 kB\n"}}) {
        const auto m = SystemStatsReader::memoryFrom(body);
        QCOMPARE(m.value(QStringLiteral("used")).toDouble(), -1.0);
        QCOMPARE(m.value(QStringLiteral("usage")).toDouble(), -1.0);
    }
    // An unavailable cache breakdown must not fabricate zero cache.
    const auto m = SystemStatsReader::memoryFrom("MemTotal: 64 kB\nMemAvailable: 40 kB\n");
    QCOMPARE(m.value(QStringLiteral("cached")).toDouble(), -1.0);
    QCOMPARE(m.value(QStringLiteral("used")).toDouble(), 24.0 * 1024);
}
void TestSystemStats::ratesRejectResetAndInvalidTime()
{
    QCOMPARE(SystemStatsReader::counterRate(100, 500, 2), 200.0);
    QCOMPARE(SystemStatsReader::counterRate(500, 100, 2), -1.0);
    QCOMPARE(SystemStatsReader::counterRate(100, 500, 0), -1.0);
    QCOMPARE(SystemStatsReader::counterRate(100, 500, -2), -1.0);
    QCOMPARE(SystemStatsReader::counterRate(100, 500, std::numeric_limits<double>::quiet_NaN()), -1.0);
    QCOMPARE(SystemStatsReader::counterRate(100, 100, 2), 0.0);
}
void TestSystemStats::collectorRebaselines()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    write(dir.path(), QStringLiteral("/proc/stat"), "cpu 100 0 0 100\ncpu0 100 0 0 100\n");
    SystemStatsReader reader(dir.path() + QStringLiteral("/proc"), dir.path() + QStringLiteral("/sys"));
    auto first = reader.sample();
    QCOMPARE(first.value(QStringLiteral("cpu")).toMap().value(QStringLiteral("usage")).toInt(), -1);
    QTest::qWait(10);
    write(dir.path(), QStringLiteral("/proc/stat"), "cpu 200 0 0 100\ncpu0 200 0 0 100\ncpu2 200 0 0 100\n");
    auto next = reader.sample().value(QStringLiteral("cpu")).toMap();
    QCOMPARE(next.value(QStringLiteral("usage")).toInt(), 100);
    const auto threads = next.value(QStringLiteral("threads")).toList();
    QCOMPARE(threads.size(), 2);
    QCOMPARE(threads[1].toMap().value(QStringLiteral("usage")).toInt(), -1);
    reader.resetRates();
    QTest::qWait(10);
    QCOMPARE(reader.sample().value(QStringLiteral("cpu")).toMap().value(QStringLiteral("usage")).toInt(), -1);
    write(dir.path(), QStringLiteral("/proc/stat"), "bad input\n");
    QCOMPARE(reader.sample().value(QStringLiteral("cpu")).toMap().value(QStringLiteral("usage")).toInt(), -1);
}
void TestSystemStats::networkDisconnectAndCounterReset()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    write(dir.path(), QStringLiteral("/proc/net/route"),
          "Iface Destination Gateway Flags RefCnt Use Metric Mask\neth0 00000000 00000000 0003 0 0 10 00000000\n");
    write(dir.path(), QStringLiteral("/sys/class/net/eth0/operstate"), "up\n");
    write(dir.path(), QStringLiteral("/sys/class/net/eth0/statistics/rx_bytes"), "1000\n");
    write(dir.path(), QStringLiteral("/sys/class/net/eth0/statistics/tx_bytes"), "2000\n");
    SystemStatsReader reader(dir.path() + QStringLiteral("/proc"), dir.path() + QStringLiteral("/sys"));
    auto net = reader.sample().value(QStringLiteral("network")).toMap();
    QVERIFY(net.value(QStringLiteral("connected")).toBool());
    QCOMPARE(net.value(QStringLiteral("down")).toDouble(), -1.0);
    QTest::qWait(10);
    write(dir.path(), QStringLiteral("/sys/class/net/eth0/statistics/rx_bytes"), "3000\n");
    net = reader.sample().value(QStringLiteral("network")).toMap();
    QVERIFY(net.value(QStringLiteral("down")).toDouble() > 0);
    QCOMPARE(net.value(QStringLiteral("received")).toDouble(), 2000.0);
    write(dir.path(), QStringLiteral("/sys/class/net/eth0/statistics/rx_bytes"), "10\n");
    QTest::qWait(10);
    net = reader.sample().value(QStringLiteral("network")).toMap();
    QCOMPARE(net.value(QStringLiteral("down")).toDouble(), -1.0);
    write(dir.path(), QStringLiteral("/sys/class/net/eth0/operstate"), "down\n");
    net = reader.sample().value(QStringLiteral("network")).toMap();
    QVERIFY(!net.value(QStringLiteral("connected")).toBool());
    QCOMPARE(net.value(QStringLiteral("received")).toDouble(), -1.0);
}
void TestSystemStats::gpuAvailability()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    write(dir.path(), QStringLiteral("/sys/class/drm/card0/device/vendor"), "0x1002\n");
    write(dir.path(), QStringLiteral("/sys/class/drm/card0/device/gpu_busy_percent"), "87\n");
    write(dir.path(), QStringLiteral("/sys/class/drm/card0/device/mem_info_vram_total"), "16000000000\n");
    SystemStatsReader reader(dir.path() + QStringLiteral("/proc"), dir.path() + QStringLiteral("/sys"));
    auto gpus = reader.sample().value(QStringLiteral("gpus")).toList();
    QCOMPARE(gpus.size(), 1);
    QCOMPARE(gpus.first().toMap().value(QStringLiteral("usage")).toDouble(), 87.0);
    QCOMPARE(gpus.first().toMap().value(QStringLiteral("temperature")).toDouble(), -1.0);
    write(dir.path(), QStringLiteral("/sys/class/drm/card0/device/gpu_busy_percent"), "N/A\n");
    gpus = reader.sample().value(QStringLiteral("gpus")).toList();
    QCOMPARE(gpus.first().toMap().value(QStringLiteral("usage")).toDouble(), -1.0);
    QDir(dir.path() + QStringLiteral("/sys/class/drm/card0")).removeRecursively();
    QVERIFY(reader.sample().value(QStringLiteral("gpus")).toList().isEmpty());
}
void TestSystemStats::sharedSamplingAndPause()
{
    SystemStats service;
    QObject bar, popup;
    QSignalSpy sample(&service, &SystemStats::sampled);
    QVERIFY(!service.active());
    service.watch(&bar, true);
    service.watch(&popup, true);
    QTRY_VERIFY_WITH_TIMEOUT(sample.count() >= 1, 10000);
    service.setInterval(1);
    QCOMPARE(service.interval(), 1000);
    service.watch(&bar, false);
    QVERIFY(service.active());
    service.setPaused(true);
    const auto frozen = service.snapshot();
    const auto frames = sample.count();
    QTest::qWait(1200);
    QCOMPARE(service.snapshot(), frozen);
    QCOMPARE(sample.count(), frames);
    service.setPaused(false);
    QTRY_VERIFY_WITH_TIMEOUT(sample.count() > frames, 10000);
    const auto history = service.history(QStringLiteral("cpu"));
    QVERIFY(!history.isEmpty());
    QVERIFY(history.size() <= 181);
    for (const auto& row : history) {
        const auto x = row.toMap().value(QStringLiteral("x")).toDouble();
        QVERIFY(x >= 0 && x <= 1);
    }
    service.watch(&popup, false);
    QVERIFY(!service.active());
    {
        QObject transient;
        service.watch(&transient, true);
        QVERIFY(service.active());
    }
    QVERIFY(!service.active());
}
QTEST_GUILESS_MAIN(TestSystemStats)
#include "test_systemstats.moc"
