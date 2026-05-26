// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snappingshaderspagecontroller.h"

#include "../core/logging.h"
#include "../core/shaderregistry.h"
#include "shaderpackinstaller.h"

#include <PhosphorShaders/ShaderRegistry.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include <PhosphorZones/Layout.h>

#include <QDir>

#include <algorithm>

namespace PlasmaZones {

SnappingShadersPageController::SnappingShadersPageController(PlasmaZones::ShaderRegistry* shaderRegistry,
                                                             PhosphorZones::IZoneLayoutRegistry* layoutRegistry,
                                                             QObject* parent)
    : PhosphorSettingsUi::PageController(QStringLiteral("snapping-shaders"), parent)
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
    // All validation + copy lives in the shared ShaderPackInstaller
    // helper. Same logic as the animations-shader page (DRY) and the
    // security-sensitive bits (symlink rejection, metadata.json
    // verification, rollback) only need an audit in one place.
    const auto result = ShaderPackInstaller::install(sourceUrl, userShaderDirectoryPath());
    if (result != ShaderPackInstaller::Result::Success) {
        qCWarning(lcConfig) << "installShaderPack (overlay):" << ShaderPackInstaller::errorMessage(result)
                            << "— source:" << sourceUrl;
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
