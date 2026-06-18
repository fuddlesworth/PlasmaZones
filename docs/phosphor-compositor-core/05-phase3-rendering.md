// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

# Phase 3: Qt RHI Rendering Pipeline

## Deliverables

- Compositor-side rendering using Qt RHI with Vulkan primary backend
- GBM buffer → Vulkan import → QRhiTexture render target
- Surface buffer import: SHM (CPU upload), linux-dmabuf (zero-copy)
- Scene graph traversal → draw command generation
- Damage-based partial rendering (only re-render dirty regions)
- Direct scanout detection and bypass
- linux-dmabuf-v1 protocol server implementation

## Class Hierarchy

```
CompositorRenderer
├── owns QRhi* instance (Vulkan backend)
├── owns OutputRenderer per DrmOutput
└── owns TextureCache (surface buffer → QRhiTexture mapping)

OutputRenderer
├── references DrmOutput (render target source)
├── owns RenderTarget (QRhiTextureRenderTarget wrapping GBM buffer)
├── owns QuadPipeline (shader + vertex buffer for textured quads)
├── reads SceneGraph to determine draw order
└── uses DamageTracker to limit render region

TextureCache
├── maps (wl_buffer, bufferAge) → SurfaceTexture
├── handles SHM upload (glTexSubImage equivalent via QRhi)
├── handles DMA-BUF import (Vulkan external memory)
└── evicts stale textures on buffer destroy

SurfaceTexture
├── QRhiTexture* (GPU texture)
├── source type (SHM or DMA-BUF)
├── buffer dimensions, format, stride
└── dirty flag (needs re-upload for SHM)

QuadPipeline
├── QRhiGraphicsPipeline* (alpha blend, textured quad)
├── QRhiShaderResourceBindings* (per-draw texture + UBO)
├── vertex shader (passthrough with transform uniform)
└── fragment shader (texture sample with opacity uniform)

RenderTarget
├── QRhiTexture* (imported from GBM buffer's dma-buf fd)
├── QRhiTextureRenderTarget*
├── QRhiRenderPassDescriptor*
└── swap logic (double-buffered: target A / target B)

DirectScanoutDetector
├── checks if single fullscreen surface covers output
├── validates format/modifier compatibility with DRM plane
├── triggers plane assignment bypass (skip render entirely)
└── falls back to composited path on TEST_ONLY commit failure
```

## File Map

```
libs/phosphor-compositor-core/src/render/
├── CMakeLists.txt
├── compositor_renderer.h
├── compositor_renderer.cpp
├── output_renderer.h
├── output_renderer.cpp
├── render_target.h
├── render_target.cpp
├── surface_texture.h
├── surface_texture.cpp
├── texture_cache.h
├── texture_cache.cpp
├── quad_pipeline.h
├── quad_pipeline.cpp
├── shaders/
│   ├── surface.vert.glsl       — passthrough with MVP uniform
│   ├── surface.frag.glsl       — texture sample + opacity
│   ├── solid.frag.glsl         — solid color (backgrounds, debug)
│   └── compile_shaders.cmake   — SPIR-V compilation via Qt shader tools
├── direct_scanout.h
├── direct_scanout.cpp
├── vulkan_import.h             — VkImage from dma-buf helper
└── vulkan_import.cpp

libs/phosphor-compositor-core/src/protocols/
├── linux_dmabuf.h              — zwp_linux_dmabuf_v1 server
└── linux_dmabuf.cpp
```

## Vulkan Initialization via Qt RHI

```cpp
bool CompositorRenderer::initialize(DrmDevice& drm) {
    // Create Vulkan instance (Qt manages this)
    QRhiVulkanInitParams vkParams;
    // We need the physical device matching our DRM device
    // Qt RHI selects the device; we validate it matches DRM's render node
    vkParams.inst = &m_vulkanInstance;

    m_rhi = QRhi::create(QRhi::Vulkan, &vkParams);
    if (!m_rhi) {
        // Fallback: try OpenGL ES via EGL on the GBM device
        return initializeGLFallback(drm);
    }

    // Verify the Vulkan device matches our DRM GPU
    // (compare PCI bus ID from Vulkan physical device props vs DRM device)
    if (!validateDeviceMatch(m_rhi, drm)) {
        qWarning("Vulkan device mismatch — falling back to EGL");
        return initializeGLFallback(drm);
    }

    return true;
}
```

## GBM Buffer → Vulkan Import

