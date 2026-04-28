// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorshell_export.h>

#include <PhosphorFsLoader/WatchedDirectorySet.h>

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

namespace PhosphorShell {

class IWallpaperProvider;

/// Registry of available shader effects.
///
/// Discovers shaders from configured search paths, validates metadata,
/// manages parameter presets, and watches for file changes.
///
/// Unlike PlasmaZones' singleton, consumers create their own instances
/// and register search paths explicitly.
///
/// ## Thread safety
///
/// GUI-thread only for both reads and mutations. The shader map
/// (`m_shaders`) is rebuilt on the GUI thread inside the rescan; the
/// public lookup methods (`availableShaders`, `shader`, `shaderInfo`,
/// `shaderUrl`) read it without synchronisation.
///
/// `searchPaths()` is the one exception: it returns a by-value snapshot
/// of an implicitly-shared QStringList, so a GUI-thread caller can
/// snapshot it and propagate the result to worker threads (this is the
/// shader-warming path's contract). Calling `searchPaths()` *from* a
/// worker thread concurrently with a GUI-thread mutation is a data race;
/// snapshot on the GUI thread first.
class PHOSPHORSHELL_EXPORT ShaderRegistry : public QObject
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

    explicit ShaderRegistry(QObject* parent = nullptr);
    ~ShaderRegistry() override;

    // ── Search paths ──────────────────────────────────────────────────

    /// Add a directory to search for shader subdirectories. Forwards to
    /// the batched form so the underlying watcher only runs one initial
    /// scan; prefer `addSearchPaths` directly when registering more than
    /// one path during construction (avoids N redundant scans, one per
    /// path).
    ///
    /// @p liveReload defaults to `On` so production callers get
    /// hot-reload by default. Pass `Off` from tests / batch-import
    /// contexts that want a one-shot scan with no background watcher.
    /// Inherits the underlying `WatchedDirectorySet`'s one-way-enable
    /// semantics: once any call passes `On`, the watcher stays armed
    /// for the registry's lifetime.
    void addSearchPath(const QString& path, PhosphorFsLoader::LiveReload liveReload = PhosphorFsLoader::LiveReload::On);

    /// Add multiple search-path directories in one shot. The strategy
    /// applies first-registration-wins on shader-id collision under the
    /// canonical convention; @p order tells the base which end of @p paths
    /// is highest-priority so it can normalise before the strategy runs.
    /// Default `LowestPriorityFirst` matches `[sys-lowest, ..., sys-highest,
    /// user]` (what the daemon's `setupAnimationShaderEffects` already
    /// builds); pass `HighestPriorityFirst` to feed `locateAll`'s natural
    /// output without a manual pre-reverse.
    ///
    /// Prefer this over a loop of `addSearchPath` calls — a single
    /// batched call runs exactly one scan instead of N.
    ///
    /// Same `liveReload` semantics as the single-path overload.
    void addSearchPaths(
        const QStringList& paths, PhosphorFsLoader::LiveReload liveReload = PhosphorFsLoader::LiveReload::On,
        PhosphorFsLoader::RegistrationOrder order = PhosphorFsLoader::RegistrationOrder::LowestPriorityFirst);

    /// Mark @p path as the "user" search path for `ShaderInfo::isUserShader`
    /// classification. Stored as-given; the rescan canonicalises both this
    /// path and each iterated search dir before comparing, so the input
    /// can be either canonical or symlinked. Pass the empty string (the
    /// default) to disable user/system differentiation — every shader will
    /// then report `isUserShader == false`.
    ///
    /// Order-independent: callable before OR after `addSearchPaths`. When
    /// the value changes and at least one search path is already
    /// registered, the call triggers a synchronous rescan so already-
    /// discovered shaders get reclassified immediately. Idempotent:
    /// passing the same value twice is a no-op. Bidirectional: passing
    /// the empty string clears the user-path designation, passing a
    /// non-empty string sets or replaces it.
    ///
    /// GUI-thread only — when the path changes, the synchronous rescan
    /// asserts the calling thread.
    void setUserShaderPath(const QString& path);

    /// Current search paths.
    QStringList searchPaths() const;

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
    Q_INVOKABLE QVariantMap presetParams(const QString& shaderId, const QString& presetName) const;
    Q_INVOKABLE QStringList shaderPresetNames(const QString& shaderId) const;
    Q_INVOKABLE QVariantList shaderPresetsVariant(const QString& shaderId) const;

    /// Always true — `Qt6::ShaderTools` is a hard build dependency of
    /// `phosphor-shell`, so a built registry necessarily has shader
    /// support. Kept as `Q_INVOKABLE` because QML callers historically
    /// used it as a feature gate; new code can omit the check.
    Q_INVOKABLE bool shadersEnabled() const
    {
        return true;
    }

    // ── Lifecycle ─────────────────────────────────────────────────────

    Q_INVOKABLE void refresh();

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
    class ShaderScanStrategy;
    QStringList performScan(const QStringList& directoriesInScanOrder);

    void loadShaderFromDir(const QString& shaderDir, bool isUserShader);
    ShaderInfo loadShaderMetadata(const QString& shaderDir);
    bool validateParameterValue(const ParameterInfo& param, const QVariant& value) const;
    QVariantMap shaderInfoToVariantMap(const ShaderInfo& info) const;
    QVariantMap parameterInfoToVariantMap(const ParameterInfo& param) const;

    QHash<QString, ShaderInfo> m_shaders;
    /// User-shader search path used to classify discovered shaders as
    /// user vs system. Compared against each iterated search dir's
    /// canonical form on every rescan — see `setUserShaderPath`.
    QString m_userShaderPath;
    /// SHA-1 over the discovered-shader set's identification + on-disk
    /// fingerprint (id, paths, isUserShader, frag mtime+size). Used by
    /// `performScan` to gate `shadersChanged` to actual content changes
    /// — the same change-only-emit pattern the sister `JsScanStrategy`
    /// and `AnimationShaderRegistry` consumers use, and a deliberate
    /// improvement on the legacy "fire on every watcher event" shape
    /// that fanned settings-page redraws across every editor save.
    QByteArray m_lastShadersSignature;
    std::unique_ptr<ShaderScanStrategy> m_strategy;
    std::unique_ptr<PhosphorFsLoader::WatchedDirectorySet> m_watcher;

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

} // namespace PhosphorShell
