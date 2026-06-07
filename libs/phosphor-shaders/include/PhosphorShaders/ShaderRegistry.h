// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/phosphorshaders_export.h>

#include <PhosphorRegistry/IFactoryBase.h>
#include <PhosphorRegistry/MetadataPackLoader.h>
#include <PhosphorRegistry/Registry.h>

#include <QHash>
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
/// `searchPaths()` is the one exception: it returns a by-value snapshot
/// of an implicitly-shared QStringList, so a GUI-thread caller can
/// snapshot it and propagate the result to worker threads (this is the
/// shader-warming path's contract). Calling `searchPaths()` *from* a
/// worker thread concurrently with a GUI-thread mutation is a data race;
/// snapshot on the GUI thread first.
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
        QString bufferWrap = QStringLiteral("clamp");
        QStringList bufferWraps;
        QString bufferFilter = QStringLiteral("linear");
        QStringList bufferFilters;
        bool useDepthBuffer = false;

        bool isValid() const
        {
            return !id.isEmpty() && (isNoneShader(id) || shaderUrl.isValid());
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
    // Same surface the legacy MetadataPackRegistryBase provided. liveReload
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
    /// missing/unreadable file or non-object JSON root. Does NOT verify that the
    /// frag/buffer files exist on disk — that's a validator lint, not a parse
    /// failure — so `sourcePath`/`bufferShaderPaths` are returned as declared.
    static ShaderInfo parsePackMetadata(const QString& packDir, QString* error = nullptr);

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
