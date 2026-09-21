// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "systemstatsgpu_p.h"
#include <QDir>
#include <QFile>
#include <QLibrary>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace PhosphorShell {
namespace {
QByteArray readGpuFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.read(65536).trimmed() : QByteArray{};
}
double sensor(const QString& path, double divisor = 1)
{
    bool ok = false;
    const auto result = readGpuFile(path).toDouble(&ok);
    return ok && std::isfinite(result) && result >= 0 ? result / divisor : -1;
}
QVariantMap emptyGpu(const QString& id, const QString& name)
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("usage"), -1.0},
            {QStringLiteral("memoryUsed"), -1.0},
            {QStringLiteral("memoryTotal"), -1.0},
            {QStringLiteral("temperature"), -1.0},
            {QStringLiteral("power"), -1.0},
            {QStringLiteral("clock"), -1.0},
            {QStringLiteral("encode"), -1.0},
            {QStringLiteral("decode"), -1.0}};
}
// Stable, unversioned NVML C ABI. Load the driver's library at runtime so the
// shell has no build-time CUDA SDK or proprietary driver dependency.
// https://docs.nvidia.com/deploy/nvml-api/api/group__nvmlDeviceQueries.html
struct nvmlDevice_st;
using Device = nvmlDevice_st*;
struct Utilization
{
    unsigned int gpu;
    unsigned int memory;
};
struct Memory
{
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};
}
struct SystemStatsGpu::Private
{
    QString sys;
    QLibrary nvml{QStringLiteral("libnvidia-ml.so.1")};
    bool initialized = false;
    template<typename F>
    F resolve(const char* name)
    {
        return reinterpret_cast<F>(nvml.resolve(name));
    }
    QVariantList nvidia()
    {
        QVariantList result;
        if (!initialized) {
            auto init = resolve<int (*)()>("nvmlInit_v2");
            initialized = init && init() == 0;
        }
        if (!initialized)
            return result;
        auto countFn = resolve<int (*)(unsigned int*)>("nvmlDeviceGetCount_v2");
        auto handleFn = resolve<int (*)(unsigned int, Device*)>("nvmlDeviceGetHandleByIndex_v2");
        unsigned int count = 0;
        if (!countFn || !handleFn || countFn(&count) != 0)
            return result;
        for (unsigned int i = 0; i < std::min(count, 32u); ++i) {
            Device device = nullptr;
            if (handleFn(i, &device) != 0)
                continue;
            char name[128]{};
            auto nameFn = resolve<int (*)(Device, char*, unsigned int)>("nvmlDeviceGetName");
            if (nameFn)
                nameFn(device, name, sizeof(name));
            auto gpu = emptyGpu(QStringLiteral("nvidia-%1").arg(i), QString::fromUtf8(name[0] ? name : "NVIDIA GPU"));
            Utilization usage{};
            auto usageFn = resolve<int (*)(Device, Utilization*)>("nvmlDeviceGetUtilizationRates");
            if (usageFn && usageFn(device, &usage) == 0 && usage.gpu <= 100)
                gpu[QStringLiteral("usage")] = double(usage.gpu);
            Memory memory{};
            auto memoryFn = resolve<int (*)(Device, Memory*)>("nvmlDeviceGetMemoryInfo");
            if (memoryFn && memoryFn(device, &memory) == 0 && memory.used <= memory.total) {
                gpu[QStringLiteral("memoryUsed")] = double(memory.used);
                gpu[QStringLiteral("memoryTotal")] = double(memory.total);
            }
            unsigned int n = 0;
            auto temperatureFn = resolve<int (*)(Device, unsigned int, unsigned int*)>("nvmlDeviceGetTemperature");
            if (temperatureFn && temperatureFn(device, 0, &n) == 0)
                gpu[QStringLiteral("temperature")] = double(n);
            auto clockFn = resolve<int (*)(Device, unsigned int, unsigned int*)>("nvmlDeviceGetClockInfo");
            if (clockFn && clockFn(device, 0, &n) == 0)
                gpu[QStringLiteral("clock")] = double(n);
            auto powerFn = resolve<int (*)(Device, unsigned int*)>("nvmlDeviceGetPowerUsage");
            if (powerFn && powerFn(device, &n) == 0)
                gpu[QStringLiteral("power")] = double(n) / 1000;
            unsigned int period = 0;
            auto encodeFn = resolve<int (*)(Device, unsigned int*, unsigned int*)>("nvmlDeviceGetEncoderUtilization");
            if (encodeFn && encodeFn(device, &n, &period) == 0 && n <= 100)
                gpu[QStringLiteral("encode")] = double(n);
            auto decodeFn = resolve<int (*)(Device, unsigned int*, unsigned int*)>("nvmlDeviceGetDecoderUtilization");
            if (decodeFn && decodeFn(device, &n, &period) == 0 && n <= 100)
                gpu[QStringLiteral("decode")] = double(n);
            result.append(gpu);
        }
        return result;
    }
};
SystemStatsGpu::SystemStatsGpu(const QString& sysRoot)
    : d(std::make_unique<Private>())
{
    d->sys = sysRoot;
}
SystemStatsGpu::~SystemStatsGpu()
{
    if (d->initialized) {
        auto shutdown = d->resolve<int (*)()>("nvmlShutdown");
        if (shutdown)
            shutdown();
    }
}
QVariantList SystemStatsGpu::sample()
{
    // Fixture roots must never fall through to the machine's GPU driver.
    QVariantList result;
    const QDir drm(d->sys + QStringLiteral("/class/drm"));
    const auto cards = drm.entryList({QStringLiteral("card*")}, QDir::Dirs, QDir::Name);
    bool nvidiaRead = false;
    bool haveNvidia = false;
    for (const QString& card : cards) {
        if (!QRegularExpression(QStringLiteral("^card[0-9]+$")).match(card).hasMatch())
            continue;
        const auto path = drm.filePath(card + QStringLiteral("/device"));
        const auto vendor = readGpuFile(path + QStringLiteral("/vendor"));
        if (vendor == "0x10de" && d->sys == QLatin1String("/sys")) {
            if (!nvidiaRead) {
                const auto devices = d->nvidia();
                haveNvidia = !devices.isEmpty();
                result += devices;
                nvidiaRead = true;
            }
            if (haveNvidia)
                continue;
        }
        auto name = QString::fromUtf8(readGpuFile(path + QStringLiteral("/product_name")));
        if (name.isEmpty())
            name = QString::fromUtf8(vendor == "0x1002"       ? "AMD GPU"
                                         : vendor == "0x8086" ? "Intel GPU"
                                         : vendor == "0x10de" ? "NVIDIA GPU"
                                                              : "GPU");
        auto gpu = emptyGpu(card, name);
        const double usage = sensor(path + QStringLiteral("/gpu_busy_percent"));
        gpu[QStringLiteral("usage")] = usage <= 100 ? usage : -1;
        gpu[QStringLiteral("memoryUsed")] = sensor(path + QStringLiteral("/mem_info_vram_used"));
        gpu[QStringLiteral("memoryTotal")] = sensor(path + QStringLiteral("/mem_info_vram_total"));
        const QDir hwmon(path + QStringLiteral("/hwmon"));
        for (const auto& dir : hwmon.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const auto base = hwmon.filePath(dir);
            gpu[QStringLiteral("temperature")] = sensor(base + QStringLiteral("/temp1_input"), 1000);
            const double average = sensor(base + QStringLiteral("/power1_average"), 1000000);
            gpu[QStringLiteral("power")] =
                average >= 0 ? average : sensor(base + QStringLiteral("/power1_input"), 1000000);
            gpu[QStringLiteral("clock")] = sensor(base + QStringLiteral("/freq1_input"), 1000000);
            break;
        }
        result.append(gpu);
    }
    return result;
}
}
