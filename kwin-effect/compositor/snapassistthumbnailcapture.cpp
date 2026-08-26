// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapassistthumbnailcapture.h"

#include "plasmazoneseffect/shader_internal.h"

#include <PhosphorProtocol/ServiceConstants.h>

// epoxy MUST precede any other GL/EGL include so it can interpose the
// function pointers. It also pulls in the EGL/GL types and the
// EGL_MESA_image_dma_buf_export entry points used by exportTextureToDmabuf.
#include <epoxy/gl.h>
#include <epoxy/egl.h>

#include <core/region.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <effect/effect.h>
#include <opengl/eglcontext.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <opengl/glframebuffer.h>
#include <opengl/gltexture.h>
#include <scene/item.h>
#include <scene/windowitem.h>

#include <algorithm>
#include <cstring>
#include <unistd.h>

#include <QByteArray>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QElapsedTimer>
#include <QImage>
#include <QLoggingCategory>
#include <QPoint>
#include <QTimer>
#include <QVariant>

Q_LOGGING_CATEGORY(lcSnapAssistCapture, "kwin.effect.plasmazones.snapassist.capture", QtWarningMsg)
// Info-level by default so PLASMAZONES_THUMBNAIL_TRACE alone surfaces the
// lines; every emit is additionally gated on m_traceEnabled so the category
// stays silent without the env var.
Q_LOGGING_CATEGORY(lcSnapAssistTrace, "kwin.effect.plasmazones.snapassist.trace", QtInfoMsg)

namespace PlasmaZones {

namespace {
/// Settle delay before the first capture attempt for a candidate. A freshly
/// mapped window may not have a renderable compositor frame the instant it is
/// queued; one frame at 60Hz (~16ms) is reliably enough for drawWindow to read
/// non-empty content, while not adding meaningful latency to the snap-assist UI
/// (which already shows icons immediately and fades thumbs in asynchronously).
constexpr int RENDER_SETTLE_MS = 16;
/// Retry delay used when the first render produced an empty buffer. Four
/// frames at 60Hz: long enough for a stalled compositor frame to clear,
/// short enough that the user still sees the thumbnail before the eye
/// notices the fallback icon.
constexpr int RENDER_RETRY_MS = 64;
/// Consecutive dma-buf capture failures (export failure or daemon import
/// rejection) before the session permanently falls back to the raw-pixel path.
/// >1 so a single transient bad frame doesn't disable the zero-copy path,
/// while a genuine capability gap (every frame fails) trips it quickly.
constexpr int DmabufFailureThreshold = 2;
/// Smallest useful thumbnail axis. A fit below this (an extreme-aspect
/// window rounding one axis toward 1px) produces a sliver that passes the
/// daemon's `width > 0` validation and then latches into the dedup window
/// as a useless thumbnail — treat it as a capture failure so the candidate
/// falls back to its icon instead.
constexpr int MinThumbnailAxisPx = 8;

/// RAII holder for a raw fd. exportTextureToDmabuf juggles a dma-buf fd and
/// a fence fd across six distinct early-return paths; a scoped owner makes
/// "every path closes what it opened" structural instead of per-branch
/// bookkeeping the next edit can silently break.
struct ScopedFd
{
    int fd = -1;
    ScopedFd() = default;
    explicit ScopedFd(int f)
        : fd(f)
    {
    }
    ~ScopedFd()
    {
        if (fd >= 0) {
            ::close(fd);
        }
    }
    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;
    /// Transfer ownership out (the success path hands the fd to the caller).
    int release()
    {
        const int f = fd;
        fd = -1;
        return f;
    }
    bool valid() const
    {
        return fd >= 0;
    }
};

/// True when every pixel of an ARGB32 image has zero alpha. A cleared FBO
/// whose drawWindow produced nothing reads back as a valid, fully transparent
/// image — isNull() cannot distinguish that from a real capture, so the
/// "window had no renderable frame yet" retry must test content, not
/// nullness. 256² is 256 KiB; a linear byte scan at capture cadence is
/// negligible next to the GPU readback that produced the image.
bool isFullyTransparent(const QImage& image)
{
    if (image.isNull() || image.format() != QImage::Format_ARGB32) {
        return false;
    }
    for (int y = 0; y < image.height(); ++y) {
        const auto* line = reinterpret_cast<const quint32*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (line[x] & 0xff000000u) {
                return false;
            }
        }
    }
    return true;
}
} // namespace

SnapAssistThumbnailCapture::SnapAssistThumbnailCapture(QObject* parent)
    : QObject(parent)
    , m_dmabufEnabled(PhosphorProtocol::Service::snapAssistDmabufThumbnailsEnabled())
    , m_traceEnabled(PhosphorProtocol::Service::snapAssistThumbnailTraceEnabled())
{
    if (m_traceEnabled) {
        qCInfo(lcSnapAssistTrace) << "PLASMAZONES_THUMBNAIL_TRACE set — per-capture stage timings enabled, path:"
                                  << (m_dmabufEnabled ? "dma-buf" : "pixels");
    }
    // Only the non-default state is worth a line. The dma-buf path is on by
    // default, so announcing it every session would be noise; the operator who
    // needs to know is the one who turned it off, or the one reading why a
    // session is on pixels.
    if (!m_dmabufEnabled) {
        qCInfo(lcSnapAssistCapture) << "PLASMAZONES_DMABUF_THUMBNAILS=0 — snap-assist thumbnails will be posted as "
                                       "raw pixels (zero-copy dma-buf path disabled).";
    }
}

SnapAssistThumbnailCapture::~SnapAssistThumbnailCapture()
{
    // ~GLTexture issues glDeleteTextures, which needs a current GL context —
    // the default destructor ran with no context current, silently leaking
    // the flip scratch on effect unload. Make the compositor context current
    // for the teardown. When that fails (compositor already torn down) there
    // is no explicit fallback: the member falls through to ordinary
    // destruction below, and the driver reclaims its storage when the
    // context itself dies.
    if (m_flipScratch && KWin::effects && KWin::effects->makeOpenGLContextCurrent()) {
        m_flipScratch.reset();
        KWin::effects->doneOpenGLContextCurrent();
    }
}

