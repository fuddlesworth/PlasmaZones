// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimationShaders/AnimationShaderRegistry.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLoggingCategory>

namespace PhosphorAnimationShaders {

namespace {
Q_LOGGING_CATEGORY(lcRegistry, "phosphoranimationshaders.registry")
}

AnimationShaderRegistry::AnimationShaderRegistry(QObject* parent)
    : QObject(parent)
{
}

AnimationShaderRegistry::~AnimationShaderRegistry() = default;

// ═══════════════════════════════════════════════════════════════════════════════
// Search paths
// ═══════════════════════════════════════════════════════════════════════════════

void AnimationShaderRegistry::addSearchPath(const QString& path)
{
    if (path.isEmpty() || m_searchPaths.contains(path))
        return;
    m_searchPaths.append(path);
    setupFileWatcher();
    scheduleRefresh();
}

void AnimationShaderRegistry::removeSearchPath(const QString& path)
{
    if (!m_searchPaths.removeAll(path))
        return;
    scheduleRefresh();
}

QStringList AnimationShaderRegistry::searchPaths() const
{
    return m_searchPaths;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Lookup
// ═══════════════════════════════════════════════════════════════════════════════

QList<AnimationShaderEffect> AnimationShaderRegistry::availableEffects() const
{
    return m_effects.values();
}

AnimationShaderEffect AnimationShaderRegistry::effect(const QString& id) const
{
    return m_effects.value(id);
}

bool AnimationShaderRegistry::hasEffect(const QString& id) const
{
    return m_effects.contains(id);
}

QStringList AnimationShaderRegistry::effectIds() const
{
    return m_effects.keys();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Discovery
// ═══════════════════════════════════════════════════════════════════════════════

void AnimationShaderRegistry::refresh()
{
    QHash<QString, AnimationShaderEffect> newEffects;

    for (const QString& searchPath : m_searchPaths) {
        const bool isUserEffect = m_searchPaths.indexOf(searchPath) == m_searchPaths.size() - 1;
        QDir dir(searchPath);
        if (!dir.exists())
            continue;

        const QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& subdir : subdirs) {
            const QString effectDir = dir.absoluteFilePath(subdir);
            const QString metadataPath = effectDir + QStringLiteral("/metadata.json");

            QFile file(metadataPath);
            if (!file.open(QIODevice::ReadOnly))
                continue;

            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
            if (parseError.error != QJsonParseError::NoError) {
                qCWarning(lcRegistry) << "Skipping" << metadataPath << ":" << parseError.errorString();
                continue;
            }
            if (!doc.isObject()) {
                qCWarning(lcRegistry) << "Skipping non-object root in" << metadataPath;
                continue;
            }

            AnimationShaderEffect e = AnimationShaderEffect::fromJson(doc.object());
            if (e.id.isEmpty()) {
                qCWarning(lcRegistry) << "Skipping" << metadataPath << ": missing 'id' field";
                continue;
            }

            e.sourceDir = effectDir;
            e.isUserEffect = isUserEffect;

            if (!e.fragmentShaderPath.isEmpty())
                e.fragmentShaderPath = effectDir + QLatin1Char('/') + e.fragmentShaderPath;
            if (!e.vertexShaderPath.isEmpty())
                e.vertexShaderPath = effectDir + QLatin1Char('/') + e.vertexShaderPath;
            if (!e.kwinFragmentShaderPath.isEmpty())
                e.kwinFragmentShaderPath = effectDir + QLatin1Char('/') + e.kwinFragmentShaderPath;
            if (!e.previewPath.isEmpty())
                e.previewPath = effectDir + QLatin1Char('/') + e.previewPath;

            newEffects.insert(e.id, std::move(e));
        }
    }

    if (newEffects != m_effects) {
        m_effects = std::move(newEffects);
        Q_EMIT effectsChanged();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// File watching
// ═══════════════════════════════════════════════════════════════════════════════

void AnimationShaderRegistry::setupFileWatcher()
{
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &AnimationShaderRegistry::onDirChanged);
    }

    for (const QString& path : m_searchPaths) {
        if (QDir(path).exists() && !m_watcher->directories().contains(path))
            m_watcher->addPath(path);
    }
}

void AnimationShaderRegistry::onDirChanged(const QString& /*path*/)
{
    scheduleRefresh();
}

void AnimationShaderRegistry::scheduleRefresh()
{
    if (!m_refreshTimer) {
        m_refreshTimer = new QTimer(this);
        m_refreshTimer->setSingleShot(true);
        m_refreshTimer->setInterval(RefreshDebounceMs);
        connect(m_refreshTimer, &QTimer::timeout, this, &AnimationShaderRegistry::performDebouncedRefresh);
    }
    m_refreshTimer->start();
}

void AnimationShaderRegistry::performDebouncedRefresh()
{
    refresh();
}

} // namespace PhosphorAnimationShaders