```cpp
// vulkan_import.h
struct ImportedBuffer {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    QRhiTexture* rhiTexture = nullptr;  // wraps the VkImage
};

ImportedBuffer importGbmBuffer(QRhi* rhi, gbm_bo* bo) {
    int fd = gbm_bo_get_fd(bo);
    if (fd < 0) return {};

    // Scope guard ensures fd is closed on any early-return path.
    // Vulkan takes ownership of fd on successful vkAllocateMemory — disarm after that.
    bool fdOwned = true;
    auto closeFdOnExit = qScopeGuard([&] { if (fdOwned) close(fd); });

    uint32_t width = gbm_bo_get_width(bo);
    uint32_t height = gbm_bo_get_height(bo);
    uint32_t stride = gbm_bo_get_stride(bo);
    uint64_t modifier = gbm_bo_get_modifier(bo);
    uint32_t format = gbm_bo_get_format(bo);  // e.g. GBM_FORMAT_XRGB8888

    // Create VkImage with external memory
    // Required extensions: VK_KHR_external_memory_fd, VK_EXT_image_drm_format_modifier
    VkImageDrmFormatModifierExplicitCreateInfoEXT modInfo{};
    modInfo.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
    modInfo.drmFormatModifier = modifier;
    modInfo.drmFormatModifierPlaneCount = 1;
    VkSubresourceLayout planeLayout{};
    planeLayout.offset = 0;
    planeLayout.rowPitch = stride;
    modInfo.pPlaneLayouts = &planeLayout;

    VkExternalMemoryImageCreateInfo extInfo{};
    extInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    extInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    extInfo.pNext = &modInfo;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.pNext = &extInfo;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = drmFormatToVkFormat(format);
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    // vkCreateImage — on failure: scope guard closes fd, return empty
    VkDevice device = rhi->nativeHandles()->dev;  // Vulkan device from QRhi
    VkImage image = VK_NULL_HANDLE;
    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS)
        return {};

    // vkAllocateMemory with VkImportMemoryFdInfoKHR — Vulkan takes fd ownership on success
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkMemoryAllocateInfo allocInfo = buildImportAllocInfo(image, fd);
    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyImage(device, image, nullptr);
        return {};
    }
    fdOwned = false;  // Vulkan now owns the fd

    if (vkBindImageMemory(device, image, memory, 0) != VK_SUCCESS) {
        vkFreeMemory(device, memory, nullptr);
        vkDestroyImage(device, image, nullptr);
        return {};
    }

    // Wrap in QRhiTexture (format-aware: match DRM fourcc to QRhi format)
    QRhiTexture::Format rhiFormat = drmFormatToRhiFormat(format);
    auto* tex = rhi->newTexture(rhiFormat, QSize(width, height), 1,
                                QRhiTexture::RenderTarget);
    tex->createFrom({/* native VkImage handle */});

    closeFdOnExit.dismiss();
    return {image, memory, tex};
}
```

## Render Pass: Per-Output Frame

```cpp
void OutputRenderer::renderFrame() {
    // 1. Determine what needs rendering
    pixman_region32_t damage;
    pixman_region32_init(&damage);
    m_damageTracker->accumulatedDamage(m_renderTarget.bufferAge(), &damage);
    if (pixman_region32_not_empty(&damage) == 0) {
        pixman_region32_fini(&damage);
        return;
    }

    // 2. Check direct scanout opportunity
    if (m_directScanout.tryDirectScanout(m_output, m_sceneGraph)) {
        // Buffer assigned directly to DRM plane — no compositing needed
        m_damageTracker->frameRendered();
        pixman_region32_fini(&damage);
        return;
    }

    // 3. Begin render pass
    QRhiResourceUpdateBatch* uploadBatch = m_rhi->nextResourceUpdateBatch();
    // Process pending texture uploads (SHM buffers queued since last frame)
    m_textureCache->flushUploads(uploadBatch);

    QRhiCommandBuffer* cmdBuf;
    if (m_rhi->beginOffscreenFrame(&cmdBuf) != QRhi::FrameOpSuccess) {
        pixman_region32_fini(&damage);
        return;  // GPU lost or swap chain invalid — skip this frame
    }
    cmdBuf->beginPass(m_renderTarget.currentRenderTarget(), Qt::black, {1.0f, 0}, uploadBatch);

    // 4. Bind the compositing pipeline, then set scissor to damage region
    cmdBuf->setGraphicsPipeline(m_compositePipeline);
    cmdBuf->setShaderResources();
    setScissorFromDamage(cmdBuf, damage);

    // 5. Traverse scene graph back-to-front
    auto nodes = m_sceneGraph->nodesForOutput(m_output, SceneGraph::BackToFront);
    for (SceneNode* node : nodes) {
        if (!node->visible()) continue;
        if (!intersectsDamage(node->globalBounds(), damage)) continue;

        if (auto* surf = node->asSurface()) {
            drawSurface(cmdBuf, surf);
        } else if (auto* rect = node->asRect()) {
            drawRect(cmdBuf, rect);
        }
    }

    // 6. End pass, submit
    cmdBuf->endPass();
    m_rhi->endOffscreenFrame();

    // 7. Submit to DRM
    gbm_bo* bo = m_renderTarget.currentBuffer();
    m_output->presentFrame(bo);

    // 8. Rotate damage history + clean up
    m_damageTracker->frameRendered();
    pixman_region32_fini(&damage);
}
```