void SnapAssistThumbnailCapture::captureCandidates(const QVector<Candidate>& candidates, QSize maxSize)
{
    // Replace any pending queue from a prior snap-assist invocation. Capture
    // is bounded by the daemon's QCache LRU; we additionally skip handles we
    // posted recently, on the assumption the daemon still holds them. The
    // assumption is allowed to be wrong — see comment on @c RecentPostedCapacity
    // for the bounded failure mode and @ref resetRecentlyPosted for the
    // daemon-restart / cache-trim escape hatches.
    m_queue.clear();
    // Invalidate any in-flight capture timer from the replaced queue: its
    // lambda compares this generation on fire and, when stale, skips the
    // render/post for its (no-longer-candidate) window and just advances the
    // queue. Without the check, the stale capture burns exactly the render
    // budget the queue replacement exists to save — and posts a thumbnail
    // for a window that is no longer a candidate.
    ++m_queueGeneration;
    // The daemon refuses anything over its shared per-axis ceiling, and a
    // refusal never marks the handle recently-posted — an oversize request
    // would therefore re-capture on every show, forever. Clamp at the API
    // boundary so no caller can construct that loop.
    constexpr int MaxDim = PhosphorProtocol::Service::SnapAssistThumbnailMaxDimension;
    maxSize = maxSize.boundedTo(QSize(MaxDim, MaxDim));
    int skipped = 0;
    QSet<QUuid> seen;
    for (const auto& c : candidates) {
        if (c.internalId.isNull()) {
            continue;
        }
        // Intra-list dedup: markRecentlyPosted is deferred to the D-Bus
        // reply, so a duplicate id inside ONE candidate list would be
        // enqueued (and rendered, and posted) twice without this.
        if (seen.contains(c.internalId)) {
            continue;
        }
        seen.insert(c.internalId);
        if (wasRecentlyPosted(c.internalId, maxSize)) {
            // Promote: the handle is being "used" via the skip-recapture
            // decision, mirroring the daemon's QCache promote-on-access.
            // Without this, a frequently re-snapped window FIFOs out of
            // the bookkeeping window even though the daemon keeps it MRU
            // and never evicts it.
            bumpRecency(c.internalId);
            ++skipped;
            continue;
        }
        m_queue.enqueue({c.internalId, maxSize});
    }
    if (skipped > 0) {
        // Info-level: this is the visible signal that the recently-posted
        // dedup window is doing its job (or, if it ever skips when the
        // daemon's cache is cold, that the cap-drift fallback path described
        // on RecentPostedCapacity has kicked in). Without this at info, an
        // operator chasing a "thumbnails not appearing" report has to
        // enable kwin.effect.plasmazones.* debug logs first.
        qCInfo(lcSnapAssistCapture) << "captureCandidates: skipped" << skipped << "recently-posted of"
                                    << candidates.size() << "; queued=" << m_queue.size();
    }
    if (m_queue.isEmpty() || m_busy) {
        return;
    }
    // Latch busy BEFORE posting the kick: m_busy is otherwise first written
    // inside processNext, which runs off this zero-timer — a second
    // captureCandidates landing before the timer delivers would observe
    // m_busy == false, post its own kick, and fork TWO concurrent capture
    // chains, breaking the one-render-at-a-time throttle this class exists
    // to provide.
    // processNext's own redundant m_busy = true stays harmless.
    m_busy = true;
    QTimer::singleShot(0, this, &SnapAssistThumbnailCapture::processNext);
}

void SnapAssistThumbnailCapture::resetRecentlyPosted()
{
    if (m_recentlyPostedSet.isEmpty() && m_recentlyPostedOrder.isEmpty()) {
        return;
    }
    // Info-level: drops happen at daemon-ready transitions and daemon-side
    // cache trims only, so they're rare and meaningful. An operator
    // diagnosing "snap-assist thumbnails are blank after restart" should see
    // this in the default-warning log rules.
    qCInfo(lcSnapAssistCapture) << "resetRecentlyPosted: dropping" << m_recentlyPostedSet.size()
                                << "tracked handles (daemon cache assumed empty)";
    m_recentlyPostedSet.clear();
    m_recentlyPostedOrder.clear();
}

void SnapAssistThumbnailCapture::rearmDmabufPath()
{
    if (!PhosphorProtocol::Service::snapAssistDmabufThumbnailsEnabled()) {
        return;
    }
    // Daemon-restart recovery: transient D-Bus errors during a restart must
    // not leave the session latched onto the pixel path when the env gate has
    // not been turned off. Genuine capability gaps (missing EGL extensions,
    // importer refusal) simply re-trip the fallback within the next show.
    // Called only from the daemon-ready transition — NOT from the cache-trim
    // reset, which fires repeatedly in a healthy session.
    if (!m_dmabufEnabled) {
        qCInfo(lcSnapAssistCapture) << "re-arming dma-buf thumbnail path after daemon-ready transition";
    }
    m_dmabufEnabled = true;
    m_dmabufConsecutiveFailures = 0;
}

static int boxMajorAxis(QSize box)
{
    return std::max(box.width(), box.height());
}

bool SnapAssistThumbnailCapture::wasRecentlyPosted(const QUuid& handle, QSize box) const
{
    const auto it = m_recentlyPostedSet.constFind(handle);
    return it != m_recentlyPostedSet.constEnd() && it.value() >= boxMajorAxis(box);
}

void SnapAssistThumbnailCapture::markRecentlyPosted(const QUuid& handle, QSize box)
{
    const auto it = m_recentlyPostedSet.find(handle);
    if (it != m_recentlyPostedSet.end()) {
        // Re-mark: bump to MRU so a re-posted handle (which the daemon
        // also just promoted via QCache::insert) stays at the head of
        // the order queue. The previous "leave queue position alone"
        // behaviour drifted re-posted handles out of the dedup window
        // in first-post order even though the daemon would never evict
        // them, causing wasted re-captures. The daemon holds the latest
        // post, which is at least this box when the re-capture was
        // triggered by a larger show, so keep the larger value.
        it.value() = std::max(it.value(), boxMajorAxis(box));
        bumpRecency(handle);
        return;
    }
    m_recentlyPostedSet.insert(handle, boxMajorAxis(box));
    m_recentlyPostedOrder.enqueue(handle);
    while (m_recentlyPostedOrder.size() > RecentPostedCapacity) {
        const QUuid evicted = m_recentlyPostedOrder.dequeue();
        m_recentlyPostedSet.remove(evicted);
    }
}

void SnapAssistThumbnailCapture::bumpRecency(const QUuid& handle)
{
    // Debug-build invariant guard: every caller (the @ref captureCandidates
    // skip-recapture path and @ref markRecentlyPosted's re-mark branch) only
    // reaches here after observing @c m_recentlyPostedSet.contains(handle).
    // If the queue diverges from the set the set/queue size invariant has
    // already broken — surface that early in debug rather than silently
    // no-op'ing here. Compiles to nothing in release.
    Q_ASSERT_X(m_recentlyPostedSet.contains(handle), "bumpRecency",
               "called for a handle not in the set — set/queue invariant broken");
    // QQueue::removeOne is O(n) over n=RecentPostedCapacity; trivially
    // cheap for this cadence. Returns false if @p handle isn't present —
    // in that case skip the enqueue, otherwise we'd add a duplicate and
    // break the set/queue size invariant. (In debug builds the assert above
    // catches this earlier; the runtime guard stays for release safety.)
    if (m_recentlyPostedOrder.removeOne(handle)) {
        m_recentlyPostedOrder.enqueue(handle);
    }
}

