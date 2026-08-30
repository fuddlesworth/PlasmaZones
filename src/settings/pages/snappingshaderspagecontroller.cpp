// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snappingshaderspagecontroller.h"

#include "core/interfaces/isettings.h"
#include "core/interfaces/shaderregistry.h"
#include "core/platform/logging.h"
#include "core/types/overlayshadertree.h"
#include "phosphor_i18n.h"
#include "settings/services/shaderpackinstaller.h"
#include "shaderpreview/shaderpreviewcontroller.h"

#include <PhosphorShaders/ShaderRegistry.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include <PhosphorZones/Layout.h>

#include <algorithm>

namespace PlasmaZones {

namespace {

QVariantMap profileToMap(const OverlayShaderProfile& profile)
{
    QVariantMap map;
    map.insert(QLatin1String("shaderId"), profile.shaderId);
    map.insert(QLatin1String("parameters"), profile.parameters);
    return map;
}

} // namespace

SnappingShadersPageController::SnappingShadersPageController(PlasmaZones::ShaderRegistry* shaderRegistry,
                                                             PhosphorZones::IZoneLayoutRegistry* layoutRegistry,
                                                             ISettings* settings,
                                                             ShaderPreviewController* previewController,
                                                             QObject* parent)
    : PhosphorControl::PageController(QStringLiteral("snapping-shaders"), parent)
    , m_shaderRegistry(shaderRegistry)
    , m_layoutRegistry(layoutRegistry)
    , m_settings(settings)
    , m_previewController(previewController)
{
    if (m_shaderRegistry) {
        connect(m_shaderRegistry, &PhosphorShaders::ShaderRegistry::shadersChanged, this,
                &SnappingShadersPageController::shaderEffectsChanged);
    }
    if (m_settings) {
        // The assignment store is the config tree; every mutation (local
        // setter, D-Bus write, global reload) funnels through this one
        // NOTIFY. Always a full-refresh emit — the tree write does not
        // say which node moved, and the cards are cheap to re-read.
        connect(m_settings, &ISettings::overlayShaderTreeChanged, this, [this]() {
            Q_EMIT shaderProfileChanged(QString());
        });
    }
    if (m_layoutRegistry) {
        // Layout add / remove / rename changes the assignment card list
        // and the labels the usage chips render.
        connect(m_layoutRegistry, &PhosphorLayout::ILayoutSourceRegistry::contentsChanged, this, [this]() {
            Q_EMIT shaderProfileChanged(QString());
        });
    }
}

SnappingShadersPageController::~SnappingShadersPageController() = default;

QObject* SnappingShadersPageController::previewController() const
{
    return m_previewController;
}

QString SnappingShadersPageController::layoutNameFor(const QString& layoutId) const
{
    if (!m_layoutRegistry)
        return {};
    const QVector<PhosphorZones::Layout*> layouts = m_layoutRegistry->layouts();
    for (PhosphorZones::Layout* layout : layouts) {
        if (layout && layout->id().toString() == layoutId)
            return layout->name();
    }
    return {};
}

QVariantList SnappingShadersPageController::assignableLayouts() const
{
    if (!m_layoutRegistry)
        return {};
    QVariantList out;
    const QVector<PhosphorZones::Layout*> layouts = m_layoutRegistry->layouts();
    for (PhosphorZones::Layout* layout : layouts) {
        if (!layout)
            continue;
        QVariantMap entry;
        entry.insert(QLatin1String("id"), layout->id().toString());
        entry.insert(QLatin1String("name"), layout->name());
        out.append(entry);
    }
    std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value(QLatin1String("name")).toString() < b.toMap().value(QLatin1String("name")).toString();
    });
    return out;
}

bool SnappingShadersPageController::hasOverride(const QString& path) const
{
    if (!m_settings || path.isEmpty())
        return false;
    return m_settings->overlayShaderTree().hasOverride(path);
}

QVariantMap SnappingShadersPageController::rawShaderProfile(const QString& path) const
{
    if (!m_settings)
        return profileToMap({});
    const OverlayShaderTree tree = m_settings->overlayShaderTree();
    return profileToMap(path.isEmpty() ? tree.baseline() : tree.directOverride(path));
}

