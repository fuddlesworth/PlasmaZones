// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/phosphorshaders_export.h>

#include <PhosphorRegistry/IFactoryBase.h>
#include <PhosphorFsLoader/WatchedDirectorySet.h>
#include <PhosphorRegistry/MetadataPackLoader.h>
#include <PhosphorRegistry/Registry.h>

#include <QImage>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QRect>
#include <QSize>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <array>
#include <memory>
#include <optional>

namespace PhosphorShaders {

class IWallpaperProvider;

/// Registry of available shader effects.
///
/// Discovers shaders from configured search paths, validates metadata,
/// manages parameter presets, and watches for file changes.
///
/// Composition roots own a per-process instance and register search
/// paths explicitly — there is no library-level singleton. Tests
/// construct a per-fixture registry; downstream consumers (Phosphor
/// shell, future plugin compositors) instantiate their own.
///
/// Storage + change-notify is the generic `PhosphorRegistry::Registry<ShaderPack>`
/// (one ShaderPack wraps one discovered ShaderInfo); the on-disk scan +
/// hot-reload is a `PhosphorRegistry::MetadataPackLoader<ShaderPack>` that
/// parses each pack's `metadata.json` and reconciles the registry.
/// Search-path management (`addSearchPath`, `addSearchPaths`,
/// `searchPaths`, `setUserPath`, `refresh`) forwards to that loader.
///
/// ## Thread safety
///
/// GUI-thread only for both reads and mutations. The shader map lives
/// inside the registry and is rebuilt on the GUI thread inside the
/// rescan; the public lookup methods (`availableShaders`, `shader`,
/// `shaderInfo`, `shaderUrl`) read it without synchronisation.
///
/// Two exceptions:
///
/// `searchPaths()` returns a by-value snapshot of an implicitly-shared
/// QStringList, so a GUI-thread caller can snapshot it and propagate the
/// result to worker threads (this is the shader-warming path's contract).
/// Calling `searchPaths()` *from* a worker thread concurrently with a
/// GUI-thread mutation is a data race; snapshot on the GUI thread first.
///
/// The static wallpaper-path cache (`resolveWallpaperPath` and friends in
/// shaderregistry_wallpaper.cpp) is guarded by its own process-wide mutex
/// and is safe to call from any thread.
class PHOSPHORSHADERS_EXPORT ShaderRegistry : public QObject
{
    Q_OBJECT

public:
    struct ParameterInfo
    {
        QString id;
        QString name;
        QString group;
        QString type; ///< "float", "color", "int", "bool", "image"
        int slot = -1;
        QVariant defaultValue;
        QVariant minValue;
        QVariant maxValue;
        bool useZoneColor = false; ///< Hint: consumer may bind to app-specific color
        QString wrap;

        /// Convert slot to uniform name (e.g., slot 0 → "customParams1_x")
        QString uniformName() const;
    };

    struct ShaderInfo
    {
        QString id;
        QString name;
        QString description;
        QString author;
        QString version;
        QUrl shaderUrl;
        QString sourcePath;
        QString vertexShaderPath;
        /// Absolute pack directory (where metadata.json lives). The frag may
        /// sit in a subdirectory of the pack, so deriving the pack root from
        /// `sourcePath` is wrong — consumers needing the pack root (watch
        /// globs, content signature, image-param containment) read this.
        QString packDir;
        QStringList bufferShaderPaths;
        QString previewPath;
        QString category;
        QList<ParameterInfo> parameters;
        QMap<QString, QVariantMap> presets;
        bool isUserShader = false;
        bool isMultipass = false;
        bool useWallpaper = false;
        bool bufferFeedback = false;
        qreal bufferScale = 1.0;
        /// Buffer-pass texel format: RGBA16F when true (default — safe for
        /// HDR, signed data, and feedback accumulators), RGBA8 when the pack
        /// declares its buffers hold plain clamped colour. Halves buffer
        /// bandwidth on the packs that can afford it.
        bool halfFloatBuffers = true;
        QString bufferWrap = QStringLiteral("clamp");
        QStringList bufferWraps;
        QString bufferFilter = QStringLiteral("linear");
        QStringList bufferFilters;
        bool useDepthBuffer = false;

        bool isValid() const
        {
            // isNoneShader(id) is just id.isEmpty(), which the first conjunct
            // already excludes, so the "none" shader is never valid here.
            return !id.isEmpty() && shaderUrl.isValid();
        }
    };

