// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorShell/SystemStatsReader.h>
#include <PhosphorShell/SystemUsage.h>
#include "systemstatsgpu_p.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStorageInfo>
#include <algorithm>
#include <cmath>
#include <limits>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>

namespace PhosphorShell {
namespace {
QByteArray readStatsFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.read(4 * 1024 * 1024) : QByteArray{};
}
double numberAt(const QString& path, double divisor = 1)
{
    bool ok = false;
    const double value = readStatsFile(path).trimmed().toDouble(&ok);
    return ok && std::isfinite(value) && value >= 0 ? value / divisor : -1;
}
QString mountPath(QByteArray value)
{
    value.replace("\\040", " ").replace("\\011", "\t").replace("\\012", "\n").replace("\\134", "\\");
    return QString::fromUtf8(value);
}
QString interfaceAddress(const QString& name)
{
    ifaddrs* addresses = nullptr;
    if (getifaddrs(&addresses) != 0)
        return {};
    QString result;
    for (auto* it = addresses; it; it = it->ifa_next) {
        if (!it->ifa_addr || QString::fromUtf8(it->ifa_name) != name)
            continue;
        char text[INET6_ADDRSTRLEN]{};
        if (it->ifa_addr->sa_family == AF_INET) {
            auto* address = reinterpret_cast<sockaddr_in*>(it->ifa_addr);
            if (inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text))) {
                result = QString::fromLatin1(text);
                break;
            }
        } else if (it->ifa_addr->sa_family == AF_INET6 && result.isEmpty()) {
            auto* address = reinterpret_cast<sockaddr_in6*>(it->ifa_addr);
            if (inet_ntop(AF_INET6, &address->sin6_addr, text, sizeof(text)))
                result = QString::fromLatin1(text);
        }
    }
    freeifaddrs(addresses);
    return result;
}
}
struct SystemStatsReader::Private
{
    QString proc;
    QString sys;
    SystemStatsGpu gpu;
    QElapsedTimer clock;
    double previousTime = -1;
    QMap<int, SystemUsage::CpuTotals> previousCpu;
    QString networkId;
    quint64 rx = 0, tx = 0, baseRx = 0, baseTx = 0;
    bool haveNetwork = false;
    QMap<QString, QPair<quint64, quint64>> previousDisks;
    double energy = -1;
    Private(const QString& procRoot, const QString& sysRoot)
        : proc(procRoot)
        , sys(sysRoot)
        , gpu(sysRoot)
    {
        clock.start();
    }
    QVariantMap cpu(double elapsed)
    {
        QVariantMap result{{QStringLiteral("usage"), -1.0},
                           {QStringLiteral("temperature"), -1.0},
                           {QStringLiteral("power"), -1.0},
                           {QStringLiteral("clock"), -1.0},
                           {QStringLiteral("name"), QString()},
                           {QStringLiteral("cores"), 0},
                           {QStringLiteral("threads"), QVariantList{}}};
        QMap<int, SystemUsage::CpuTotals> current;
        QVariantList threads;
        for (const auto& line : readStatsFile(proc + QStringLiteral("/stat")).split('\n')) {
            const auto fields = line.simplified().split(' ');
            if (fields.size() < 5 || !fields[0].startsWith("cpu"))
                continue;
            bool ok = false;
            const int index = fields[0] == "cpu" ? -1 : fields[0].mid(3).toInt(&ok);
            if (index != -1 && !ok)
                continue;
            auto body = line;
            body.replace(0, fields[0].size(), "cpu");
            SystemUsage::CpuTotals totals;
            if (!SystemUsage::parseCpuTotals(body, &totals))
                continue;
            current[index] = totals;
            const int usage = elapsed > 0 && previousCpu.contains(index)
                ? SystemUsage::cpuPercentBetween(previousCpu[index], totals)
                : -1;
            if (index == -1)
                result[QStringLiteral("usage")] = usage;
            else
                threads.append(QVariantMap{{QStringLiteral("id"), index}, {QStringLiteral("usage"), usage}});
        }
        previousCpu = current;
        result[QStringLiteral("threads")] = threads;
        QSet<QByteArray> cores;
        QByteArray package, core;
        double frequency = 0;
        int count = 0;
        auto finishCore = [&] {
            if (!core.isEmpty())
                cores.insert(package + ':' + core);
            package.clear();
            core.clear();
        };
        for (const auto& line : readStatsFile(proc + QStringLiteral("/cpuinfo")).split('\n')) {
            if (line.trimmed().isEmpty()) {
                finishCore();
                continue;
            }
            const auto colon = line.indexOf(':');
            if (colon < 0)
                continue;
            const auto key = line.left(colon).trimmed(), value = line.mid(colon + 1).trimmed();
            if (key == "model name" || (key == "Hardware" && result.value(QStringLiteral("name")).toString().isEmpty()))
                result[QStringLiteral("name")] = QString::fromUtf8(value);
            if (key == "physical id")
                package = value;
            if (key == "core id")
                core = value;
            if (key == "cpu MHz") {
                bool ok = false;
                const auto mhz = value.toDouble(&ok);
                if (ok && std::isfinite(mhz) && mhz > 0) {
                    frequency += mhz;
                    ++count;
                }
            }
        }
        finishCore();
        result[QStringLiteral("cores")] = cores.isEmpty() ? -1 : int(cores.size());
        if (count)
            result[QStringLiteral("clock")] = frequency / count;
        const QDir hwmon(sys + QStringLiteral("/class/hwmon"));
        for (const auto& dir : hwmon.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const auto path = hwmon.filePath(dir);
            const auto name = readStatsFile(path + QStringLiteral("/name")).trimmed();
            if (name != "coretemp" && name != "k10temp" && name != "zenpower" && name != "cpu_thermal")
                continue;
            for (const auto& input : QDir(path).entryList({QStringLiteral("temp*_input")}, QDir::Files)) {
                const double temperature = numberAt(path + QLatin1Char('/') + input, 1000);
                if (temperature <= 150)
                    result[QStringLiteral("temperature")] =
                        std::max(result.value(QStringLiteral("temperature")).toDouble(), temperature);
            }
        }
        const auto rapl = sys + QStringLiteral("/class/powercap/intel-rapl:0");
        const auto joules = numberAt(rapl + QStringLiteral("/energy_uj"), 1000000);
        if (joules >= 0 && energy >= 0 && elapsed > 0) {
            double delta = joules - energy;
            if (delta < 0) {
                const double max = numberAt(rapl + QStringLiteral("/max_energy_range_uj"), 1000000);
                if (max > energy)
                    delta += max;
            }
            if (delta >= 0)
                result[QStringLiteral("power")] = delta / elapsed;
        }
        energy = joules;
        return result;
    }
    QVariantMap network(double elapsed)
    {
        QVariantMap result{{QStringLiteral("connected"), false},  {QStringLiteral("interface"), QString()},
                           {QStringLiteral("wireless"), false},   {QStringLiteral("down"), -1.0},
                           {QStringLiteral("up"), -1.0},          {QStringLiteral("speed"), -1.0},
                           {QStringLiteral("received"), -1.0},    {QStringLiteral("sent"), -1.0},
                           {QStringLiteral("address"), QString()}};
        QString selected;
        quint64 bestMetric = std::numeric_limits<quint64>::max();
        auto isUp = [&](const QString& id) {
            const auto state =
                readStatsFile(sys + QStringLiteral("/class/net/") + id + QStringLiteral("/operstate")).trimmed();
            return state == "up" || state == "unknown";
        };
        for (const auto& line : readStatsFile(proc + QStringLiteral("/net/route")).split('\n')) {
            const auto f = line.simplified().split(' ');
            if (f.size() < 8 || f[1] != "00000000" || f[7] != "00000000")
                continue;
            bool ok = false;
            const auto metric = f[6].toULongLong(&ok);
            const auto id = QString::fromUtf8(f[0]);
            if (ok && (f[3].toUInt(nullptr, 16) & 1) && metric < bestMetric && isUp(id)) {
                selected = id;
                bestMetric = metric;
            }
        }
        if (selected.isEmpty())
            for (const auto& line : readStatsFile(proc + QStringLiteral("/net/ipv6_route")).split('\n')) {
                const auto f = line.simplified().split(' ');
                if (f.size() < 10 || f[0] != QByteArray(32, '0') || f[1] != "00")
                    continue;
                const auto id = QString::fromUtf8(f.last());
                bool ok = false;
                const auto metric = f[5].toULongLong(&ok, 16);
                if (ok && (f[8].toUInt(nullptr, 16) & 1) && metric < bestMetric && id != QLatin1String("lo")
                    && isUp(id)) {
                    selected = id;
                    bestMetric = metric;
                }
            }
        if (selected.isEmpty())
            for (const auto& id :
                 QDir(sys + QStringLiteral("/class/net")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                if (id != QLatin1String("lo")
                    && QFileInfo::exists(sys + QStringLiteral("/class/net/") + id + QStringLiteral("/device"))
                    && isUp(id)) {
                    selected = id;
                    break;
                }
            }
        if (selected.isEmpty()) {
            haveNetwork = false;
            networkId.clear();
            return result;
        }
        result[QStringLiteral("interface")] = selected;
        result[QStringLiteral("connected")] = true;
        const auto path = sys + QStringLiteral("/class/net/") + selected;
        result[QStringLiteral("wireless")] = QFileInfo::exists(path + QStringLiteral("/wireless"));
        result[QStringLiteral("speed")] = numberAt(path + QStringLiteral("/speed"));
        if (sys == QLatin1String("/sys"))
            result[QStringLiteral("address")] = interfaceAddress(selected);
        bool rxOk = false, txOk = false;
        const auto currentRx =
            readStatsFile(path + QStringLiteral("/statistics/rx_bytes")).trimmed().toULongLong(&rxOk);
        const auto currentTx =
            readStatsFile(path + QStringLiteral("/statistics/tx_bytes")).trimmed().toULongLong(&txOk);
        if (!rxOk || !txOk) {
            haveNetwork = false;
            return result;
        }
        const bool continuous = haveNetwork && networkId == selected && currentRx >= rx && currentTx >= tx;
        if (!continuous) {
            baseRx = currentRx;
            baseTx = currentTx;
        } else if (elapsed > 0) {
            result[QStringLiteral("down")] = SystemStatsReader::counterRate(rx, currentRx, elapsed);
            result[QStringLiteral("up")] = SystemStatsReader::counterRate(tx, currentTx, elapsed);
        }
        result[QStringLiteral("received")] = double(currentRx - baseRx);
        result[QStringLiteral("sent")] = double(currentTx - baseTx);
        networkId = selected;
        rx = currentRx;
        tx = currentTx;
        haveNetwork = true;
        return result;
    }
    QVariantMap storage(double elapsed)
    {
        QVariantList drives;
        QSet<QByteArray> devices;
        const QSet<QByteArray> localTypes{"ext2", "ext3", "ext4",  "btrfs", "xfs",
                                          "f2fs", "vfat", "exfat", "ntfs3", "zfs"};
        for (const auto& line : readStatsFile(proc + QStringLiteral("/self/mountinfo")).split('\n')) {
            const auto halves = line.split(' ');
            const auto separator = halves.indexOf("-");
            if (separator < 6 || halves.size() < separator + 3 || !localTypes.contains(halves[separator + 1]))
                continue;
            const auto mount = mountPath(halves[4]);
            // Exclude container bind mounts and duplicate Btrfs subvolumes.
            if (devices.contains(halves[2]))
                continue;
            devices.insert(halves[2]);
            if (proc != QLatin1String("/proc"))
                continue;
            const QStorageInfo volume(mount);
            if (!volume.isValid() || !volume.isReady() || volume.bytesTotal() <= 0)
                continue;
            const double total = volume.bytesTotal(), free = std::clamp(double(volume.bytesAvailable()), 0.0, total);
            drives.append(QVariantMap{{QStringLiteral("mount"), mount},
                                      {QStringLiteral("name"), volume.name()},
                                      {QStringLiteral("device"), QString::fromUtf8(halves[separator + 2])},
                                      {QStringLiteral("total"), total},
                                      {QStringLiteral("free"), free},
                                      {QStringLiteral("used"), total - free},
                                      {QStringLiteral("usage"), (total - free) / total * 100}});
        }
        QMap<QString, QPair<quint64, quint64>> disks;
        double read = 0, write = 0;
        bool haveRate = false;
        for (const auto& line : readStatsFile(proc + QStringLiteral("/diskstats")).split('\n')) {
            const auto f = line.simplified().split(' ');
            if (f.size() < 14)
                continue;
            const auto id = QString::fromUtf8(f[2]);
            if (!QFileInfo::exists(sys + QStringLiteral("/block/") + id + QStringLiteral("/device")))
                continue;
            bool readOk = false, writeOk = false;
            const quint64 readSectors = f[5].toULongLong(&readOk), writeSectors = f[9].toULongLong(&writeOk);
            if (!readOk || !writeOk)
                continue;
            disks[id] = {readSectors, writeSectors};
            if (!previousDisks.contains(id) || elapsed <= 0)
                continue;
            const auto before = previousDisks[id];
            const double r = SystemStatsReader::counterRate(before.first, readSectors, elapsed),
                         w = SystemStatsReader::counterRate(before.second, writeSectors, elapsed);
            if (r >= 0 && w >= 0) {
                read += r * 512;
                write += w * 512;
                haveRate = true;
            }
        }
        previousDisks = disks;
        return {{QStringLiteral("drives"), drives},
                {QStringLiteral("read"), haveRate ? read : -1},
                {QStringLiteral("write"), haveRate ? write : -1}};
    }
};
SystemStatsReader::SystemStatsReader(const QString& procRoot, const QString& sysRoot)
    : d(std::make_unique<Private>(procRoot, sysRoot))
{
}
SystemStatsReader::~SystemStatsReader() = default;
void SystemStatsReader::resetRates()
{
    d->previousTime = -1;
    d->previousCpu.clear();
    d->previousDisks.clear();
    d->energy = -1;
}
double SystemStatsReader::counterRate(quint64 previous, quint64 current, double seconds)
{
    return std::isfinite(seconds) && seconds > 0 && current >= previous ? double(current - previous) / seconds : -1;
}
QVariantMap SystemStatsReader::memoryFrom(const QByteArray& content)
{
    QMap<QByteArray, double> values;
    for (const auto& line : content.split('\n')) {
        const auto f = line.simplified().split(' ');
        if (f.size() < 2)
            continue;
        bool ok = false;
        const double n = f[1].toDouble(&ok);
        if (ok && std::isfinite(n) && n >= 0)
            values[f[0]] = n * 1024;
    }
    QVariantMap result{{QStringLiteral("total"), -1.0},     {QStringLiteral("used"), -1.0},
                       {QStringLiteral("available"), -1.0}, {QStringLiteral("free"), -1.0},
                       {QStringLiteral("cached"), -1.0},    {QStringLiteral("usage"), -1.0},
                       {QStringLiteral("swapUsed"), -1.0},  {QStringLiteral("swapTotal"), -1.0}};
    if (!values.contains("MemTotal:") || !values.contains("MemAvailable:") || values["MemTotal:"] <= 0
        || values["MemAvailable:"] > values["MemTotal:"])
        return result;
    const double total = values["MemTotal:"], available = values["MemAvailable:"];
    result[QStringLiteral("total")] = total;
    result[QStringLiteral("available")] = available;
    result[QStringLiteral("used")] = total - available;
    result[QStringLiteral("usage")] = (total - available) / total * 100;
    if (values.contains("MemFree:") && values["MemFree:"] <= available) {
        result[QStringLiteral("free")] = values["MemFree:"];
        // The reclaimable part of MemAvailable forms a disjoint breakdown.
        result[QStringLiteral("cached")] = available - values["MemFree:"];
    }
    if (values.contains("SwapTotal:") && values.contains("SwapFree:") && values["SwapFree:"] <= values["SwapTotal:"]) {
        result[QStringLiteral("swapTotal")] = values["SwapTotal:"];
        result[QStringLiteral("swapUsed")] = values["SwapTotal:"] - values["SwapFree:"];
    }
    return result;
}
QVariantMap SystemStatsReader::sample()
{
    const double now = d->clock.elapsed() / 1000.0, elapsed = d->previousTime < 0 ? -1 : now - d->previousTime;
    const auto storage = d->storage(elapsed);
    bool ok = false;
    const double uptime = readStatsFile(d->proc + QStringLiteral("/uptime")).split(' ').value(0).toDouble(&ok);
    QVariantMap result{{QStringLiteral("cpu"), d->cpu(elapsed)},
                       {QStringLiteral("memory"), memoryFrom(readStatsFile(d->proc + QStringLiteral("/meminfo")))},
                       {QStringLiteral("network"), d->network(elapsed)},
                       {QStringLiteral("gpus"), d->gpu.sample()},
                       {QStringLiteral("storage"), storage},
                       {QStringLiteral("uptime"), ok && std::isfinite(uptime) && uptime >= 0 ? uptime : -1}};
    d->previousTime = now;
    return result;
}
}
