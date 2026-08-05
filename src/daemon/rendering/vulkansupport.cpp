// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vulkansupport.h"

#include "core/platform/logging.h"

#include <QDir>
#include <QFile>
#include <QLibrary>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QStringList>
#include <QVarLengthArray>
#if QT_CONFIG(vulkan)
#include <QVulkanFunctions>
#endif

// Logging note: this TU compiles into both the daemon and the editor
// binaries, so it logs under lcCore rather than either app's category.
// Filter with plasmazones.core=true when debugging GPU selection.

namespace PlasmaZones {

namespace {

/// Environment variables THIS process exported for the GPU preference, and
/// the ones it cleared (with their original values, so spawn sites can
/// restore them for children). Written only from the two apply functions
/// below, which run once each on the main thread during startup, before any
/// spawn site can read either.
QStringList g_exportedGpuVars;
QVariantMap g_clearedGpuVars;

void exportGpuVar(const char* name, const QByteArray& value)
{
    qputenv(name, value);
    g_exportedGpuVars.append(QString::fromLatin1(name));
}

/// Parse a "vendor:device" hex PCI pair. Returns false for "auto", the empty
/// string, or anything malformed. Kept strict — exactly four hex digits per
/// field — in lockstep with ConfigDefaults::normalizeGpuDevice's accepted
/// shape (this file deliberately takes no config-layer dependency, so the
/// contract is duplicated here rather than called).
bool parseGpuPciPair(const QString& gpuDevice, uint32_t& vendorId, uint32_t& deviceId)
{
    if (gpuDevice.size() != 9 || gpuDevice.at(4) != QLatin1Char(':')) {
        return false;
    }
    bool vendorOk = false;
    bool deviceOk = false;
    const uint32_t vendor = gpuDevice.left(4).toUInt(&vendorOk, 16);
    const uint32_t device = gpuDevice.mid(5).toUInt(&deviceOk, 16);
    if (!vendorOk || !deviceOk) {
        return false;
    }
    vendorId = vendor;
    deviceId = device;
    return true;
}

/// Read a sysfs attribute like "0x1002\n" and return its numeric value, or 0
/// when unreadable.
uint32_t readSysfsPciId(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 0;
    }
    bool ok = false;
    const uint32_t value = QString::fromLatin1(f.readAll()).trimmed().toUInt(&ok, 16);
    return ok ? value : 0;
}

/// Check the configured pair against the PCI DRM render nodes in sysfs.
/// Returns true when a matching node exists, or when the scan finds no
/// readable node at all (an unreadable /sys must not veto a valid config).
bool gpuPairPresentInSysfs(uint32_t vendorId, uint32_t deviceId)
{
    const QDir drm(QStringLiteral("/sys/class/drm"));
    const QStringList nodes =
        drm.entryList(QStringList{QStringLiteral("renderD*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    bool sawReadableNode = false;
    for (const QString& node : nodes) {
        const QString deviceDir = drm.filePath(node) + QStringLiteral("/device");
        const uint32_t nodeVendor = readSysfsPciId(deviceDir + QStringLiteral("/vendor"));
        const uint32_t nodeDevice = readSysfsPciId(deviceDir + QStringLiteral("/device"));
        if (nodeVendor == 0 || nodeDevice == 0) {
            continue;
        }
        sawReadableNode = true;
        if (nodeVendor == vendorId && nodeDevice == deviceId) {
            return true;
        }
    }
    return !sawReadableNode;
}

} // namespace

QStringList exportedGpuPreferenceVariables()
{
    return g_exportedGpuVars;
}

QVariantMap clearedGpuPreferenceVariables()
{
    return g_clearedGpuVars;
}

bool probeAndSetGraphicsApi(const QString& backend)
{
    if (backend == QLatin1String("vulkan")) {
#if QT_CONFIG(vulkan)
        QLibrary vulkanLib(QStringLiteral("vulkan"), 1);
        bool vulkanLibAvailable = vulkanLib.load();
        if (!vulkanLibAvailable) {
            vulkanLib.setFileName(QStringLiteral("vulkan"));
            vulkanLibAvailable = vulkanLib.load();
        }
        if (vulkanLibAvailable) {
            QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
            return true;
        }
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
        return false;
#else
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
        return false;
#endif
    }
    if (backend == QLatin1String("opengl")) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    }
    // "auto" → let Qt choose (default behavior)
    return false;
}

void applyOpenGlGpuPreference(const QString& gpuDevice)
{
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    if (!parseGpuPciPair(gpuDevice, vendorId, deviceId)) {
        return;
    }
    if (qEnvironmentVariableIsSet("DRI_PRIME")) {
        // The value may come from the user's session or from a parent
        // PlasmaZones process that failed to scrub its spawn environment —
        // this process cannot tell the two apart, so it defers either way.
        qCInfo(lcCore) << "DRI_PRIME already set in environment — leaving it in place over the configured GPU"
                       << gpuDevice;
        return;
    }
    if (!gpuPairPresentInSysfs(vendorId, deviceId)) {
        // Mirror the Vulkan path's no-match diagnostic: without this check a
        // stale pin (card removed, config copied between machines) would be
        // exported to Mesa verbatim with no feedback anywhere.
        qCWarning(lcCore) << "Configured GPU" << gpuDevice
                          << "not found among DRM render nodes — leaving the driver's default choice";
        return;
    }
    // GL has no device-selection API on Linux; the choice belongs to the
    // driver loader, and the two loaders speak different variables.
    // Recent Mesa (AMD / Intel / nouveau+NVK) honors
    // DRI_PRIME=vendor_id:device_id (hex, no 0x). The proprietary NVIDIA
    // stack ignores DRI_PRIME and instead offloads via
    // __NV_PRIME_RENDER_OFFLOAD plus the GLX vendor pick for XWayland/GLX
    // contexts. The EGL side of a Wayland session may additionally involve
    // glvnd's __EGL_VENDOR_LIBRARY_FILENAMES, which is NOT exported here: its
    // value is a distro-specific ICD manifest path, and exporting a wrong
    // path breaks EGL entirely, so NVIDIA-on-pure-Wayland offload is left to
    // the driver stack's own defaults. Exporting both loader sides below is
    // safe — each loader only reads its own variables — so the selected GPU
    // wins whichever driver stack owns it.
    exportGpuVar("DRI_PRIME", gpuDevice.toLatin1());
    constexpr uint32_t NvidiaPciVendorId = 0x10de;
    if (vendorId == NvidiaPciVendorId) {
        if (!qEnvironmentVariableIsSet("__NV_PRIME_RENDER_OFFLOAD")) {
            exportGpuVar("__NV_PRIME_RENDER_OFFLOAD", "1");
            if (!qEnvironmentVariableIsSet("__GLX_VENDOR_LIBRARY_NAME")) {
                exportGpuVar("__GLX_VENDOR_LIBRARY_NAME", "nvidia");
            }
        }
        qCInfo(lcCore) << "OpenGL GPU preference applied: DRI_PRIME =" << gpuDevice
                       << "(with NVIDIA PRIME render offload)";
    } else {
        // A session-wide NVIDIA offload (hybrid-laptop wrapper scripts export
        // __NV_PRIME_RENDER_OFFLOAD=1 globally) would override the pin and
        // silently render on the NVIDIA card instead of the selected one —
        // clear it for this process when the selected vendor is not NVIDIA.
        if (qEnvironmentVariableIsSet("__NV_PRIME_RENDER_OFFLOAD")) {
            // Record the original value so spawn sites can restore it for
            // children — the clear is only THIS process's business.
            g_clearedGpuVars.insert(QStringLiteral("__NV_PRIME_RENDER_OFFLOAD"),
                                    QString::fromLocal8Bit(qgetenv("__NV_PRIME_RENDER_OFFLOAD")));
            qunsetenv("__NV_PRIME_RENDER_OFFLOAD");
            qCInfo(lcCore) << "Cleared inherited __NV_PRIME_RENDER_OFFLOAD — the configured GPU is not NVIDIA";
        }
        qCInfo(lcCore) << "OpenGL GPU preference applied: DRI_PRIME =" << gpuDevice;
    }
}

#if QT_CONFIG(vulkan)
bool createAndRegisterVulkanInstance(QVulkanInstance& vulkanInstance, QGuiApplication& app, const QString& gpuDevice)
{
    vulkanInstance.setApiVersion(PVulkanApiVersion);
    vulkanInstance.setExtensions(vulkanInstance.extensions() << QByteArrayLiteral("VK_EXT_swapchain_colorspace"));
    if (!vulkanInstance.create()) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
        return false;
    }

    // A successful create() is NOT sufficient: vkCreateInstance only loads the
    // Vulkan loader + ICD, it does NOT require an enumerable GPU. When the
    // userspace driver and the loaded kernel module are version-skewed (e.g. an
    // nvidia-utils upgrade without a reboot), the loader is present and the
    // instance is created, but vkEnumeratePhysicalDevices returns
    // VK_ERROR_INITIALIZATION_FAILED / zero devices. If we proceed, QRhi
    // discovers this only at scenegraph init on the render thread, where
    // QQuickWindow treats it as a qFatal — aborting the whole process and
    // crash-looping the daemon under systemd. Probe for a usable physical
    // device here, while we can still cleanly fall back to OpenGL.
    QVulkanFunctions* functions = vulkanInstance.functions();
    uint32_t physicalDeviceCount = 0;
    const VkResult enumResult = functions
        ? functions->vkEnumeratePhysicalDevices(vulkanInstance.vkInstance(), &physicalDeviceCount, nullptr)
        : VK_ERROR_INITIALIZATION_FAILED;
    if (enumResult != VK_SUCCESS || physicalDeviceCount == 0) {
        vulkanInstance.destroy();
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
        return false;
    }

    // Pin the configured GPU by index. Qt's Vulkan RHI reads
    // QT_VK_PHYSICAL_DEVICE_INDEX when it picks the physical device at
    // scenegraph init, which is later than this point — so exporting it here,
    // before any QQuickWindow exists, is early enough. An index already in
    // the environment outranks the config (it may be the user's, or a parent
    // process's unscrubbed spawn environment; this process cannot tell).
    //
    // Thread-safety note: this qputenv runs after QGuiApplication
    // construction, i.e. after the Wayland QPA's event thread has started.
    // Qt code reads the environment through qgetenv, which shares qputenv's
    // internal lock, so the Qt-side consumers are safe; a third-party library
    // calling raw getenv concurrently is a theoretical (glibc setenv may
    // grow environ) but unobserved race. Moving the export earlier is not an
    // option — the index is computed by enumerating THIS instance, which
    // needs the instance created first.
    uint32_t wantVendor = 0;
    uint32_t wantDevice = 0;
    if (parseGpuPciPair(gpuDevice, wantVendor, wantDevice)) {
        if (qEnvironmentVariableIsSet("QT_VK_PHYSICAL_DEVICE_INDEX")) {
            qCInfo(lcCore) << "QT_VK_PHYSICAL_DEVICE_INDEX already set in environment —"
                           << "leaving it in place over the configured GPU" << gpuDevice;
        } else {
            QVarLengthArray<VkPhysicalDevice, 8> devices(physicalDeviceCount);
            // VK_INCOMPLETE means the device count changed between the two
            // enumerate calls (e.g. eGPU hot-plug); the call still filled
            // physicalDeviceCount entries, so the pin can proceed over what
            // was returned.
            const VkResult fillResult = functions->vkEnumeratePhysicalDevices(vulkanInstance.vkInstance(),
                                                                              &physicalDeviceCount, devices.data());
            if (fillResult == VK_SUCCESS || fillResult == VK_INCOMPLETE) {
                bool found = false;
                // First match wins: two identical cards share a vendor:device
                // pair (the picker collapses them to one row, see
                // GpuDeviceList), so the pin lands on whichever duplicate the
                // loader enumerates first. Distinguishing duplicates would
                // need a bus-address identity, a config-format change.
                for (uint32_t i = 0; i < physicalDeviceCount; ++i) {
                    VkPhysicalDeviceProperties props = {};
                    functions->vkGetPhysicalDeviceProperties(devices[i], &props);
                    if (props.vendorID == wantVendor && props.deviceID == wantDevice) {
                        exportGpuVar("QT_VK_PHYSICAL_DEVICE_INDEX", QByteArray::number(i));
                        qCInfo(lcCore) << "Vulkan GPU preference applied: device" << i << props.deviceName << "("
                                       << gpuDevice << ")";
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    qCWarning(lcCore) << "Configured GPU" << gpuDevice
                                      << "not found among Vulkan physical devices — using Qt's default choice";
                }
            } else {
                qCWarning(lcCore) << "vkEnumeratePhysicalDevices failed (VkResult" << fillResult << ") — GPU preference"
                                  << gpuDevice << "not applied, using Qt's default choice";
            }
        }
    }

    app.setProperty(PVulkanInstanceProperty, QVariant::fromValue(&vulkanInstance));
    return true;
}
#endif

} // namespace PlasmaZones