QVariantMap SnappingShadersPageController::resolvedShaderProfile(const QString& path) const
{
    if (!m_settings)
        return profileToMap({});
    const OverlayShaderTree tree = m_settings->overlayShaderTree();
    return profileToMap(path.isEmpty() ? tree.baseline() : tree.resolve(path));
}

void SnappingShadersPageController::setShaderOverride(const QString& path, const QString& effectId,
                                                      const QVariantMap& params)
{
    if (!m_settings)
        return;
    OverlayShaderTree tree = m_settings->overlayShaderTree();
    const OverlayShaderProfile node{effectId, params};
    if (path.isEmpty())
        tree.setBaseline(node);
    else
        tree.setOverride(path, node);
    m_settings->setOverlayShaderTree(tree);
}

void SnappingShadersPageController::setShaderParameters(const QString& path, const QVariantMap& params)
{
    if (!m_settings)
        return;
    OverlayShaderTree tree = m_settings->overlayShaderTree();
    if (path.isEmpty()) {
        OverlayShaderProfile node = tree.baseline();
        node.parameters = params;
        tree.setBaseline(node);
    } else {
        if (!tree.hasOverride(path))
            return;
        OverlayShaderProfile node = tree.directOverride(path);
        node.parameters = params;
        tree.setOverride(path, node);
    }
    m_settings->setOverlayShaderTree(tree);
}

bool SnappingShadersPageController::clearOverride(const QString& path)
{
    if (!m_settings || path.isEmpty())
        return false;
    OverlayShaderTree tree = m_settings->overlayShaderTree();
    if (!tree.clearOverride(path))
        return false;
    m_settings->setOverlayShaderTree(tree);
    return true;
}

QVariantList SnappingShadersPageController::shaderParameters(const QString& effectId) const
{
    if (!m_shaderRegistry || effectId.isEmpty())
        return {};
    // The registry's flattened rows already carry each pack's
    // ParameterInfo maps; pull the matching row's list rather than
    // duplicating the effect→variant mapping here.
    const QVariantList effects = m_shaderRegistry->availableShadersVariant();
    for (const QVariant& v : effects) {
        const QVariantMap m = v.toMap();
        if (m.value(QLatin1String("id")).toString() == effectId)
            return m.value(QLatin1String("parameters")).toList();
    }
    return {};
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
        const QString message = ShaderPackInstaller::errorMessage(result);
        qCWarning(lcConfig) << "installShaderPack (overlay):" << message << "— source:" << sourceUrl;
        // Surface the reason via the chrome toast — the InlineMessage
        // in the drop zone is generic; the underlying failure reason
        // (DestinationExists, MissingMetadata, PackTooLarge…) gives
        // the user a concrete next step.
        Q_EMIT toastRequested(message);
        return false;
    }
    // The registry's filewatcher rescans on its own — `shadersChanged`
    // fires automatically and reaches QML through this controller's
    // forwarded `shaderEffectsChanged` signal.
    return true;
}

QVariantList SnappingShadersPageController::shaderEffectUsages(const QString& effectId) const
{
    if (!m_settings || effectId.isEmpty())
        return {};
    const OverlayShaderTree tree = m_settings->overlayShaderTree();
    QVariantList out;
    if (tree.baseline().shaderId == effectId) {
        QVariantMap entry;
        entry.insert(QLatin1String("path"), QString());
        entry.insert(QLatin1String("label"), PhosphorI18n::tr("Global default"));
        out.append(entry);
    }
    QVariantList layoutRows;
    const QStringList overridden = tree.overriddenLayouts();
    for (const QString& layoutId : overridden) {
        if (tree.directOverride(layoutId).shaderId != effectId)
            continue;
        QVariantMap entry;
        // `path` is the layout's UUID-with-braces (matches the rest of
        // the codebase's QUuid::toString convention); `label` is the
        // user-facing name. The browser renders `label` and falls back
        // to `path` when `label` is empty (e.g. a stale entry for a
        // deleted layout).
        entry.insert(QLatin1String("path"), layoutId);
        entry.insert(QLatin1String("label"), layoutNameFor(layoutId));
        layoutRows.append(entry);
    }
    std::sort(layoutRows.begin(), layoutRows.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value(QLatin1String("label")).toString() < b.toMap().value(QLatin1String("label")).toString();
    });
    out.append(layoutRows);
    return out;
}

} // namespace PlasmaZones
