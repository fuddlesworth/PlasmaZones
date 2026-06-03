// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dmabuftextureprovider.h"
#include "dmabufglimport.h"
#include "../core/logging.h"

#include <vulkan/vulkan.h>

#include <rhi/qrhi.h>

#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSGTexture>
#include <QVulkanDeviceFunctions>
#include <QVulkanInstance>

#include <array>
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

/// Import a single-plane dma-buf into a VkImage + bound (imported) device
/// memory. On success the fd is CONSUMED by vkAllocateMemory (Vulkan owns it);
/// on every failure path the caller still owns @p fd and must close it. The
/// returned ImportedImage is owned by the caller (destroy via vkDestroyImage +
/// vkFreeMemory through the same device functions).
ImportedImage importDmabuf(VkDevice device, QVulkanInstance* inst, const DmabufThumbnailDesc& desc, VkFormat format)
{
    ImportedImage out;
    if (device == VK_NULL_HANDLE || !inst) {
        return out;
    }
    QVulkanDeviceFunctions* df = inst->deviceFunctions(device);
    if (!df) {
        return out;
    }
    // vkGetMemoryFdPropertiesKHR is a VK_KHR_external_memory_fd device function
    // not surfaced by QVulkanDeviceFunctions — resolve it manually.
    auto getDeviceProcAddr =
        reinterpret_cast<PFN_vkGetDeviceProcAddr>(inst->getInstanceProcAddr("vkGetDeviceProcAddr"));
    if (!getDeviceProcAddr) {
        return out;
    }
    auto pGetMemoryFdProperties =
        reinterpret_cast<PFN_vkGetMemoryFdPropertiesKHR>(getDeviceProcAddr(device, "vkGetMemoryFdPropertiesKHR"));
    if (!pGetMemoryFdProperties) {
        qCWarning(lcOverlay) << "dmabuf import: vkGetMemoryFdPropertiesKHR unavailable (no VK_KHR_external_memory_fd)";
        return out;
    }

    const bool haveModifier = desc.modifier != DrmFormatModInvalid;

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
        qCWarning(lcOverlay) << "dmabuf import: vkCreateImage failed";
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
    uint32_t memoryTypeIndex = 0;
    for (uint32_t i = 0; i < 32; ++i) {
        if (typeBits & (1u << i)) {
            memoryTypeIndex = i;
            break;
        }
    }

    // vkAllocateMemory consumes the fd on success; dup so our caller's fd
    // lifetime is independent of the allocation outcome.
    const int importFd = ::dup(desc.fd);
    if (importFd < 0) {
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
    // once), and once the copy has run the producer is free to reuse its
    // buffer. src is only read during that one copy, so a later producer
    // overwrite of the aliased buffer is harmless.
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
    // here: the consumer (OverlayService, via DmabufFenceWaiter) only reveals
    // the thumbnail URL after the producer's render has completed, so by the
    // time QML loads it and we import, the buffer is already finished.
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
        // if the dimension ceiling is ever raised; bounded to <=1024^2 today.
        return static_cast<int>(static_cast<qsizetype>(m_desc.width) * m_desc.height * 4);
    }

    QSGTexture* createTexture(QQuickWindow* window) const override
    {
        if (!window || m_desc.fd < 0) {
            return nullptr;
        }
        // No fence wait here: OverlayService defers revealing the thumbnail URL
        // until the producer's render-completion fence has signaled (see
        // DmabufFenceWaiter), so the buffer is already fully rendered by the
        // time QML loads the URL and this import runs.
        QSGRendererInterface* rif = window->rendererInterface();
        QRhi* rhi = window->rhi();
        if (!rif || !rhi) {
            return nullptr;
        }
        const FormatMapping fmt = mapDrmFormat(m_desc.fourcc);
        if (!fmt.ok) {
            qCWarning(lcOverlay) << "dmabuf import: unsupported DRM format 0x" << QString::number(m_desc.fourcc, 16);
            return nullptr;
        }

        // Import the dma-buf into a native texture per RHI backend, capturing a
        // backend-specific release into the QSGTexture. The rest (copy src->dst,
        // QSGTexture wrap) is backend-agnostic.
        quint64 nativeObject = 0;
        int nativeLayout = 0;
        std::function<void()> release;
        const QSGRendererInterface::GraphicsApi api = rif->graphicsApi();
        if (api == QSGRendererInterface::Vulkan) {
            QVulkanInstance* inst = window->vulkanInstance();
            auto* devicePtr = static_cast<VkDevice*>(rif->getResource(window, QSGRendererInterface::DeviceResource));
            if (!inst || !devicePtr || *devicePtr == VK_NULL_HANDLE) {
                return nullptr;
            }
            const ImportedImage imported = importDmabuf(*devicePtr, inst, m_desc, fmt.vkFormat);
            if (!imported.valid()) {
                return nullptr;
            }
            nativeObject = reinterpret_cast<quint64>(imported.image);
            nativeLayout = static_cast<int>(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            release = [imported]() {
                destroyImported(imported);
            };
        } else if (api == QSGRendererInterface::OpenGL) {
            GlDmabufImport gl = importDmabufToGlTexture(m_desc);
            if (!gl.ok) {
                return nullptr;
            }
            nativeObject = static_cast<quint64>(gl.textureId);
            nativeLayout = 0;
            release = std::move(gl.release);
        } else {
            qCWarning(lcOverlay) << "dmabuf import: unsupported RHI backend" << int(api)
                                 << "(candidate falls back to pixels)";
            return nullptr;
        }

        const QSize size(m_desc.width, m_desc.height);
        // src: wraps the imported native texture as a transfer source (aliases
        // the producer buffer, read only during the one-time copy below).
        QRhiTexture* src = rhi->newTexture(fmt.rhiFormat, size, 1, QRhiTexture::UsedAsTransferSource);
        if (!src) {
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
            qCWarning(lcOverlay) << "dmabuf import: owned destination texture creation failed";
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
    const int storedFd = ::dup(desc.fd);
    if (storedFd < 0) {
        return QString();
    }
    const QString key = normaliseHandle(compositorHandle);
    QMutexLocker lock(&m_mutex);
    auto existing = m_pending.find(key);
    if (existing != m_pending.end() && existing->fd >= 0) {
        ::close(existing->fd); // replace: drop the previous generation's fd
    }
    DmabufThumbnailDesc owned = desc;
    owned.fd = storedFd;
    owned.fenceFd = -1; // the fence is consumed by the consumer's reveal-gate, not stored here
    m_pending.insert(key, owned);
    // Pure cache-buster (see header): wrap after 2^32 inserts is benign because
    // requestTexture serves the latest buffer per handle regardless of the
    // generation in the URL, so a recycled value can't select a stale buffer.
    const quint32 gen = ++m_generation;
    return makeUrl(key, gen);
}

QQuickTextureFactory* DmabufTextureProvider::requestTexture(const QString& id, QSize* size, const QSize& requestedSize)
{
    Q_UNUSED(requestedSize)
    const QString key = normaliseHandle(id.section(QLatin1Char('/'), 0, 0));
    DmabufThumbnailDesc desc;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_pending.constFind(key);
        if (it == m_pending.constEnd() || it->fd < 0) {
            if (size) {
                *size = QSize(0, 0);
            }
            return nullptr;
        }
        desc = it.value();
        // dup the stored fd UNDER the lock and hand the factory its own copy.
        // requestTexture runs on Qt's image-loader thread while insert()/clear()
        // run on the GUI thread; duping after releasing the lock would race a
        // concurrent insert/clear that closes (and possibly recycles) the fd.
        desc.fd = ::dup(it->fd);
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
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        if (it->fd >= 0) {
            ::close(it->fd);
        }
    }
    m_pending.clear();
}

QString DmabufTextureProvider::makeUrl(const QString& handle, quint32 generation)
{
    // Single multi-arg .arg() substitutes all placeholders in one pass, so a
    // handle containing a literal "%N" can't be re-substituted by a later
    // chained .arg() (chaining is a known QString footgun on dynamic middle
    // args). Handles are KWin window ids, but build the URL defensively anyway.
    return QStringLiteral("image://%1/%2/%3").arg(QString::fromLatin1(ProviderId), handle, QString::number(generation));
}

QString DmabufTextureProvider::normaliseHandle(const QString& handle)
{
    if (handle.size() >= 2 && handle.startsWith(QLatin1Char('{')) && handle.endsWith(QLatin1Char('}'))) {
        return handle.mid(1, handle.size() - 2);
    }
    return handle;
}

} // namespace PlasmaZones