QSize SnapAssistThumbnailCapture::fittedThumbnailSize(const KWin::RectF& wg, QSize box, qreal* scaleOut)
{
    // Fit the window into the bounding box, preserving aspect ratio, but
    // never upscale: a 64×64 dialog shipped at 256×256 is 16× the pixels,
    // 16× the D-Bus payload, and a blurry magnified thumbnail.
    const qreal scale = qMin<qreal>(1.0, qMin(qreal(box.width()) / wg.width(), qreal(box.height()) / wg.height()));
    if (scaleOut) {
        *scaleOut = scale;
    }
    const QSize fitted(qMax(1, qRound(wg.width() * scale)), qMax(1, qRound(wg.height() * scale)));
    if (fitted.width() < MinThumbnailAxisPx || fitted.height() < MinThumbnailAxisPx) {
        return {};
    }
    return fitted;
}

QImage SnapAssistThumbnailCapture::grabWindowImage(KWin::EffectWindow* w, QSize box, TraceSample* trace) const
{
    if (!w || box.isEmpty() || !KWin::effects) {
        return {};
    }
    // KWin 6.7: frameGeometry() is a KWin::RectF.
    const KWin::RectF wg = w->frameGeometry();
    if (wg.width() <= 0 || wg.height() <= 0) {
        return {};
    }
    qreal scale = 1.0;
    // The FBO is sized to the fitted content (not the full box) so the
    // readback carries no letterbox padding — the daemon stores the image at
    // whatever size we ship.
    const QSize fbSize = fittedThumbnailSize(wg, box, &scale);
    if (fbSize.isEmpty()) {
        return {};
    }
    if (trace) {
        trace->fitted = fbSize;
    }
    QElapsedTimer stage;

    // drawWindow() and the GLFramebuffer path issue raw GL, so the compositor
    // EGL context must be current. We run timer-driven, outside a paint pass, so
    // make it current ourselves (KWin 6.7's manual-FBO render needs no
    // OutputFrame). Non-OpenGL backends (software/QPainter compositing) return
    // false — snap-assist then falls back to icons.
    if (!KWin::effects->makeOpenGLContextCurrent()) {
        return {};
    }

    QImage result;
    {
        // Snapshot + restore blend/viewport/clear-colour/scissor state around
        // the offscreen pass. This capture runs between frames, so any state
        // it leaks (the (0,0,0,0) clear colour, a flipped scissor from the
        // nested drawWindow) is inherited by the NEXT real compositor frame —
        // the same state-leak class every other offscreen capture site in
        // this effect guards against.
        const ShaderInternal::ScopedGlState glStateGuard;
        // The guard only snapshots scissor state; explicitly disable it so a
        // scissor inherited from the last presented frame cannot clip our
        // clear or the nested drawWindow (the guard restores the enable
        // state on exit) — same shape as paint_capture.cpp's pre-clear
        // disable.
        glDisable(GL_SCISSOR_TEST);
        auto texture = KWin::GLTexture::allocate(GL_RGBA8, fbSize);
        if (texture) {
            KWin::GLFramebuffer fbo(texture.get());
            // An incomplete attachment silently drops the render while the
            // readback still returns a valid cleared image — which would be
            // posted and latched. Treat it as a capture failure instead.
            if (fbo.valid()) {
                KWin::RenderTarget renderTarget(&fbo);
                // renderRect = the window's logical geometry; scale maps it onto the
                // fbSize device target so the window fills the FBO at thumbnail size.
                KWin::RenderViewport viewport(wg, scale, renderTarget, QPoint());

                KWin::GLFramebuffer::pushFramebuffer(&fbo);
                stage.start();
                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                glClear(GL_COLOR_BUFFER_BIT);

                // Hold an effect reference on the window item for the draw:
                // a snap-assist candidate is by definition unfocused and
                // often obscured or minimized, so without the reference the
                // item may have no renderable content and drawWindow writes
                // nothing — every sibling offscreen capture site holds one.
                KWin::ItemEffect keepRenderable(w->windowItem());
                KWin::WindowPaintData data;
                // Route through effects->drawWindow (the same entry the effect's own
                // paintWindow uses) so the full draw chain renders the window's live
                // texture into our bound FBO. infinite() repaints the whole target.
                // TRANSLUCENT is required alongside TRANSFORMED: without it the
                // scene may take the opaque path and composite an alpha-carrying
                // window (rounded corners, transparent terminal) incorrectly over
                // the transparent clear. m_capturingSnapshot is deliberately NOT
                // set around this draw: the flag suppresses the present-bind in
                // PlasmaZonesEffect::drawWindow, which strips decorations — a
                // thumbnail wants them (see desktoptransitioncapture.cpp for the
                // history of getting this wrong in both directions).
                KWin::effects->drawWindow(renderTarget, viewport, w,
                                          KWin::Effect::PAINT_WINDOW_TRANSFORMED
                                              | KWin::Effect::PAINT_WINDOW_TRANSLUCENT,
                                          KWin::Region::infinite(), data);
                KWin::GLFramebuffer::popFramebuffer();
                if (trace) {
                    trace->renderUs = stage.nsecsElapsed() / 1000;
                    stage.start();
                }

                // toImage() yields Format_RGBA8888_Premultiplied; GL's framebuffer
                // origin is bottom-left, so flip to a top-down QImage.
                result = texture->toImage().flipped(Qt::Vertical);
                if (trace) {
                    trace->readbackUs = stage.nsecsElapsed() / 1000;
                }
            }
        }
        // texture (and the GL state guard) are destroyed HERE, while the GL
        // context is still current — destroying the GLTexture after
        // doneOpenGLContextCurrent() leaked one GPU texture per capture.
    }
    KWin::effects->doneOpenGLContextCurrent();

    if (result.isNull()) {
        return {};
    }
    // Ship plain (straight-alpha) ARGB32 so the raw bytes match the daemon's
    // storage format and semi-transparent edges aren't darkened by an
    // unintended premultiplied composite at the image-provider boundary.
    stage.start();
    QImage converted = result.convertToFormat(QImage::Format_ARGB32);
    if (trace) {
        trace->convertUs = stage.nsecsElapsed() / 1000;
    }
    return converted;
}

