// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dmabuftextureprovider.h"
#include "dmabufglimport.h"
#include "thumbnailurlutil.h"
#include "core/platform/logging.h"

#include <vulkan/vulkan.h>

#include <rhi/qrhi.h>

#include <QHash>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSGTexture>
#include <QVulkanDeviceFunctions>
#include <QVulkanInstance>

#include <functional>
#include <unistd.h>

namespace PlasmaZones {

namespace {

// ── DRM FourCC + modifier constants ─────────────────────────────────────────
// Defined locally to avoid a libdrm build dependency for four format codes.
constexpr uint32_t drmFourcc(char a, char b, char c, char d)
{
    return static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8) | (static_cast<uint32_t>(c) << 16)
        | (static_cast<uint32_t>(d) << 24);
}
constexpr uint32_t DrmFormatARGB8888 = drmFourcc('A', 'R', '2', '4');
constexpr uint32_t DrmFormatABGR8888 = drmFourcc('A', 'B', '2', '4');
constexpr uint32_t DrmFormatXRGB8888 = drmFourcc('X', 'R', '2', '4');
constexpr uint32_t DrmFormatXBGR8888 = drmFourcc('X', 'B', '2', '4');
// DrmFormatModInvalid is shared via dmabufthumbnail.h (PlasmaZones namespace).

struct FormatMapping
{
    VkFormat vkFormat = VK_FORMAT_UNDEFINED;
    QRhiTexture::Format rhiFormat = QRhiTexture::RGBA8;
    bool hasAlpha = true;
    bool ok = false;
};

// For the X-variants the fourth byte is UNDEFINED producer memory: the copy
// into the owned dst texture carries it verbatim, and only the
// hasAlphaChannel()==false report (which makes the scene graph pick an
// opaque material) keeps it invisible. Any future consumer that BLENDS these
// textures (Image with opacity < 1, a ShaderEffect sampling the provider)
// would read that byte as alpha and get arbitrary transparency — such a
// consumer must force alpha to 1 itself or extend the copy to do so.
FormatMapping mapDrmFormat(uint32_t fourcc)
{
    switch (fourcc) {
    case DrmFormatABGR8888:
        return {VK_FORMAT_R8G8B8A8_UNORM, QRhiTexture::RGBA8, true, true};
    case DrmFormatXBGR8888:
        return {VK_FORMAT_R8G8B8A8_UNORM, QRhiTexture::RGBA8, false, true};
    case DrmFormatARGB8888:
        return {VK_FORMAT_B8G8R8A8_UNORM, QRhiTexture::BGRA8, true, true};
    case DrmFormatXRGB8888:
        return {VK_FORMAT_B8G8R8A8_UNORM, QRhiTexture::BGRA8, false, true};
    default:
        return {};
    }
}

// ── Imported Vulkan resources, owned by the QSGTexture below ─────────────────
struct ImportedImage
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    QVulkanDeviceFunctions* df = nullptr;
    bool valid() const
    {
        return image != VK_NULL_HANDLE && memory != VK_NULL_HANDLE;
    }
};

/// Resolve vkGetMemoryFdPropertiesKHR once per VkDevice. requestTexture can
/// run on a different render thread per QQuickWindow, so the tiny cache is
/// mutex-guarded; the device outlives every import made against it.
PFN_vkGetMemoryFdPropertiesKHR memoryFdPropertiesFn(VkDevice device, QVulkanInstance* inst)
{
    static QMutex mutex;
    static QHash<VkDevice, PFN_vkGetMemoryFdPropertiesKHR> cache;
    QMutexLocker lock(&mutex);
    auto it = cache.constFind(device);
    if (it != cache.constEnd()) {
        return it.value();
    }
    auto getDeviceProcAddr =
        reinterpret_cast<PFN_vkGetDeviceProcAddr>(inst->getInstanceProcAddr("vkGetDeviceProcAddr"));
    if (!getDeviceProcAddr) {
        qCWarning(lcOverlay) << "dmabuf import: vkGetDeviceProcAddr unavailable from the Vulkan instance";
        return nullptr;
    }
    auto fn = reinterpret_cast<PFN_vkGetMemoryFdPropertiesKHR>(getDeviceProcAddr(device, "vkGetMemoryFdPropertiesKHR"));
    if (!fn) {
        qCWarning(lcOverlay) << "dmabuf import: vkGetMemoryFdPropertiesKHR unavailable (no VK_KHR_external_memory_fd)";
        return nullptr;
    }
    cache.insert(device, fn);
    return fn;
}

