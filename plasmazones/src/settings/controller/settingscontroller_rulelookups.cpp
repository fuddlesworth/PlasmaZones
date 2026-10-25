// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// SettingsController — the rule-list label lookups. The RuleController renders
// its match / action summaries through resolver closures this controller
// installs (screen and activity ids, desktop numbers, zone ids, layout ids,
// algorithm tokens, animation, overlay shader and decoration pack ids,
// animation event paths), the two zone enumerators its pickers read, and the
// refresh wiring that keeps all of them live. Split from settingscontroller.cpp
// for file-size; same class, separate TU.

#include "settingscontroller.h"

#include "core/interfaces/shaderregistry.h"
#include "phosphor_i18n.h"
#include "settings/rules/ruleauthoring.h"
#include "settings/rules/rulepickeroptions.h"

#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorRules/ActionParams.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>

#include <QSet>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>

namespace PlasmaZones {

void SettingsController::refreshRuleLabels()
{
    if (m_rulesPage && m_rulesPage->model()) {
        m_rulesPage->model()->refreshLabels();
    }
}

void SettingsController::installRuleLabelLookups()
{
    // Wire the label resolvers so the rule model and monitor-overview render
    // friendly names instead of raw connector strings, activity UUIDs, layout
    // UUIDs and the like.
    //
    // The closures capture `this` and read live state on every call, so they
    // are installed exactly once, from the ctor. An upstream change does not
    // re-install them: it reaches `RuleModel::refreshLabels()` through the
    // connections at the end of this function, which emits a single
    // dataChanged covering every label-derived role.
    m_rulesPage->setScreenLookup([this](const QString& screenId) -> QString {
        const QVariantList all = screens();
        for (const QVariant& sv : all) {
            const QVariantMap m = sv.toMap();
            // Match against `name` (the connector / virtual-screen id) or
            // `screenId` (the daemon-stable screen identifier). The screen
            // payload built by `screenInfoListToVariantList` never emits an
            // `id` key — comparing against `"id"` would be dead code. Case is
            // ignored, as the rule engine's Equals and the editor's picker
            // ignore it, so all three agree on a hand-edited id.
            if (m.value(QStringLiteral("name")).toString().compare(screenId, Qt::CaseInsensitive) == 0
                || m.value(QStringLiteral("screenId")).toString().compare(screenId, Qt::CaseInsensitive) == 0) {
                const QString label = m.value(QStringLiteral("displayLabel")).toString();
                return label.isEmpty() ? screenId : label;
            }
        }
        return screenId;
    });
    m_rulesPage->setActivityLookup([this](const QString& activityId) -> QString {
        for (const QVariant& av : std::as_const(m_activities)) {
            const QVariantMap m = av.toMap();
            // Case ignored for the same reason as the screen lookup above.
            if (m.value(QStringLiteral("id")).toString().compare(activityId, Qt::CaseInsensitive) == 0) {
                const QString name = m.value(QStringLiteral("name")).toString();
                return name.isEmpty() ? activityId : name;
            }
        }
        return activityId;
    });
    m_rulesPage->setVirtualDesktopLookup([this](const QString& desktopNumber) -> QString {
        // Desktop numbers are 1-based; the names list is 0-indexed. Return the name
        // for a valid in-range number; an out-of-range / unnamed / unparseable value
        // returns empty so the summary falls back to the bare number.
        bool ok = false;
        const int num = desktopNumber.toInt(&ok);
        if (ok && num >= 1 && num <= m_virtualDesktopNames.size()) {
            return m_virtualDesktopNames.at(num - 1);
        }
        return QString();
    });
    // Distinct zone names for the SnapToZone "Zone names" picker, read from the
    // registry's Zone objects because the LayoutPreview list carries no zone
    // names. Deduplicated on the case-folded key the engine's zoneByName
    // matches on, keeping the first spelling seen, and sorted with locale-aware
    // collation. Refreshed from the registry's layoutsChanged handler; the
    // controller emits only on change.
    m_rulesPage->setZoneNamesProvider([this]() -> QStringList {
        QStringList names;
        if (!m_localLayoutManager) {
            return names;
        }
        QSet<QString> seen;
        for (PhosphorZones::Layout* layout : m_localLayoutManager->layouts()) {
            if (!layout) {
                continue;
            }
            for (PhosphorZones::Zone* zone : layout->zones()) {
                if (!zone) {
                    continue;
                }
                const QString name = zone->name().trimmed();
                // Never offer a name the rule validator would refuse (the editor
                // caps zone names far below this; only a hand-edited layout
                // can carry one this long).
                if (name.isEmpty() || name.size() > PhosphorRules::MaxZoneNameLength) {
                    continue;
                }
                const QString key = name.toCaseFolded();
                if (seen.contains(key)) {
                    continue;
                }
                seen.insert(key);
                names.append(name);
            }
        }
        std::sort(names.begin(), names.end(), [](const QString& a, const QString& b) {
            return QString::localeAwareCompare(a, b) < 0;
        });
        return names;
    });
    // Zone (snap-zone UUID) → the "<layout> — <zone>" label the Zone picker
    // shows for it, read from the RuleController's cached zone options so the
    // rule list, the read-only tree and the picker share one label for every
    // zone, including the suffix a same-named layout carries. The comparison
    // ignores case, as the rule engine's Equals does, so a hand-edited id in
    // upper case still resolves. Unknown ids (deleted layout, hand-edited
    // rule) round-trip verbatim.
    m_rulesPage->setZoneLookup([this](const QString& zoneId) -> QString {
        if (zoneId.isEmpty() || !m_rulesPage) {
            return zoneId;
        }
        const QVariantList options = m_rulesPage->zoneMatchOptions();
        for (const QVariant& ov : options) {
            const QVariantMap option = ov.toMap();
            if (option.value(QStringLiteral("id")).toString().compare(zoneId, Qt::CaseInsensitive) == 0) {
                return option.value(QStringLiteral("displayName")).toString();
            }
        }
        return zoneId;
    });
    // The zone lookup above reads the option list, and a zone rename changes
    // that list without changing the layout list (LayoutPreview carries no
    // zone names), so layoutsChanged alone would leave the rule list's Zone
    // labels stale. Connected before the provider is installed, so the first
    // publish below already re-renders the rule list.
    connect(m_rulesPage, &RuleController::zoneMatchOptionsChanged, this, &SettingsController::refreshRuleLabels);
    // Every zone of every local manual layout, for the Zone condition's picker
    // and, through the lookup above, the rule list. Built by
    // RulePickerOptions::zoneMatchOptions, which documents the id form, the
    // labels and the order.
    m_rulesPage->setZoneMatchOptionsProvider([this]() -> QVariantList {
        if (!m_localLayoutManager) {
            return {};
        }
        return RulePickerOptions::zoneMatchOptions(m_localLayoutManager->layouts());
    });
    // SettingsController::layouts() is the union of three id families:
    // snapping layouts (UUID-keyed), autotile entries (algorithm-token-keyed
    // via the "autotile:<token>" or bare-token shape PhosphorTiles ships), and
    // native scrolling templates (UUID-keyed in their own namespace, flagged
    // isScrollingTemplate). All three are looked up by the same raw id key, so
    // one resolver lambda is sufficient — the rules side strips the
    // "scrolling:" prefix before calling in. The typed setters below are about
    // CONTRACT clarity at the RuleController API surface so a
    // future caller can wire a more restrictive snapping-only lookup
    // without also constraining the tiling resolver.
    auto resolveByLayoutsLookup = [this](const QString& tokenOrId) -> QString {
        for (const QVariant& lv : std::as_const(m_layouts)) {
            const QVariantMap m = lv.toMap();
            if (m.value(QStringLiteral("id")).toString() == tokenOrId) {
                // Layouts are serialised via `toVariantMap(LayoutPreview)`
                // which stamps the friendly label under `displayName`, not
                // `name`. Reading `name` here would always return an empty
                // string and the tile caption would show the raw UUID.
                const QString name = m.value(QStringLiteral("displayName")).toString();
                return name.isEmpty() ? tokenOrId : name;
            }
        }
        return tokenOrId;
    };
    // Snapping layouts are stored by UUID, which matches the layouts-list id
    // directly. Tiling-algorithm actions, however, store the BARE algorithm
    // token ("bsp"), while the layouts list keys autotile entries by the
    // "autotile:<token>" form — so the bare token must be prefixed before the
    // lookup, or the list shows the raw id instead of the friendly name. Try
    // the prefixed form first, then fall back to the bare token (covering the
    // bare-keyed shape PhosphorTiles can also ship, and already-prefixed data).
    auto resolveTilingAlgorithmLookup = [resolveByLayoutsLookup](const QString& algorithmToken) -> QString {
        const QString prefixed = PhosphorLayout::LayoutId::makeAutotileId(algorithmToken);
        const QString label = resolveByLayoutsLookup(prefixed);
        return label == prefixed ? resolveByLayoutsLookup(algorithmToken) : label;
    };
    m_rulesPage->setSnappingLayoutLookup(resolveByLayoutsLookup);
    m_rulesPage->setTilingAlgorithmLookup(resolveTilingAlgorithmLookup);
    // OverrideAnimationShader actions store an effect id ("dissolve"); resolve
    // it to the friendly name via the same animation shader registry the rule
    // editor's shader picker reads (availableShaderEffects), so the list shows
    // "Shader: Dissolve" rather than the raw id. Unknown ids round-trip
    // verbatim (registry miss → raw id), matching the editor's fallback.
    auto resolveShaderEffectLookup = [this](const QString& effectId) -> QString {
        if (effectId.isEmpty() || !m_animationShaderRegistry || !m_animationShaderRegistry->hasEffect(effectId)) {
            return effectId;
        }
        const QString name = m_animationShaderRegistry->effect(effectId).name;
        return name.isEmpty() ? effectId : name;
    };
    m_rulesPage->setShaderEffectLookup(resolveShaderEffectLookup);
    // The per-event animation overrides carry a profile path ("window.open");
    // resolve it to the SAME "Section · Event" composition the rule editor's
    // read-only row renders (ActionListView.qml walks eventSections() the same
    // way), so "Window · Open shader: Dissolve" and "Window · Close shader:
    // Dissolve" read as two rows and two sections' "Show" events cannot
    // collapse into one label. Both halves are the translated segment labels
    // the animations page and the event picker show
    // (AnimationsPageController::segmentLabel). m_animationsPage
    // already exists here (the SettingsController ctor builds it before calling
    // installRuleLabelLookups); the null guard
    // only covers the lookup being exercised after teardown has cleared it.
    m_rulesPage->setAnimationEventLookup([this](const QString& path) -> QString {
        if (path.isEmpty() || !m_animationsPage) {
            return path;
        }
        const QVariantList sections = m_animationsPage->eventSections();
        for (const QVariant& sectionVar : sections) {
            const QVariantMap section = sectionVar.toMap();
            const QVariantList paths = section.value(QStringLiteral("paths")).toList();
            for (const QVariant& pathVar : paths) {
                const QVariantMap entry = pathVar.toMap();
                if (entry.value(QStringLiteral("path")).toString() != path
                    || entry.value(QStringLiteral("isCategory")).toBool()) {
                    continue;
                }
                const QString sectionLabel = section.value(QStringLiteral("label")).toString();
                const QString eventLabel = entry.value(QStringLiteral("label")).toString();
                if (sectionLabel.isEmpty()) {
                    return eventLabel.isEmpty() ? path : eventLabel;
                }
                return PhosphorI18n::tr("%1 · %2", "animation section, then the event inside it")
                    .arg(sectionLabel, eventLabel.isEmpty() ? path : eventLabel);
            }
        }
        const QString label = m_animationsPage->eventLabel(path);
        return label.isEmpty() ? path : label;
    });
    // OverrideOverlayShader stores an overlay/snapping shader id; resolve it to
    // the friendly name via the overlay shader registry (the same source the
    // rule editor's overlay-shader picker reads), so the list shows
    // "Overlay shader: <name>" rather than the raw id. Unknown ids round-trip
    // verbatim (registry miss → empty name → raw id). m_overlayShaderRegistry is
    // built later in the SettingsController ctor than the
    // installRuleLabelLookups call; the `!m_overlayShaderRegistry` guard below
    // covers that window — the lambda captures `this` and is invoked only lazily
    // (on the model's first label render, after construction completes).
    auto resolveOverlayShaderLookup = [this](const QString& effectId) -> QString {
        if (effectId.isEmpty() || !m_overlayShaderRegistry) {
            return effectId;
        }
        const QString name = m_overlayShaderRegistry->shader(effectId).name;
        return name.isEmpty() ? effectId : name;
    };
    m_rulesPage->setOverlayShaderLookup(resolveOverlayShaderLookup);
    // OverrideDecorationChain stores surface-pack ids ("frosted-glass");
    // resolve them to friendly names via the surface shader registry (the
    // same source the decoration pages' pack picker reads), so the list
    // shows "Decoration: Frosted Glass, Glow" rather than raw ids. Unknown
    // ids round-trip verbatim, matching the other lookups' fallbacks.
    auto resolveDecorationPackLookup = [this](const QString& packId) -> QString {
        if (packId.isEmpty() || !m_surfaceShaderRegistry || !m_surfaceShaderRegistry->hasEffect(packId)) {
            return packId;
        }
        const QString name = m_surfaceShaderRegistry->effect(packId).name;
        return name.isEmpty() ? packId : name;
    };
    m_rulesPage->setDecorationPackLookup(resolveDecorationPackLookup);
    connect(this, &SettingsController::screensChanged, this, &SettingsController::refreshRuleLabels);
    connect(this, &SettingsController::activitiesChanged, this, &SettingsController::refreshRuleLabels);
    connect(this, &SettingsController::layoutsChanged, this, &SettingsController::refreshRuleLabels);
    // The virtual-desktop resolver reads m_virtualDesktopNames live — refresh
    // on renames too, like its screen/activity/layout siblings, or a rule's
    // "Desktop: Work" label stays stale until an unrelated refresh fires.
    connect(this, &SettingsController::virtualDesktopsChanged, this, &SettingsController::refreshRuleLabels);
    // A shader-pack rescan (user drops in a new effect, or one is removed)
    // can change an id→name mapping; refresh so resolved Shader labels track it.
    connect(m_animationShaderRegistry, &PhosphorAnimationShaders::AnimationShaderRegistry::effectsChanged, this,
            &SettingsController::refreshRuleLabels);
    // Same refresh for surface-pack rescans so resolved Decoration labels
    // track pack installs/removals.
    connect(m_surfaceShaderRegistry, &PhosphorSurfaceShaders::SurfaceShaderRegistry::effectsChanged, this,
            &SettingsController::refreshRuleLabels);
}

} // namespace PlasmaZones