std::unique_ptr<KWin::GLTexture> SnapAssistThumbnailCapture::renderWindowToExportTexture(KWin::EffectWindow* w,
                                                                                         QSize box,
                                                                                         bool* candidateNotRenderable,
                                                                                         TraceSample* trace)
{
    if (candidateNotRenderable) {
        *candidateNotRenderable = false;
    }
    // Caller guarantees the compositor GL/EGL context is current (the dma-buf
    // export that follows reads it via eglGetCurrentContext), so unlike
    // grabWindowImage this neither makes the context current nor releases it.
    if (!w || box.isEmpty() || !KWin::effects) {
        if (candidateNotRenderable) {
            *candidateNotRenderable = true;
        }
        return nullptr;
    }
    const KWin::RectF wg = w->frameGeometry();
    if (wg.width() <= 0 || wg.height() <= 0) {
        if (candidateNotRenderable) {
            *candidateNotRenderable = true;
        }
        return nullptr;
    }
    qreal scale = 1.0;
    const QSize fbSize = fittedThumbnailSize(wg, box, &scale);
    if (fbSize.isEmpty()) {
        if (candidateNotRenderable) {
            *candidateNotRenderable = true;
        }
        return nullptr;
    }

    // Framebuffer blits need GL 3.0 / ARB_framebuffer_object; without them
    // the flip below would silently not write and the export would ship the
    // export texture's cleared (or uninitialised) content. Fail the render (as a
    // CAPABILITY failure, so the session fallback counts it and the pixel
    // path takes over) — same gate the sibling capture site in
    // desktoptransitioncapture.cpp uses.
    const KWin::EglContext* const glContext = KWin::EglContext::currentContext();
    if (!glContext || !glContext->supportsBlits()) {
        qCDebug(lcSnapAssistCapture) << "renderWindowToExportTexture: framebuffer blits unsupported";
        return nullptr;
    }

    // Same state-leak guard as grabWindowImage — this path additionally runs
    // a blit, which touches read/draw framebuffer bindings the push/pop stack
    // restores, but blend/scissor/clear-colour still need the snapshot.
    const ShaderInternal::ScopedGlState glStateGuard;
    // The guard SNAPSHOTS scissor state, it does not neutralise it: an
    // enabled scissor inherited from the previous compositor frame would
    // clip our clears, the nested drawWindow AND the blit. Disable it for
    // the offscreen pass (the guard restores the enable state on exit) —
    // same shape as paint_capture.cpp's pre-clear disable.
    glDisable(GL_SCISSOR_TEST);

    // The window is rendered into a throwaway scratch texture first, then
    // blit-flipped into the export texture: GL's framebuffer origin is
    // bottom-left, and unlike the raw-pixel path (which flips CPU-side via
    // QImage::flipped) the dma-buf consumer samples the texture memory
    // directly — without this flip every zero-copy thumbnail displays
    // upside down. The wire descriptor deliberately carries no orientation
    // field; the producer ships top-down, always.
    if (!m_flipScratch || m_flipScratch->size() != fbSize) {
        m_flipScratch = KWin::GLTexture::allocate(GL_RGBA8, fbSize);
    }
    if (!m_flipScratch) {
        return nullptr;
    }

    // A fresh texture per export. The exported fd keeps the underlying
    // buffer alive independently of the GLTexture (see exportTextureToDmabuf),
    // so the caller frees this texture as soon as the export is done and the
    // daemon becomes the buffer's only owner. Nothing the effect renders
    // later can touch it, which is what lets the daemon sample the import
    // directly rather than copy it out of a shared slot before it is reused.
    // The earlier round-robin pool could only offer a best-effort margin
    // here, and candidates are uncapped, so a burst larger than the pool
    // overwrote a slot the daemon had not copied yet.
    std::unique_ptr<KWin::GLTexture> texture = KWin::GLTexture::allocate(GL_RGBA8, fbSize);
    if (!texture) {
        return nullptr;
    }

    KWin::GLFramebuffer scratchFbo(m_flipScratch.get());
    KWin::GLFramebuffer exportFbo(texture.get());
    if (!scratchFbo.valid() || !exportFbo.valid()) {
        // Incomplete FBO: the render would be dropped by the driver and a
        // cleared (or stale) buffer exported in its place.
        return nullptr;
    }

    KWin::RenderTarget renderTarget(&scratchFbo);
    KWin::RenderViewport viewport(wg, scale, renderTarget, QPoint());

    KWin::GLFramebuffer::pushFramebuffer(&scratchFbo);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Same renderability + translucency contract as grabWindowImage (see the
    // comment there); m_capturingSnapshot stays untouched here for the same
    // keep-decorations reason.
    KWin::ItemEffect keepRenderable(w->windowItem());
    KWin::WindowPaintData data;
    QElapsedTimer stage;
    stage.start();
    KWin::effects->drawWindow(renderTarget, viewport, w,
                              KWin::Effect::PAINT_WINDOW_TRANSFORMED | KWin::Effect::PAINT_WINDOW_TRANSLUCENT,
                              KWin::Region::infinite(), data);

    KWin::GLFramebuffer::popFramebuffer();

    // Clear the export texture BEFORE the blit: allocate() leaves its content
    // undefined, and exporting a texture the blit failed to write would ship
    // uninitialised VRAM to the daemon as a thumbnail.
    KWin::GLFramebuffer::pushFramebuffer(&exportFbo);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    KWin::GLFramebuffer::popFramebuffer();

    // Blit-flip scratch → export texture (source = the pushed framebuffer).
    const KWin::Rect fullRect(0, 0, fbSize.width(), fbSize.height());
    KWin::GLFramebuffer::pushFramebuffer(&scratchFbo);
    exportFbo.blitFromFramebuffer(fullRect, fullRect, GL_NEAREST, /*flipX=*/false, /*flipY=*/true);
    KWin::GLFramebuffer::popFramebuffer();
    if (trace) {
        // Render + clear + flip-blit, as submitted. The export that follows
        // is timed separately by the caller.
        trace->fitted = fbSize;
        trace->renderUs = stage.nsecsElapsed() / 1000;
    }

    return texture;
}

void SnapAssistThumbnailCapture::processNext()
{
    if (m_queue.isEmpty()) {
        m_busy = false;
        return;
    }
    m_busy = true;
    if (m_dmabufEnabled) {
        // Drain the whole queue into one batch: the settle delay and the GL
        // context window are paid once for every candidate queued so far.
        QVector<Pending> batch;
        batch.reserve(m_queue.size());
        while (!m_queue.isEmpty()) {
            batch.append(m_queue.dequeue());
        }
        attemptDmabufBatch(batch, RENDER_SETTLE_MS, /*retriesLeft=*/1, m_queueGeneration);
        return;
    }
    Pending p = m_queue.dequeue();
    attemptCapture(p, RENDER_SETTLE_MS, /*retriesLeft=*/1, m_queueGeneration);
}

void SnapAssistThumbnailCapture::attemptDmabufBatch(const QVector<Pending>& batch, int delayMs, int retriesLeft,
                                                    int generation)
{
    QTimer::singleShot(delayMs, this, [this, batch, retriesLeft, generation]() {
        // Same generation contract as attemptCapture: a replaced queue makes
        // this batch stale, and the kick must still run or the new queue
        // stalls until the next captureCandidates.
        if (generation != m_queueGeneration) {
            QTimer::singleShot(0, this, &SnapAssistThumbnailCapture::processNext);
            return;
        }
        // The session may have fallen back to pixels between the timer being
        // armed and firing (a rejection reply landing in between). Hand the
        // batch back to the queue; processNext re-dispatches it down the
        // pixel path one candidate at a time.
        if (!m_dmabufEnabled) {
            for (const Pending& p : batch) {
                m_queue.enqueue(p);
            }
            QTimer::singleShot(0, this, &SnapAssistThumbnailCapture::processNext);
            return;
        }

        // Zero-copy path: render each window into its own export texture and
        // export it as a dma-buf. The render and the EGL export must share one
        // context-current window — renderWindowToExportTexture assumes the
        // context is already current and exportTextureToDmabuf reads it via
        // eglGetCurrentContext — so currency is managed here, once, around
        // the whole batch.
        //
        // NOTE the dma-buf path deliberately has NO transparent-content gate
        // (the pixel path's isFullyTransparent test): probing the exported
        // buffer would need exactly the GPU readback the zero-copy path
        // exists to avoid. The not-yet-renderable window the pixel gate
        // defends against is instead handled at the source — the ItemEffect
        // renderability reference held during the draw plus the
        // settle-delay/retry — so a fully transparent export is expected to
        // be rare, and it degrades to one blank thumbnail rather than a
        // wrong one.
        struct Ready
        {
            Pending p;
            DmabufExport exported;
            TraceSample trace;
        };
        QVector<Ready> ready;
        QVector<Pending> retry;
        const bool haveContext = KWin::effects && KWin::effects->makeOpenGLContextCurrent();
        for (const Pending& p : batch) {
            // Resolve the EffectWindow fresh each attempt: the candidate may
            // have closed between queueing and firing. A closed window is NOT
            // a capture-capability signal — feeding it to onDmabufRejected
            // used to count toward the permanent dma-buf fallback. Drop it.
            KWin::EffectWindow* w = KWin::effects ? KWin::effects->findWindow(p.internalId) : nullptr;
            if (!w) {
                qCDebug(lcSnapAssistCapture) << "captureCandidates:" << p.internalId.toString()
                                             << "window closed before capture — dropping candidate";
                continue;
            }
            TraceSample trace;
            TraceSample* const tracePtr = m_traceEnabled ? &trace : nullptr;
            bool candidateNotRenderable = false;
            DmabufExport exported;
            if (haveContext) {
                // The texture is destroyed at the end of this iteration, with
                // the context still current; the exported fd owns the buffer
                // from here on.
                const std::unique_ptr<KWin::GLTexture> texture =
                    renderWindowToExportTexture(w, p.maxSize, &candidateNotRenderable, tracePtr);
                if (texture) {
                    QElapsedTimer exportTimer;
                    exportTimer.start();
                    exported = exportTextureToDmabuf(texture.get());
                    trace.exportUs = exportTimer.nsecsElapsed() / 1000;
                }
            }
            if (exported.ok) {
                ready.append({p, exported, trace});
                continue;
            }
            if (retriesLeft > 0) {
                retry.append(p);
                continue;
            }
            if (haveContext && candidateNotRenderable) {
                // Nothing renderable for THIS candidate (degenerate geometry,
                // sliver fit below the minimum axis) — not a dma-buf
                // capability signal. Counting it used to let a single
                // extreme-aspect window trip the threshold and disable the
                // zero-copy path for the whole session. Drop the candidate;
                // snap-assist shows its icon.
                qCDebug(lcSnapAssistCapture)
                    << "captureCandidates:" << p.internalId.toString() << "not renderable — dropping candidate";
                continue;
            }
            qCDebug(lcSnapAssistCapture) << "captureCandidates:" << p.internalId.toString()
                                         << "dma-buf capture failed after retry";
            // Count CAPABILITY/EXPORT failures toward the session fallback:
            // export failures, no GL context, and render-side capability or
            // resource failures (no blit support, allocation failure,
            // incomplete FBO — anything the render reported without flagging
            // the candidate itself). onDmabufRejected re-enqueues p — via
            // dma-buf below the threshold, via pixels once the fallback
            // trips; the trailing kick below picks it up.
            onDmabufRejected(p);
        }
        if (haveContext) {
            KWin::effects->doneOpenGLContextCurrent();
        }
        // Post after the context is released: the posts are async D-Bus calls
        // and need no GL.
        for (const Ready& r : ready) {
            postThumbnailDmabuf(r.p, r.exported, generation, r.trace);
        }
        if (!retry.isEmpty()) {
            // This retry batch is the chain's continuation; it kicks
            // processNext itself when it completes.
            attemptDmabufBatch(retry, RENDER_RETRY_MS, retriesLeft - 1, generation);
            return;
        }
        // Advance the queue now — the dma-buf posts are async; we do not wait
        // for their replies. The reply lambda has its own guarded kick
        // (latching m_busy, only when idle) solely to drain a candidate that
        // onDmabufRejected re-enqueued after the queue had already emptied.
        // processNext is idempotent on an empty queue, so the two kick sites
        // never double-dispatch real work.
        QTimer::singleShot(0, this, &SnapAssistThumbnailCapture::processNext);
    });
}

void SnapAssistThumbnailCapture::attemptCapture(const Pending& p, int delayMs, int retriesLeft, int generation)
{
    QTimer::singleShot(delayMs, this, [this, p, retriesLeft, generation]() {
        // A newer captureCandidates replaced the queue while this capture's
        // timer was pending: p is no longer a candidate. Skip its render and
        // post, but keep draining — this lambda IS the (single) capture
        // chain's continuation, so the kick must still run or the new queue
        // stalls until the next captureCandidates.
        if (generation != m_queueGeneration) {
            QTimer::singleShot(0, this, &SnapAssistThumbnailCapture::processNext);
            return;
        }
        // Resolve the EffectWindow fresh each attempt: the candidate may have
        // closed between queueing and firing. A closed window is NOT a
        // capture-capability signal — feeding it to onDmabufRejected used to
        // count toward the permanent dma-buf fallback (two closed candidates
        // in a row disabled the zero-copy path for the session). Drop the
        // candidate and advance; there is nothing to render on either path.
        KWin::EffectWindow* w = KWin::effects ? KWin::effects->findWindow(p.internalId) : nullptr;
        if (!w) {
            qCDebug(lcSnapAssistCapture) << "captureCandidates:" << p.internalId.toString()
                                         << "window closed before capture — dropping candidate";
            QTimer::singleShot(0, this, &SnapAssistThumbnailCapture::processNext);
            return;
        }

        TraceSample trace;
        TraceSample* const tracePtr = m_traceEnabled ? &trace : nullptr;
        const QImage image = grabWindowImage(w, p.maxSize, tracePtr);
        // isNull() alone cannot detect the failure this retry exists for: a
        // window with no renderable frame yet draws NOTHING into the cleared
        // FBO, and the readback is a valid, fully transparent image. Posting
        // that would latch an invisible thumbnail into the dedup window —
        // strictly worse than the icon fallback.
        const bool empty = image.isNull() || isFullyTransparent(image);
        if (empty && retriesLeft > 0) {
            // The window's first compositor frame after mapping is occasionally
            // not yet renderable. Wait one more frame interval — same Pending,
            // no queue shuffle — before giving up.
            attemptCapture(p, RENDER_RETRY_MS, retriesLeft - 1, generation);
            return;
        }
        if (!empty) {
            // Mark-recently-posted is deferred into the D-Bus success
            // callback inside @ref postThumbnail — see that function for
            // why "we sent it" isn't strong enough to claim the daemon
            // holds the entry.
            postThumbnail(p, image, trace);
        } else {
            qCDebug(lcSnapAssistCapture) << "captureCandidates:" << p.internalId.toString()
                                         << "produced empty image after retry";
        }
        QTimer::singleShot(0, this, &SnapAssistThumbnailCapture::processNext);
    });
}

void SnapAssistThumbnailCapture::postThumbnail(const Pending& p, const QImage& image, TraceSample trace)
{
    const QUuid internalId = p.internalId;
    const QSize box = p.maxSize;
    // Image is already Format_ARGB32 by the caller. Pack tight (no row
    // padding) so the daemon can reconstruct via QImage(uchar*, w, h,
    // bytesPerLine=w*4, Format_ARGB32) without having to communicate the
    // stride. Qt's internal stride for Format_ARGB32 is naturally 4-aligned
    // and almost always equals width*4; on that fast-path one bulk memcpy
    // beats a per-row loop. Fall back to the row loop only when Qt has
    // padded the stride (rare — typically only for non-aligned widths).
    const int width = image.width();
    const int height = image.height();
    const qsizetype rowBytes = qsizetype(width) * 4;
    QElapsedTimer packTimer;
    packTimer.start();
    QByteArray pixels;
    pixels.resize(rowBytes * height);
    char* dst = pixels.data();
    if (image.bytesPerLine() == rowBytes) {
        std::memcpy(dst, image.constBits(), rowBytes * height);
    } else {
        for (int y = 0; y < height; ++y) {
            std::memcpy(dst + y * rowBytes, image.constScanLine(y), rowBytes);
        }
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(
        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
        PhosphorProtocol::Service::Interface::Overlay, QStringLiteral("setSnapAssistThumbnail"));
    msg << internalId.toString() << width << height << pixels;
    trace.packUs = packTimer.nsecsElapsed() / 1000;
    trace.payloadBytes = pixels.size();
    QElapsedTimer roundTrip;
    roundTrip.start();

    // Bound watcher accumulation if the daemon's main thread wedges. The
    // post is genuinely async — `SnapAssistThumbnailPostTimeoutMs` is
    // "definitely something is wrong, drop the watcher" rather than a
    // meaningful expected latency. Without an explicit timeout the
    // kwin-effect could otherwise leak a watcher per snap-assist candidate
    // per show until Qt's default 25 s timeout expires, which under daemon
    // stress turns a transient hang into accumulated compositor-process
    // state.
    QDBusPendingCall pending =
        QDBusConnection::sessionBus().asyncCall(msg, PhosphorProtocol::Service::SnapAssistThumbnailPostTimeoutMs);
    auto* watcher = new QDBusPendingCallWatcher(pending, this);
    // Capture @c this — the connect's context arg auto-disconnects the
    // lambda if `this` dies, so capture-by-pointer is safe across the
    // call round-trip. Only the QUuid is captured; the braced string form
    // is derived inside for log lines (matching postThumbnailDmabuf) so the
    // lambda doesn't carry two representations of one value.
    // Mark-recently-posted is gated on the daemon's
    // explicit `accepted` reply, NOT just on transport success. The
    // daemon's slot is `bool`-returning and replies @c false on every
    // silent rejection path (auth failure, oversize-payload cap,
    // dimension/byte-count mismatch, post-shutdown engine teardown) —
    // treating those as success would mark the handle in the dedup
    // window, skip the next capture, and strand snap-assist on icons
    // until the LRU window rolls past.
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, this,
                     [this, internalId, box, trace, roundTrip](QDBusPendingCallWatcher* w) {
                         w->deleteLater();
                         QDBusPendingReply<bool> reply = *w;
                         if (m_traceEnabled) {
                             qCInfo(lcSnapAssistTrace).nospace()
                                 << "pixels " << internalId.toString() << " fitted=" << trace.fitted.width() << "x"
                                 << trace.fitted.height() << " render=" << trace.renderUs
                                 << "us readback=" << trace.readbackUs << "us convert=" << trace.convertUs
                                 << "us pack=" << trace.packUs << "us payload=" << trace.payloadBytes
                                 << "B dbus=" << roundTrip.nsecsElapsed() / 1000 << "us result="
                                 << (reply.isError() ? "error" : (reply.value() ? "accepted" : "rejected"));
                         }
                         if (reply.isError()) {
                             qCDebug(lcSnapAssistCapture) << "setSnapAssistThumbnail D-Bus call failed for"
                                                          << internalId.toString() << ":" << reply.error().message();
                             return;
                         }
                         if (!reply.value()) {
                             qCDebug(lcSnapAssistCapture)
                                 << "setSnapAssistThumbnail rejected by daemon for" << internalId.toString()
                                 << "— leaving handle out of recently-posted set so the next snap-assist re-captures.";
                             return;
                         }
                         markRecentlyPosted(internalId, box);
                     });
}

SnapAssistThumbnailCapture::DmabufExport SnapAssistThumbnailCapture::exportTextureToDmabuf(KWin::GLTexture* texture)
{
    DmabufExport result;
    if (!texture) {
        return result;
    }
    // EGL display/context of the compositor's current GL backend. EGL_NO_*
    // means KWin is not on an EGL/GL backend (e.g. a future Vulkan compositor
    // backend) — there is nothing to export here, so the caller drops the
    // candidate and snap-assist shows its icon.
    EGLDisplay dpy = eglGetCurrentDisplay();
    EGLContext ctx = eglGetCurrentContext();
    if (dpy == EGL_NO_DISPLAY || ctx == EGL_NO_CONTEXT) {
        qCDebug(lcSnapAssistCapture) << "exportTextureToDmabuf: no current EGL context (non-EGL backend)";
        return result;
    }
    // Verify the driver actually provides the EGL entry points this path calls
    // BEFORE calling any of them. With libepoxy a missing entry point resolves
    // to a stub that abort()s the process rather than returning an error, so
    // without these guards a driver lacking dma-buf export or native-fence sync
    // would CRASH the compositor the first time this path runs — which, since
    // the dma-buf transport became the default, is every session —
    // instead of returning {ok=false} and taking the raw-pixel fallback the
    // comments below rely on. EGL_KHR_gl_texture_2D_image is what admits the
    // EGL_GL_TEXTURE_2D_KHR target passed to eglCreateImageKHR below; without
    // it the call merely fails (image_base ships the entry point), but the
    // wasted attempt would count toward the capability-fallback threshold.
    if (!epoxy_has_egl_extension(dpy, "EGL_KHR_image_base")
        || !epoxy_has_egl_extension(dpy, "EGL_KHR_gl_texture_2D_image")
        || !epoxy_has_egl_extension(dpy, "EGL_MESA_image_dma_buf_export")
        || !epoxy_has_egl_extension(dpy, "EGL_KHR_fence_sync")
        || !epoxy_has_egl_extension(dpy, "EGL_ANDROID_native_fence_sync")) {
        qCDebug(lcSnapAssistCapture)
            << "exportTextureToDmabuf: required EGL dma-buf/fence extensions unavailable — using raw-pixel path";
        return result;
    }
    if (texture->target() != GL_TEXTURE_2D) {
        qCDebug(lcSnapAssistCapture) << "exportTextureToDmabuf: unexpected texture target" << texture->target();
        return result;
    }
    if (texture->size().isEmpty()) {
        // A zero-sized FBO texture would export a w=0/h=0 buffer the daemon is
        // guaranteed to reject — fail early (routes to retry, then fallback)
        // rather than allocate and ship an EGLImage + fd + fence for nothing.
        qCDebug(lcSnapAssistCapture) << "exportTextureToDmabuf: zero-sized texture" << texture->size();
        return result;
    }
    // Wrap the GL texture as an EGLImage, then export its backing dma-buf.
    const EGLImageKHR image =
        eglCreateImageKHR(dpy, ctx, EGL_GL_TEXTURE_2D_KHR,
                          reinterpret_cast<EGLClientBuffer>(static_cast<uintptr_t>(texture->texture())), nullptr);
    if (image == EGL_NO_IMAGE_KHR) {
        qCDebug(lcSnapAssistCapture) << "exportTextureToDmabuf: eglCreateImageKHR failed";
        return result;
    }
    // Two-call query: the modifiers argument is an ARRAY the driver fills
    // with num_planes entries, so passing the address of a single stack
    // uint64 before validating the plane count invites an out-of-bounds
    // driver write. First learn (and validate) the plane count with
    // modifiers=nullptr — the spec allows any out-pointer to be null — then
    // fetch the single modifier once numPlanes == 1 is established.
    int fourcc = 0;
    int numPlanes = 0;
    if (!eglExportDMABUFImageQueryMESA(dpy, image, &fourcc, &numPlanes, nullptr) || numPlanes != 1) {
        qCDebug(lcSnapAssistCapture) << "exportTextureToDmabuf: query failed or unsupported plane count" << numPlanes;
        eglDestroyImageKHR(dpy, image);
        return result;
    }
    EGLuint64KHR modifier = 0;
    if (!eglExportDMABUFImageQueryMESA(dpy, image, nullptr, nullptr, &modifier)) {
        qCDebug(lcSnapAssistCapture) << "exportTextureToDmabuf: modifier query failed";
        eglDestroyImageKHR(dpy, image);
        return result;
    }
    int rawFd = -1;
    EGLint stride = 0;
    EGLint offset = 0;
    const bool exported = eglExportDMABUFImageMESA(dpy, image, &rawFd, &stride, &offset);
    // The exported fd holds its own reference to the underlying buffer, so it
    // outlives the EGLImage — destroy the image regardless of success.
    eglDestroyImageKHR(dpy, image);
    ScopedFd fd(rawFd); // owns whatever the driver returned, success or not
    if (!exported || !fd.valid()) {
        qCDebug(lcSnapAssistCapture) << "exportTextureToDmabuf: eglExportDMABUFImageMESA failed";
        return result;
    }
    // Mandatory render-completion fence for the dma-buf path: a sync_file that
    // signals when the GL render into this buffer finishes, so the daemon waits
    // before sampling (correctness under repeated/live capture). If the driver
    // can't produce one, fail the export — the capability fallback then switches
    // the session to the raw-pixel path.
    //
    // Ordering: renderWindowToExportTexture (issued earlier this call) renders
    // into this texture on the current GL context's command stream. eglCreateSync
    // with EGL_SYNC_NATIVE_FENCE_ANDROID inserts the fence into that same
    // stream AFTER those render commands, then glFlush() flushes the stream to
    // the GPU so the fence is schedulable and eglDupNativeFenceFDANDROID can
    // return a real sync_file. The fence therefore signals only once the render
    // it follows has completed — exactly the guarantee the daemon relies on.
    const EGLSyncKHR sync = eglCreateSyncKHR(dpy, EGL_SYNC_NATIVE_FENCE_ANDROID, nullptr);
    if (sync == EGL_NO_SYNC_KHR) {
        qCDebug(lcSnapAssistCapture) << "exportTextureToDmabuf: native fence sync unavailable";
        return result;
    }
    glFlush(); // flush the stream (render + fence) to the GPU so the fence can signal
    ScopedFd fenceFd(eglDupNativeFenceFDANDROID(dpy, sync));
    eglDestroySyncKHR(dpy, sync);
    if (!fenceFd.valid()) {
        qCDebug(lcSnapAssistCapture) << "exportTextureToDmabuf: eglDupNativeFenceFDANDROID failed";
        return result;
    }
    result.ok = true;
    result.fd = fd.release();
    result.fenceFd = fenceFd.release();
    result.width = texture->size().width();
    result.height = texture->size().height();
    result.fourcc = static_cast<uint32_t>(fourcc);
    result.modifier = static_cast<uint64_t>(modifier);
    result.stride = static_cast<uint32_t>(stride);
    result.offset = static_cast<uint32_t>(offset);
    return result;
}

void SnapAssistThumbnailCapture::postThumbnailDmabuf(const Pending& p, const DmabufExport& exported, int generation,
                                                     TraceSample trace)
{
    // The exported dma-buf aliases an export texture the effect has already
    // dropped, so the daemon is its only owner from here; it ships with a
    // render-completion fence (exported.fenceFd) that the daemon waits on
    // before sampling, which makes the capture correct rather than relying on
    // D-Bus round-trip latency outrunning the GPU.
    const QString compositorHandle = p.internalId.toString();

    QDBusMessage msg = QDBusMessage::createMethodCall(
        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
        PhosphorProtocol::Service::Interface::Overlay, QStringLiteral("setWindowThumbnailDmabuf"));
    // QDBusUnixFileDescriptor dup()s each fd in its constructor; we close our
    // originals after queuing the call.
    msg << compositorHandle << exported.width << exported.height << static_cast<uint>(exported.fourcc)
        << static_cast<qulonglong>(exported.modifier) << static_cast<uint>(exported.stride)
        << static_cast<uint>(exported.offset) << QVariant::fromValue(QDBusUnixFileDescriptor(exported.fd))
        << QVariant::fromValue(QDBusUnixFileDescriptor(exported.fenceFd));
    QElapsedTimer roundTrip;
    roundTrip.start();

    QDBusPendingCall pending =
        QDBusConnection::sessionBus().asyncCall(msg, PhosphorProtocol::Service::SnapAssistThumbnailPostTimeoutMs);
    ::close(exported.fd);
    ::close(exported.fenceFd);

    auto* watcher = new QDBusPendingCallWatcher(pending, this);
    // Same accepted-gated recently-posted contract as the raw-pixel path: only
    // mark the handle when the daemon confirms it imported+stored the buffer.
    // An explicit daemon refusal feeds onDmabufRejected (capability domain);
    // a TRANSPORT error — timeout on a busy daemon, restart mid-flight — does
    // NOT: transport health says nothing about dma-buf support, and counting
    // it used to let two slow replies permanently disable a working zero-copy
    // path. On transport error the handle is simply left unmarked, so the
    // next snap-assist re-captures (mirroring the raw-pixel path).
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, this,
                     [this, p, generation, trace, roundTrip](QDBusPendingCallWatcher* w) {
                         w->deleteLater();
                         QDBusPendingReply<bool> reply = *w;
                         if (m_traceEnabled) {
                             qCInfo(lcSnapAssistTrace).nospace()
                                 << "dmabuf " << p.internalId.toString() << " fitted=" << trace.fitted.width() << "x"
                                 << trace.fitted.height() << " render=" << trace.renderUs
                                 << "us export=" << trace.exportUs << "us dbus=" << roundTrip.nsecsElapsed() / 1000
                                 << "us result="
                                 << (reply.isError() ? "error" : (reply.value() ? "accepted" : "rejected"));
                         }
                         if (reply.isError()) {
                             qCDebug(lcSnapAssistCapture) << "setWindowThumbnailDmabuf D-Bus call failed for"
                                                          << p.internalId.toString() << ":" << reply.error().message();
                         } else if (!reply.value()) {
                             qCDebug(lcSnapAssistCapture)
                                 << "setWindowThumbnailDmabuf rejected by daemon for" << p.internalId.toString();
                             // Honour the queue generation, mirroring attemptCapture's stale
                             // check: a rejection reply landing after captureCandidates
                             // replaced the queue must not re-inject its stale candidate into
                             // the fresh queue (the new list may even contain the same
                             // handle, which would duplicate it — marking is deferred to the
                             // reply, so intra-list dedup can't catch that). Still COUNT the
                             // failure: capability is a session property, not a per-show one.
                             if (generation == m_queueGeneration) {
                                 onDmabufRejected(p);
                             } else {
                                 countDmabufFailure();
                             }
                         } else {
                             m_dmabufConsecutiveFailures = 0;
                             markRecentlyPosted(p.internalId, p.maxSize);
                         }
                         // onDmabufRejected may have re-enqueued p; if no capture is in
                         // flight, kick the queue so it isn't left stranded. Latch m_busy
                         // BEFORE posting, mirroring captureCandidates: several rejection
                         // replies can be delivered in ONE event-loop turn (posted D-Bus
                         // events drain before the zero-timers), and without the latch each
                         // reply observed m_busy == false and posted its own kick — forking
                         // one capture chain per reply and destroying the
                         // one-render-at-a-time throttle.
                         if (!m_busy && !m_queue.isEmpty()) {
                             m_busy = true;
                             QTimer::singleShot(0, this, &SnapAssistThumbnailCapture::processNext);
                         }
                     });
}

