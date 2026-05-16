// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snappingshaderspagecontroller.h"

#include "../core/logging.h"
#include "../core/shaderregistry.h"

#include <PhosphorShaders/ShaderRegistry.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include <PhosphorZones/Layout.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include <algorithm>

namespace PlasmaZones {

namespace snapping_shaders_controller_detail {

/// Recursive directory copy with symlink protection. Mirror of the helper
/// in `animationspagecontroller.cpp` — same SOLID/security rationale
/// (drag-drop sources are untrusted; symlinks would let a malicious pack
/// smuggle arbitrary readable filesystem content under deceptive names).
/// Kept private to this TU rather than promoted to a shared helper
/// because each controller's call site has its own pre/post-flight
/// validation; the bare copy primitive is too small to justify a header
/// dependency just for DRY.
static bool copyDirRecursive(const QString& sourcePath, const QString& destPath)
{
    QDir sourceDir(sourcePath);
    if (!sourceDir.exists())
        return false;
    if (!QDir().mkpath(destPath))
        return false;

    const QFileInfoList entries =
        sourceDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        if (entry.isSymLink())
            continue;

        const QString destEntryPath = destPath + QLatin1Char('/') + entry.fileName();
        if (entry.isDir()) {
            if (!copyDirRecursive(entry.absoluteFilePath(), destEntryPath))
                return false;
        } else if (entry.isFile()) {
            if (QFile::exists(destEntryPath))
                QFile::remove(destEntryPath);
            if (!QFile::copy(entry.absoluteFilePath(), destEntryPath))
                return false;
        }
    }
    return true;
}

} // namespace snapping_shaders_controller_detail

SnappingShadersPageController::SnappingShadersPageController(PlasmaZones::ShaderRegistry* shaderRegistry,
                                                             PhosphorZones::IZoneLayoutRegistry* layoutRegistry,
                                                             QObject* parent)
    : QObject(parent)
    , m_shaderRegistry(shaderRegistry)
    , m_layoutRegistry(layoutRegistry)
{
    if (m_shaderRegistry) {
        connect(m_shaderRegistry, &PhosphorShaders::ShaderRegistry::shadersChanged, this,
                &SnappingShadersPageController::shaderEffectsChanged);
    }
    if (m_layoutRegistry) {
        // The registry's `contentsChanged` fires on add/remove/import as
        // well as in-place layout edits. We blanket-emit a path-agnostic
        // `shaderProfileChanged` so the QML browser refreshes its
        // "Used in:" chips, then re-attach the per-layout signal hooks
        // (the layout list may have grown / shrunk — wiring the new ones
        // covers future shaderId edits).
        connect(m_layoutRegistry, &PhosphorLayout::ILayoutSourceRegistry::contentsChanged, this, [this]() {
            connectLayoutSignals();
            Q_EMIT shaderProfileChanged(QString());
        });
        connectLayoutSignals();
    }
}

SnappingShadersPageController::~SnappingShadersPageController() = default;

void SnappingShadersPageController::connectLayoutSignals()
{
    if (!m_layoutRegistry)
        return;
    // Per-layout `shaderIdChanged` connections. A second call to this
    // function (from contentsChanged) re-tries every existing layout; we
    // route through a member slot so `Qt::UniqueConnection` actually
    // dedupes — Qt cannot dedupe functor / lambda connections, only
    // pointer-to-member-function ones, and a lambda would silently
    // accumulate one fresh edge per refire.
    const QVector<PhosphorZones::Layout*> layouts = m_layoutRegistry->layouts();
    for (PhosphorZones::Layout* layout : layouts) {
        if (!layout)
            continue;
        connect(layout, &PhosphorZones::Layout::shaderIdChanged, this,
                &SnappingShadersPageController::onLayoutShaderIdChanged, Qt::UniqueConnection);
    }
}

void SnappingShadersPageController::onLayoutShaderIdChanged()
{
    auto* layout = qobject_cast<PhosphorZones::Layout*>(sender());
    if (!layout)
        return;
    Q_EMIT shaderProfileChanged(layout->id().toString());
}

QString SnappingShadersPageController::userShaderDirectoryPath() const
{
    if (!m_shaderRegistry)
        return {};
    return m_shaderRegistry->userShaderDirectory();
}

