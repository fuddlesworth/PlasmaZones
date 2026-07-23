// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zoneshaderitem.h"
#include "zoneentryscaffold.h"
#include "zoneshadernoderhi.h"

#include <PhosphorRendering/ZoneShaderCommon.h>
#include <PhosphorRendering/ZoneShaderNodeRhi.h>
#include <PhosphorRendering/ZoneUniformExtension.h>

#include "config/configdefaults.h"
#include "core/types/constants.h"
#include "core/platform/logging.h"

#include <PhosphorRendering/ShaderEffect.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMetaType>
#include <QMutexLocker>
#include <QStandardPaths>
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

    // Set PlasmaZones-specific shader include paths so that #include <common.glsl>
    // in zone.vert/effect.frag resolves to the system shaders directory.
    // Use locateAll() because locate() stops at the first match (~/.local/share/
    // which has user shaders but NOT common.glsl). We need the system dir too.
    const QStringList allShaderDirs = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/overlays"), QStandardPaths::LocateDirectory);
    QStringList includePaths;
    for (const QString& dir : allShaderDirs) {
        const QString sharedDir = dir + QStringLiteral("/shared");
        if (QDir(sharedDir).exists()) {
            includePaths.append(sharedDir);
        }
        includePaths.append(dir);
    }
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
    // The zone UBO layout is load-bearing: ZoneShaderNodeRhi installs a
    // ZoneUniformExtension whose byte layout matches common.glsl's zone
    // arrays. Accepting a caller-supplied extension here would either
    // de-align that layout or silently replace zone rendering with garbage.
    // Log loudly at the point of misuse instead of inheriting the base
    // class's silent-store behaviour.
    Q_UNUSED(extension);
    qCWarning(PlasmaZones::lcOverlay) << "ZoneShaderItem::setUniformExtension: ignored: zone rendering owns its own "
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

    // Update zone counts
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
        qCWarning(lcOverlay) << "updateHoveredHighlightOnly: zone data out of sync (rects=" << m_zoneData.rects.size()
                             << "zones=" << m_zones.size() << ") - setZones must be called first";
        return;
    }
    // Pre-compute highlight flags outside the mutex to avoid blocking the render
    // thread with QVariant::toMap() conversions.
    const int count = static_cast<int>(m_zoneData.rects.size());
    QVector<bool> highlights(count, false);
    int highlightedCount = 0;
    for (int i = 0; i < count; ++i) {
        const bool fromZone = (i < m_zones.size())
            ? m_zones[i].toMap().value(QLatin1String(::PhosphorZones::ZoneJsonKeys::IsHighlighted), false).toBool()
            : false;
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

QVector<PhosphorRendering::ZoneRect> ZoneShaderItem::zoneRects() const
{
    QMutexLocker lock(&m_zoneDataMutex);
    return m_zoneData.rects;
}

QVector<PhosphorRendering::ZoneColor> ZoneShaderItem::zoneFillColors() const
{
    QMutexLocker lock(&m_zoneDataMutex);
    return m_zoneData.fillColors;
}

QVector<PhosphorRendering::ZoneColor> ZoneShaderItem::zoneBorderColors() const
{
    QMutexLocker lock(&m_zoneDataMutex);
    return m_zoneData.borderColors;
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
            if (auto* rhiNode = static_cast<PhosphorRendering::ZoneShaderNodeRhi*>(oldNode)) {
                rhiNode->invalidateItem();
            }
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
        registerRenderNode(node);
        freshNode = true;
        qCDebug(PlasmaZones::lcOverlay) << "updatePaintNode: created NEW ZoneShaderNodeRhi (oldNode was null)";
    }
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
            m_zoneDataDirty.exchange(false);
            PhosphorRendering::ZoneDataSnapshot snapshot = getZoneDataSnapshot();

            QVector<PhosphorRendering::ZoneData> zoneDataVec;
            zoneDataVec.reserve(snapshot.zoneCount);

            for (int i = 0; i < snapshot.zoneCount; ++i) {
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

                zoneDataVec.append(zd);
            }

            m_zoneExtension->updateFromZones(zoneDataVec);
            node->setZoneCounts(snapshot.zoneCount, snapshot.highlightedCount);
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