void SnapAssistThumbnailCapture::countDmabufFailure()
{
    if (!m_dmabufEnabled) {
        return;
    }
    ++m_dmabufConsecutiveFailures;
    if (m_dmabufConsecutiveFailures < DmabufFailureThreshold) {
        qCDebug(lcSnapAssistCapture) << "dma-buf capture failed (" << m_dmabufConsecutiveFailures
                                     << "consecutive) — will retry dma-buf on the next candidate";
        return;
    }
    qCWarning(lcSnapAssistCapture)
        << "dma-buf thumbnails repeatedly unavailable (export or daemon import failing) — falling back to "
           "raw-pixel thumbnails for the rest of this session.";
    m_dmabufEnabled = false;
    m_dmabufConsecutiveFailures = 0;
    // The mode flip is picked up by the next processNext, which routes
    // through the raw-pixel grabWindowImage path; an already-armed dma-buf
    // batch hands its candidates back to the queue when it sees the flip.
}

void SnapAssistThumbnailCapture::onDmabufRejected(const Pending& p)
{
    if (!m_dmabufEnabled) {
        // Already fell back to the pixel path this session — but the
        // candidate still deserves a thumbnail. The dma-buf path posts
        // without waiting for replies, so several failures can be in flight
        // when the threshold trips; dropping the late arrivals here used to
        // strand those windows on icons until the next snap-assist show.
        m_queue.enqueue(p);
        return;
    }
    countDmabufFailure();
    // Re-enqueue on EVERY failure, not only the threshold-crossing one: the
    // old shape silently dropped each sub-threshold candidate (no thumbnail
    // this show), and an alternating fail/success pattern reset the counter
    // so the same window could be dropped indefinitely across shows. The
    // retry goes back through the dma-buf path below the threshold and the
    // pixel path after the flip; a candidate that keeps failing trips the
    // threshold within two consecutive attempts, so this cannot loop.
    m_queue.enqueue(p);
}

} // namespace PlasmaZones
