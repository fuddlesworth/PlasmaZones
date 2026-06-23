// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRendering/ZoneShaderNodeRhi.h>

#include <QImage>
#include <QList>
#include <QLoggingCategory>
#include <QPoint>
#include <QRect>
#include <QSize>

#include <atomic>

namespace PhosphorRendering {

Q_LOGGING_CATEGORY(lcZoneShader, "phosphorrendering.zoneshader")

namespace {

// Process-global counter for instance ids. Starts at 1 so 0 can serve as a
// "never seen a node" sentinel for hosts that compare against a default-
// constructed id. std::memory_order_relaxed: instance ids only need to be
// unique, not cross-thread-ordered against unrelated state.
std::atomic<quint64> g_zoneShaderNodeRhiInstanceCounter{0};

quint64 nextInstanceId()
{
    return g_zoneShaderNodeRhiInstanceCounter.fetch_add(1, std::memory_order_relaxed) + 1;
}

} // namespace

ZoneShaderNodeRhi::ZoneShaderNodeRhi(QQuickItem* item)
    : ShaderNodeRhi(item)
    , m_instanceId(nextInstanceId())
{
    // The ZoneUniformExtension is owned by the host item and pushed down each
    // frame through ShaderEffect::syncBasePropertiesToNode(). No extension
    // allocation happens here.
}

ZoneShaderNodeRhi::~ZoneShaderNodeRhi()
{
    // The parent's m_extraBindings map stores raw pointers to our
    // m_labelsTexture / m_labelsSampler. Those unique_ptr members run their
    // destructors AFTER this body returns (normal member-destruction order),
    // but the parent destructor runs AFTER them — so a hypothetical future
    // ~ShaderNodeRhi that walked m_extraBindings would see dangling pointers.
    // Call removeExtraBinding() now so the invariant "entries in
    // m_extraBindings always point to live resources" holds for the entire
    // teardown sequence.
    removeExtraBinding(1);
}

void ZoneShaderNodeRhi::setZoneCounts(int total, int highlighted)
{
    const int clampedTotal = qBound(0, total, MaxZones);
    const int clampedHighlighted = qBound(0, highlighted, clampedTotal);
    setAppField0(clampedTotal);
    setAppField1(clampedHighlighted);
}

void ZoneShaderNodeRhi::setLabelsTexture(const ZoneLabelTexture& labels)
{
    // The host item pushes the payload on every updatePaintNode (i.e. every
    // animated-shader repaint), but the GPU texture only needs re-uploading when
    // the labels actually change. Skip redundant re-uploads via value compare.
    // The m_labelsInitialized guard ensures the first push (and any push after
    // releaseResources) always uploads so the texture + slot-1 binding get
    // created; an already-pending dirty upload of the same payload still
    // completes on the next prepare(), so it's safe to skip here.
    if (m_labelsInitialized && labels == m_labels) {
        return;
    }
    m_labels = labels;
    m_labelsTextureDirty = true;
}

void ZoneShaderNodeRhi::uploadLabelsTexture(QRhi* rhi, QRhiCommandBuffer* cb)
{
    if (!m_labelsTextureDirty) {
        return;
    }

    // After K consecutive texture/sampler-create failures (whether on first
    // init OR on a later resize) we stop trying. This keeps prepare() from
    // running newTexture/newSampler + logging on every frame forever when the
    // underlying RHI has wedged. After an init give-up the SRB has no binding at
    // slot 1 (pipeline renders without labels); after a resize give-up the old
    // texture stays bound (labels frozen at the old size). releaseResources()
    // clears the latch so a fresh scene-graph cycle can recover.
    constexpr int kMaxInitAttempts = 5;
    if (m_labelsInitGaveUp) {
        m_labelsTextureDirty = false;
        return;
    }

    // The payload is sparse and the texture is screen-addressed. An empty
    // payload needs only a 1×1 transparent texture (no full-screen allocation).
    const bool hasLabels = !m_labels.isEmpty();
    const QSize targetSize = hasLabels ? m_labels.size : QSize(1, 1);

    // Initialize labels texture resources on first use. Only flip
    // m_labelsInitialized after BOTH resources create successfully — otherwise
    // a transient RHI failure (device lost, OOM) would leave the init block
    // skipped forever while m_labelsTextureDirty stays set.
    if (!m_labelsInitialized) {
        std::unique_ptr<QRhiTexture> tex(rhi->newTexture(QRhiTexture::RGBA8, targetSize));
        if (!tex->create()) {
            ++m_labelsInitFailureCount;
            if (m_labelsInitFailureCount >= kMaxInitAttempts) {
                qCWarning(lcZoneShader) << "labels texture init failed" << kMaxInitAttempts
                                        << "times — giving up; shader will render"
                                        << "without uZoneLabels (halo/chroma/glyph effects will be absent)";
                m_labelsInitGaveUp = true;
                m_labelsTextureDirty = false;
            }
            return; // retry next frame (or stay given-up)
        }
        std::unique_ptr<QRhiSampler> sam(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                                         QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if (!sam->create()) {
            ++m_labelsInitFailureCount;
            if (m_labelsInitFailureCount >= kMaxInitAttempts) {
                qCWarning(lcZoneShader) << "labels sampler init failed" << kMaxInitAttempts
                                        << "times — giving up; shader will render without uZoneLabels";
                m_labelsInitGaveUp = true;
                m_labelsTextureDirty = false;
            }
            return; // retry next frame; tex deleted by unique_ptr
        }
        m_labelsTexture = std::move(tex);
        m_labelsSampler = std::move(sam);
        m_labelsInitialized = true;
        m_labelsInitFailureCount = 0;
        setExtraBinding(1, m_labelsTexture.get(), m_labelsSampler.get());
        m_labelsNeedFullClear = true;
    } else if (m_labelsTexture->pixelSize() != targetSize) {
        std::unique_ptr<QRhiTexture> resized(rhi->newTexture(QRhiTexture::RGBA8, targetSize));
        if (!resized->create()) {
            // Same give-up cap as init: a persistently-failing resize would
            // otherwise re-run newTexture/create every repaint forever. On
            // give-up the old (wrong-size) texture stays bound — labels frozen
            // at the old size — rather than churning the GPU each frame.
            ++m_labelsInitFailureCount;
            if (m_labelsInitFailureCount >= kMaxInitAttempts) {
                qCWarning(lcZoneShader) << "labels texture resize failed" << kMaxInitAttempts
                                        << "times — giving up; labels frozen at the previous size";
                m_labelsInitGaveUp = true;
                m_labelsTextureDirty = false;
            }
            return; // keep dirty; retry next frame with old texture still bound
        }
        m_labelsInitFailureCount = 0; // recovered
        // Register the new binding BEFORE dropping the old texture. setExtraBinding
        // resets the SRB (in resetAllBindingsAndPipelines), so the SRB never
        // transiently holds a binding to the freed old QRhiTexture pointer.
        QRhiTexture* newPtr = resized.get();
        setExtraBinding(1, newPtr, m_labelsSampler.get());
        m_labelsTexture = std::move(resized);
        m_labelsNeedFullClear = true;
    }

    // Snapshot the latched flag; it is reset only after a successful upload so a
    // pool-exhaustion retry still full-clears the freshly (re)allocated texture.
    const bool needFullClear = m_labelsNeedFullClear;

    // ── Clear entries ────────────────────────────────────────────────────────
    // Clearing reuses one small transparent block uploaded in sub-regions, so it
    // never allocates a full-screen image. On (re)allocation the whole texture is
    // cleared in a grid; otherwise only the rects vacated since the last upload.
    // m_prevTileRects / m_labelsNeedFullClear are NOT mutated here — they are
    // committed only after a successful upload (below), so an early return leaves
    // the correct regions to be re-cleared on the retry. clearScratch must
    // outlive the resourceUpdate below (the batch holds an implicitly-shared
    // ref), so it is function-scoped.
    QList<QRhiTextureUploadEntry> clearEntries;
    QImage clearScratch;
    if (needFullClear) {
        constexpr int kClearBlock = 512;
        clearScratch = QImage(qMin(kClearBlock, targetSize.width()), qMin(kClearBlock, targetSize.height()),
                              QImage::Format_ARGB32_Premultiplied);
        clearScratch.fill(Qt::transparent);
        for (int by = 0; by < targetSize.height(); by += clearScratch.height()) {
            for (int bx = 0; bx < targetSize.width(); bx += clearScratch.width()) {
                const QSize block(qMin(clearScratch.width(), targetSize.width() - bx),
                                  qMin(clearScratch.height(), targetSize.height() - by));
                QRhiTextureSubresourceUploadDescription sub(clearScratch);
                sub.setSourceSize(block);
                sub.setDestinationTopLeft(QPoint(bx, by));
                clearEntries.append(QRhiTextureUploadEntry(0, 0, sub));
            }
        }
    } else if (!m_prevTileRects.isEmpty()) {
        int maxW = 1;
        int maxH = 1;
        for (const QRect& r : m_prevTileRects) {
            maxW = qMax(maxW, r.width());
            maxH = qMax(maxH, r.height());
        }
        clearScratch = QImage(maxW, maxH, QImage::Format_ARGB32_Premultiplied);
        clearScratch.fill(Qt::transparent);
        for (const QRect& r : m_prevTileRects) {
            QRhiTextureSubresourceUploadDescription sub(clearScratch);
            sub.setSourceSize(r.size());
            sub.setDestinationTopLeft(r.topLeft());
            clearEntries.append(QRhiTextureUploadEntry(0, 0, sub));
        }
    }

    // ── Tile entries ───────────────────────────────────────────────────────
    // Each glyph tile uploads directly to its position; no full-screen image is
    // ever materialised. Overlapping tiles (only from overlapping-zone layouts)
    // replace rather than alpha-blend, but zone numbers are centred + small so
    // they don't overlap in practice.
    QList<QRhiTextureUploadEntry> tileEntries;
    QList<QRect> newTileRects;
    if (hasLabels) {
        for (const ZoneLabelTile& tile : m_labels.tiles) {
            if (tile.image.isNull()) {
                continue;
            }
            QRhiTextureSubresourceUploadDescription sub(tile.image);
            sub.setDestinationTopLeft(tile.dest);
            tileEntries.append(QRhiTextureUploadEntry(0, 0, sub));
            newTileRects.append(QRect(tile.dest, tile.image.size()));
        }
    }

    if (clearEntries.isEmpty() && tileEntries.isEmpty()) {
        m_prevTileRects = newTileRects;
        m_labelsNeedFullClear = false;
        m_labelsTextureDirty = false;
        return;
    }

    // Acquire both batches up-front so the upload is all-or-nothing: clears must
    // precede tiles, and a partially-applied upload would leave the texture with
    // vacated-but-not-redrawn labels for a frame.
    QRhiResourceUpdateBatch* clearBatch = clearEntries.isEmpty() ? nullptr : rhi->nextResourceUpdateBatch();
    QRhiResourceUpdateBatch* tileBatch = tileEntries.isEmpty() ? nullptr : rhi->nextResourceUpdateBatch();
    if ((!clearEntries.isEmpty() && !clearBatch) || (!tileEntries.isEmpty() && !tileBatch)) {
        // Pool exhausted — release any acquired batch and retry next frame with
        // nothing applied (dirty stays set; m_prevTileRects and
        // m_labelsNeedFullClear unchanged so the retry re-clears correctly).
        if (clearBatch) {
            clearBatch->release();
        }
        if (tileBatch) {
            tileBatch->release();
        }
        return;
    }

    // Separate batches are applied in resourceUpdate() submission order, so a
    // tile re-occupying a just-vacated rect is never erased by its own clear.
    if (clearBatch) {
        QRhiTextureUploadDescription desc;
        desc.setEntries(clearEntries.cbegin(), clearEntries.cend());
        clearBatch->uploadTexture(m_labelsTexture.get(), desc);
        cb->resourceUpdate(clearBatch);
    }
    if (tileBatch) {
        QRhiTextureUploadDescription desc;
        desc.setEntries(tileEntries.cbegin(), tileEntries.cend());
        tileBatch->uploadTexture(m_labelsTexture.get(), desc);
        cb->resourceUpdate(tileBatch);
    }

    // Commit state only now that the upload is fully submitted.
    m_prevTileRects = newTileRects;
    m_labelsNeedFullClear = false;
    m_labelsTextureDirty = false;
}

void ZoneShaderNodeRhi::prepare()
{
    // Upload labels texture BEFORE parent's prepare(). The parent's
    // ensurePipeline() will include our extra binding (labels at 1) in its
    // SRB creation via appendExtraBindings().
    QRhiCommandBuffer* cb = commandBuffer();
    if (cb) {
        QRhi* rhi = cb->rhi();
        if (rhi) {
            uploadLabelsTexture(rhi, cb);
        }
    }

    // Delegate to parent for all base rendering.
    ShaderNodeRhi::prepare();
}

void ZoneShaderNodeRhi::releaseResources()
{
    qCInfo(lcZoneShader) << "releasing labels RHI resources";
    m_labelsTexture.reset();
    m_labelsSampler.reset();
    m_labelsInitialized = false;
    m_labelsTextureDirty = true;
    // Reset the give-up state so a fresh scene-graph cycle (e.g. after a
    // window hide/show that destroyed the QRhi) starts from a clean slate
    // — the previous wedge may have been backend-state that the new QRhi
    // doesn't share.
    m_labelsInitFailureCount = 0;
    m_labelsInitGaveUp = false;
    // The texture is gone; the next upload re-creates and fully clears it, so
    // drop the stale vacated-rect tracking from the old texture.
    m_prevTileRects.clear();
    removeExtraBinding(1);

    ShaderNodeRhi::releaseResources();
}

} // namespace PhosphorRendering