    /// Registry entry: a discovered shader as a `PhosphorRegistry` factory.
    /// `Registry<ShaderPack>` keys on `id()`; `displayName()` is the shader's
    /// human name. Wraps the parsed `ShaderInfo` by value (consumers read it
    /// back via `info()`). Shaders carry no capability metadata, so the
    /// `IFactoryBase` default `capabilities()` (`{}`) applies unchanged.
    class ShaderPack final : public PhosphorRegistry::IFactoryBase
    {
    public:
        explicit ShaderPack(ShaderInfo info)
            : m_info(std::move(info))
        {
        }
        [[nodiscard]] QString id() const override
        {
            return m_info.id;
        }
        [[nodiscard]] QString displayName() const override
        {
            return m_info.name;
        }
        [[nodiscard]] const ShaderInfo& info() const
        {
            return m_info;
        }

    private:
        ShaderInfo m_info;
    };

    explicit ShaderRegistry(QObject* parent = nullptr);
    ~ShaderRegistry() override;

    // ── Search paths (forwarded to the internal MetadataPackLoader) ───
    //
    // Same surface the since-removed MetadataPackRegistryBase provided. liveReload
    // defaults to On (production hot-reload); pass Off for one-shot scans.
    void addSearchPath(const QString& path, PhosphorFsLoader::LiveReload liveReload = PhosphorFsLoader::LiveReload::On);
    void addSearchPaths(
        const QStringList& paths, PhosphorFsLoader::LiveReload liveReload = PhosphorFsLoader::LiveReload::On,
        PhosphorFsLoader::RegistrationOrder order = PhosphorFsLoader::RegistrationOrder::LowestPriorityFirst);
    [[nodiscard]] QStringList searchPaths() const;
    void setUserPath(const QString& path);
    Q_INVOKABLE void refresh();

    // ── Shader discovery ──────────────────────────────────────────────

    static QString noneShaderUuid();
    static bool isNoneShader(const QString& id);

    QList<ShaderInfo> availableShaders() const;
    Q_INVOKABLE QVariantList availableShadersVariant() const;

    ShaderInfo shader(const QString& id) const;
    Q_INVOKABLE QVariantMap shaderInfo(const QString& id) const;
    Q_INVOKABLE QUrl shaderUrl(const QString& id) const;

    // ── Parameters & presets ──────────────────────────────────────────

    Q_INVOKABLE QVariantMap defaultParams(const QString& id) const;
    bool validateParams(const QString& id, const QVariantMap& params) const;
    QVariantMap validateAndCoerceParams(const QString& id, const QVariantMap& params) const;
    Q_INVOKABLE QVariantMap translateParamsToUniforms(const QString& shaderId, const QVariantMap& storedParams) const;

    /// Build the generated `#define p_<id> <glsl-accessor>` preamble (T1.1) for
    /// @p info's declared parameters, so a zone shader author reads a parameter
    /// by name (`p_borderRadius`) instead of hand-decoding a
    /// `customParams[N].xyzw` lane. Each param's explicit `slot` drives the
    /// accessor exactly as `ParameterInfo::uniformName()` /
    /// `translateParamsToUniforms` derive the upload target — scalar slot N →
    /// `customParams[N/4].<xyzw>`, color slot N → `customColors[N]`, image slot
    /// N → `uTexture<N>` — so the macro a shader reads resolves to the same UBO
    /// lane the value is uploaded to. The daemon overlay splices the result
    /// after the shader's `#version` (via `PhosphorShaders::spliceAfterVersion`).
    /// Empty when the shader declares no parameters.
    static QString paramPreamble(const ShaderInfo& info);

