// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snappingshaderspagecontroller.h"

#include "../core/logging.h"
#include "../core/shaderregistry.h"
#include "../shaderpreview/shaderpreviewcontroller.h"
#include "shaderpackinstaller.h"

#include <PhosphorShaders/ShaderRegistry.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include <PhosphorZones/Layout.h>

#include <QDir>

#include <algorithm>

namespace PlasmaZones {

SnappingShadersPageController::SnappingShadersPageController(PlasmaZones::ShaderRegistry* shaderRegistry,
                                                             PhosphorZones::IZoneLayoutRegistry* layoutRegistry,
                                                             ShaderPreviewController* previewController,
                                                             QObject* parent)
    : PhosphorControl::PageController(QStringLiteral("snapping-shaders"), parent)
    , m_shaderRegistry(shaderRegistry)
    , m_layoutRegistry(layoutRegistry)
    , m_previewController(previewController)
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

QObject* SnappingShadersPageController::previewController() const
{
    return m_previewController;
}

void SnappingShadersPageController::connectLayoutSignals()
{
    if (!m_layoutRegistry)
        return;
    // Track which layouts are already wired so the O(N) walk on every
    // contentsChanged shrinks to O(new-layouts). Qt::UniqueConnection
    // still guarantees idempotence at the QObject layer, but the
    // membership check spares the per-call function-prologue cost and,
    // more importantly, scales sublinearly when contentsChanged fires
    // repeatedly during a bulk-edit (drag-reorder, import).
    //
    // Stale-entry eviction: `m_wiredLayouts` is drained by
    // `onWiredLayoutDestroyed` (wired below) which assumes "removed from
    // registry == destroyed". The registry's contract is that layouts
    // are QObject-parented to it, so removal => destruction in the same
    // tick. Belt-and-braces: also drop any tracked entry not present in
    // the current registry snapshot, so a future registry refactor that
    // detaches without destroying (e.g. cache-eviction) doesn't grow
    // `m_wiredLayouts` unbounded.
    const QVector<PhosphorZones::Layout*> layouts = m_layoutRegistry->layouts();
    // Build a live-pointer set keyed as QObject* so the membership
    // test can be applied to the tracked QObject* entries directly,
    // sidestepping a qobject_cast on what could theoretically be a
    // dangling pointer (see the destroyed-eviction note below).
    QSet<QObject*> live;
    live.reserve(layouts.size());
    for (PhosphorZones::Layout* layout : layouts) {
        if (layout)
            live.insert(layout);
    }
    // Stale-entry eviction. onWiredLayoutDestroyed() drains entries
    // synchronously from QObject::destroyed (wired below), so under
    // the current registry contract m_wiredLayouts never carries a
    // dangling pointer. The live-set guard here keeps disconnect()
    // calls bounded to objects we KNOW are still alive — defence in
    // depth against a future registry refactor that detaches without
    // destroying (cache eviction etc.).
    for (auto it = m_wiredLayouts.begin(); it != m_wiredLayouts.end();) {
        QObject* tracked = *it;
        if (live.contains(tracked)) {
            ++it;
            continue;
        }
        // Tracked entry no longer in the live registry snapshot. If
        // it's also been destroyed, onWiredLayoutDestroyed already
        // removed it earlier in this same event loop turn and this
        // branch is unreachable. If it's been *detached* (hypothetical
        // future case), tracked is still alive and we can safely
        // disconnect — no UB.
        disconnect(tracked, &QObject::destroyed, this, &SnappingShadersPageController::onWiredLayoutDestroyed);
        disconnect(tracked, nullptr, this, nullptr);
        it = m_wiredLayouts.erase(it);
    }
    for (PhosphorZones::Layout* layout : layouts) {
        if (!layout)
            continue;
        if (m_wiredLayouts.contains(layout))
            continue;
        connect(layout, &PhosphorZones::Layout::shaderIdChanged, this,
                &SnappingShadersPageController::onLayoutShaderIdChanged, Qt::UniqueConnection);
        // Track destruction so the set stays in sync — without this,
        // a deleted-then-reused address would skip the connect path
        // and the new layout's shaderIdChanged would never reach us.
        connect(layout, &QObject::destroyed, this, &SnappingShadersPageController::onWiredLayoutDestroyed,
                Qt::UniqueConnection);
        m_wiredLayouts.insert(layout);
    }
}

void SnappingShadersPageController::onWiredLayoutDestroyed(QObject* layout)
{
    m_wiredLayouts.remove(layout);
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
