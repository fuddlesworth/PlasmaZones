// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimationShaders/AnimationShaderEffect.h>
#include <PhosphorAnimationShaders/phosphoranimationshaders_export.h>

#include <QFileSystemWatcher>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

namespace PhosphorAnimationShaders {

/**
 * @brief Registry of available animation shader transition effects.
 *
 * Discovers shader packs from configured search paths. Each pack is a
 * subdirectory containing a `metadata.json` describing the effect plus
 * the shader source files it references.
 *
 * ## Directory layout
 *
 * ```
 * <search-path>/
 *   dissolve/
 *     metadata.json    ← { "id": "dissolve", "fragmentShader": "effect.frag", ... }
 *     effect.frag
 *     effect_kwin.frag
 *   slide/
 *     metadata.json
 *     effect.frag
 * ```
 *
 * The subdirectory name is decorative — the `id` field in metadata.json
 * is the registry key. This matches `PhosphorShell::ShaderRegistry`'s
 * convention for zone shaders.
 *
 * ## Search path ordering
 *
 * Paths added later override earlier on id collision (user-wins-over-
 * system). Pass system paths first, user paths last.
 *
 * ## Live reload
 *
 * `addSearchPath` installs a `QFileSystemWatcher` with 500 ms debounce.
 * User drops a new pack → registry picks it up on the next debounce
 * without a daemon restart.
 *
 * ## Thread safety
 *
 * GUI-thread only. Same contract as PhosphorShell::ShaderRegistry.
 */
class PHOSPHORANIMATIONSHADERS_EXPORT AnimationShaderRegistry : public QObject
{
    Q_OBJECT

public:
    explicit AnimationShaderRegistry(QObject* parent = nullptr);
    ~AnimationShaderRegistry() override;

    // ── Search paths ──────────────────────────────────────────────────

    void addSearchPath(const QString& path);
    void removeSearchPath(const QString& path);
    QStringList searchPaths() const;

    // ── Lookup ────────────────────────────────────────────────────────

    QList<AnimationShaderEffect> availableEffects() const;
    AnimationShaderEffect effect(const QString& id) const;
    bool hasEffect(const QString& id) const;
    QStringList effectIds() const;

    // ── Lifecycle ─────────────────────────────────────────────────────

    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void effectsChanged();

private Q_SLOTS:
    void onDirChanged(const QString& path);
    void performDebouncedRefresh();

private:
    void setupFileWatcher();
    void scheduleRefresh();

    QStringList m_searchPaths;
    QHash<QString, AnimationShaderEffect> m_effects;
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer* m_refreshTimer = nullptr;

    static constexpr int RefreshDebounceMs = 500;
};

} // namespace PhosphorAnimationShaders
