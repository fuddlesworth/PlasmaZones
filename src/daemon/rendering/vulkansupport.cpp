// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vulkansupport.h"

#include "core/platform/logging.h"

#include <QLibrary>
#include <QQuickWindow>
#include <QVarLengthArray>
#include <QSGRendererInterface>
#if QT_CONFIG(vulkan)
#include <QVulkanFunctions>
#endif

namespace PlasmaZones {

namespace {

/// Parse a normalized "vendor:device" hex pair. Returns false for "auto",
/// the empty string, or anything malformed (defensive — callers pass
/// normalizeGpuDevice output, but env-editing users do not).
bool parseGpuPciPair(const QString& gpuDevice, uint32_t& vendorId, uint32_t& deviceId)
{
    if (gpuDevice.isEmpty() || gpuDevice == QLatin1String("auto")) {
        return false;
    }
    const qsizetype sep = gpuDevice.indexOf(QLatin1Char(':'));
    if (sep <= 0) {
        return false;
    }
    bool vendorOk = false;
    bool deviceOk = false;
    vendorId = gpuDevice.left(sep).toUInt(&vendorOk, 16);
    deviceId = gpuDevice.mid(sep + 1).toUInt(&deviceOk, 16);
    return vendorOk && deviceOk;
}

} // namespace

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
        qCInfo(lcCore) << "DRI_PRIME already set in environment — leaving it in place over the configured GPU"
                       << gpuDevice;
        return;
    }
    // GL has no device-selection API on Linux; the choice belongs to the
    // driver loader, and the two loaders speak different variables.
    // Mesa (AMD / Intel / nouveau+NVK) honors DRI_PRIME=vendor_id:device_id
    // (hex, no 0x; Mesa >= 22.0). The proprietary NVIDIA stack ignores
    // DRI_PRIME and instead offloads via __NV_PRIME_RENDER_OFFLOAD (plus the
    // GLX vendor pick for XWayland/GLX contexts). Exporting both sides is
    // safe — each loader only reads its own variables — so the selected GPU
    // wins whichever driver stack owns it.
    qputenv("DRI_PRIME", gpuDevice.toLatin1());
    constexpr uint32_t NvidiaPciVendorId = 0x10de;
    if (vendorId == NvidiaPciVendorId && !qEnvironmentVariableIsSet("__NV_PRIME_RENDER_OFFLOAD")) {
        qputenv("__NV_PRIME_RENDER_OFFLOAD", "1");
        if (!qEnvironmentVariableIsSet("__GLX_VENDOR_LIBRARY_NAME")) {
            qputenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia");
        }
    }
    qCInfo(lcCore) << "OpenGL GPU preference applied: DRI_PRIME =" << gpuDevice
                   << (vendorId == NvidiaPciVendorId ? "(with NVIDIA PRIME render offload)" : "");
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
    // the environment is an explicit user override and outranks the config.
    uint32_t wantVendor = 0;
    uint32_t wantDevice = 0;
    if (parseGpuPciPair(gpuDevice, wantVendor, wantDevice)
        && !qEnvironmentVariableIsSet("QT_VK_PHYSICAL_DEVICE_INDEX")) {
        QVarLengthArray<VkPhysicalDevice, 8> devices(physicalDeviceCount);
        if (functions->vkEnumeratePhysicalDevices(vulkanInstance.vkInstance(), &physicalDeviceCount, devices.data())
            == VK_SUCCESS) {
            bool found = false;
            for (uint32_t i = 0; i < physicalDeviceCount; ++i) {
                VkPhysicalDeviceProperties props = {};
                functions->vkGetPhysicalDeviceProperties(devices[i], &props);
                if (props.vendorID == wantVendor && props.deviceID == wantDevice) {
                    qputenv("QT_VK_PHYSICAL_DEVICE_INDEX", QByteArray::number(i));
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
        }
    }

    app.setProperty(PVulkanInstanceProperty, QVariant::fromValue(&vulkanInstance));
    return true;
}
#endif

} // namespace PlasmaZones
