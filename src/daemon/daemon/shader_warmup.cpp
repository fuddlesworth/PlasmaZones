// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/daemon.h"

#include "config/settings.h"
#include "core/interfaces/shaderregistry.h"
#include "core/platform/logging.h"
#include "daemon/overlayservice.h"
#include "daemon/rendering/surfaceshaderitem.h"
#include "daemon/rendering/zoneentryscaffold.h"
#include "daemon/rendering/zoneshadernoderhi.h"

#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QList>
#include <QPointer>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QtConcurrent>

#include <algorithm>
#include <memory>
#include <utility>

namespace PlasmaZones {

void Daemon::setupAnimationShaderEffects()
{
    m_animationShaderRegistry = std::make_unique<PhosphorAnimationShaders::AnimationShaderRegistry>(nullptr);

    // System dirs from XDG_DATA_DIRS in descending priority. Reverse so
    // the first registered is the lowest-priority system dir — the
    // strategy reverse-iterates and applies first-registration-wins,
    // which yields the canonical XDG semantic
    // `user > sys-highest > ... > sys-lowest` after the user dir is
    // appended last.
    QStringList animDirs = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/animations"), QStandardPaths::LocateDirectory);
    std::reverse(animDirs.begin(), animDirs.end());

    const QString userAnimDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/plasmazones/animations");
    if (!animDirs.contains(userAnimDir))
        animDirs.append(userAnimDir);

    // Materialise the user dir BEFORE registering so the watcher attaches
    // a direct watch instead of a parent-watch proxy. Without this, on a
    // fresh install (where `~/.local/share/plasmazones/animations` does
    // not yet exist) the watcher would climb to the user data root —
    // which is forbidden under the new fsloader rules — and silently
    // disable live-reload until the user manually triggered a refresh.
    // Mirrors the curve/profile/script setup pattern. Failures are non-
    // fatal — the on-demand scan still runs without a watch.
    QDir().mkpath(userAnimDir);

    m_animationShaderRegistry->setUserPath(userAnimDir);
    m_animationShaderRegistry->addSearchPaths(animDirs);

    if (m_overlayService) {
        m_overlayService->setAnimationShaderRegistry(m_animationShaderRegistry.get());
    }
}

void Daemon::setupSurfaceShaderEffects()
{
    m_surfaceShaderRegistry = std::make_unique<PhosphorSurfaceShaders::SurfaceShaderRegistry>(nullptr);

    // System dirs from XDG_DATA_DIRS in descending priority. Reverse so the
    // first registered is the lowest-priority system dir — the loader applies
    // first-registration-wins, yielding `user > sys-highest > ... > sys-lowest`
    // after the user dir is appended last. Surface packs install to
    // `plasmazones/surface` (singular), the third category beside
    // `plasmazones/overlays` and `plasmazones/animations`.
    QStringList surfaceDirs = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/surface"), QStandardPaths::LocateDirectory);
    std::reverse(surfaceDirs.begin(), surfaceDirs.end());

    const QString userSurfaceDir =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/plasmazones/surface");
    if (!surfaceDirs.contains(userSurfaceDir))
        surfaceDirs.append(userSurfaceDir);

    // Materialise the user dir BEFORE registering so the watcher attaches a
    // direct watch instead of a parent-watch proxy (mirrors the animation /
    // curve / profile setup). Failures are non-fatal — the on-demand scan still
    // runs without a watch.
    QDir().mkpath(userSurfaceDir);

    m_surfaceShaderRegistry->setUserPath(userSurfaceDir);
    m_surfaceShaderRegistry->addSearchPaths(surfaceDirs);

    // Stage d: hand the registry to the overlay service so the OSD show path can
    // resolve its decoration pack's fragment shader + translated params. Borrow
    // is nulled in stop() before the registry is reset, mirroring the animation
    // registry's teardown.
    if (m_overlayService) {
        m_overlayService->setSurfaceShaderRegistry(m_surfaceShaderRegistry.get());
    }
}

void Daemon::setupShaderWarmBakes()
{
    // QShaderBaker/glslang is not thread-safe — concurrent bake() calls crash
    // in QSpirvCompiler::compileToSpirv(). Limit to 1 thread so bakes are
    // sequential but still off the main thread.
    m_shaderBakePool.setMaxThreadCount(1);

    // Re-init guard: m_shaderRegistry is ctor-owned and survives stop(), so a
    // stop() → init() cycle would stack a second shadersChanged handler here
    // and double-schedule every zone bake from then on. The animation and
    // surface connections escape this only because their registries are
    // recreated each init.
    if (m_zoneWarmBakeConnection) {
        disconnect(m_zoneWarmBakeConnection);
    }

    // Skip-unchanged gate shared by all three categories below. A registry
    // emit reports the whole catalog, but the bake cache is keyed on content
    // (paths + preambles), so re-queuing a pack whose key inputs are
    // unchanged is pure wasted work on the single-threaded pool — one user
    // pack edit used to re-queue N bakes, and any UI listening to the zone
    // path's started/finished relays saw the entire catalog flip to
    // "compiling" on every one-file change. Returns true when the entry was
    // recorded (caller should schedule); false when the fingerprint is
    // already known.
    const auto shouldScheduleBake = [this](const QString& categoryAndId, const QString& fingerprint) {
        const auto it = m_scheduledBakeFingerprints.constFind(categoryAndId);
        if (it != m_scheduledBakeFingerprints.constEnd() && it.value() == fingerprint) {
            return false;
        }
        m_scheduledBakeFingerprints.insert(categoryAndId, fingerprint);
        return true;
    };

    // Warm cached shader bakes on every registry refresh so overlay paints
    // never block the GUI thread waiting for qsb. m_shaderRegistry itself
    // is constructed in the ctor init list (before m_overlayService, which
    // borrows it).
    auto scheduleWarmForShader =
        [this, shouldScheduleBake,
         registryPtr = QPointer<ShaderRegistry>(m_shaderRegistry.get())](const ShaderRegistry::ShaderInfo& info) {
            if (ShaderRegistry::isNoneShader(info.id) || !info.isValid()) {
                return;
            }
            if (info.sourcePath.isEmpty() || !QFile::exists(info.sourcePath)) {
                return;
            }
            ShaderRegistry* reg = registryPtr.data();
            if (!reg) {
                return;
            }
            // Resolve the vertex stage through the SHARED resolver so the warm
            // bake can never pick a different file than the live load — the
            // resolved path is folded into the bake-cache key, so any
            // divergence silently wastes the bake. Two earlier attempts here
            // diverged (wrong directory level; then reversed root priority,
            // because ShaderRegistry::searchPaths() is reverse(locateAll)),
            // which is why this now goes through one helper instead of
            // re-deriving the walk.
            const QString zoneVertPath = resolveZoneVertexPath(info.sourcePath);
            if (zoneVertPath.isEmpty() || !QFile::exists(zoneVertPath)) {
                return;
            }
            const QString shaderId = info.id;
            // Computed here (rather than at the capture below) because it is
            // part of the skip-unchanged fingerprint.
            const QString paramPreamble = ShaderRegistry::paramPreamble(info);
            if (!shouldScheduleBake(QStringLiteral("zone:") + shaderId,
                                    zoneVertPath + QLatin1Char('\n') + info.sourcePath + QLatin1Char('\n')
                                        + paramPreamble)) {
                return;
            }
            auto* watcher = new QFutureWatcher<PhosphorRendering::WarmShaderBakeResult>(this);
            connect(watcher, &QFutureWatcher<PhosphorRendering::WarmShaderBakeResult>::finished, this,
                    [registryPtr, watcher, shaderId]() {
                        if (!registryPtr) {
                            watcher->deleteLater();
                            return;
                        }
                        const PhosphorRendering::WarmShaderBakeResult r = watcher->result();
                        if (!r.success) {
                            qCWarning(lcDaemon) << "Shader bake: failed for" << shaderId << r.errorMessage;
                        }
                        registryPtr->reportShaderBakeFinished(shaderId, r.success, r.errorMessage);
                        watcher->deleteLater();
                    });
            reg->reportShaderBakeStarted(shaderId);
            // Pass the registry's authoritative search paths to the bake worker
            // so include resolution matches the on-screen render path exactly.
            // Snapshot now (registry can be mutated on the GUI thread; we're about
            // to hop onto the bake thread).
            const QStringList includePaths = reg->searchPaths();
            // T1.4 + T1.1: warm-bake with the SAME entry-point scaffold AND the
            // SAME generated param preamble ZoneShaderItem installs at runtime,
            // so a zone pack's warm entry keys identically to its live load (both
            // are folded into the bake-cache key). Computed on the GUI thread
            // (reads `info`) and captured by value into the bake-thread lambda.
            const QString entryPrologue = zoneEntryPrologue();
            const QList<PhosphorShaders::EntryCandidate> entryCandidates = zoneEntryCandidates();
            // Zone shaders bake through the UNQUALIFIED (PlasmaZones) wrapper on
            // purpose: it prepends the plasmazones/overlays trusted roots so
            // `#include <common.glsl>` resolves the same way ZoneShaderItem's
            // live load does. The animation and surface warm-bakes below MUST
            // instead call PhosphorRendering::warmShaderBakeCacheForPaths
            // directly — their include lists are already the animation/surface
            // shared roots, and routing them through this wrapper would prepend
            // overlays/shared, shadowing the animation/surface copies of
            // audio.glsl with the overlay copy (different UBO contract) and
            // fingerprinting the cache key on the wrong include content.
            watcher->setFuture(QtConcurrent::run(&m_shaderBakePool,
                                                 [vertPath = zoneVertPath, fragPath = info.sourcePath, includePaths,
                                                  paramPreamble, entryPrologue, entryCandidates]() {
                                                     return warmShaderBakeCacheForPaths(vertPath, fragPath,
                                                                                        includePaths, paramPreamble,
                                                                                        entryPrologue, entryCandidates);
                                                 }));
        };
    m_zoneWarmBakeConnection =
        connect(m_shaderRegistry.get(), &ShaderRegistry::shadersChanged, this, [this, scheduleWarmForShader]() {
            const QList<ShaderRegistry::ShaderInfo> shaders = m_shaderRegistry->availableShaders();
            for (const ShaderRegistry::ShaderInfo& info : shaders) {
                scheduleWarmForShader(info);
            }
        });
    // Warm cache once for shaders already loaded by ShaderRegistry ctor
    for (const ShaderRegistry::ShaderInfo& info : m_shaderRegistry->availableShaders()) {
        scheduleWarmForShader(info);
    }

    // Warm-bake ANIMATION shaders (fly-in / dissolve / etc.) the same
    // way zone shaders are warmed above. Without this, the first OSD
    // show after a fresh daemon start hits a cold cache: QShaderBaker
    // compiles the animation shader synchronously on the render thread
    // during the first frame, and the AV's animation duration (e.g.
    // 500 ms) can elapse before the shader is ready to paint. The
    // observed symptom: card "pops in" at rest with no slide animation
    // on the first OSD; every subsequent OSD show animates correctly
    // because the cache is now warm.
    //
    // Shares m_shaderBakePool with the zone-shader warm-bake. The
    // pool is single-threaded (QShaderBaker / glslang isn't thread-
    // safe), so animation and zone bakes serialise without interfering.
    //
    // Include-path resolution mirrors `SurfaceAnimator::runLeg`
    // (surfaceanimator.cpp): every animation search-path's `/shared`
    // subdir is added so an effect's vert / frag can
    // `#include <animation_uniforms.glsl>` and the bake worker resolves
    // it identically to the render-thread load path. The vertex-path fallback (default
    // `shared/animation.vert` when an effect doesn't ship its own)
    // also mirrors the runtime, otherwise the warm-baked entry's
    // cache key would differ from what runtime queries.
    // Defensive rather than load-bearing on the normal path: init() calls
    // setupAnimationShaderEffects() / setupSurfaceShaderEffects() immediately
    // before this, so both registries are non-null here. The guards remain
    // because stop() resets both (lifecycle.cpp), and a caller reaching this
    // function outside that ordering must skip rather than crash.
    if (m_animationShaderRegistry) {
        auto scheduleWarmForAnimEffect = [this, shouldScheduleBake,
                                          registryPtr = QPointer<PhosphorAnimationShaders::AnimationShaderRegistry>(
                                              m_animationShaderRegistry.get())](
                                             const PhosphorAnimationShaders::AnimationShaderEffect& info) {
            if (!info.isValid() || info.fragmentShaderPath.isEmpty() || !QFile::exists(info.fragmentShaderPath)) {
                return;
            }
            // Compositor-only packs (desktop / geometry / move classes, no
            // appearance) are authored against the kwin classic-GL dialect
            // with no daemon branch — their source does not compile on the
            // strict SPIR-V qsb target, and the daemon never runs them, so
            // warming them would only log a bake failure per pack per scan.
            if (PhosphorAnimationShaders::shaderEffectIsCompositorOnly(info)) {
                return;
            }
            PhosphorAnimationShaders::AnimationShaderRegistry* reg = registryPtr.data();
            if (!reg) {
                return;
            }
            QString vertPath = info.vertexShaderPath;
            QStringList includePaths;
            for (const QString& sp : reg->searchPaths()) {
                const QString sharedDir = sp + QStringLiteral("/shared");
                if (QDir(sharedDir).exists()) {
                    includePaths.append(sharedDir);
                    if (vertPath.isEmpty()) {
                        const QString sharedVert = sharedDir + QStringLiteral("/animation.vert");
                        if (QFile::exists(sharedVert)) {
                            vertPath = sharedVert;
                        }
                    }
                }
            }
            if (vertPath.isEmpty() || !QFile::exists(vertPath)) {
                return;
            }
            const QString effectId = info.id;
            // T1.1: warm-bake with the SAME generated preamble SurfaceAnimator
            // splices at runtime, so the warm entry's cache key (fingerprinted
            // on the preamble) matches the live load — otherwise the live bake
            // would miss this entry, or worse, a key collision would serve the
            // wrong SPIR-V. Computed on the GUI thread (reads `info`) and
            // captured by value into the bake-thread lambda.
            const QString paramPreamble = PhosphorAnimationShaders::AnimationShaderRegistry::paramPreamble(info);
            if (!shouldScheduleBake(QStringLiteral("anim:") + effectId,
                                    vertPath + QLatin1Char('\n') + info.fragmentShaderPath + QLatin1Char('\n')
                                        + paramPreamble)) {
                return;
            }
            // T1.5: warm-bake with the same entry-point scaffold SurfaceAnimator
            // installs at runtime, so an entry-only animation pack's warm entry
            // keys identically to its live load. Inert for every traditional
            // pack (which assembles to itself).
            const QString entryPrologue = PhosphorAnimationShaders::AnimationShaderRegistry::animationEntryPrologue();
            const QList<PhosphorShaders::EntryCandidate> entryCandidates =
                PhosphorAnimationShaders::AnimationShaderRegistry::animationEntryCandidates();
            auto* watcher = new QFutureWatcher<PhosphorRendering::WarmShaderBakeResult>(this);
            connect(watcher, &QFutureWatcher<PhosphorRendering::WarmShaderBakeResult>::finished, this,
                    [watcher, effectId]() {
                        const PhosphorRendering::WarmShaderBakeResult r = watcher->result();
                        if (!r.success) {
                            qCWarning(lcDaemon) << "Animation shader bake: failed for" << effectId << r.errorMessage;
                        }
                        watcher->deleteLater();
                    });
            watcher->setFuture(QtConcurrent::run(&m_shaderBakePool,
                                                 [vertPath, fragPath = info.fragmentShaderPath, includePaths,
                                                  paramPreamble, entryPrologue, entryCandidates]() {
                                                     return PhosphorRendering::warmShaderBakeCacheForPaths(
                                                         vertPath, fragPath, includePaths, paramPreamble, entryPrologue,
                                                         entryCandidates);
                                                 }));
        };
        connect(m_animationShaderRegistry.get(), &PhosphorAnimationShaders::AnimationShaderRegistry::effectsChanged,
                this, [this, scheduleWarmForAnimEffect]() {
                    if (!m_animationShaderRegistry) {
                        return;
                    }
                    const QList<PhosphorAnimationShaders::AnimationShaderEffect> effects =
                        m_animationShaderRegistry->availableEffects();
                    for (const PhosphorAnimationShaders::AnimationShaderEffect& info : effects) {
                        scheduleWarmForAnimEffect(info);
                    }
                });
        for (const PhosphorAnimationShaders::AnimationShaderEffect& info :
             m_animationShaderRegistry->availableEffects()) {
            scheduleWarmForAnimEffect(info);
        }
    }

    // Warm-bake SURFACE shaders (window border / rounded corners / glow) the
    // same way zone + animation shaders are warmed above, so the first surface
    // paint never blocks the render thread on a cold glslang compile. Shares the
    // single-threaded m_shaderBakePool, so all three categories serialise.
    //
    // Like the zone/animation warm-bakes, the surface bake installs the same
    // fragment entry-point scaffold the live loader uses (the
    // surfaceEntryPrologue()/surfaceEntryCandidates() passed below), so a
    // pSurface()-only pack compiles and the warm key matches SurfaceShaderItem's
    // scaffolded live load. The one thing that differs is vertex-shader
    // resolution: surface packs ship NO vertex shader, so the vert is resolved
    // from a shared `surface.vert` in the include paths (same resolution as
    // SurfaceShaderItem). A pack with no resolvable vert is skipped — the live
    // path would error on it too, so there is nothing to warm.
    if (m_surfaceShaderRegistry) {
        auto scheduleWarmForSurfaceEffect =
            [this, shouldScheduleBake,
             registryPtr = QPointer<PhosphorSurfaceShaders::SurfaceShaderRegistry>(m_surfaceShaderRegistry.get())](
                const PhosphorSurfaceShaders::SurfaceShaderEffect& info) {
                if (!info.isValid() || info.fragmentShaderPath.isEmpty() || !QFile::exists(info.fragmentShaderPath)) {
                    return;
                }
                // Pure liveness gate: the include paths come from the static
                // helper below (not the registry), so the QPointer is checked
                // only to skip bakes scheduled across registry teardown.
                if (!registryPtr) {
                    return;
                }
                // Include paths come from the SAME function the live loader's
                // constructor uses (SurfaceShaderItem::surfaceIncludePaths — XDG
                // dirs user-first, each contributing its `shared` subdir then
                // itself), so the bake-cache key the warm compile writes is
                // structurally guaranteed to be the one the first live paint
                // looks up; the two can no longer silently diverge. The vert
                // resolves as the live loader does: the pack's metadata vert
                // first, else `surface.vert` beside the frag, else the first
                // `surface.vert` in the include dirs (the `shared` subdir
                // carries the shipped one — packs themselves ship no vert, so
                // both sides land on the shared vert today). The overlay host
                // satisfies the matching side of this contract: applyDecoration
                // writes each chain stage's vertexSource from the pack's declared
                // vertexShaderPath (and the registry preamble), so a pack that
                // ships its own vert keys the same vert on both the warm bake
                // and the live load.
                const QStringList includePaths = SurfaceShaderItem::surfaceIncludePaths();
                QString vertPath = info.vertexShaderPath;
                if (vertPath.isEmpty()) {
                    const QString besideFrag =
                        QFileInfo(info.fragmentShaderPath).absolutePath() + QStringLiteral("/surface.vert");
                    if (QFile::exists(besideFrag)) {
                        vertPath = besideFrag;
                    }
                }
                if (vertPath.isEmpty()) {
                    for (const QString& incDir : std::as_const(includePaths)) {
                        const QString candidate = incDir + QStringLiteral("/surface.vert");
                        if (QFile::exists(candidate)) {
                            vertPath = candidate;
                            break;
                        }
                    }
                }
                if (vertPath.isEmpty() || !QFile::exists(vertPath)) {
                    return;
                }
                const QString effectId = info.id;
                const QString paramPreamble = PhosphorSurfaceShaders::SurfaceShaderRegistry::paramPreamble(info);
                if (!shouldScheduleBake(QStringLiteral("surface:") + effectId,
                                        vertPath + QLatin1Char('\n') + info.fragmentShaderPath + QLatin1Char('\n')
                                            + paramPreamble)) {
                    return;
                }
                // Bake WITH the same fragment entry-point scaffold the live loader
                // installs (surfaceshaderitem.cpp setEntryScaffold), or a
                // `pSurface()`-only pack bakes its raw main()-less source and fails
                // to compile, and even a traditional main() pack would key the
                // cache differently from the scaffolded live load and miss it.
                // Mirrors the zone and animation warm-bakes above.
                const QString entryPrologue = PhosphorSurfaceShaders::SurfaceShaderRegistry::surfaceEntryPrologue();
                const QList<PhosphorShaders::EntryCandidate> entryCandidates =
                    PhosphorSurfaceShaders::SurfaceShaderRegistry::surfaceEntryCandidates();
                auto* watcher = new QFutureWatcher<PhosphorRendering::WarmShaderBakeResult>(this);
                connect(watcher, &QFutureWatcher<PhosphorRendering::WarmShaderBakeResult>::finished, this,
                        [watcher, effectId]() {
                            const PhosphorRendering::WarmShaderBakeResult r = watcher->result();
                            if (!r.success) {
                                qCWarning(lcDaemon) << "Surface shader bake: failed for" << effectId << r.errorMessage;
                            }
                            watcher->deleteLater();
                        });
                watcher->setFuture(QtConcurrent::run(&m_shaderBakePool,
                                                     [vertPath, fragPath = info.fragmentShaderPath, includePaths,
                                                      paramPreamble, entryPrologue, entryCandidates]() {
                                                         return PhosphorRendering::warmShaderBakeCacheForPaths(
                                                             vertPath, fragPath, includePaths, paramPreamble,
                                                             entryPrologue, entryCandidates);
                                                     }));
            };
        connect(m_surfaceShaderRegistry.get(), &PhosphorSurfaceShaders::SurfaceShaderRegistry::effectsChanged, this,
                [this, scheduleWarmForSurfaceEffect]() {
                    if (!m_surfaceShaderRegistry) {
                        return;
                    }
                    const QList<PhosphorSurfaceShaders::SurfaceShaderEffect> effects =
                        m_surfaceShaderRegistry->availableEffects();
                    for (const PhosphorSurfaceShaders::SurfaceShaderEffect& info : effects) {
                        scheduleWarmForSurfaceEffect(info);
                    }
                });
        for (const PhosphorSurfaceShaders::SurfaceShaderEffect& info : m_surfaceShaderRegistry->availableEffects()) {
            scheduleWarmForSurfaceEffect(info);
        }
    }
}

} // namespace PlasmaZones