    /// Parse a pack directory's `metadata.json` into a ShaderInfo using the SAME
    /// parser the live registry uses (T1.1 auto-slot assignment included), so an
    /// offline validator (`phosphor-shader-validate`) and the daemon agree on
    /// what a pack is. Returns an invalid ShaderInfo and sets @p error on a
    /// missing/unreadable/oversized file, non-object JSON root, or a root
    /// failing the same structural schema gate the live scan applies. Two live
    /// rejections are deliberately NOT reproduced here — a missing fragment
    /// shader and a multipass pack with no surviving buffer shader — because
    /// the offline validator reports those as its own lints with file context.
    ///
    /// Existence is checked unevenly, and a caller cannot infer "not declared"
    /// from an empty field: the FRAGMENT path is returned whether or not the file
    /// is there (that one is a validator lint), a declared-but-missing
    /// `vertexShader` is DROPPED, and a missing buffer shader clears the whole
    /// `bufferShaderPaths` list.
    ///
    /// Declared paths are containment-checked: a `fragmentShader`,
    /// `vertexShader` or `bufferShaders` entry resolving outside the pack
    /// directory is refused, because these name files that get compiled and run
    /// on the GPU. A refused `sourcePath` comes back EMPTY, and a refused entry
    /// drops the whole `bufferShaderPaths` list (they are positionally aligned
    /// with the per-buffer wrap/filter overrides, so compacting one out would
    /// shift the rest onto the wrong buffer). Image params are NOT part of this
    /// parse — they are resolved, and containment-checked, in
    /// `translateParamsToUniforms`.
    /// Parse a pack's metadata.json into a ShaderInfo (id, paths, parameters
    /// with auto-slot assignment).
    ///
    /// @param validateSchema when true (the default), the metadata is first run
    /// through the shared shader-metadata JSON schema and REJECTED on any
    /// violation — the same gate the daemon's live scan applies, so the offline
    /// validator refuses exactly the packs the daemon refuses. The shader-render
    /// preview tool passes false: it is a developer aid that tolerantly previews
    /// whatever metadata it is handed (defaulting names, dropping out-of-range
    /// slots) rather than gatekeeping, and its own parse defaults already mirror
    /// the tolerant behaviour. Path-traversal confinement is applied either way.
    static ShaderInfo parsePackMetadata(const QString& packDir, QString* error = nullptr, bool validateSchema = true);

    Q_INVOKABLE QVariantMap presetParams(const QString& shaderId, const QString& presetName) const;
    Q_INVOKABLE QStringList shaderPresetNames(const QString& shaderId) const;
    Q_INVOKABLE QVariantList shaderPresetsVariant(const QString& shaderId) const;

    /// Always true — once a `ShaderRegistry` is constructed, shader
    /// discovery and metadata are functional (the registry is purely a
    /// metadata-pack walker; actual shader compilation lives in the
    /// `phosphor-rendering` library which carries the `Qt6::ShaderTools`
    /// dependency). Kept as `Q_INVOKABLE` because QML callers
    /// historically used it as a feature gate from the era when the
    /// build had an opt-out for shader support; new code can omit the
    /// check.
    Q_INVOKABLE bool shadersEnabled() const
    {
        return true;
    }

    // ── Lifecycle ─────────────────────────────────────────────────────

    void reportShaderBakeStarted(const QString& shaderId);
    void reportShaderBakeFinished(const QString& shaderId, bool success, const QString& error);

    // ── Wallpaper ─────────────────────────────────────────────────────

    static QString wallpaperPath();
    static QImage loadWallpaperImage();

    /// Return the wallpaper image cropped to the portion that a sub-region
    /// (@p subGeom) occupies on a physical screen (@p physGeom), assuming
    /// "cover" scaling (aspect-correct fill, centered, overflow cropped) —
    /// the same placement model the `wallpaperUv` GLSL helper uses.
    ///
    /// Returns the full (uncropped) wallpaper when either rect is invalid
    /// or when @p subGeom covers all of @p physGeom.
    ///
    /// Virtual screens that share a physical monitor need this so each VS
    /// samples the wallpaper portion it occupies on the monitor, instead of
    /// each getting the center-cropped wallpaper as if it were a full screen.
    ///
    /// The result is memoized keyed on (@p subGeom, @p physGeom, wallpaper
    /// mtime), so repeated calls for the same VS return the same QImage
    /// (stable cacheKey()) and avoid re-uploading to the GPU each frame.
    static QImage loadWallpaperImage(const QRect& subGeom, const QRect& physGeom);

    /// Pure geometry helper: compute the pixel rect inside a wallpaper of
    /// size @p wpSize that corresponds to @p subGeom when the wallpaper
    /// covers @p physGeom under the same "cover" placement used by
    /// `wallpaperUv`. Returns an invalid rect if inputs are degenerate or
    /// if @p subGeom fully covers @p physGeom (caller should use the full
    /// image in that case). Exposed for unit testing.
    static QRect computeWallpaperCropRect(QSize wpSize, const QRect& physGeom, const QRect& subGeom);