## Surface Texture Management

### SHM Buffer Upload

```cpp
void TextureCache::importShmBuffer(Surface* surface, wl_shm_buffer* shmBuf) {
    wl_shm_buffer_begin_access(shmBuf);

    void* data = wl_shm_buffer_get_data(shmBuf);
    int width = wl_shm_buffer_get_width(shmBuf);
    int height = wl_shm_buffer_get_height(shmBuf);
    int stride = wl_shm_buffer_get_stride(shmBuf);
    uint32_t format = wl_shm_buffer_get_format(shmBuf);

    SurfaceTexture& tex = m_textures[surface];

    if (!tex.texture || tex.width != width || tex.height != height) {
        // Recreate texture (old texture released via unique_ptr reset)
        tex.texture.reset(m_rhi->newTexture(shmFormatToRhiFormat(format),
                                             QSize(width, height)));
        tex.texture->create();
        tex.width = width;
        tex.height = height;
    }

    // Upload pixel data
    QRhiTextureSubresourceUploadDescription desc(data, stride * height);
    desc.setSourceSize(QSize(width, height));
    desc.setDataStride(stride);

    QRhiTextureUploadDescription uploadDesc({0, 0, desc});
    // Queue upload (executed at next beginFrame)
    m_pendingUploads.append({tex.texture, uploadDesc});

    wl_shm_buffer_end_access(shmBuf);
}
```

### DMA-BUF Import (Zero-Copy)

```cpp
void TextureCache::importDmaBuf(Surface* surface, const DmaBufAttributes& attrs) {
    if (attrs.width <= 0 || attrs.height <= 0 || attrs.fd < 0) return;

    SurfaceTexture& tex = m_textures[surface];

    // attrs: fd, width, height, stride, format, modifier, offset (per plane)
    // Import via Vulkan external memory (same path as GBM import but for client buffers)

    int importFd = dup(attrs.fd);
    if (importFd < 0) return;

    // Scope guard: close dup'd fd on any failure before Vulkan takes ownership
    bool fdOwned = true;
    auto closeFdOnExit = qScopeGuard([&] { if (fdOwned) close(importFd); });

    VkImageCreateInfo imageInfo = buildDmaBufImageInfo(attrs);
    VkImportMemoryFdInfoKHR importInfo{};
    importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
    importInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    importInfo.fd = importFd;

    // create VkImage, allocate memory (takes fd ownership on success), bind
    // On vkAllocateMemory success: fdOwned = false (Vulkan owns it)
    // On any failure: scope guard closes fd, return without modifying tex

    auto* wrappedTexture = createTextureFromImport(imageInfo, importInfo, attrs);
    if (!wrappedTexture) return;
    fdOwned = false;

    tex.texture.reset(wrappedTexture);
    tex.width = attrs.width;
    tex.height = attrs.height;
    tex.source = TextureSource::DmaBuf;
    closeFdOnExit.dismiss();
}
```

## linux-dmabuf-v1 Protocol Server

```cpp
// Server advertises supported formats + modifiers
class LinuxDmaBufGlobal {
    // On bind: send supported format/modifier pairs per tranche
    // Tranche = per-device (the GPU we render on)
    void sendFormats(wl_resource* resource) {
        // Send tranche header (target device, flags)
        zwp_linux_dmabuf_feedback_v1_send_tranche_target_device(resource, m_drmDevice);
        zwp_linux_dmabuf_feedback_v1_send_tranche_formats(resource, m_formatTable);
        zwp_linux_dmabuf_feedback_v1_send_tranche_done(resource);
        zwp_linux_dmabuf_feedback_v1_send_done(resource);
    }

    // Client creates buffer via:
    //   create_params → add(fd, plane, offset, stride, modifier) → create(w, h, format)
    // Server validates params, creates DmaBufBuffer, returns wl_buffer
    void handleCreateParams(wl_client* client, wl_resource* resource);
    void handleParamsAdd(DmaBufParams* params, int fd, uint32_t plane,
                         uint32_t offset, uint32_t stride, uint64_t modifier);
    void handleParamsCreate(DmaBufParams* params, int w, int h, uint32_t format);
};
```

## Direct Scanout