QVariantList SnappingShadersPageController::availableShaderEffects() const
{
    if (!m_shaderRegistry)
        return {};
    // Registry returns its native shape with `isUserShader`; rename to
    // `isUserEffect` so the pack-agnostic ShaderBrowserPage / Card /
    // Dialog can read both registries through the same key. The rest of
    // the keys (id, name, description, author, version, category,
    // previewPath, parameters) already match.
    QVariantList effects = m_shaderRegistry->availableShadersVariant();
    for (QVariant& v : effects) {
        QVariantMap m = v.toMap();
        if (m.contains(QLatin1String("isUserShader"))) {
            m.insert(QLatin1String("isUserEffect"), m.value(QLatin1String("isUserShader")));
            m.remove(QLatin1String("isUserShader"));
        }
        v = m;
    }
    return effects;
}

void SnappingShadersPageController::openUserShaderDirectory()
{
    if (!m_shaderRegistry)
        return;
    // Forward to the registry's create-and-open primitive — keeps the
    // mkpath / openUrl pair in one place so `installShaderPack` and the
    // "Open Folder" button can never drift apart on what counts as the
    // user shader directory.
    m_shaderRegistry->openUserShaderDirectory();
}

bool SnappingShadersPageController::installShaderPack(const QString& sourceUrl)
{
    if (sourceUrl.isEmpty())
        return false;

    // Accept both `file://...` URLs (drag-drop from a file manager) and
    // bare paths (programmatic callers).
    QString sourcePath = sourceUrl;
    if (sourcePath.startsWith(QLatin1String("file://")))
        sourcePath = QUrl(sourceUrl).toLocalFile();

    sourcePath = QDir::cleanPath(sourcePath);

    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isDir() || sourceInfo.isSymLink()) {
        qCWarning(lcConfig) << "installShaderPack (overlay): source is not an existing directory:" << sourcePath;
        return false;
    }
    const QString sourceBasename = sourceInfo.fileName();
    if (sourceBasename.isEmpty()) {
        qCWarning(lcConfig) << "installShaderPack (overlay): source path has no basename:" << sourcePath;
        return false;
    }

    // Validate metadata.json — without it the registry won't pick up the
    // pack, so accepting the drop would silently be a no-op. Reject
    // symlinked metadata so a malicious pack can't smuggle a non-shader
    // JSON file's content past validation.
    const QString metadataPath = sourceInfo.absoluteFilePath() + QLatin1String("/metadata.json");
    const QFileInfo metadataInfo(metadataPath);
    if (!metadataInfo.exists() || !metadataInfo.isFile() || metadataInfo.isSymLink()) {
        qCWarning(lcConfig) << "installShaderPack (overlay): source has no metadata.json:" << sourcePath;
        return false;
    }

    const QString userDir = userShaderDirectoryPath();
    if (userDir.isEmpty()) {
        qCWarning(lcConfig) << "installShaderPack (overlay): no user shader directory available (registry missing).";
        return false;
    }
    if (!QDir().mkpath(userDir)) {
        qCWarning(lcConfig) << "installShaderPack (overlay): could not create user shader directory:" << userDir;
        return false;
    }

    const QString destDir = userDir + QLatin1Char('/') + sourceBasename;
    if (QFileInfo::exists(destDir)) {
        qCWarning(lcConfig) << "installShaderPack (overlay): destination already exists, refusing to overwrite:"
                            << destDir;
        return false;
    }

    if (!snapping_shaders_controller_detail::copyDirRecursive(sourceInfo.absoluteFilePath(), destDir)) {
        qCWarning(lcConfig) << "installShaderPack (overlay): copy failed; rolling back:" << destDir;
        QDir(destDir).removeRecursively();
        return false;
    }

    // The registry's filewatcher rescans on its own — `shadersChanged`
    // fires automatically and reaches QML through this controller's
    // forwarded `shaderEffectsChanged` signal.
    return true;
}

QVariantList SnappingShadersPageController::shaderEffectUsages(const QString& effectId) const
{
    if (!m_layoutRegistry || effectId.isEmpty())
        return {};
    QVariantList out;
    const QVector<PhosphorZones::Layout*> layouts = m_layoutRegistry->layouts();
    for (PhosphorZones::Layout* layout : layouts) {
        if (!layout || layout->shaderId() != effectId)
            continue;
        QVariantMap entry;
        // `path` is the layout's UUID-with-braces (matches the rest of
        // the codebase's QUuid::toString convention); `label` is the
        // user-facing name. The browser renders `label` and falls back
        // to `path` when `label` is empty.
        entry.insert(QLatin1String("path"), layout->id().toString());
        entry.insert(QLatin1String("label"), layout->name());
        out.append(entry);
    }
    std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value(QLatin1String("label")).toString() < b.toMap().value(QLatin1String("label")).toString();
    });
    return out;
}

} // namespace PlasmaZones