    /// The same placement as computeWallpaperCropRect, but NORMALIZED and
    /// UNCLAMPED: the slice of the wallpaper corresponding to @p subGeom, in
    /// [0,1] texture coords, allowed to run outside that range when @p subGeom
    /// hangs over @p physGeom.
    ///
    /// For a caller that maps the result across a whole quad rather than
    /// cropping an image out of it. computeWallpaperCropRect intersects
    /// @p subGeom with @p physGeom, which is right for producing an image but
    /// wrong here: the shader spreads uv over the FULL surface regardless, so a
    /// clamped slice would be stretched to cover an overhanging one, shifting
    /// and mis-scaling the result. Letting the rect run out of range instead
    /// lets the sampler's edge clamp handle the overhang, which is what an
    /// outer glow reaching past the screen should look like.
    ///
    /// Returns a null optional only for degenerate inputs. A surface exactly
    /// covering the screen is NOT degenerate here — it legitimately maps to the
    /// whole cover region — which is the other reason this cannot reuse
    /// computeWallpaperCropRect, whose contract treats that as "use the full
    /// image".
    static std::optional<QRectF> wallpaperSliceNormalized(QSize wpSize, const QRect& physGeom, const QRect& subGeom);

    static void invalidateWallpaperCache();

Q_SIGNALS:
    void shadersChanged();
    void shaderCompilationStarted(const QString& shaderId);
    void shaderCompilationFinished(const QString& shaderId, bool success, const QString& error);

private:
    bool validateParameterValue(const ParameterInfo& param, const QVariant& value) const;
    QVariantMap shaderInfoToVariantMap(const ShaderInfo& info) const;
    QVariantMap parameterInfoToVariantMap(const ParameterInfo& param) const;

    // Generic id-keyed storage + change-notify for the discovered shader
    // packs. m_loader (below) populates it from disk; the lookup methods
    // read it. Declared before m_loader so it is destroyed AFTER the loader:
    // the loader holds a borrowed Registry pointer (used during live rescans
    // in reconcile(), not at teardown), which must stay valid for the loader's
    // whole lifetime.
    PhosphorRegistry::Registry<ShaderPack> m_registry;
    // On-disk scan + hot-reload. Parses each pack's metadata.json into a
    // ShaderPack and reconciles m_registry; its onCommitted hook re-emits
    // shadersChanged. unique_ptr so the ctor can configure it after the
    // member-init list (parser, watch paths, signature, commit hook).
    std::unique_ptr<PhosphorRegistry::MetadataPackLoader<ShaderPack>> m_loader;

    static std::unique_ptr<IWallpaperProvider> s_wallpaperProvider;
    static QString s_cachedWallpaperPath;
    /// Whether the provider has been asked since the last invalidation, and when.
    ///
    /// Distinguishes "not looked up yet" from "looked up, and there is no
    /// wallpaper": without it the empty answer is indistinguishable from a cache
    /// miss and every call re-queries a provider that may block for a second. The
    /// timestamp bounds that negative answer, because `invalidateWallpaperCache()`
    /// has no callers — a permanent latch would strand every wallpaper shader on
    /// the transparent fallback for the whole session if the first resolve landed
    /// before the desktop was ready.
    static bool s_wallpaperPathResolved;
    static qint64 s_wallpaperPathResolvedAtMs;
    static QImage s_cachedWallpaperImage;
    static qint64 s_cachedWallpaperMtime;
    static QMutex s_wallpaperCacheMutex;

    // Per-VS crop cache: keeps the same QImage (and cacheKey) for repeated
    // loadWallpaperImage(sub, phys) calls so downstream cacheKey()-based
    // short-circuits in ShaderEffect/ShaderNodeRhi keep working and the GPU
    // doesn't re-upload the wallpaper on every overlay update.
    struct WallpaperCropEntry
    {
        QRect sub;
        QRect phys;
        qint64 mtime = 0;
        QImage img;
    };
    static constexpr int CropCacheCapacity = 8;
    static std::array<WallpaperCropEntry, CropCacheCapacity> s_cachedWallpaperCrops;
    static int s_cachedWallpaperCropNextSlot;
};

} // namespace PhosphorShaders
