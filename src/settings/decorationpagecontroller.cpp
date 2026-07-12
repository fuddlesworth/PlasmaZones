// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "decorationpagecontroller.h"

#include "decoration_controller_detail.h"

#include "../core/isettings.h"

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>
#include <PhosphorSurface/DecorationSupportedPaths.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include <QLatin1String>

#include <algorithm>

namespace PlasmaZones {

using namespace decoration_controller_detail;
using PhosphorSurfaceShaders::DecorationProfile;
using PhosphorSurfaceShaders::DecorationProfileTree;

namespace {

/// Read the current tree from Settings (empty tree when no settings).
DecorationProfileTree readTree(ISettings* settings)
{
    return settings ? settings->decorationProfileTree() : DecorationProfileTree{};
}

/// Overridden paths strictly BELOW @p path (its descendants), e.g. for
/// "window" → every overridden "window.*". Excludes @p path itself. These are
/// the surfaces that shadow the parent node.
QStringList overrideDescendantsOf(const DecorationProfileTree& tree, const QString& path)
{
    QStringList out;
    if (path.isEmpty())
        return out;
    const QString prefix = path + QLatin1Char('.');
    const QStringList overridden = tree.overriddenPaths();
    for (const QString& p : overridden) {
        if (p.startsWith(prefix))
            out.append(p);
    }
    return out;
}

/// Read the DIRECT profile at @p path: the baseline for the empty path,
/// otherwise the per-surface override (default-constructed = all-inherit
/// when there is no override).
DecorationProfile directProfileAt(const DecorationProfileTree& tree, const QString& path)
{
    return path.isEmpty() ? tree.baseline() : tree.directOverride(path);
}

/// Write @p profile back as the DIRECT profile at @p path (baseline for the
/// empty path), then persist the whole tree through Settings.
void writeDirectProfile(ISettings* settings, DecorationProfileTree& tree, const QString& path,
                        const DecorationProfile& profile)
{
    if (!settings)
        return;
    if (path.isEmpty())
        tree.setBaseline(profile);
    else
        tree.setOverride(path, profile);
    // Settings::setDecorationProfileTree short-circuits on an unchanged tree
    // (and only then emits NOTIFY), so a redundant write does not trip the
    // dirty loop. profilesChanged() fires from the decorationProfileTreeChanged
    // connection wired in the ctor, covering both our own writes and reloads.
    settings->setDecorationProfileTree(tree);
}

} // namespace

DecorationPageController::DecorationPageController(PhosphorSurfaceShaders::SurfaceShaderRegistry* registry,
                                                   ISettings* settings, QObject* parent)
    // "decoration-staging", not "decorations": the sidebar nav node owns the
    // bare id (regVirtual in settingscontroller_pageregistration.cpp), and the
    // staging controller stays independently addressable — same split as
    // AnimationsPageController's "animations-staging".
    : PhosphorControl::PageController(QStringLiteral("decoration-staging"), parent)
    , m_registry(registry)
    , m_settings(settings)
{
    if (m_registry) {
        connect(m_registry, &PhosphorSurfaceShaders::SurfaceShaderRegistry::effectsChanged, this,
                &DecorationPageController::shaderEffectsChanged);
    }
    if (m_settings) {
        // Re-fire profilesChanged so every visible card rebinds after a
        // global reload (Discard / Settings::load()) AND after our own
        // mutators write the tree back.
        connect(m_settings, &ISettings::decorationProfileTreeChanged, this, &DecorationPageController::profilesChanged);
    }
    // Must come after the profilesChanged wiring above: the store connects to
    // that signal to refresh its `active` flags.
    initSetsStore();
}

DecorationPageController::~DecorationPageController() = default;

// ── Available packs ───────────────────────────────────────────────────────

QVariantList DecorationPageController::availableShaderEffects() const
{
    QVariantList result;
    if (!m_registry)
        return result;
    const auto effects = m_registry->availableEffects();
    result.reserve(effects.size());
    for (const auto& effect : effects)
        result.append(effectToMap(effect));
    return result;
}

// ── Profile readers ─────────────────────────────────────────────────────────

QVariantMap DecorationPageController::resolvedProfile(const QString& path) const
{
    const DecorationProfileTree tree = readTree(m_settings);
    // resolve("") returns the baseline (the walk terminates at "" with the
    // baseline as the accumulator), so the empty-path case is handled
    // directly by the tree's own resolve.
    return profileToResolvedMap(tree.resolve(path));
}

QVariantMap DecorationPageController::rawProfile(const QString& path) const
{
    const DecorationProfileTree tree = readTree(m_settings);
    return profileToSparseMap(directProfileAt(tree, path));
}

bool DecorationPageController::hasOverride(const QString& path) const
{
    if (path.isEmpty())
        return false;
    if (!PhosphorSurfaceShaders::decorationSurfaceSupported(path))
        return false;
    return readTree(m_settings).hasOverride(path);
}

// ── Chain mutators ───────────────────────────────────────────────────────────

QStringList DecorationPageController::chainAt(const QString& path) const
{
    return readTree(m_settings).resolve(path).effectiveChain();
}

void DecorationPageController::setChain(const QString& path, const QStringList& chain)
{
    if (!m_settings)
        return;
    if (!path.isEmpty() && !PhosphorSurfaceShaders::decorationSurfaceSupported(path))
        return;
    DecorationProfileTree tree = readTree(m_settings);
    DecorationProfile profile = directProfileAt(tree, path);
    profile.chain = chain;
    // Drop per-pack parameter overrides for any pack no longer in the chain, so
    // removing a pack discards its settings — re-adding it later starts from the
    // pack's defaults rather than resurrecting the old overrides. Only the
    // DIRECT overrides at this path are pruned (the engaged optional); an
    // inherited (nullopt) parameters map is left untouched. Reorder keeps every
    // pack, so nothing is pruned then.
    if (profile.parameters) {
        QVariantMap params = *profile.parameters;
        for (auto it = params.begin(); it != params.end();) {
            if (!chain.contains(it.key()))
                it = params.erase(it);
            else
                ++it;
        }
        profile.parameters = params;
    }
    // Same pruning for the per-layer disable set: a removed pack's toggle
    // state dies with it, so re-adding starts enabled (the default).
    if (profile.disabledPacks) {
        QStringList disabled = *profile.disabledPacks;
        disabled.erase(std::remove_if(disabled.begin(), disabled.end(),
                                      [&chain](const QString& id) {
                                          return !chain.contains(id);
                                      }),
                       disabled.end());
        profile.disabledPacks = disabled;
    }
    writeDirectProfile(m_settings, tree, path, profile);
}

QStringList DecorationPageController::disabledPacksAt(const QString& path) const
{
    return readTree(m_settings).resolve(path).effectiveDisabledPacks();
}

void DecorationPageController::setChainLayerEnabled(const QString& path, const QString& packId, bool enabled)
{
    if (!m_settings || packId.isEmpty())
        return;
    if (!path.isEmpty() && !PhosphorSurfaceShaders::decorationSurfaceSupported(path))
        return;
    DecorationProfileTree tree = readTree(m_settings);
    DecorationProfile profile = directProfileAt(tree, path);
    // First direct edit at this path: seed from the RESOLVED value so the
    // toggle diverges from what the user was previewing instead of silently
    // re-enabling every inherited-off layer (mirrors _engageOverride's
    // chain/params seeding in DecorationSurfaceCard).
    QStringList disabled = profile.disabledPacks ? *profile.disabledPacks : tree.resolve(path).effectiveDisabledPacks();
    const bool currentlyDisabled = disabled.contains(packId);
    if (enabled == !currentlyDisabled) {
        return; // already in the requested state — avoid a no-op tree write
    }
    if (enabled)
        disabled.removeAll(packId);
    else
        disabled.append(packId);
    profile.disabledPacks = disabled;
    writeDirectProfile(m_settings, tree, path, profile);
}

void DecorationPageController::setChainParam(const QString& path, const QString& packId, const QString& paramId,
                                             const QVariant& value)
{
    if (!m_settings || packId.isEmpty() || paramId.isEmpty())
        return;
    if (!path.isEmpty() && !PhosphorSurfaceShaders::decorationSurfaceSupported(path))
        return;
    DecorationProfileTree tree = readTree(m_settings);
    DecorationProfile profile = directProfileAt(tree, path);
    // Copy-mutate the {packId -> {paramId -> value}} two-level map, engaging
    // the parameters optional if it was inherited.
    QVariantMap params = profile.parameters.value_or(QVariantMap());
    QVariantMap packParams = params.value(packId).toMap();
    packParams.insert(paramId, value);
    params.insert(packId, packParams);
    profile.parameters = params;
    writeDirectProfile(m_settings, tree, path, profile);
}

void DecorationPageController::setChainParams(const QString& path, const QString& packId, const QVariantMap& params)
{
    if (!m_settings || packId.isEmpty() || params.isEmpty())
        return;
    if (!path.isEmpty() && !PhosphorSurfaceShaders::decorationSurfaceSupported(path))
        return;
    DecorationProfileTree tree = readTree(m_settings);
    DecorationProfile profile = directProfileAt(tree, path);
    QVariantMap allParams = profile.parameters.value_or(QVariantMap());
    QVariantMap packParams = allParams.value(packId).toMap();
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
        packParams.insert(it.key(), it.value());
    allParams.insert(packId, packParams);
    profile.parameters = allParams;
    writeDirectProfile(m_settings, tree, path, profile);
}

// ── Whole-override mutator ────────────────────────────────────────────────────

bool DecorationPageController::clearOverride(const QString& path)
{
    // The baseline can't be "inherited away" — reject the empty path. QML
    // disables the reset affordance for the global card, but guard here too.
    if (!m_settings || path.isEmpty())
        return false;
    // Reject unsupported paths for parity with the chain mutators (setChain /
    // setChainParam / setChainLayerEnabled) — an unsupported path has no
    // override to clear anyway, so this only tightens the contract.
    if (!PhosphorSurfaceShaders::decorationSurfaceSupported(path))
        return false;
    DecorationProfileTree tree = readTree(m_settings);
    if (!tree.clearOverride(path))
        return false;
    m_settings->setDecorationProfileTree(tree);
    return true;
}

int DecorationPageController::overrideDescendantCount(const QString& path) const
{
    if (!m_settings)
        return 0;
    return overrideDescendantsOf(readTree(m_settings), path).size();
}

int DecorationPageController::clearOverrideDescendants(const QString& path)
{
    if (!m_settings)
        return 0;
    DecorationProfileTree tree = readTree(m_settings);
    const QStringList toClear = overrideDescendantsOf(tree, path);
    if (toClear.isEmpty())
        return 0;
    for (const QString& p : toClear)
        tree.clearOverride(p);
    m_settings->setDecorationProfileTree(tree);
    return toClear.size();
}

} // namespace PlasmaZones
