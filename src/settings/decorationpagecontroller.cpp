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

#include <QColor>
#include <QLatin1String>

#include <algorithm>

namespace PlasmaZones {

using namespace decoration_controller_detail;
using PhosphorSurfaceShaders::DecorationProfile;
using PhosphorSurfaceShaders::DecorationProfileTree;

namespace {

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

/// The resolved effective parameters at @p path, filtered to the packs in the
/// resolved effective chain. The base map for ENGAGING a direct parameters
/// override at an inheriting path: DecorationProfile::overlay replaces the
/// map wholesale, so engaging from empty would drop sibling packs' inherited
/// params, while engaging unfiltered could materialize stale params for a
/// pack an ancestor's chain edit already removed (they would silently
/// resurrect if that pack is ever re-added).
QVariantMap inheritedParamsForChain(const DecorationProfileTree& tree, const QString& path)
{
    const DecorationProfile resolved = tree.resolve(path);
    const QStringList chain = resolved.effectiveChain();
    const QVariantMap inherited = resolved.effectiveParameters();
    QVariantMap out;
    for (auto it = inherited.constBegin(); it != inherited.constEnd(); ++it) {
        if (chain.contains(it.key()))
            out.insert(it.key(), it.value());
    }
    return out;
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

const DecorationProfileTree& DecorationPageController::tree() const
{
    if (!m_treeCache.has_value()) {
        m_treeCache = m_settings ? m_settings->decorationProfileTree() : DecorationProfileTree{};
    }
    return *m_treeCache;
}

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
        // mutators write the tree back. Drop the parsed-tree cache on the same
        // signal: it is the only thing that can move the tree, whoever wrote it.
        connect(m_settings, &ISettings::decorationProfileTreeChanged, this, [this]() {
            m_treeCache.reset();
            Q_EMIT profilesChanged();
        });
    }
    // initSetsStore() wires profilesChanged into the store's
    // notifyLiveStateChanged, so an edit made anywhere on the Decoration pages
    // re-derives the `active` badge on every saved set.
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
    for (const auto& effect : effects) {
        // The reserved "border" and "opacity-tint" packs render the PLAIN
        // config/rule-owned layers; the kwin effect strips them from every
        // user chain (tree and rule alike), so offering them in the picker
        // would produce an entry that silently never renders. Their knobs
        // live on the Windows appearance page instead.
        if (effect.id == QLatin1String("border") || effect.id == QLatin1String("opacity-tint"))
            continue;
        result.append(effectToMap(effect));
    }
    return result;
}

// ── Profile readers ─────────────────────────────────────────────────────────

QVariantMap DecorationPageController::resolvedProfile(const QString& path) const
{
    const DecorationProfileTree& tree = this->tree();
    // resolve("") returns the baseline (the walk terminates at "" with the
    // baseline as the accumulator), so the empty-path case is handled
    // directly by the tree's own resolve.
    return profileToResolvedMap(tree.resolve(path));
}

QVariantMap DecorationPageController::rawProfile(const QString& path) const
{
    const DecorationProfileTree& tree = this->tree();
    return profileToSparseMap(directProfileAt(tree, path));
}

bool DecorationPageController::hasOverride(const QString& path) const
{
    if (path.isEmpty())
        return false;
    if (!PhosphorSurfaceShaders::decorationSurfaceSupported(path))
        return false;
    return tree().hasOverride(path);
}

// ── Chain mutators ───────────────────────────────────────────────────────────

QStringList DecorationPageController::chainAt(const QString& path) const
{
    return tree().resolve(path).effectiveChain();
}

void DecorationPageController::setChain(const QString& path, const QStringList& chain)
{
    if (!m_settings)
        return;
    if (!path.isEmpty() && !PhosphorSurfaceShaders::decorationSurfaceSupported(path))
        return;
    DecorationProfileTree tree = this->tree();
    DecorationProfile profile = directProfileAt(tree, path);
    // Packs newly entering the chain, for the border-seed below. The previous
    // chain is the DIRECT one when engaged, else the resolved effective chain
    // (same first-direct-edit seeding rationale as setChainLayerEnabled): a
    // pack the user was already previewing via inheritance is not "new".
    const QStringList prevChain = profile.chain ? *profile.chain : tree.resolve(path).effectiveChain();
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
    // Plain-look handoff: any user pack suppresses the plain Windows border
    // and opacity+tint layers wholesale in the kwin effect, so a pack newly
    // added to the chain that provides one of those looks (providesBorder /
    // providesOpacityTint metadata) gets its shared contract params seeded
    // from the matching setting — the user's border keeps its size and colour
    // (and their fade its strength and tint) when they trade the plain layer
    // for a pack. Seeds only while the matching plain layer is on (otherwise
    // there is no look to carry), only ids the pack declares (border-rgb has
    // no colour slots, border-double no borderWidth), clamped to the pack's
    // declared bounds, and never over an existing value. The "accent" colour
    // sentinel is not a valid QColor and is skipped, leaving the pack's own
    // default colours.
    if (m_registry && (m_settings->showWindowBorder() || m_settings->showWindowOpacityTint())) {
        // Base the working map on the DIRECT override when one is engaged, else
        // on the RESOLVED effective parameters. Seeding engages the optional
        // (profile.parameters = allParams below), and DecorationProfile::overlay
        // replaces the map wholesale — starting from an empty map at an
        // inheriting path would materialize a direct override of just the
        // seeded pack and silently drop every other pack's inherited params.
        // Filtered to the new chain, mirroring the pruning block above (which
        // only runs on an already-engaged optional), so packs dropped by this
        // edit don't get their stale inherited params materialized. Same
        // engage-from-resolved discipline as setChainLayerEnabled's disabled
        // set.
        QVariantMap allParams;
        if (profile.parameters) {
            allParams = *profile.parameters;
        } else {
            const QVariantMap inherited = tree.resolve(path).effectiveParameters();
            for (auto it = inherited.constBegin(); it != inherited.constEnd(); ++it) {
                if (chain.contains(it.key()))
                    allParams.insert(it.key(), it.value());
            }
        }
        bool seeded = false;
        for (const QString& packId : chain) {
            if (prevChain.contains(packId) || !m_registry->hasEffect(packId))
                continue;
            const PhosphorSurfaceShaders::SurfaceShaderEffect effect = m_registry->effect(packId);
            const bool seedBorder = effect.providesBorder && m_settings->showWindowBorder();
            const bool seedOpacityTint = effect.providesOpacityTint && m_settings->showWindowOpacityTint();
            if (!seedBorder && !seedOpacityTint)
                continue;
            QVariantMap packParams = allParams.value(packId).toMap();
            const auto seedParam = [&](const QString& id, const QVariant& value) {
                if (packParams.contains(id))
                    return;
                for (const auto& p : effect.parameters) {
                    if (p.id != id)
                        continue;
                    QVariant v = value;
                    // Clamp NUMERIC params only — a colour string also
                    // canConvert<double>() (to 0.0), so gate on the declared
                    // type, not on convertibility.
                    const bool numeric = p.type == QLatin1String("int") || p.type == QLatin1String("float");
                    // min <= max guard: qBound is UB on an inverted range, and
                    // the declared bounds come from pack-authored metadata.
                    if (numeric && p.minValue.isValid() && p.maxValue.isValid()
                        && p.minValue.toDouble() <= p.maxValue.toDouble()) {
                        v = qBound(p.minValue.toDouble(), v.toDouble(), p.maxValue.toDouble());
                        // Keep integer params integral so the editor's spinbox
                        // round-trips without a fractional cast.
                        if (p.type == QLatin1String("int"))
                            v = v.toInt();
                    }
                    packParams.insert(id, v);
                    seeded = true;
                    break;
                }
            };
            if (seedBorder) {
                seedParam(QStringLiteral("borderWidth"), m_settings->windowBorderWidth());
                seedParam(QStringLiteral("cornerRadius"), m_settings->windowBorderRadius());
                // Colours ride as their #AARRGGBB strings — the runtime
                // converts to QColor and JSON persistence round-trips them
                // untouched.
                const QString active = m_settings->windowBorderColorActive();
                if (QColor(active).isValid())
                    seedParam(QStringLiteral("activeColor"), active);
                const QString inactive = m_settings->windowBorderColorInactive();
                if (QColor(inactive).isValid())
                    seedParam(QStringLiteral("inactiveColor"), inactive);
            }
            if (seedOpacityTint) {
                seedParam(QStringLiteral("opacity"), m_settings->windowOpacity());
                seedParam(QStringLiteral("tintStrength"), m_settings->windowTintStrength());
                const QString tint = m_settings->windowTintColor();
                if (QColor(tint).isValid())
                    seedParam(QStringLiteral("tintColor"), tint);
            }
            if (!packParams.isEmpty())
                allParams.insert(packId, packParams);
        }
        if (seeded)
            profile.parameters = allParams;
    }
    writeDirectProfile(m_settings, tree, path, profile);
}

QStringList DecorationPageController::disabledPacksAt(const QString& path) const
{
    return tree().resolve(path).effectiveDisabledPacks();
}

void DecorationPageController::setChainLayerEnabled(const QString& path, const QString& packId, bool enabled)
{
    if (!m_settings || packId.isEmpty())
        return;
    if (!path.isEmpty() && !PhosphorSurfaceShaders::decorationSurfaceSupported(path))
        return;
    DecorationProfileTree tree = this->tree();
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
    DecorationProfileTree tree = this->tree();
    DecorationProfile profile = directProfileAt(tree, path);
    // Copy-mutate the {packId -> {paramId -> value}} two-level map, engaging
    // the parameters optional if it was inherited. Engaging bases on the
    // RESOLVED effective map, not an empty one: DecorationProfile::overlay
    // replaces the map wholesale, so a first per-param edit at an inheriting
    // path would otherwise materialize an override of just this pack and drop
    // every other pack's inherited params (same discipline as setChain's seed).
    QVariantMap params = profile.parameters ? *profile.parameters : inheritedParamsForChain(tree, path);
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
    DecorationProfileTree tree = this->tree();
    DecorationProfile profile = directProfileAt(tree, path);
    // Engage-from-resolved, same rationale as setChainParam above.
    QVariantMap allParams = profile.parameters ? *profile.parameters : inheritedParamsForChain(tree, path);
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
    DecorationProfileTree tree = this->tree();
    if (!tree.clearOverride(path))
        return false;
    m_settings->setDecorationProfileTree(tree);
    return true;
}

int DecorationPageController::overrideDescendantCount(const QString& path) const
{
    if (!m_settings)
        return 0;
    return overrideDescendantsOf(tree(), path).size();
}

int DecorationPageController::clearOverrideDescendants(const QString& path)
{
    if (!m_settings)
        return 0;
    DecorationProfileTree tree = this->tree();
    const QStringList toClear = overrideDescendantsOf(tree, path);
    if (toClear.isEmpty())
        return 0;
    for (const QString& p : toClear)
        tree.clearOverride(p);
    m_settings->setDecorationProfileTree(tree);
    return toClear.size();
}

} // namespace PlasmaZones
