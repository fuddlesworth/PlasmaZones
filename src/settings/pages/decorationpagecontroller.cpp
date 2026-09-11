// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "decorationpagecontroller.h"

#include "decoration_controller_detail.h"
#include "decorationpreviewcontroller.h"
#include "pointerpreviewcontroller.h"

#include "config/configdefaults.h"
#include "core/interfaces/isettings.h"
#include "core/platform/logging.h"

#include <PhosphorPointer/PointerShaderRegistry.h>
#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>
#include <PhosphorSurface/DecorationSupportedPaths.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include <QColor>
#include <QLatin1String>
#include <QLoggingCategory>
#include <QSet>

#include <algorithm>

namespace PlasmaZones {

using namespace decoration_controller_detail;
using PhosphorSurfaceShaders::DecorationProfile;
using PhosphorSurfaceShaders::DecorationProfileTree;

namespace {

/// A decoration preset id is a UUID or a pack-declared preset name. Bounded on
/// the way in like its two siblings (`kMaxOverlayPresetIdChars` on the overlay
/// page, `MaxShaderPresetIdLength` in the rules vocabulary). The schema
/// sanitizer bounds it again on the read path, and an id naming no preset
/// resolves to the layer's own parameters, so dropping one degrades rather than
/// breaks.
constexpr int kMaxChainPresetIdChars = 1024;

/// The built-in seed layer `Settings::decorationProfileTree()` overlays on
/// every read. Built once: it is a pure function of compiled-in literals, and
/// the readers below run for every visible card on every tree write (each
/// slider drag tick), where rebuilding four profiles and their parameter maps
/// per call is pure waste.
const DecorationProfileTree& decorationSeeds()
{
    static const DecorationProfileTree seeds = ConfigDefaults::decorationProfileTree();
    return seeds;
}

/// True when the read-side seed overlay would inject an override at @p path
/// were @p tree to carry none there — the path ships card chrome AND nothing
/// on its walk-up vetoes the seed. This, not "the chain is engaged and empty",
/// is what makes a path one whose OFF state has to be persisted as the
/// explicit empty chain.
bool seedWouldInjectAt(const DecorationProfileTree& tree, const QString& path)
{
    DecorationProfileTree without = tree;
    without.clearOverride(path);
    return without.withSeedDefaults(decorationSeeds()).hasOverride(path);
}

/// True when the override @p tree stores at @p path is nothing but an
/// untouched seed injection. Compared PER FIELD against the seed rather than
/// whole-profile: the overlay injects a seed field only where this tree
/// engages that field nowhere on the walk-up, so a seed whose parameters were
/// gated off by an engagement at an ancestor arrives as a chain-only
/// injection, which is still untouched AT THIS PATH. A whole-profile compare
/// would read that as a user override.
bool isUntouchedSeedInjection(const DecorationProfileTree& tree, const QString& path)
{
    if (!decorationSeeds().hasOverride(path))
        return false;
    const DecorationProfile seed = decorationSeeds().directOverride(path);
    const DecorationProfile mine = tree.directOverride(path);
    if (mine.chain && mine.chain != seed.chain)
        return false;
    if (mine.parameters && mine.parameters != seed.parameters)
        return false;
    // The preset slot is compared on the same terms as the other three. Without
    // it, a seeded path whose ONLY user edit was picking a preset still read as
    // an untouched seed, so the parent card's shadowing count missed it and
    // clearOverrideDescendants would not clear it.
    if (mine.presetIds && mine.presetIds != seed.presetIds)
        return false;
    return !(mine.disabledPacks && mine.disabledPacks != seed.disabledPacks);
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
        if (!p.startsWith(prefix))
            continue;
        // The tree the controller reads carries the shipped card chrome as an
        // injected override at its seed paths, and an untouched injection is
        // not a user override: counting it would put a "3 descendant surfaces
        // shadow this parent" warning on the popup card of a config nobody has
        // ever edited, with a Clear action that cannot clear it (the overlay
        // re-injects on the next read). A seeded path the user HAS edited no
        // longer matches the seed and counts normally.
        if (isUntouchedSeedInjection(tree, p))
            continue;
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

/// @p params filtered to the packs in @p chain. Filtering prevents a
/// materialized direct override from carrying stale params for a pack the
/// chain no longer contains (they would silently resurrect if that pack is
/// ever re-added).
QVariantMap paramsFilteredToChain(const QVariantMap& params, const QStringList& chain)
{
    QVariantMap out;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        if (chain.contains(it.key()))
            out.insert(it.key(), it.value());
    }
    return out;
}

/// The resolved effective parameters at @p path, filtered to the packs in the
/// resolved effective chain. The base map for ENGAGING a direct parameters
/// override at an inheriting path: DecorationProfile::overlay replaces the
/// map wholesale, so engaging from empty would drop sibling packs' inherited
/// params, while engaging unfiltered could materialize stale params.
QVariantMap inheritedParamsForChain(const DecorationProfileTree& tree, const QString& path)
{
    const DecorationProfile resolved = tree.resolve(path);
    return paramsFilteredToChain(resolved.effectiveParameters(), resolved.effectiveChain());
}

/// The preset twin of inheritedParamsForChain, and there for the same reason:
/// DecorationProfile::overlay replaces the presetIds map wholesale, so engaging
/// a direct override from an empty map at an inheriting path would silently
/// drop every sibling layer's inherited preset. Filtered to the resolved chain
/// so a pack no longer in it cannot be carried back in.
QVariantMap inheritedPresetIdsForChain(const DecorationProfileTree& tree, const QString& path)
{
    const DecorationProfile resolved = tree.resolve(path);
    return paramsFilteredToChain(resolved.effectivePresetIds(), resolved.effectiveChain());
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
                                                   ISettings* settings,
                                                   PhosphorPointerShaders::PointerShaderRegistry* pointerRegistry,
                                                   QObject* parent)
    // "decoration-staging", not "decorations": the sidebar nav node owns the
    // bare id (regVirtual in settingscontroller_pageregistration.cpp), and the
    // staging controller stays independently addressable — same split as
    // AnimationsPageController's "animations-staging".
    : PhosphorControl::PageController(QStringLiteral("decoration-staging"), parent)
    , m_registry(registry)
    , m_pointerRegistry(pointerRegistry)
    , m_settings(settings)
    , m_preview(new DecorationPreviewController(registry, settings, this))
    , m_pointerPreview(new PointerPreviewController(pointerRegistry, this))
{
    if (m_registry) {
        connect(m_registry, &PhosphorSurfaceShaders::SurfaceShaderRegistry::effectsChanged, this,
                &DecorationPageController::shaderEffectsChanged);
    }
    // Both families feed the same browser and the same set of cards, so a
    // rescan on either registry has to re-fire the one catalogue signal.
    if (m_pointerRegistry) {
        connect(m_pointerRegistry, &PhosphorPointerShaders::PointerShaderRegistry::effectsChanged, this,
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

QObject* DecorationPageController::previewController() const
{
    return m_preview;
}

QObject* DecorationPageController::pointerPreviewController() const
{
    return m_pointerPreview;
}

QString DecorationPageController::previewKind() const
{
    return QStringLiteral("decoration");
}

bool DecorationPageController::isPointerPack(const QString& effectId) const
{
    if (!m_pointerRegistry || effectId.isEmpty() || !m_pointerRegistry->hasEffect(effectId))
        return false;
    // An id both registries answer to is a pack-author error: the two
    // families draw through different passes, so one id cannot mean both.
    // Resolve to the surface family, which was there first, and say so once
    // per id rather than on every preview or chain read that asks.
    if (m_registry && m_registry->hasEffect(effectId)) {
        static QSet<QString> warned;
        if (!warned.contains(effectId)) {
            warned.insert(effectId);
            qCWarning(lcConfig) << "decoration: pack id" << effectId
                                << "is claimed by both a surface pack and a pointer pack; treating it as the "
                                   "surface pack";
        }
        return false;
    }
    return true;
}

QString DecorationPageController::previewKindFor(const QString& effectId) const
{
    return isPointerPack(effectId) ? QStringLiteral("pointer") : previewKind();
}

QObject* DecorationPageController::previewControllerFor(const QString& effectId) const
{
    if (isPointerPack(effectId))
        return m_pointerPreview;
    return m_preview;
}

QStringList DecorationPageController::unresolvableChainPacks(const QString& path, const QStringList& chain) const
{
    // The pointer surface renders through the pointer pass and every other
    // surface through a window paint, so a chain is judged against the one
    // registry its path consumes. What is refused is a pack the OTHER family
    // owns: a surface pack at the pointer path cannot be drawn by the pointer
    // pass (different uniform contract), and the reverse is just as dead. A
    // pack that neither registry knows is NOT refused — that is an
    // uninstalled pack, a legitimate state the chain editor already shows as
    // "(missing)", and refusing it would leave the user unable to reorder or
    // remove anything around that row. A family whose registry is absent (a
    // headless host, or a test built without one) judges nothing.
    QStringList foreign;
    const bool pointerPath = path == PhosphorSurfaceShaders::decorationPointerPath();
    const bool haveOwn = pointerPath ? m_pointerRegistry != nullptr : m_registry != nullptr;
    if (!haveOwn) {
        return foreign;
    }
    for (const QString& id : chain) {
        const bool inPointer = m_pointerRegistry && m_pointerRegistry->hasEffect(id);
        const bool inSurface = m_registry && m_registry->hasEffect(id);
        const bool inOwn = pointerPath ? inPointer : inSurface;
        const bool inOther = pointerPath ? inSurface : inPointer;
        if (!inOwn && inOther) {
            foreign.append(id);
        }
    }
    return foreign;
}

// ── Available packs ───────────────────────────────────────────────────────

namespace {

/// Type token a browser row carries, and the sole value of its `appliesTo`
/// list. Two families share the decoration browser now, and the type axis is
/// what separates them there.
constexpr QLatin1String kTypeSurface{"surface"};
constexpr QLatin1String kTypePointer{"pointer"};

/// Tag @p row with the family it came from, in the shape ShaderBrowserPage's
/// type axis reads: a `type` token plus a one-element `appliesTo`.
QVariantMap tagged(QVariantMap row, QLatin1String type)
{
    row.insert(QLatin1String("type"), QString(type));
    row.insert(QLatin1String("appliesTo"), QStringList{QString(type)});
    return row;
}

} // namespace

QVariantList DecorationPageController::availableShaderEffects() const
{
    QVariantList result;
    if (m_pointerRegistry) {
        const auto pointerEffects = m_pointerRegistry->availableEffects();
        result.reserve(pointerEffects.size());
        for (const auto& effect : pointerEffects)
            result.append(tagged(effectToMap(effect), kTypePointer));
    }
    if (!m_registry)
        return result;
    const auto effects = m_registry->availableEffects();
    result.reserve(result.size() + effects.size());
    for (const auto& effect : effects) {
        // EVERY pack is offered, including "border" and "opacity-tint". Those two
        // also back the plain config/rule-owned layers in easy mode, but that is a
        // separate injection the effect makes ONLY when the chain has no user packs
        // — so picking one here cannot double-apply with its plain layer. Picked
        // into a chain they are ordinary packs that render through their OWN params
        // (setChain seeds those from the plain setting via providesBorder /
        // providesOpacityTint), exactly like frost's contentOpacity.
        result.append(tagged(effectToMap(effect), kTypeSurface));
    }
    return result;
}

QVariantList DecorationPageController::availableShaderEffectsForPath(const QString& path) const
{
    QVariantList result;
    // The pointer surface renders through the pointer pass, which knows only
    // the pointer contract, so offering it a surface pack would be offering a
    // pack that cannot draw. The reverse holds for every other surface.
    if (path == PhosphorSurfaceShaders::decorationPointerPath()) {
        if (!m_pointerRegistry)
            return result;
        const auto effects = m_pointerRegistry->availableEffects();
        result.reserve(effects.size());
        for (const auto& effect : effects)
            result.append(tagged(effectToMap(effect), kTypePointer));
        return result;
    }
    if (!m_registry)
        return result;
    const auto effects = m_registry->availableEffects();
    result.reserve(effects.size());
    for (const auto& effect : effects)
        result.append(tagged(effectToMap(effect), kTypeSurface));
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
    // Same family gate a decoration set passes on import: a surface pack at
    // the pointer path (or a pointer pack anywhere else) would persist and
    // then render nothing, because the pass that draws that surface knows
    // only its own contract.
    const QStringList unresolvable = unresolvableChainPacks(path, chain);
    if (!unresolvable.isEmpty()) {
        qCWarning(lcConfig) << "setChain: refusing chain at" << path
                            << "with packs the surface cannot draw:" << unresolvable;
        return;
    }
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
    // Same pruning for the per-layer preset references. Without this a removed
    // pack's preset reference survived forever and came back on re-add with its
    // parameter deltas already gone — the exact stale state the parameter prune
    // above exists to prevent — and the orphans accumulated across every
    // add/remove cycle.
    if (profile.presetIds) {
        QVariantMap presets = *profile.presetIds;
        for (auto it = presets.begin(); it != presets.end();) {
            if (!chain.contains(it.key()))
                it = presets.erase(it);
            else
                ++it;
        }
        profile.presetIds = presets;
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
    // declared bounds, and never over an existing value. The empty
    // follow-the-theme sentinel — the shipped default — is seeded from the
    // RESOLVED zone colours it follows, so the colour half of the carry-over
    // promise above holds in the default configuration too.
    // Never for the baseline-isolated shell subtree: those surfaces take no
    // plain border / opacity-tint layer at all (chain-only by contract, see
    // DecorationSupportedPaths.h), so there is no plain look to carry over and
    // a pack added there starts from its own defaults.
    if (m_registry && !PhosphorSurfaceShaders::decorationPathIsBaselineIsolated(path)
        && (m_settings->showWindowBorder() || m_settings->showWindowOpacityTint())) {
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
            allParams = paramsFilteredToChain(tree.resolve(path).effectiveParameters(), chain);
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
                // untouched. The empty sentinel resolves through the same
                // ISettings getters the D-Bus adaptor uses (active/tint →
                // zone highlight, inactive → zone inactive), so the seeded
                // pack shows the colour the user was actually looking at.
                const QString active = m_settings->windowBorderColorActive();
                seedParam(QStringLiteral("activeColor"),
                          QColor(active).isValid() ? active : m_settings->highlightColor().name(QColor::HexArgb));
                const QString inactive = m_settings->windowBorderColorInactive();
                seedParam(QStringLiteral("inactiveColor"),
                          QColor(inactive).isValid() ? inactive : m_settings->inactiveColor().name(QColor::HexArgb));
            }
            if (seedOpacityTint) {
                seedParam(QStringLiteral("opacity"), m_settings->windowOpacity());
                seedParam(QStringLiteral("tintStrength"), m_settings->windowTintStrength());
                const QString tint = m_settings->windowTintColor();
                // Alpha stripped on the sentinel seed: the tint contract is
                // "stored opaque, tint strength is the sole alpha" (the
                // plain layer's hexToOpaqueHex enforces it for picks), while
                // the resolved highlight carries the zone alpha.
                QColor resolvedTint = m_settings->highlightColor();
                resolvedTint.setAlpha(255);
                seedParam(QStringLiteral("tintColor"),
                          QColor(tint).isValid() ? tint : resolvedTint.name(QColor::HexArgb));
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
    // re-enabling every inherited-off layer (same engage-from-resolved
    // discipline as setChain's prevChain above).
    QStringList disabled = profile.disabledPacks ? *profile.disabledPacks : tree.resolve(path).effectiveDisabledPacks();
    // Filter an inherited seed to this path's effective chain, mirroring
    // setChain's prune: an inherited disabled entry for a pack not in the
    // chain would otherwise be materialised into the direct override and
    // resurrect a long-forgotten toggle if that pack ever re-enters.
    if (!profile.disabledPacks) {
        const QStringList chain = tree.resolve(path).effectiveChain();
        disabled.erase(std::remove_if(disabled.begin(), disabled.end(),
                                      [&chain](const QString& id) {
                                          return !chain.contains(id);
                                      }),
                       disabled.end());
    }
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
    if (!m_settings || packId.isEmpty())
        return;
    if (!path.isEmpty() && !PhosphorSurfaceShaders::decorationSurfaceSupported(path))
        return;
    DecorationProfileTree tree = this->tree();
    DecorationProfile profile = directProfileAt(tree, path);
    // Engage-from-resolved, same rationale as setChainParam above.
    QVariantMap allParams = profile.parameters ? *profile.parameters : inheritedParamsForChain(tree, path);
    if (params.isEmpty()) {
        // An empty map CLEARS this layer's deltas, matching the contract the
        // animation writer documents for the same gesture. It used to early-
        // return, and because the loop below merges rather than replaces there
        // was no input of any shape that could clear a layer — so "Revert to
        // preset" was a silent no-op for decoration and pointer, the two
        // families whose only way back to the preset's own values is this call.
        allParams.remove(packId);
    } else {
        QVariantMap packParams = allParams.value(packId).toMap();
        for (auto it = params.constBegin(); it != params.constEnd(); ++it)
            packParams.insert(it.key(), it.value());
        allParams.insert(packId, packParams);
    }
    profile.parameters = allParams;
    writeDirectProfile(m_settings, tree, path, profile);
}

void DecorationPageController::setChainPreset(const QString& path, const QString& packId, const QString& presetId)
{
    if (!m_settings || packId.isEmpty())
        return;
    if (!path.isEmpty() && !PhosphorSurfaceShaders::decorationSurfaceSupported(path))
        return;
    DecorationProfileTree tree = this->tree();
    DecorationProfile profile = directProfileAt(tree, path);
    // Engage from the RESOLVED map, not an empty one, for the same reason
    // setChainParam does: DecorationProfile::overlay replaces the map
    // wholesale, so a first preset pick at an inheriting path would otherwise
    // materialize an override naming only this pack and drop every other
    // layer's inherited preset.
    if (presetId.size() > kMaxChainPresetIdChars) {
        qCWarning(lcConfig) << "DecorationPageController: refusing an over-long preset id for pack" << packId
                            << "at path" << path;
        return;
    }
    QVariantMap presets = profile.presetIds ? *profile.presetIds : inheritedPresetIdsForChain(tree, path);
    if (presetId.isEmpty())
        presets.remove(packId);
    else
        presets.insert(packId, presetId);
    profile.presetIds = presets;
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
    // override to clear anyway, so this only tightens the contract. The
    // descendants variant below needs no such guard: it matches by prefix over
    // paths that ARE overridden, and an unsupported path is never one of them.
    if (!PhosphorSurfaceShaders::decorationSurfaceSupported(path))
        return false;
    DecorationProfileTree tree = this->tree();
    const bool removed = tree.clearOverride(path);
    // Seeded surfaces (the card chrome in ConfigDefaults::decorationProfileTree:
    // the OSD and the three PopupFrame popups) are re-injected by the read-side
    // seed overlay, so a plain clear here is undone on the very next read and
    // the card's toggle snaps straight back ON — the surface could not be
    // turned off at all. Persist the explicit empty chain the overlay's master
    // gate honours instead, which IS what OFF means for a seeded surface:
    // undecorated. Nothing to inherit is lost, since the seed was the only
    // thing this path was getting. The whole override goes, parameters
    // included, exactly as it does on an unseeded path — OFF is a clear, not a
    // retune.
    if (seedWouldInjectAt(tree, path)) {
        DecorationProfile undecorated;
        undecorated.chain = QStringList{};
        tree.setOverride(path, undecorated);
    } else if (!removed) {
        return false;
    }
    m_settings->setDecorationProfileTree(tree);
    return true;
}

bool DecorationPageController::isExplicitlyUndecorated(const QString& path) const
{
    if (path.isEmpty() || !PhosphorSurfaceShaders::decorationSurfaceSupported(path))
        return false;
    const DecorationProfileTree& t = tree();
    if (!t.hasOverride(path))
        return false;
    const DecorationProfile direct = t.directOverride(path);
    if (!direct.chain || !direct.chain->isEmpty())
        return false;
    // Only at a path the seed overlay would re-inject. An engaged empty chain
    // ANYWHERE else is a real user look — the documented way for a leaf to
    // disable an ancestor's pack chain (DecorationProfile's "explicitly-empty
    // chain" contract) — and reading it as OFF would both mislabel the card
    // and let the ON path (clearUndecorated) delete the user's choice.
    return seedWouldInjectAt(t, path);
}

bool DecorationPageController::clearUndecorated(const QString& path)
{
    if (!m_settings || !isExplicitlyUndecorated(path))
        return false;
    DecorationProfileTree tree = this->tree();
    DecorationProfile profile = tree.directOverride(path);
    // Disengage only the chain slot, so the seed chain flows in again while
    // any parameters or disable set still engaged at this path stay put (the
    // overlay gates those on their own engagement). clearOverride writes a
    // chain-only marker, so in practice there is nothing else to keep; this is
    // the slot-scoped write regardless, not a whole-override drop.
    profile.chain.reset();
    // `presetIds` counts as content worth keeping, like the other two slots. A
    // profile whose only remaining engaged slot was its preset references used
    // to take the clearOverride branch and lose them.
    if (!profile.parameters && !profile.disabledPacks && !profile.presetIds)
        tree.clearOverride(path);
    else
        tree.setOverride(path, profile);
    m_settings->setDecorationProfileTree(tree);
    return true;
}

int DecorationPageController::overrideDescendantCount(const QString& path) const
{
    // No !m_settings guard: tree() already answers with a default-constructed
    // tree when settings are null, which has no overrides, so the count is 0
    // either way. The sibling readers (hasOverride, chainAt, rawProfile) lean on
    // the same thing rather than each repeating the check.
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