/// Import a single-plane dma-buf into a VkImage + bound (imported) device
/// memory. @p desc.fd is BORROWED for the call on every path: an internal
/// @c dup is made under the hood — vkAllocateMemory consumes the dup on
/// success, the failure paths close (or vkFreeMemory-release) it — and
/// @c desc.fd itself is never taken, so the caller retains ownership and
/// must close its own fd regardless of the outcome (DmabufTextureFactory's
/// destructor does exactly that). The returned ImportedImage is owned by the
/// caller (destroy via destroyImported).
ImportedImage importDmabuf(VkDevice device, VkPhysicalDevice physDev, QVulkanInstance* inst,
                           const DmabufThumbnailDesc& desc, VkFormat format)
{
    ImportedImage out;
    if (device == VK_NULL_HANDLE || !inst) {
        qCWarning(lcOverlay) << "dmabuf import: no Vulkan device/instance available";
        return out;
    }
    QVulkanDeviceFunctions* df = inst->deviceFunctions(device);
    if (!df) {
        qCWarning(lcOverlay) << "dmabuf import: QVulkanInstance::deviceFunctions returned null";
        return out;
    }
    // vkGetMemoryFdPropertiesKHR is a VK_KHR_external_memory_fd device function
    // not surfaced by QVulkanDeviceFunctions — resolved (and cached per device)
    // manually.
    auto pGetMemoryFdProperties = memoryFdPropertiesFn(device, inst);
    if (!pGetMemoryFdProperties) {
        return out;
    }

    const bool haveModifier = desc.modifier != DrmFormatModInvalid;
    if (!haveModifier) {
        // Without an explicit modifier the image is created VK_IMAGE_TILING_LINEAR
        // and the DRIVER picks the row pitch and start offset — desc.stride and
        // desc.offset are structurally unrepresentable on this path (the explicit
        // plane layout below is only chain-able together with a modifier). A
        // padded producer stride would render skewed and a non-zero offset would
        // sample the wrong region entirely, so reject anything the implicit
        // linear layout cannot express and let the consumer fall back.
        if (desc.stride != static_cast<uint32_t>(desc.width) * 4 || desc.offset != 0) {
            qCWarning(lcOverlay) << "dmabuf import: modifier-less buffer with padded stride" << desc.stride
                                 << "or offset" << desc.offset << "cannot be imported as LINEAR — rejecting";
            return out;
        }
    }

    VkSubresourceLayout planeLayout{};
    planeLayout.offset = desc.offset;
    planeLayout.rowPitch = desc.stride;

    VkImageDrmFormatModifierExplicitCreateInfoEXT modifierInfo{};
    modifierInfo.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
    modifierInfo.drmFormatModifier = desc.modifier;
    modifierInfo.drmFormatModifierPlaneCount = 1;
    modifierInfo.pPlaneLayouts = &planeLayout;

    VkExternalMemoryImageCreateInfo externalInfo{};
    externalInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    externalInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    externalInfo.pNext = haveModifier ? &modifierInfo : nullptr;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.pNext = &externalInfo;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {static_cast<uint32_t>(desc.width), static_cast<uint32_t>(desc.height), 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = haveModifier ? VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT : VK_IMAGE_TILING_LINEAR;
    // TRANSFER_SRC is mandatory: the imported image is consumed only as the
    // source of a GPU->GPU copy into our owned sampled texture (see
    // DmabufQsgTexture::commitTextureOperations, which records copyTexture(dst,
    // src) with src declared UsedAsTransferSource). createFrom() adopts this
    // VkImage as-is — it does NOT recreate it — so the RHI's transfer-source
    // intent is only honoured if the underlying image was created with
    // TRANSFER_SRC usage. Without it the copy reads an image never created for
    // transfer use: undefined behaviour that, on the NVIDIA release driver,
    // yields a blank texture with no validation error. SAMPLED is retained for
    // any backend path that samples the wrapper directly.
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    // initialLayout is UNDEFINED, yet createFrom() (below) declares the image's
    // current layout to the RHI as SHADER_READ_ONLY_OPTIMAL — deliberately, not
    // UNDEFINED. QRhi issues a layout transition before using src as a copy
    // source, and a transition *from* UNDEFINED may discard contents, which
    // would lose the imported thumbnail; declaring a defined source layout keeps
    // that transition content-preserving. Strict cross-driver correctness would
    // additionally acquire the image from VK_QUEUE_FAMILY_FOREIGN_EXT (the
    // producer's queue) with an ownership + layout barrier. That foreign-queue
    // acquire is omitted here as a documented portability constraint: it is
    // validated against the NVIDIA proprietary stack, which samples correctly
    // without it.
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image = VK_NULL_HANDLE;
    if (df->vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        // No support probe (vkGetPhysicalDeviceImageFormatProperties2 with
        // VkPhysicalDeviceImageDrmFormatModifierInfoEXT) precedes this call:
        // the create failure IS the intended support probe for an unsupported
        // (format, modifier, usage) triple — log enough to diagnose which one.
        qCWarning(lcOverlay) << "dmabuf import: vkCreateImage failed (format" << int(format) << "modifier 0x"
                             << QString::number(desc.modifier, 16) << "tiling"
                             << (haveModifier ? "modifier-explicit" : "linear") << ")";
        return out;
    }

    VkMemoryFdPropertiesKHR fdProps{};
    fdProps.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR;
    if (pGetMemoryFdProperties(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, desc.fd, &fdProps)
        != VK_SUCCESS) {
        qCWarning(lcOverlay) << "dmabuf import: vkGetMemoryFdPropertiesKHR failed";
        df->vkDestroyImage(device, image, nullptr);
        return out;
    }

    VkMemoryRequirements memReq{};
    df->vkGetImageMemoryRequirements(device, image, &memReq);
    const uint32_t typeBits = memReq.memoryTypeBits & fdProps.memoryTypeBits;
    if (typeBits == 0) {
        qCWarning(lcOverlay) << "dmabuf import: no compatible memory type";
        df->vkDestroyImage(device, image, nullptr);
        return out;
    }
    // Diagnosability aid for the LINEAR path: the driver computed its own
    // layout for the image, and binding an allocation of memReq.size against
    // a producer buffer that is actually smaller would read past its end on
    // a permissive driver. The stride/offset reject above keeps the two
    // layouts equal in practice; if a driver ever disagrees, this line is
    // the breadcrumb.
    if (!haveModifier) {
        qCDebug(lcOverlay) << "dmabuf import (LINEAR): driver memReq.size" << memReq.size << "vs producer bytes"
                           << (static_cast<qulonglong>(desc.stride) * desc.height);
    }
    // Prefer a DEVICE_LOCAL memory type; a host-visible/uncached type is
    // still functionally correct but makes the subsequent GPU copy crawl on
    // some drivers. Fall back to the lowest set bit when the physical device
    // is unavailable or no device-local type is importable.
    uint32_t memoryTypeIndex = UINT32_MAX;
    if (physDev != VK_NULL_HANDLE) {
        VkPhysicalDeviceMemoryProperties memProps{};
        inst->functions()->vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount && i < 32; ++i) {
            if ((typeBits & (1u << i))
                && (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                memoryTypeIndex = i;
                break;
            }
        }
    }
    if (memoryTypeIndex == UINT32_MAX) {
        for (uint32_t i = 0; i < 32; ++i) {
            if (typeBits & (1u << i)) {
                memoryTypeIndex = i;
                break;
            }
        }
    }

    // vkAllocateMemory consumes the fd on success; dup so our caller's fd
    // lifetime is independent of the allocation outcome.
    const int importFd = ::dup(desc.fd);
    if (importFd < 0) {
        // The likeliest cause is fd exhaustion — exactly the condition an
        // operator needs to see, since its user-visible face is a silent
        // icon fallback.
        qCWarning(lcOverlay) << "dmabuf import: dup() of the dma-buf fd failed (fd exhaustion?)";
        df->vkDestroyImage(device, image, nullptr);
        return out;
    }

    VkImportMemoryFdInfoKHR importInfo{};
    importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
    importInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    importInfo.fd = importFd;

    VkMemoryDedicatedAllocateInfo dedicatedInfo{};
    dedicatedInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicatedInfo.image = image;
    dedicatedInfo.pNext = &importInfo;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = &dedicatedInfo;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (df->vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        qCWarning(lcOverlay) << "dmabuf import: vkAllocateMemory failed";
        ::close(importFd); // allocation failed → Vulkan did not take the fd
        df->vkDestroyImage(device, image, nullptr);
        return out;
    }
    if (df->vkBindImageMemory(device, image, memory, 0) != VK_SUCCESS) {
        qCWarning(lcOverlay) << "dmabuf import: vkBindImageMemory failed";
        df->vkFreeMemory(device, memory, nullptr); // also releases the consumed fd
        df->vkDestroyImage(device, image, nullptr);
        return out;
    }

    out.image = image;
    out.memory = memory;
    out.device = device;
    out.df = df;
    return out;
}

// ── Custom QSGTexture owning the imported Vulkan image + RHI wrapper ─────────
// Owning the resources here (rather than via createTextureFromRhiTexture) gives
// deterministic lifetime: Qt destroys the QSGTexture on the render thread when
// the scene no longer references it, and our dtor frees the QRhiTexture, VkImage
// and imported memory on that same (correct) thread.
class DmabufQsgTexture : public QSGTexture
{
public:
    // @p src wraps the imported dma-buf (a transfer source aliasing the
    // producer buffer); @p dst is our OWN sampled texture. On the first frame
    // we copy src -> dst, then Qt only ever samples dst. This decouples the
    // displayed texture from the producer buffer: each candidate gets an
    // independent copy (so snap-assist can show many distinct thumbnails at
    // once). NOTE the decoupling holds only once the recorded copy has
    // actually EXECUTED, and the producer's pool round-robin gives that a
    // bounded margin (TexturePoolSize further exports), not a guarantee — a
    // burst with more candidates than producer pool slots can overwrite a
    // slot before this texture's first frame runs the copy, in which case
    // dst captures the newer slot content. Making that airtight needs a
    // copy-completion ack back to the producer, which the experimental
    // protocol does not carry. src is read only during the one copy, so a
    // LATER producer overwrite (after the copy ran) is harmless.
    DmabufQsgTexture(QRhiTexture* src, QRhiTexture* dst, std::function<void()> releaseImport, QSize size, bool hasAlpha)
        : m_src(src)
        , m_dst(dst)
        , m_releaseImport(std::move(releaseImport))
        , m_size(size)
        , m_hasAlpha(hasAlpha)
    {
    }

    ~DmabufQsgTexture() override
    {
        delete m_dst; // owned copy
        delete m_src; // wrapper over the imported native texture (does not own it)
        if (m_releaseImport) {
            // Frees the backend import resources on the render thread: the
            // Vulkan VkImage/VkDeviceMemory, or the GL texture + EGLImage.
            m_releaseImport();
        }
    }

    qint64 comparisonKey() const override
    {
        return static_cast<qint64>(reinterpret_cast<quintptr>(m_dst));
    }
    QRhiTexture* rhiTexture() const override
    {
        return m_dst;
    }
    QSize textureSize() const override
    {
        return m_size;
    }
    bool hasAlphaChannel() const override
    {
        return m_hasAlpha;
    }
    bool hasMipmaps() const override
    {
        return false;
    }

    void commitTextureOperations(QRhi* rhi, QRhiResourceUpdateBatch* resourceUpdates) override
    {
        Q_UNUSED(rhi)
        // One-shot latch, and deliberately so. The latch records "copy
        // RECORDED", not "copy executed" — Qt discarding the very batch
        // that carried the copy (aborted frame, swapchain out-of-date)
        // leaves dst blank permanently, and that residual is not
        // observable from here. It is also not repairable from here:
        // re-copying on a later commit would sample the producer's
        // possibly-recycled pool slot live (wrong content beats blank),
        // and after a device loss both wrapped textures belong to the dead
        // QRhi anyway, so a re-record would touch invalid resources. The
        // scene graph rebuilds textures from the provider on
        // re-initialisation, which is the real recovery path.
        if (m_copied || !m_src || !m_dst || !resourceUpdates) {
            return;
        }
        // Record the GPU->GPU copy of the imported dma-buf into our owned
        // texture, in Qt's own frame/batch (no nested QRhi frame, no CPU
        // readback). After this executes the producer buffer is no longer read.
        resourceUpdates->copyTexture(m_dst, m_src);
        m_copied = true;
    }

private:
    QRhiTexture* m_src = nullptr;
    QRhiTexture* m_dst = nullptr;
    std::function<void()> m_releaseImport;
    QSize m_size;
    bool m_hasAlpha = true;
    bool m_copied = false;
};

// ── Texture factory: owns the dup'd fd; imports on the render thread ─────────
class DmabufTextureFactory : public QQuickTextureFactory
{
public:
    // Takes OWNERSHIP of desc.fd (already an independent dup made by the
    // provider under its lock — see requestTexture). The fence is NOT needed
    // here: the consumer (OverlayService, via DmabufFenceWaiter) reveals the
    // thumbnail URL only after the producer's render has completed — except
    // on the defensive fence-less branches (no fence supplied by a direct
    // C++ caller, or a dup() failure under fd exhaustion), which reveal
    // immediately and accept the theoretical mid-render sample rather than
    // dropping the thumbnail. By the time QML loads a gated URL and this
    // import runs, the buffer is finished.
    explicit DmabufTextureFactory(const DmabufThumbnailDesc& desc)
        : m_desc(desc)
    {
        m_desc.fenceFd = -1;
    }

    ~DmabufTextureFactory() override
    {
        if (m_desc.fd >= 0) {
            ::close(m_desc.fd);
        }
    }

    QSize textureSize() const override
    {
        return QSize(m_desc.width, m_desc.height);
    }
    int textureByteCount() const override
    {
        // qsizetype intermediate so the 4 bpp multiply can't overflow int even
        // if the dimension ceiling is ever raised; both the D-Bus adaptor and
        // OverlayService::setWindowThumbnailDmabuf enforce the shared
        // SnapAssistThumbnailMaxDimension (1024) ceiling before a descriptor
        // can reach this factory.
        return static_cast<int>(static_cast<qsizetype>(m_desc.width) * m_desc.height * 4);
    }

    // QQuickTextureFactory::image() is deliberately NOT overridden: this is a
    // GPU-only transport, and the base implementation's null QImage is the
    // honest answer for CPU-side consumers (grabToImage of an item showing
    // the thumbnail yields an empty region rather than a readback).

    QSGTexture* createTexture(QQuickWindow* window) const override
    {
        if (!window || m_desc.fd < 0) {
            qCWarning(lcOverlay) << "dmabuf import: createTexture with no window or invalid fd";
            return nullptr;
        }
        QSGRendererInterface* rif = window->rendererInterface();
        QRhi* rhi = window->rhi();
        if (!rif || !rhi) {
            qCWarning(lcOverlay) << "dmabuf import: window has no renderer interface / RHI yet";
            return nullptr;
        }
        const FormatMapping fmt = mapDrmFormat(m_desc.fourcc);
        if (!fmt.ok) {
            qCWarning(lcOverlay) << "dmabuf import: unsupported DRM format 0x" << QString::number(m_desc.fourcc, 16);
            return nullptr;
        }

        // Import the dma-buf into a native texture per RHI backend, capturing a
        // backend-specific release into the QSGTexture. The rest (copy src->dst,
        // QSGTexture wrap) is backend-agnostic. The Vulkan importer lives
        // inline in this TU deliberately — unlike the GL importer
        // (dmabufglimport.cpp), which is split out because epoxy's EGL/GL
        // headers cannot coexist with Qt's RHI headers, Vulkan headers have
        // no such conflict, so a mirrored split would be structure without a
        // motive.
        quint64 nativeObject = 0;
        int nativeLayout = 0;
        std::function<void()> release;
        const QSGRendererInterface::GraphicsApi api = rif->graphicsApi();
        if (api == QSGRendererInterface::Vulkan) {
            QVulkanInstance* inst = window->vulkanInstance();
            auto* devicePtr = static_cast<VkDevice*>(rif->getResource(window, QSGRendererInterface::DeviceResource));
            if (!inst || !devicePtr || *devicePtr == VK_NULL_HANDLE) {
                qCWarning(lcOverlay) << "dmabuf import: Vulkan instance/device unavailable from the window";
                return nullptr;
            }
            // Physical device is optional (used only to prefer DEVICE_LOCAL
            // memory); a null resource degrades to lowest-set-bit selection.
            auto* physDevPtr =
                static_cast<VkPhysicalDevice*>(rif->getResource(window, QSGRendererInterface::PhysicalDeviceResource));
            const VkPhysicalDevice physDev = physDevPtr ? *physDevPtr : VK_NULL_HANDLE;
            const ImportedImage imported = importDmabuf(*devicePtr, physDev, inst, m_desc, fmt.vkFormat);
            if (!imported.valid()) {
                return nullptr;
            }
            nativeObject = reinterpret_cast<quint64>(imported.image);
            nativeLayout = static_cast<int>(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            release = [imported]() {
                destroyImported(imported);
            };
        } else if (api == QSGRendererInterface::OpenGL) {
            // NOTE: the GL release lambda (glDeleteTextures + eglDestroyImageKHR)
            // assumes a current GL context at QSGTexture destruction time. Qt's
            // GL-on-RHI scene graph destroys textures on the render thread with
            // the context current in the common paths; scene-graph invalidation
            // during window teardown is the unverified edge. If GL-backend
            // thumbnails ever leak at teardown, route the release through
            // QQuickWindow::scheduleRenderJob(..., BeforeSynchronizingStage).
            GlDmabufImport gl = importDmabufToGlTexture(m_desc);
            if (!gl.ok) {
                return nullptr;
            }
            nativeObject = static_cast<quint64>(gl.textureId);
            nativeLayout = 0;
            release = std::move(gl.release);
        } else {
            qCWarning(lcOverlay) << "dmabuf import: unsupported RHI backend" << int(api)
                                 << "(candidate falls back to its icon; the producer is NOT re-engaged — see the"
                                 << "class comment on DmabufTextureProvider)";
            return nullptr;
        }

        const QSize size(m_desc.width, m_desc.height);
        // src: wraps the imported native texture as a transfer source (aliases
        // the producer buffer, read only during the one-time copy below).
        QRhiTexture* src = rhi->newTexture(fmt.rhiFormat, size, 1, QRhiTexture::UsedAsTransferSource);
        if (!src) {
            qCWarning(lcOverlay) << "dmabuf import: source wrapper texture creation failed (RHI format"
                                 << int(fmt.rhiFormat) << "fourcc 0x" << QString::number(m_desc.fourcc, 16) << ")";
            release();
            return nullptr;
        }
        QRhiTexture::NativeTexture native;
        native.object = nativeObject;
        native.layout = nativeLayout;
        if (!src->createFrom(native)) {
            qCWarning(lcOverlay) << "dmabuf import: QRhiTexture::createFrom failed";
            delete src;
            release();
            return nullptr;
        }
        // dst: our OWN sampled texture; DmabufQsgTexture copies src -> dst on
        // the first frame, after which the producer buffer can be reused.
        QRhiTexture* dst = rhi->newTexture(fmt.rhiFormat, size, 1, {});
        if (!dst || !dst->create()) {
            qCWarning(lcOverlay) << "dmabuf import: owned destination texture creation failed (RHI format"
                                 << int(fmt.rhiFormat) << "fourcc 0x" << QString::number(m_desc.fourcc, 16) << ")";
            delete dst;
            delete src;
            release();
            return nullptr;
        }
        qCDebug(lcOverlay) << "dmabuf import: created GPU thumbnail texture" << size << "fourcc=0x"
                           << QString::number(m_desc.fourcc, 16) << "backend=" << int(api);
        return new DmabufQsgTexture(src, dst, std::move(release), size, fmt.hasAlpha);
    }

private:
    static void destroyImported(const ImportedImage& imported)
    {
        if (!imported.df || imported.device == VK_NULL_HANDLE) {
            return;
        }
        // The copy recorded in commitTextureOperations may still be in flight
        // on the GPU when the scene drops this texture (destroying a VkImage
        // referenced by a submitted command buffer is a spec violation with
        // driver-dependent fallout, up to device-lost). Waiting the device
        // idle before the destroy is heavyweight but correct on every
        // driver, and this path runs only at thumbnail teardown (snap-assist
        // dismissal / candidate replacement), never per frame. ASSUMPTION:
        // this render thread is the device's only submitter — Qt creates one
        // QRhi (and VkDevice) per QQuickWindow, so the wait-idle never races
        // another window's queue. If the scene graph ever shares one device
        // across windows, replace this with a per-import fence.
        imported.df->vkDeviceWaitIdle(imported.device);
        if (imported.image != VK_NULL_HANDLE) {
            imported.df->vkDestroyImage(imported.device, imported.image, nullptr);
        }
        if (imported.memory != VK_NULL_HANDLE) {
            imported.df->vkFreeMemory(imported.device, imported.memory, nullptr);
        }
    }

    DmabufThumbnailDesc m_desc;
};

} // namespace

DmabufTextureProvider::PendingEntry::~PendingEntry()
{
    if (desc.fd >= 0) {
        ::close(desc.fd);
    }
}

DmabufTextureProvider::DmabufTextureProvider()
    : QQuickImageProvider(QQuickImageProvider::Texture)
{
}

DmabufTextureProvider::~DmabufTextureProvider()
{
    clear();
}

QString DmabufTextureProvider::insert(const QString& compositorHandle, const DmabufThumbnailDesc& desc)
{
    if (compositorHandle.isEmpty() || desc.fd < 0) {
        return QString();
    }
    const QString key = ThumbnailUrl::normaliseHandle(compositorHandle);
    // Same key discipline as the pixel provider: an empty-after-normalise or
    // '/'-bearing handle would be stored under a key requestTexture's
    // `id.section('/', 0, 0)` parse can never reconstruct. Refuse it so the
    // producer sees accepted=false instead of latching an unservable entry.
    if (!ThumbnailUrl::isValidHandleKey(key)) {
        return QString();
    }
    const int storedFd = ::dup(desc.fd);
    if (storedFd < 0) {
        qCWarning(lcOverlay) << "dmabuf provider: dup() of the incoming dma-buf fd failed (fd exhaustion?)";
        return QString();
    }
    QMutexLocker lock(&m_mutex);
    // A fresh generation is un-revealed until its fence signals.
    m_revealed.remove(key);
    auto* entry = new PendingEntry;
    entry->desc = desc;
    entry->desc.fd = storedFd;
    entry->desc.fenceFd = -1; // the fence is consumed by the consumer's reveal-gate, not stored here
    // Pure cache-buster (see header): wrap after 2^32 inserts is benign because
    // requestTexture serves the latest buffer per handle regardless of the
    // generation in the URL, so a recycled value can't select a stale buffer.
    const quint32 gen = ++m_generation;
    entry->url = ThumbnailUrl::makeUrl(ProviderId, key, gen);
    const QString url = entry->url;
    // QCache::insert deletes any replaced entry (closing its fd via
    // ~PendingEntry) and, at capacity, LRU-evicts the same way. A refused
    // insert deletes the new entry immediately — return empty so the
    // producer's accepted-gated dedup never latches a handle we don't hold.
    if (!m_pending.insert(key, entry)) {
        return QString();
    }
    return url;
}

void DmabufTextureProvider::markRevealed(const QString& compositorHandle, const QString& url)
{
    const QString key = ThumbnailUrl::normaliseHandle(compositorHandle);
    QMutexLocker lock(&m_mutex);
    // Generation match: only reveal when the stored entry still IS the
    // generation whose fence signaled. The store is latest-wins per handle,
    // so an OLDER generation's late fence firing after a re-post must not
    // reveal the NEWER, possibly still-rendering buffer. (The object()
    // promotion is fine here — a reveal is a legitimate "this entry is
    // live" touch, unlike the warm-pass probe in urlFor.) An evicted entry
    // yields no match, so the set cannot accumulate keys the store no
    // longer holds either.
    const PendingEntry* entry = m_pending.object(key);
    if (entry && entry->url == url) {
        m_revealed.insert(key);
    }
}

QString DmabufTextureProvider::urlFor(const QString& compositorHandle) const
{
    if (compositorHandle.isEmpty()) {
        return QString();
    }
    const QString key = ThumbnailUrl::normaliseHandle(compositorHandle);
    QMutexLocker lock(&m_mutex);
    // Withhold un-revealed entries: their render fence may not have signaled
    // yet (the reveal gate marks them via markRevealed), or timed out and
    // never will. Test the side set FIRST — QCache::object() promotes to
    // MRU as a side effect, and probing an unservable entry on every
    // warm-cache pass used to pin it (and its dup'd fd) in the LRU
    // indefinitely.
    if (!m_revealed.contains(key)) {
        return QString();
    }
    const PendingEntry* entry = m_pending.object(key);
    return entry ? entry->url : QString();
}

QQuickTextureFactory* DmabufTextureProvider::requestTexture(const QString& id, QSize* size, const QSize& requestedSize)
{
    // requestedSize is intentionally ignored: the import is 1:1 by
    // construction (the producer already rendered at thumbnail size), so
    // there is nothing to downscale at load time.
    Q_UNUSED(requestedSize)
    const QString key = ThumbnailUrl::normaliseHandle(id.section(QLatin1Char('/'), 0, 0));
    DmabufThumbnailDesc desc;
    {
        QMutexLocker lock(&m_mutex);
        // The reveal gate applies here too, not only in urlFor: the store is
        // latest-wins per handle, so QML re-loading a PREVIOUS generation's
        // URL while a fresh un-revealed buffer is stored would otherwise be
        // served the new buffer mid-render — the exact sample the fence
        // exists to prevent. An un-revealed handle loads as an error and the
        // card falls back to its icon until the reveal pushes the new URL.
        if (!m_revealed.contains(key)) {
            if (size) {
                *size = QSize(0, 0);
            }
            return nullptr;
        }
        const PendingEntry* entry = m_pending.object(key);
        if (!entry || entry->desc.fd < 0) {
            if (size) {
                *size = QSize(0, 0);
            }
            return nullptr;
        }
        desc = entry->desc;
        // dup the stored fd UNDER the lock and hand the factory its own copy.
        // requestTexture runs on Qt's image-loader thread while insert()/clear()
        // run on the GUI thread; duping after releasing the lock would race a
        // concurrent insert/clear that closes (and possibly recycles) the fd.
        desc.fd = ::dup(entry->desc.fd);
    }
    if (desc.fd < 0) {
        if (size) {
            *size = QSize(0, 0);
        }
        return nullptr;
    }
    if (size) {
        *size = QSize(desc.width, desc.height);
    }
    return new DmabufTextureFactory(desc); // takes ownership of the dup'd fd
}

void DmabufTextureProvider::clear()
{
    QMutexLocker lock(&m_mutex);
    m_pending.clear(); // ~PendingEntry closes each stored fd
    m_revealed.clear();
}

} // namespace PlasmaZones
