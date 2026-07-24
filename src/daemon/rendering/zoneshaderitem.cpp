// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zoneshaderitem.h"
#include "zoneentryscaffold.h"
#include "zoneshadernoderhi.h"

#include "config/configdefaults.h"
#include "core/platform/logging.h"
#include "core/types/constants.h"

#include <PhosphorRendering/ShaderEffect.h>
#include <PhosphorRendering/ZoneShaderCommon.h>
#include <PhosphorRendering/ZoneShaderNodeRhi.h>
#include <PhosphorRendering/ZoneUniformExtension.h>

#include <QImage>
#include <QMetaType>
#include <QMutexLocker>
#include <QVariantMap>

#include <mutex>

// Lock the assumption made in updatePaintNode() that the base
// ShaderEffect's syncBasePropertiesToNode covers all 4 user-texture
// slots. If the base library ever grows or shrinks its slot count
// without us updating the override's local sync logic, this trips
// the build instead of silently dropping (or double-pushing) slots.
static_assert(PhosphorRendering::kMaxUserTextureSlots == 4,
              "ZoneShaderItem assumes ShaderEffect base sync covers 4 user-texture slots");

namespace PlasmaZones {

// QVariantMap payload keys for zone snapshots passed from QML / overlay
// glue down to parseZoneData. Distinct from PhosphorZones::ZoneJsonKeys
// (which owns the ON-DISK wire-format keys) — these are runtime-only,
// PlasmaZones-internal payload keys, not part of the zone/layout file
// format. Centralising them here keeps the writer (overlay QML setting
// these via writeQmlProperty) and the reader (parseZoneData below) in
// lockstep — a typo on either side previously failed silently with the
// default-value fallback.
namespace ZoneSnapshotKeys {
inline constexpr QLatin1String FillR{"fillR"};
inline constexpr QLatin1String FillG{"fillG"};
inline constexpr QLatin1String FillB{"fillB"};
inline constexpr QLatin1String FillA{"fillA"};
inline constexpr QLatin1String BorderR{"borderR"};
inline constexpr QLatin1String BorderG{"borderG"};
inline constexpr QLatin1String BorderB{"borderB"};
inline constexpr QLatin1String BorderA{"borderA"};
inline constexpr QLatin1String ShaderBorderRadius{"shaderBorderRadius"};
inline constexpr QLatin1String ShaderBorderWidth{"shaderBorderWidth"};
} // namespace ZoneSnapshotKeys

// ============================================================================
// Construction / Destruction
// ============================================================================

ZoneShaderItem::ZoneShaderItem(QQuickItem* parent)
    : PhosphorRendering::ShaderEffect(parent)
    , m_zoneExtension(std::make_shared<PhosphorRendering::ZoneUniformExtension>())
{
    // Register the labels payload metatype + a QImage→ZoneLabelTexture converter
    // once per process. The daemon overlay path assigns a sparse payload
    // directly, but the editor/settings shader previews still hand a full QImage
    // to the (now ZoneLabelTexture-typed) labelsTexture property; the converter
    // wraps such an image as a single full-size tile so those paths keep working
    // unchanged. Done here so any process that uses ZoneShaderItem gets it
    // without touching its main().
    static std::once_flag labelTypeOnce;
    std::call_once(labelTypeOnce, [] {
        qRegisterMetaType<PhosphorRendering::ZoneLabelTexture>();
        QMetaType::registerConverter<QImage, PhosphorRendering::ZoneLabelTexture>(
            &PhosphorRendering::ZoneLabelTexture::fromImage);
    });

    // Install our ZoneUniformExtension on the base class. We call the
    // qualified base setter to bypass our own setUniformExtension() override
    // (which rejects caller-supplied extensions). Thereafter, every
    // updatePaintNode() → syncBasePropertiesToNode() pushes this extension
    // down to the render node, so the zone UBO region stays populated across
    // scene-graph node recreations.
    ShaderEffect::setUniformExtension(m_zoneExtension);

    // Shader include paths, so #include <common.glsl> in zone.vert/effect.frag
    // resolves against every trusted overlay root rather than stopping at the
    // user one (which has user shaders but NOT common.glsl). Shared with the
    // daemon warm bake: both must compile against the same list or their
    // bake-cache keys diverge.
    const QStringList includePaths = expandShaderIncludePaths();
    setShaderIncludePaths(includePaths);

    // Set PlasmaZones-specific default colors from ConfigDefaults
    // (the library defaults are all-transparent).
    setCustomColor1(ConfigDefaults::highlightColor());
    setCustomColor2(ConfigDefaults::inactiveColor());
    setCustomColor3(ConfigDefaults::borderColor());
}

ZoneShaderItem::~ZoneShaderItem()
{
    // Nothing to do HERE: updatePaintNode registers its node with the base
    // (registerRenderNode), so the base ~ShaderEffect severs the node's
    // back-pointer (invalidateItem) and the sceneGraphAboutToStop handler
    // releases its GPU resources — same coverage as SurfaceShaderItem. The
    // scene graph owns and deletes the node itself; the zero-size branch in
    // updatePaintNode handles the live resize-to-zero case.
}

// ============================================================================
// Render Node Factory
// ============================================================================

PhosphorRendering::ShaderNodeRhi* ZoneShaderItem::createShaderNode()
{
    return new PhosphorRendering::ZoneShaderNodeRhi(this);
}

// ============================================================================
// Refuse external uniform-extension replacement
// ============================================================================

void ZoneShaderItem::setUniformExtension(std::shared_ptr<PhosphorShaders::IUniformExtension> extension)
{
    // The zone UBO layout is load-bearing: this item's own constructor installs
    // a ZoneUniformExtension whose byte layout matches common.glsl's zone
    // arrays. Accepting a caller-supplied extension here would either
    // de-align that layout or silently replace zone rendering with garbage.
    // Log loudly at the point of misuse instead of inheriting the base
    // class's silent-store behaviour.
    Q_UNUSED(extension);
    qCWarning(PlasmaZones::lcOverlay) << "ZoneShaderItem::setUniformExtension: ignored: zone rendering owns its own"
                                      << "IUniformExtension (ZoneUniformExtension) and cannot accept a replacement.";
}

// ============================================================================
// Zone Data Parsing
// ============================================================================

void ZoneShaderItem::parseZoneData()
{
    // Get current resolution for normalization
    const float resW = iResolution().width() > 0 ? static_cast<float>(iResolution().width()) : 1.0f;
    const float resH = iResolution().height() > 0 ? static_cast<float>(iResolution().height()) : 1.0f;

    // Prepare new zone data structures
    QVector<PhosphorRendering::ZoneRect> newRects;
    QVector<PhosphorRendering::ZoneColor> newFillColors;
    QVector<PhosphorRendering::ZoneColor> newBorderColors;
    newRects.reserve(m_zones.size());
    newFillColors.reserve(m_zones.size());
    newBorderColors.reserve(m_zones.size());

    int highlightedCount = 0;
    int index = 0;

    for (const QVariant& zoneVar : std::as_const(m_zones)) {
        const QVariantMap z = zoneVar.toMap();

        // Extract zone rectangle
        PhosphorRendering::ZoneRect rect;

        // Extract pixel coordinates and normalize to 0-1 using iResolution
        const float px = z.value(QLatin1String(::PhosphorZones::ZoneJsonKeys::X), 0).toFloat();
        const float py = z.value(QLatin1String(::PhosphorZones::ZoneJsonKeys::Y), 0).toFloat();
        const float pw = z.value(QLatin1String(::PhosphorZones::ZoneJsonKeys::Width), 0).toFloat();
        const float ph = z.value(QLatin1String(::PhosphorZones::ZoneJsonKeys::Height), 0).toFloat();

        rect.x = px / resW;
        rect.y = py / resH;
        rect.width = pw / resW;
        rect.height = ph / resH;

        // Extract zone number and highlighted state (zone selector or hover override)
        rect.zoneNumber = z.value(QLatin1String(::PhosphorZones::ZoneJsonKeys::ZoneNumber), 0).toInt();
        rect.highlighted = z.value(QLatin1String(::PhosphorZones::ZoneJsonKeys::IsHighlighted), false).toBool()
            || (m_hoveredZoneIndex >= 0 && index == m_hoveredZoneIndex);

        // Extract shader border properties (stored in snapshot for thread-safe access).
        // Defaults mirror ConfigDefaults so the shader path picks up the same
        // fallback values as the non-shader path when a zone doesn't override them.
        rect.borderRadius =
            z.value(ZoneSnapshotKeys::ShaderBorderRadius, static_cast<float>(ConfigDefaults::borderRadius())).toFloat();
        rect.borderWidth =
            z.value(ZoneSnapshotKeys::ShaderBorderWidth, static_cast<float>(ConfigDefaults::borderWidth())).toFloat();

        if (rect.highlighted) {
            ++highlightedCount;
        }

        newRects.append(rect);

        // Extract fill color (premultiplied RGBA, 0-1 range)
        PhosphorRendering::ZoneColor fillColor;
        fillColor.r = z.value(ZoneSnapshotKeys::FillR, 0.0f).toFloat();
        fillColor.g = z.value(ZoneSnapshotKeys::FillG, 0.0f).toFloat();
        fillColor.b = z.value(ZoneSnapshotKeys::FillB, 0.0f).toFloat();
        fillColor.a = z.value(ZoneSnapshotKeys::FillA, 0.0f).toFloat();
        newFillColors.append(fillColor);

        // Extract border color (RGBA, 0-1 range)
        PhosphorRendering::ZoneColor borderColor;
        borderColor.r = z.value(ZoneSnapshotKeys::BorderR, 1.0f).toFloat();
        borderColor.g = z.value(ZoneSnapshotKeys::BorderG, 1.0f).toFloat();
        borderColor.b = z.value(ZoneSnapshotKeys::BorderB, 1.0f).toFloat();
        borderColor.a = z.value(ZoneSnapshotKeys::BorderA, 1.0f).toFloat();
        newBorderColors.append(borderColor);
        ++index;
    }

    // Update zone counts. parseZoneData is a pure recompute helper; emitting
    // the zoneCount / highlightedCount NOTIFY signals is the CALLER's job.
    // setZones() captures the old counts and emits on change (setters.cpp);
    // the geometryChange and componentComplete callers deliberately do not,
    // because a resolution re-parse leaves the count invariant and the initial
    // parse predates any binding observer. Emitting here would double-fire.
    m_zoneCount = newRects.size();
    m_highlightedCount = highlightedCount;

    // Thread-safe update of zone data snapshot
    {
        QMutexLocker lock(&m_zoneDataMutex);
        m_zoneData.rects = std::move(newRects);
        m_zoneData.fillColors = std::move(newFillColors);
        m_zoneData.borderColors = std::move(newBorderColors);
        m_zoneData.zoneCount = m_zoneCount;
        m_zoneData.highlightedCount = m_highlightedCount;
        m_zoneData.version = ++m_dataVersion;
    }

    m_zoneDataDirty = true;
}

void ZoneShaderItem::updateHoveredHighlightOnly()
{
    // Precondition: m_zoneData must be populated by a prior setZones/parseZoneData call.
    if (m_zoneData.rects.size() != static_cast<qsizetype>(m_zones.size())) {
        qCWarning(PlasmaZones::lcOverlay)
            << "updateHoveredHighlightOnly: zone data out of sync (rects=" << m_zoneData.rects.size()
            << "zones=" << m_zones.size() << ") - setZones must be called first";
        return;
    }
    // Pre-compute highlight flags outside the mutex to avoid blocking the render
    // thread with QVariant::toMap() conversions.
    const int count = static_cast<int>(m_zoneData.rects.size());
    QVector<bool> highlights(count, false);
    int highlightedCount = 0;
    for (int i = 0; i < count; ++i) {
        // .at(), not operator[]: m_zones is a non-const member, so the mutable
        // overload would detach the whole list on the first iteration of every
        // mouse-move. The early return above already pins i in range.
        const bool fromZone =
            m_zones.at(i).toMap().value(QLatin1String(::PhosphorZones::ZoneJsonKeys::IsHighlighted), false).toBool();
        const bool hovered = (m_hoveredZoneIndex >= 0 && i == m_hoveredZoneIndex);
        highlights[i] = fromZone || hovered;
        if (highlights[i]) {
            ++highlightedCount;
        }
    }
    {
        QMutexLocker lock(&m_zoneDataMutex);
        for (int i = 0; i < count; ++i) {
            m_zoneData.rects[i].highlighted = highlights[i];
        }
        m_zoneData.highlightedCount = highlightedCount;
        m_zoneData.version = ++m_dataVersion;
    }
    const int oldCount = m_highlightedCount;
    m_highlightedCount = highlightedCount;
    if (oldCount != m_highlightedCount) {
        Q_EMIT highlightedCountChanged();
    }
    m_zoneDataDirty = true;
}

// ============================================================================
// Thread-Safe Zone Data Accessors
// ============================================================================

PhosphorRendering::ZoneDataSnapshot ZoneShaderItem::getZoneDataSnapshot() const
{
    QMutexLocker lock(&m_zoneDataMutex);
    return m_zoneData;
}

// ============================================================================
// Scene Graph Integration
// ============================================================================

QSGNode* ZoneShaderItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data)
{
    Q_UNUSED(data)

    if (width() <= 0 || height() <= 0) {
        if (oldNode) {
            // Mirror the parent ShaderEffect's zero-size branch: sever the
            // node's back-pointer to this item via invalidateItem() before
            // deleting it, so any in-flight render-thread access fails safe
            // instead of walking a freed item.
            static_cast<PhosphorRendering::ZoneShaderNodeRhi*>(oldNode)->invalidateItem();
            // Deregister from the base so its destructor doesn't invalidate
            // a dangling pointer.
            registerRenderNode(nullptr);
            delete oldNode;
        }
        return nullptr;
    }

    // freshNode covers SG-deletion + first-call: a brand-new node has no shader
    // baked, so it must trigger a load even when nothing is dirty.
    bool freshNode = false;
    auto* node = static_cast<PhosphorRendering::ZoneShaderNodeRhi*>(oldNode);
    if (!node) {
        // Scene graph deleted the previous node, or first call. Route node
        // creation through the parent's createShaderNode() factory hook so
        // both ShaderEffect subclasses (this and tools/shader-render's
        // RenderEffect) follow the same factory pattern — and a future
        // refactor to delegate updatePaintNode to the parent picks up the
        // right node type for free. REGISTER the node with the base (this
        // override replaces the base updatePaintNode that normally does the
        // tracking) so the base destructor / sceneGraphAboutToStop teardown
        // severs the node's item back-pointer — the same participation
        // SurfaceShaderItem has.
        registerRenderNode(nullptr);
        node = static_cast<PhosphorRendering::ZoneShaderNodeRhi*>(createShaderNode());
        freshNode = true;
        qCDebug(PlasmaZones::lcOverlay) << "updatePaintNode: created NEW ZoneShaderNodeRhi (oldNode was null)";
    }
    // Register on every frame, not just the fresh-node path: windowChanged
    // clears the base's tracked node, and a reuse-path frame after that would
    // leave the teardown guard permanently disarmed.
    registerRenderNode(node);
    // No per-frame log on the reuse path: updatePaintNode runs on every rendered
    // frame, so logging here floods the journal at the repaint rate.

    // ── Sync base properties (time, params, colors, audio, multipass, depth, wallpaper) ──
    // syncBasePropertiesToNode now also pushes m_uniformExtension down to the
    // node — we seeded it in our constructor (qualified base setter to bypass
    // our own override) with our internal ZoneUniformExtension, so the zone
    // UBO region stays populated across scene-graph node recreations without
    // any extra plumbing here.
    // syncBasePropertiesToNode pushes user textures (slots 0..3) already.
    syncBasePropertiesToNode(node);

    // ── Sync labels texture (zone-specific, not in parent) ───────────
    {
        QMutexLocker lock(&m_labelsTextureMutex);
        node->setLabelsTexture(m_labelsTexture);
    }

    // ── Sync shader source ───────────────────────────────────────────
    // Reload only on an actual dirty flag (runtime setShaderSource /
    // setShaderIncludePaths / reloadShader, or device-loss via
    // sceneGraphAboutToStop) or a freshly created node. This mirrors
    // ShaderEffect::updatePaintNode and its warning: do NOT reload on
    // !node->isShaderReady() — a permanent load/compile failure leaves the node
    // un-ready forever, so reloading on it re-runs the loader + glslang bake on
    // EVERY frame (hard CPU spike + journal flood). A transient failure retries
    // on the next genuine shaderSource/param change, which sets the dirty flag.
    const bool wasDirty = consumeShaderDirty();
    const bool needLoad = wasDirty || freshNode;
    const bool shaderSourceValid = shaderSource().isValid() && !shaderSource().isEmpty();

    if (needLoad) {
        if (shaderSourceValid) {
            QString fragPath = shaderSource().toLocalFile();
            if (shaderSource().scheme() == QLatin1String("qrc")) {
                fragPath = QLatin1Char(':') + shaderSource().path();
            }

            // Resolve vertex shader: per-shader zone.vert > zone.vert from the
            // include paths. Shared with the daemon warm bake via
            // resolveZoneVertexPath so the two cannot pick different files —
            // the resolved path is part of the bake-cache key.
            const QString vertPath = resolveZoneVertexPath(fragPath, shaderIncludePaths());

            node->setShaderIncludePaths(shaderIncludePaths());
            // T1.4: install the zone entry-point scaffold so a pack authored as
            // just `vec4 pZone(ZoneCtx)` / `pImage(vec2)` (no main()) is
            // assembled — prologue prepended, generated main() appended — before
            // include expansion. A traditional pack with its own main() is left
            // untouched. Must match the warm-bake scaffold (daemon.cpp) so the
            // bake-cache key agrees.
            node->setEntryScaffold(zoneEntryPrologue(), zoneEntryCandidates());
            // T1.1 (zone): push the generated `#define p_<id> ...` preamble
            // (set on this item via the paramPreamble Q_PROPERTY by the overlay)
            // so loadFragmentShader splices it and keys the bake cache on it.
            // Empty when the pack declares no params, or for a pack not yet
            // migrated to p_ names — a no-op either way.
            node->setParamPreamble(paramPreamble());
            qCDebug(PlasmaZones::lcOverlay)
                << "Shader include paths:" << shaderIncludePaths() << "vertPath:" << vertPath;

            node->setVertexShaderSource(QString());
            node->setFragmentShaderSource(QString());

            bool loaded = true;
            if (!vertPath.isEmpty()) {
                if (!node->loadVertexShader(vertPath)) {
                    qCWarning(PlasmaZones::lcOverlay)
                        << "Failed to load vertex shader:" << vertPath << "error:" << node->shaderError();
                    loaded = false;
                }
            } else {
                qCWarning(PlasmaZones::lcOverlay)
                    << "No vertex shader found for" << fragPath << "(expected zone.vert in shader dir or search paths)";
                loaded = false;
            }

            // Load fragment shader
            if (loaded && !fragPath.isEmpty()) {
                if (!node->loadFragmentShader(fragPath)) {
                    loaded = false;
                }
            }

            if (loaded) {
                node->invalidateShader(); // Ensure node re-bakes
                setStatus(Status::Ready);
                // Force zone data resync when shader changes successfully
                m_zoneDataDirty = true;
            } else {
                QString errorMsg = node->shaderError();
                if (errorMsg.isEmpty()) {
                    errorMsg = QStringLiteral("Shader loading failed - missing required files");
                }
                setError(errorMsg);
            }
        } else {
            // Source empty — clear node
            node->setVertexShaderSource(QString());
            node->setFragmentShaderSource(QString());
            node->invalidateShader();
            setStatus(Status::Null);
        }
    }

    // ── Logical-to-device scale for the lengths in zoneParams ────────
    //
    // Corner radius and border width arrive from settings in LOGICAL px, while
    // the shader's rect / fragCoord space follows iResolution. Ask the base for
    // the exact factor it scaled iResolution by, so the two cannot disagree.
    //
    // This used to re-derive the ratio here, duplicating the base's logic
    // across a library boundary and reading a different member that only
    // aliases the base's because the constructor installs one into the other.
    // Any later change to how the base derives it would have left zoneScale
    // silently disagreeing with iResolution, which is the one invariant this
    // call exists to hold.
    //
    // Pushed unconditionally rather than under m_zoneDataDirty: moving the
    // overlay to a differently-scaled screen changes the ratio without
    // touching zone contents, and setScale() early-outs when the value is
    // unchanged, so the steady-state cost is a mutex lock and a compare.
    // setScale is [[nodiscard]] and its contract says the caller reports a
    // rejection, so this branch handles it. No current DPR source can trigger
    // it: effectiveResolutionScale() returns either the window's device-pixel
    // ratio or literal 1.0, both always positive and finite. It is a backstop
    // against that contract changing, not an observed runtime state.
    const qreal scale = effectiveResolutionScale();
    if (!m_zoneExtension->setScale(static_cast<float>(scale)) && !m_loggedBadScale) {
        m_loggedBadScale = true;
        qCWarning(PlasmaZones::lcOverlay)
            << "ZoneShaderItem: rejected out-of-contract zone scale" << scale
            << "- keeping the last good value. Corner radii and border widths will not track this screen.";
    }

    // ── Sync zone data to the node AFTER shader is ready ─────────────
    //
    // Three things happen together:
    //   1. Convert our thread-safe ZoneDataSnapshot into the wire-format
    //      ZoneData vector the extension's writer expects.
    //   2. Push zone contents into m_zoneExtension (writes the UBO region).
    //   3. Tell the node the new counts so it can update appField0/appField1
    //      for the shader's per-zone loops / highlight gating.
    //
    // The extension update bypasses the node entirely — the node is a
    // transient scene-graph object that gets recreated on releaseResources,
    // while the extension lives for the lifetime of this item.
    if (m_zoneDataDirty.load()) {
        if (node->isShaderReady()) {
            m_zoneDataDirty.store(false);
            PhosphorRendering::ZoneDataSnapshot snapshot = getZoneDataSnapshot();

            // The UBO's zone arrays are fixed at MaxZones, and both the
            // extension writer and setZoneCounts clamp to it, so an oversized
            // layout truncates rather than corrupting. Truncation is silent
            // everywhere else in the chain though, and the symptom is zones
            // that simply do not appear, so say so once.
            if (snapshot.zoneCount > PhosphorRendering::MaxZones && !m_loggedZoneOverflow) {
                m_loggedZoneOverflow = true;
                qCWarning(PlasmaZones::lcOverlay)
                    << "ZoneShaderItem: layout has" << snapshot.zoneCount << "zones but the shader UBO holds"
                    << PhosphorRendering::MaxZones << "- the excess will not be rendered.";
            }

            // Clamped: everything past MaxZones is discarded by
            // updateFromZones() anyway, so building it (two QColor conversions
            // apiece) is pure waste on an oversized layout. The raw count still
            // goes to setZoneCounts and to the warning above, both of which
            // want the true number.
            const int syncCount = qMin(snapshot.zoneCount, PhosphorRendering::MaxZones);
            QVector<PhosphorRendering::ZoneData> zoneDataVec;
            zoneDataVec.reserve(syncCount);

            int uploadedHighlighted = 0;
            for (int i = 0; i < syncCount; ++i) {
                PhosphorRendering::ZoneData zd;

                // Rectangle (already normalized 0-1)
                const PhosphorRendering::ZoneRect& rect = snapshot.rects[i];
                zd.rect = QRectF(static_cast<qreal>(rect.x), static_cast<qreal>(rect.y), static_cast<qreal>(rect.width),
                                 static_cast<qreal>(rect.height));
                zd.zoneNumber = rect.zoneNumber;
                zd.isHighlighted = rect.highlighted;
                zd.borderRadius = rect.borderRadius;
                zd.borderWidth = rect.borderWidth;

                // Fill color
                const PhosphorRendering::ZoneColor& fill = snapshot.fillColors[i];
                zd.fillColor = QColor::fromRgbF(static_cast<float>(fill.r), static_cast<float>(fill.g),
                                                static_cast<float>(fill.b), static_cast<float>(fill.a));

                // Border color
                const PhosphorRendering::ZoneColor& border = snapshot.borderColors[i];
                zd.borderColor = QColor::fromRgbF(static_cast<float>(border.r), static_cast<float>(border.g),
                                                  static_cast<float>(border.b), static_cast<float>(border.a));

                if (rect.highlighted) {
                    ++uploadedHighlighted;
                }
                zoneDataVec.append(zd);
            }

            m_zoneExtension->updateFromZones(zoneDataVec);
            // Highlight count is the count AMONG THE UPLOADED zones, not among
            // all of them: on an oversized layout the shader can only see the
            // first MaxZones, so a snapshot-wide count would make appField1
            // claim more highlights than the shader has zones for.
            node->setZoneCounts(snapshot.zoneCount, uploadedHighlighted);
        }
        // If shader not ready, leave dirty flag set so we sync on next frame
    }

    // ── Update status based on shader node state ─────────────────────
    if (node->isShaderReady() && status() != Status::Ready) {
        setStatus(Status::Ready);
    } else if (!node->shaderError().isEmpty() && status() != Status::Error) {
        setError(node->shaderError());
    }

    // Mark node as dirty to trigger re-render
    node->markDirty(QSGNode::DirtyMaterial);

    return node;
}

// ============================================================================
// Geometry Handling
// ============================================================================

void ZoneShaderItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    // Let parent handle iResolution update
    PhosphorRendering::ShaderEffect::geometryChange(newGeometry, oldGeometry);

    // Re-parse zones with new resolution for normalization
    if (newGeometry.size() != oldGeometry.size() && !m_zones.isEmpty()) {
        parseZoneData();
    }
}

void ZoneShaderItem::componentComplete()
{
    PhosphorRendering::ShaderEffect::componentComplete();

    // Parse initial zone data if any
    if (!m_zones.isEmpty()) {
        parseZoneData();
    }
}

} // namespace PlasmaZones