```cpp
class DirectScanoutDetector {
public:
    /// Returns true if direct scanout was successfully assigned.
    bool tryDirectScanout(DrmOutput* output, SceneGraph* scene) {
        // 1. Find the topmost opaque surface on this output
        auto* candidate = findFullscreenCandidate(output, scene);
        if (!candidate) return false;

        // 2. Verify it's the only visible content (no overlays, no cursor over it)
        if (hasOverlappingContent(output, candidate, scene)) return false;

        // 3. Get its dma-buf attributes
        auto* tex = m_textureCache->textureFor(candidate->surface());
        if (!tex || tex->source != TextureSource::DmaBuf) return false;

        // 4. TEST_ONLY atomic commit: try assigning this buffer to the primary plane
        uint32_t fbId = drmModeAddFB2WithModifiers(output->device().fd(), ...);
        if (fbId == 0) return false;

        auto* req = drmModeAtomicAlloc();
        if (!req) {
            drmModeRmFB(output->device().fd(), fbId);
            return false;
        }
        // ... set plane properties to client's buffer ...
        int ret = drmModeAtomicCommit(output->device().fd(), req,
                                       DRM_MODE_ATOMIC_TEST_ONLY, nullptr);
        drmModeAtomicFree(req);

        if (ret != 0) {
            // Format/modifier not scanout-capable — fall back
            drmModeRmFB(output->device().fd(), fbId);
            return false;
        }

        // 5. Success — do the real commit
        output->presentDirectScanout(fbId);
        return true;
    }

private:
    SceneSurface* findFullscreenCandidate(DrmOutput* output, SceneGraph* scene);
    bool hasOverlappingContent(DrmOutput* output, SceneSurface* candidate, SceneGraph* scene);
};
```

## Quad Shader

```glsl
// surface.vert.glsl
#version 450

layout(location = 0) in vec2 inPosition;   // unit quad [0,1]
layout(location = 1) in vec2 inTexCoord;

layout(std140, binding = 0) uniform Uniforms {
    mat4 projection;      // output-space orthographic
    vec4 srcRect;         // texture source rect (for viewporter crop)
    vec4 dstRect;         // destination rect in output pixels
    float opacity;
    int transform;        // buffer transform (0-7)
};

layout(location = 0) out vec2 vTexCoord;

void main() {
    vec2 pos = dstRect.xy + inPosition * dstRect.zw;
    gl_Position = projection * vec4(pos, 0.0, 1.0);
    vTexCoord = applyTransform(inTexCoord, srcRect, transform);
}
```

```glsl
// surface.frag.glsl
#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D surfaceTexture;
layout(std140, binding = 0) uniform Uniforms {
    mat4 projection;
    vec4 srcRect;
    vec4 dstRect;
    float opacity;
    int transform;
};

void main() {
    vec4 color = texture(surfaceTexture, vTexCoord);
    // Pre-multiplied alpha output
    fragColor = color * opacity;
}
```

## Data Flow: Client Buffer → Screen

```
Client                     Compositor                         GPU/DRM
  │                            │                                │
  │ wl_surface.attach(buf)     │                                │
  │ wl_surface.commit()  ────→ │                                │
  │                            │ Import buffer:                 │
  │                            │   SHM: CPU → QRhiTexture       │
  │                            │   DMA-BUF: fd → VkImage        │
  │                            │                                │
  │                            │ Mark surface damaged            │
  │                            │ Mark output damaged             │
  │                            │                                │
  │                            │ [on next frame]:               │
  │                            │ Begin render pass ────────────→│
  │                            │ Draw textured quads            │
  │                            │ End render pass               │
  │                            │ Submit to DRM ───────────────→│
  │                            │                                │ Page flip
  │                            │              ←─────────────────│ Flip done
  │                            │                                │
  │  ←─── frame callback ─────│                                │
  │ (render next frame)        │                                │
```

## Verification

1. `weston-simple-shm` renders solid color on real hardware via SHM path
2. `mpv --vo=gpu-next` plays video via dma-buf (zero-copy, verify no tearing)
3. FPS counter shows consistent vsync-locked delivery (monitor refresh Hz)
4. Debug overlay: highlight damaged regions per frame (should be minimal at idle)
5. Direct scanout: fullscreen `mpv` → verify GPU load drops to near-zero (compositor bypassed)
6. Multi-output: different content renders independently on each output
7. Unit tests:
   - `test_texture_cache` — SHM import produces valid texture, eviction works
   - `test_damage_partial` — only damaged quads are drawn (mock pipeline)
   - `test_direct_scanout_detection` — single fullscreen surface detected correctly
   - `test_buffer_transform` — all 8 transforms produce correct UV mapping
