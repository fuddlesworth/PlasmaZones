// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Read-only / projection methods of RuleController:
//   * sections() — canonical display-order
//   * rulesSnapshot() — full QML view of every rule
//   * monitorOverview() — per-screen aggregate tile data
//   * screenSizeHint() — the rule editor's note on a pinned monitor's size
//   * matchFields/operatorsForField/actionTypes/defaultPayloadFor — author surfaces
//   * validationIssuesForJson/matchIsContextOnly — editor validation hooks
//
// All methods are members of PlasmaZones::RuleController and operate on its
// private model state. Same class as rulecontroller.cpp, separate translation
// unit, no API change.

#include "rulecontroller.h"
#include "ruleauthoring.h"

#include "phosphor_i18n.h"

#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorRules/ActionParams.h>
#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleEvaluator.h>
#include <PhosphorRules/RuleSet.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/WindowQuery.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocale>
#include <QSet>
#include <QSize>

#include <algorithm>
#include <optional>

namespace PlasmaZones {

namespace {

namespace ActionType = PhosphorRules::ActionType;
using PhosphorRules::Field;
using PhosphorRules::MatchExpression;
using PhosphorRules::Rule;
using PhosphorRules::RuleAction;

/// The device-pixel size of @p screen (a `SettingsController::screens` map)
/// when the daemon's rules see one: the compositor's report, or the seeded
/// size, the one last accepted for that monitor in this session or an
/// earlier one (`pixelSizeKnown`). The estimate the daemon's getScreenInfo
/// carries before either exists is not one, so it answers nullopt here.
std::optional<QSize> knownPixelSizeOf(const QVariantMap& screen)
{
    if (!screen.value(QStringLiteral("pixelSizeKnown")).toBool()) {
        return std::nullopt;
    }
    const QSize size(screen.value(QStringLiteral("pixelWidth")).toInt(),
                     screen.value(QStringLiteral("pixelHeight")).toInt());
    if (size.width() <= 0 || size.height() <= 0) {
        return std::nullopt;
    }
    return size;
}

/// The shape the daemon reads for @p screen, the same way
/// LayoutRegistry::screenShape reads its providers: a known device-pixel
/// size decides the orientation, and only while the size is unknown does
/// the logical geometry answer it (the daemon's orientation provider reads
/// the same logical geometry).
PhosphorZones::LayoutRegistry::ScreenShape screenShapeOf(const QVariantMap& screen)
{
    PhosphorZones::LayoutRegistry::ScreenShape shape;
    if (const auto size = knownPixelSizeOf(screen)) {
        shape.pixelSize = *size;
        shape.orientation = PhosphorRules::orientationTokenFor(size->width(), size->height());
        return shape;
    }
    shape.orientation = PhosphorRules::orientationTokenFor(screen.value(QStringLiteral("width")).toInt(),
                                                           screen.value(QStringLiteral("height")).toInt());
    return shape;
}

/// The ActiveLayout value the daemon's post-assignment resolvers stamp for
/// @p entry (LayoutRegistry::rulesVisibleActiveLayoutId): the entry's layout
/// id, with a scrolling screen's template written as "scrolling:<id>". The
/// daemon also substitutes its configured default template when the entry
/// names none, which the settings process does not know, so such a screen
/// reads as the bare "scrolling:" here.
QString rulesVisibleActiveLayout(const PhosphorZones::AssignmentEntry& entry)
{
    if (entry.mode == PhosphorZones::AssignmentEntry::Scrolling && !entry.scrollingTemplateLayout.isEmpty()
        && entry.scrollingTemplateLayout != PhosphorZones::NoScrollingTemplate) {
        return PhosphorLayout::LayoutId::makeScrollingId(entry.scrollingTemplateLayout);
    }
    return entry.activeLayoutId();
}

/// The Mode condition's token for an engine mode.
QString ruleModeToken(PhosphorZones::AssignmentEntry::Mode mode)
{
    switch (mode) {
    case PhosphorZones::AssignmentEntry::Snapping:
        return QString(PhosphorRules::ModeToken::Snapping);
    case PhosphorZones::AssignmentEntry::Autotile:
        return QString(PhosphorRules::ModeToken::Tiling);
    case PhosphorZones::AssignmentEntry::Scrolling:
        return QString(PhosphorRules::ModeToken::Scrolling);
    }
    return QString();
}

/// True when @p match has a ScreenId leaf, of any operator, it reaches
/// without passing through a negation: a rule that addresses a monitor
/// rather than excluding one.
bool namesScreenPositively(const MatchExpression& match)
{
    if (match.isLeaf()) {
        return match.predicate().field == Field::ScreenId;
    }
    if (match.kind() == MatchExpression::Kind::None) {
        return false;
    }
    return std::any_of(match.children().cbegin(), match.children().cend(), namesScreenPositively);
}

/// True when @p pin (a ScreenId condition's value) names @p screen (a
/// `SettingsController::screens` map), ignoring case as the rule engine's
/// Equals does: the screen's effective id, its stable id, or for a virtual
/// screen the monitor it is part of. The last is how the daemon carries a
/// monitor's rules to its virtual screens (the physical-id retry, run only
/// when the virtual screen's own rules fill no slot), and on that retry
/// those rules read the virtual screen's own shape.
bool pinNamesScreen(const QString& pin, const QVariantMap& screen)
{
    const auto same = [&pin](const QString& id) {
        return !id.isEmpty() && pin.compare(id, Qt::CaseInsensitive) == 0;
    };
    const QString name = screen.value(QStringLiteral("name")).toString();
    if (same(name) || same(screen.value(QStringLiteral("screenId")).toString())) {
        return true;
    }
    return PhosphorIdentity::VirtualScreenId::isVirtual(name)
        && same(PhosphorIdentity::VirtualScreenId::extractPhysicalId(name));
}

/// A short name for @p screen (a `SettingsController::screens` map) to lead a
/// sentence with: make and model, else the connector. displayLabel is not
/// used because it carries the LOGICAL size, which would sit beside the
/// device-pixel size the sentence states.
QString monitorNameOf(const QVariantMap& screen)
{
    QStringList parts;
    for (const QString& key : {QStringLiteral("manufacturer"), QStringLiteral("model")}) {
        const QString part = screen.value(key).toString();
        if (!part.isEmpty()) {
            parts.append(part);
        }
    }
    QString name = parts.join(QLatin1Char(' '));
    if (name.isEmpty()) {
        name = screen.value(QStringLiteral("connectorName")).toString();
    }
    if (name.isEmpty()) {
        name = screen.value(QStringLiteral("name")).toString();
    }
    if (screen.value(QStringLiteral("isVirtualScreen")).toBool()) {
        QString region = screen.value(QStringLiteral("virtualDisplayName")).toString();
        if (region.isEmpty()) {
            region =
                PhosphorI18n::tr("Virtual screen %1").arg(screen.value(QStringLiteral("virtualIndex")).toInt() + 1);
        }
        return PhosphorI18n::tr("%1 on %2", "a virtual screen, then the monitor it is part of").arg(region, name);
    }
    return name;
}

} // namespace

QVariantList RuleController::sections() const
{
    // Canonical display order — Monitor & Layout first, then the user-authored
    // sections, then the app-managed System defaults last. The enum values are
    // emitted as data so QML never hardcodes them.
    static const QList<RuleModel::Section> kOrder = {
        RuleModel::Section::Monitor,   RuleModel::Section::Application, RuleModel::Section::Activity,
        RuleModel::Section::Animation, RuleModel::Section::Advanced,    RuleModel::Section::System,
    };
    QVariantList out;
    for (RuleModel::Section s : kOrder) {
        QVariantMap entry;
        entry[QStringLiteral("value")] = static_cast<int>(s);
        entry[QStringLiteral("label")] = RuleModel::sectionLabel(s);
        out.append(entry);
    }
    return out;
}

QVariantList RuleController::rulesSnapshot() const
{
    // Read every field through the model's own data() + role enum so the
    // section / summary logic stays in exactly one place and QML never has to
    // reference raw `Qt.UserRole + N` integers.
    QVariantList out;
    const int n = m_model.rowCount();
    for (int i = 0; i < n; ++i) {
        const QModelIndex idx = m_model.index(i, 0);
        QVariantMap entry;
        entry[QStringLiteral("ruleId")] = m_model.data(idx, RuleModel::IdRole);
        entry[QStringLiteral("name")] = m_model.data(idx, RuleModel::NameRole);
        entry[QStringLiteral("enabled")] = m_model.data(idx, RuleModel::EnabledRole);
        entry[QStringLiteral("priority")] = m_model.data(idx, RuleModel::PriorityRole);
        const auto section = m_model.data(idx, RuleModel::SectionRole).value<RuleModel::Section>();
        entry[QStringLiteral("section")] = static_cast<int>(section);
        // Localized section name, for the per-row badge the flat list shows so
        // category stays legible without grouping.
        entry[QStringLiteral("sectionLabel")] = RuleModel::sectionLabel(section);
        entry[QStringLiteral("matchSummary")] = m_model.data(idx, RuleModel::MatchSummaryRole);
        entry[QStringLiteral("actionSummary")] = m_model.data(idx, RuleModel::ActionSummaryRole);
        entry[QStringLiteral("conditionCount")] = m_model.data(idx, RuleModel::ConditionCountRole);
        entry[QStringLiteral("actionCount")] = m_model.data(idx, RuleModel::ActionCountRole);
        entry[QStringLiteral("isComposite")] = m_model.data(idx, RuleModel::IsCompositeRole);
        // ScreenIdsRole is computed in the model's data() — no per-row
        // by-id lookup, so the snapshot stays O(n).
        entry[QStringLiteral("screenIds")] = m_model.data(idx, RuleModel::ScreenIdsRole);
        entry[QStringLiteral("validationIssueCount")] = m_model.data(idx, RuleModel::ValidationIssueCountRole);
        entry[QStringLiteral("inertAnimationActionCount")] =
            m_model.data(idx, RuleModel::InertAnimationActionCountRole);
        entry[QStringLiteral("managed")] = m_model.data(idx, RuleModel::ManagedRole);
        out.append(entry);
    }
    return out;
}

QVariantList RuleController::monitorOverview(const QVariantList& screens, const QVariantList& screenStates) const
{
    // Each tile shows what the STAGED rules do to one screen as it stands
    // right now. Every condition, the shape and state ones included, is
    // evaluated against a query built from that screen's current state, and
    // the layout and lock come from the daemon's own slot resolvers
    // (LayoutRegistry::resolveRuleSlots / resolveLockRule), so the tile and the
    // daemon cannot pick a different winner for a slot. A screen whose state
    // moves (a rotation, a mode change, a desktop switch, a window opening on
    // a tiling screen) needs the caller to pass the new state and rebuild.
    //
    // Rebuilt on every QML rebind (no cache). Rule counts in practice are
    // O(10s), so the recompute cost is negligible.

    // The staged rules as a set the evaluator can walk. An invalid rule is
    // skipped here because the daemon's RuleSet::setRules drops it too.
    PhosphorRules::RuleSet ruleSet;
    for (const Rule& rule : m_model.rules()) {
        if (rule.isValid()) {
            ruleSet.addRule(rule);
        }
    }
    const PhosphorRules::RuleEvaluator evaluator(ruleSet);

    QHash<QString, QVariantMap> stateById;
    for (const QVariant& sv : screenStates) {
        const QVariantMap state = sv.toMap();
        const QString id = state.value(QStringLiteral("screenId")).toString();
        if (!id.isEmpty()) {
            stateById.insert(id, state);
        }
    }

    const QString colorScheme = m_colorSchemeProvider ? m_colorSchemeProvider() : QString();
    const PhosphorZones::AssignmentEntry defaultEntry =
        m_defaultAssignmentProvider ? m_defaultAssignmentProvider() : PhosphorZones::AssignmentEntry{};

    QVariantList out;
    for (const QVariant& sv : screens) {
        const QVariantMap screen = sv.toMap();
        // Rule matches pin an EDID screen id, so the tile has to be keyed by the
        // same thing. On the daemon-served path "name" IS that id:
        // ScreenProvider::fetchScreens fills ScreenInfo::name from the daemon's
        // `getScreens`, which returns effective screen identifiers (see
        // dbus/org.phosphor_works.Placement.Screen.xml), and parks the real connector under a
        // separate "connectorName" key this lookup never reads. "screenId" is the
        // alternate; fall back to it so the overview never silently drops a tile
        // whose name happens to be empty.
        QString screenId = screen.value(QStringLiteral("name")).toString();
        if (screenId.isEmpty()) {
            screenId = screen.value(QStringLiteral("screenId")).toString();
        }
        if (screenId.isEmpty()) {
            continue;
        }

        // The windowless context query the daemon's assignment cascade builds
        // for this screen: desktop, activity and tiled-window count from the
        // daemon's own getScreenStates answer, the colour scheme, and the
        // screen shape. While the daemon is down the tiles keep the last states
        // seen. Only when it never ran during this session is there no state
        // entry, and then the desktop reads 0 and the activity empty.
        PhosphorRules::WindowQuery query;
        query.screenId = screenId;
        const auto stateIt = stateById.constFind(screenId);
        if (stateIt != stateById.constEnd()) {
            query.virtualDesktop = stateIt->value(QStringLiteral("virtualDesktop")).toInt();
            query.activity = stateIt->value(QStringLiteral("activity")).toString();
            // Present only while the screen tiles, matching the daemon's
            // tiled-window-count provider, so the query leaves the count
            // unset on a snapping screen in both places.
            const QVariant count = stateIt->value(QStringLiteral("tiledWindowCount"));
            if (count.isValid()) {
                query.tiledWindowCount = count.toInt();
            }
        }
        query.colorScheme = colorScheme;
        PhosphorZones::LayoutRegistry::applyScreenShape(query, screenShapeOf(screen));

        // The assignment slots, with the daemon's retry of a virtual screen
        // against its monitor's rules. Only the id the rules are matched
        // against changes: the retry keeps everything else the virtual
        // screen's query carries, its shape and its tiled-window count
        // included, as the daemon's retry does.
        std::optional<PhosphorZones::LayoutRegistry::RuleSlotResolution> slots =
            PhosphorZones::LayoutRegistry::resolveRuleSlots(evaluator, query);
        // The monitor's id when the slots came from that retry, so the rule
        // count below can judge the monitor's rules too.
        QString retriedPhysicalId;
        if (!slots && PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
            PhosphorRules::WindowQuery physicalQuery = query;
            physicalQuery.screenId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId);
            slots = PhosphorZones::LayoutRegistry::resolveRuleSlots(evaluator, physicalQuery);
            if (slots) {
                retriedPhysicalId = physicalQuery.screenId;
            }
        }
        // The entry is built the way LayoutRegistry::resolveAssignmentEntry
        // builds it: the engine-mode rule's entry, or the global default when
        // only a layout slot matched, with each slot winner over its field.
        // A layout rule with no engine mode therefore applies, to the engine
        // the default picks. The daemon also lets a DefaultLayoutAssignment
        // rule swap that default for this one context; the tile does not
        // evaluate that rule, so it shows the global default there.
        PhosphorZones::AssignmentEntry entry = defaultEntry;
        if (slots) {
            if (slots->modeEntry) {
                entry = *slots->modeEntry;
            }
            if (slots->snappingLayout) {
                entry.snappingLayout = *slots->snappingLayout;
            }
            if (slots->tilingAlgorithm) {
                entry.tilingAlgorithm = *slots->tilingAlgorithm;
            }
            if (slots->scrollingTemplate) {
                entry.scrollingTemplateLayout = *slots->scrollingTemplate;
            }
        }

        // The lock resolver sees the layout the assignment resolved, as the
        // daemon's does. The daemon reads that id through
        // assignmentIdForScreen, which differs from the entry's own id in two
        // cases this process cannot follow: an entry naming no layout that is
        // not an explicit Snapping pin takes the global default's id there,
        // and a scrolling entry naming no template takes the configured
        // default template (see rulesVisibleActiveLayout). An Active layout
        // condition on a lock rule can read differently here in those cases.
        query.activeLayout = rulesVisibleActiveLayout(entry);
        bool locked = false;
        if (const Rule* lockRule = PhosphorZones::LayoutRegistry::resolveLockRule(evaluator, query)) {
            for (const RuleAction& a : lockRule->actions) {
                // The winner's value decides: a higher-priority value:false
                // rule reports unlocked even when a lower one says lock, as
                // resolveContextLocked does.
                const QJsonValue value = a.params.value(PhosphorRules::ActionParam::Value);
                if (a.type == ActionType::LockContext && value.isBool()) {
                    locked = value.toBool();
                    break;
                }
            }
        }

        // The rule count: every enabled context rule with a ScreenId condition
        // of any operator outside a negation, whose whole match holds for this
        // screen now, the engine mode included. A `none{ScreenId}` group
        // excludes a monitor rather than addressing it. Evaluating the match
        // settles which screens the condition names, ignoring case as the
        // engine does, so a "starts with" monitor rule counts on every screen
        // it matches. When the slots came from the physical-id retry, a rule
        // also counts when its match holds with the monitor's id in place of
        // this screen's, the shape and state still this screen's, mirroring
        // how the slots resolved: those rules are the ones giving this screen
        // its layout.
        //
        // DisableEngine is the one action the daemon does not run through the
        // evaluator. A monitor is disabled iff its id appears in
        // `Settings::disabledMonitors`, which `Settings::disableEntriesFor`
        // builds from MONITOR-AXIS disable rules only: exactly one
        // DisableEngine action carrying a mode token
        // (ContextRuleBridge::disableRuleMode) plus a match whose
        // `contextAxisFor` is Monitor. That second half is exactly what
        // `matchIsExactContextBase` computes, so the tile and the daemon admit
        // the same rules by construction. Anything else is honoured on a
        // narrower axis or not at all:
        //   - screen + desktop / screen + activity feed disabledDesktops /
        //     disabledActivities, which gate one desktop or activity, not the
        //     monitor, so labelling the whole screen "Engine off" overstates it.
        //   - a match carrying a window-property leaf, or pinning a
        //     non-dimension context field (Mode, TiledWindowCount, the screen
        //     shape fields, ActiveLayout, ColorScheme), is not a bare monitor
        //     pin. contextAxisFor reports CatchAll for it.
        //   - a rule carrying two DisableEngine actions is ambiguous
        //     (disableRuleMode returns nullopt).
        // The daemon's check is a per-mode UNION, not a single-winner slot:
        // every disable rule whose token equals the mode it was asked about
        // counts, so priority plays no part and every token is collected.
        // Only a rule holding for this screen's own id feeds it: the daemon's
        // monitor gate (Settings::isMonitorDisabled) matches the screen's id
        // and its connector form, never a virtual screen's monitor.
        query.mode = ruleModeToken(entry.mode);
        PhosphorRules::WindowQuery retriedQuery = query;
        retriedQuery.screenId = retriedPhysicalId;
        int ruleCount = 0;
        QSet<QString> disabledEngineModes;
        for (const Rule& rule : ruleSet.rules()) {
            if (!rule.enabled || !rule.match.isContextOnly() || !namesScreenPositively(rule.match)) {
                continue;
            }
            const bool holdsForScreen = rule.match.evaluate(query);
            if (!holdsForScreen && (retriedPhysicalId.isEmpty() || !rule.match.evaluate(retriedQuery))) {
                continue;
            }
            ++ruleCount;
            const auto disableMode = PhosphorRules::ContextRuleBridge::disableRuleMode(rule);
            if (holdsForScreen && disableMode.has_value()
                && PhosphorRules::ContextRuleBridge::matchIsExactContextBase(rule.match)) {
                disabledEngineModes.insert(*disableMode);
            }
        }

        QVariantMap tile;
        tile[QStringLiteral("screenId")] = screenId;
        // Show the layout the resolved entry runs, picked by ITS engine mode,
        // mirroring how the daemon's AssignmentEntry (which carries all three
        // tokens) is consumed. Only when a rule filled a slot: a screen that
        // runs the global default because no rule assigns it shows no layout,
        // since the tile summarises the rules.
        QString layoutLabel;
        // Track WHICH lookup applies. Three engine families feed two lookups:
        // the tiling one is kept separate so a UUID-shaped algorithm token
        // cannot resolve through the layouts model, and a tokenised layout or
        // template id cannot resolve through the algorithm list.
        const RuleModel::LabelLookup* labelLookup = nullptr;
        if (slots) {
            if (entry.mode == PhosphorZones::AssignmentEntry::Snapping) {
                // The reserved "explicitly none" word gets the same
                // substitution the scrolling arm below documents, for the
                // same reason: no lookup resolves it and the fallback showed
                // a layout apparently named "none". Bare-name wording for the
                // tile's "%1 · %2" composition, like its scrolling twin.
                if (entry.snappingLayout == PhosphorZones::NoSnappingLayout) {
                    layoutLabel = PhosphorI18n::tr("Snapping (no layout)");
                } else {
                    layoutLabel = entry.snappingLayout;
                    labelLookup = &m_snappingLayoutLookup;
                }
            } else if (entry.mode == PhosphorZones::AssignmentEntry::Autotile) {
                if (entry.tilingAlgorithm == PhosphorZones::NoTilingAlgorithm) {
                    layoutLabel = PhosphorI18n::tr("Tiling (no algorithm)");
                } else {
                    layoutLabel = entry.tilingAlgorithm;
                    labelLookup = &m_tilingAlgorithmLookup;
                }
            } else if (entry.mode == PhosphorZones::AssignmentEntry::Scrolling) {
                // The reserved "explicitly none" word is not an id any lookup
                // can resolve, and the resolve below renders an unresolvable
                // value verbatim, which showed this tile a template apparently
                // named "none". Every Monitors-page and picker None pick writes
                // that word, so it is the common case rather than a malformed
                // one. Deliberately leaves labelLookup null: the substituted
                // prose is already a label and must not be fed to a lookup.
                //
                // The bare-name wording, not the rule-list summary's
                // "Scrolling template: None" — this slot holds a layout NAME
                // that MonitorOverviewTile composes into "%1 · %2" with the
                // rule count, where the summary's "Label: payload" form would
                // read as a caption with a colon in the middle. Reuses the
                // string leafLabel already shows for a scrolling match with no
                // template, so the concept keeps one translation.
                if (entry.scrollingTemplateLayout == PhosphorZones::NoScrollingTemplate) {
                    layoutLabel = PhosphorI18n::tr("Scrolling (no template)");
                } else {
                    // The snapping lookup reads the shared layouts model, which
                    // carries the native template rows keyed by their raw UUID,
                    // so it resolves a template id to its display name too.
                    layoutLabel = entry.scrollingTemplateLayout;
                    labelLookup = &m_snappingLayoutLookup;
                }
            }
        }
        // The token is the raw layoutId / algorithm name from the rule's
        // action params — resolve it to a user-facing label when a lookup
        // is wired so the tile reads "BSP" instead of "{25828c9b-…}".
        if (labelLookup && *labelLookup && !layoutLabel.isEmpty()) {
            const QString resolved = (*labelLookup)(layoutLabel);
            if (!resolved.isEmpty()) {
                layoutLabel = resolved;
            }
        }
        tile[QStringLiteral("layoutName")] = layoutLabel;
        // "Engine off" only when a disable rule targets the engine the screen
        // actually runs: a DisableEngine{scrolling} rule on a snapping screen
        // leaves the tile on. The mode is the resolved entry's, which is the
        // global default's when no rule sets one. The QML reads this as
        // `tilingEnabled` (kept for backwards-compatibility with the existing
        // tile component); it means "the engine running on this screen is NOT
        // disabled".
        const bool engineDisabled = disabledEngineModes.contains(PhosphorZones::modeToWireString(entry.mode));
        tile[QStringLiteral("tilingEnabled")] = !engineDisabled;
        tile[QStringLiteral("ruleCount")] = ruleCount;
        tile[QStringLiteral("assigned")] = ruleCount > 0 || slots.has_value();
        // True when the winning LockContext rule locks this monitor's layout.
        // The tile shows a lock badge.
        tile[QStringLiteral("locked")] = locked;
        out.append(tile);
    }
    return out;
}

QString RuleController::screenSizeHint(const QVariantList& screens, const QVariantMap& matchJson) const
{
    const QJsonObject obj = QJsonObject::fromVariantMap(matchJson);
    if (obj.isEmpty()) {
        return QString();
    }
    const auto match = MatchExpression::fromJson(obj);
    if (!match) {
        return QString();
    }
    const QStringList pinned = RuleModel::screenIdsOf(*match);

    QStringList lines;
    for (const QVariant& sv : screens) {
        const QVariantMap screen = sv.toMap();
        const bool isPinned = std::any_of(pinned.cbegin(), pinned.cend(), [&screen](const QString& pin) {
            return pinNamesScreen(pin, screen);
        });
        if (!isPinned) {
            continue;
        }
        // Only a size the daemon's rules see. An estimate could name a size
        // the rule is not judged against.
        const auto size = knownPixelSizeOf(screen);
        if (!size) {
            continue;
        }
        const int width = size->width();
        const int height = size->height();
        const QLocale locale;
        lines.append(PhosphorI18n::tr("%1 is %2 × %3 pixels right now, which is closest to %4.",
                                      "monitor name, its width and height in device pixels, then its aspect ratio")
                         .arg(monitorNameOf(screen), locale.toString(width), locale.toString(height),
                              RuleAuthoring::aspectRatioLabel(PhosphorRules::aspectRatioTokenFor(width, height))));
    }
    return lines.join(QLatin1Char('\n'));
}

QVariantList RuleController::matchFields() const
{
    return RuleAuthoring::matchFields();
}

QVariantList RuleController::operatorsForField(int fieldValue) const
{
    return RuleAuthoring::operatorsForField(fieldValue);
}

QVariantList RuleController::allOperators() const
{
    return RuleAuthoring::allOperators();
}

QString RuleController::matchValueHint(const QString& op) const
{
    return RuleAuthoring::matchValueHint(op);
}

QVariantList RuleController::actionTypes() const
{
    return RuleAuthoring::actionTypes();
}

QVariantMap RuleController::defaultPayloadFor(const QString& typeWire) const
{
    return RuleAuthoring::defaultPayloadFor(typeWire);
}

QStringList RuleController::parseZoneNameList(const QString& text) const
{
    return RuleAuthoring::parseZoneNameList(text);
}

QString RuleController::formatZoneNameList(const QStringList& names) const
{
    return RuleAuthoring::formatZoneNameList(names);
}

QVariantList RuleController::validationIssuesForJson(const QVariantMap& ruleJson) const
{
    // Build a partial rule from the variant map — enough to run the semantic
    // compatibility check without requiring a full `Rule::fromJson`
    // (which would refuse a rule mid-edit: no id, no actions yet). The
    // validator only consults `match` and `actions`, so reconstruct just those.
    const QJsonObject obj = QJsonObject::fromVariantMap(ruleJson);

    Rule probe;
    const QJsonValue matchValue = obj.value(QLatin1String("match"));
    if (matchValue.isObject()) {
        if (const auto match = MatchExpression::fromJson(matchValue.toObject())) {
            probe.match = *match;
        }
        // A malformed match leaves `probe.match` as the default catch-all,
        // which is context-only — so the check still works as the user fills
        // out leaves: the issue only surfaces once a window-property leaf
        // lands AND a context action is present.
    }
    // Preserve every original action slot — including malformed entries
    // and non-object JSON values — as a placeholder `RuleAction{}` so
    // `probe.actions` index N corresponds to ruleJson.actions[N] in the
    // editor. The previous shape `continue`'d past invalid entries,
    // which made the validator's `issue.actionIndex` point at the wrong
    // QML editor row (every malformed entry above an issue shifted
    // subsequent indices down by one).
    //
    // A placeholder is NOT inert to the whole validator. Its empty type maps
    // to the default `Window` domain via `ActionRegistry::domainFor`'s
    // unregistered-type fallback, so the context/match check never trips on
    // it, but the co-located-Exclude check flags every non-Exclude action by
    // index and would name the placeholder with an empty action label. The
    // issue loop below drops empty-type issues for that reason.
    //
    // A rejected payload on an action whose TYPE is registered is a different
    // case and gets its own issue: that is an unfilled picker (the rule
    // templates seed empty screen / layout / algorithm ids), and saving would
    // drop the action silently because `Rule::fromJson` rejects it the same
    // way this loop does.
    struct RejectedPayload
    {
        int index;
        QString type;
    };
    QList<RejectedPayload> rejected;
    const QJsonValue actionsValue = obj.value(QLatin1String("actions"));
    if (actionsValue.isArray()) {
        int index = 0;
        for (const QJsonValue& v : actionsValue.toArray()) {
            if (v.isObject()) {
                const QJsonObject actionObj = v.toObject();
                if (const auto action = RuleAction::fromJson(actionObj)) {
                    probe.actions.append(*action);
                    ++index;
                    continue;
                }
                const QString type = actionObj.value(QLatin1String("type")).toString();
                if (!type.isEmpty()) {
                    rejected.append({index, type});
                }
            }
            // Malformed action (non-object JSON or descriptor-rejected
            // payload) — preserve a placeholder so index alignment with
            // the editor's actions array stays intact.
            probe.actions.append(RuleAction{});
            ++index;
        }
    }

    QVariantList out;
    for (const RejectedPayload& entry : rejected) {
        QVariantMap m;
        m[QStringLiteral("code")] = static_cast<int>(PhosphorRules::ValidationIssue::Code::IncompleteActionPayload);
        m[QStringLiteral("actionIndex")] = entry.index;
        m[QStringLiteral("actionType")] = entry.type;
        m[QStringLiteral("actionLabel")] = RuleAuthoring::actionTypeLabel(entry.type);
        // English, like the library's own issue messages: the UI localises
        // from the code.
        m[QStringLiteral("message")] =
            QStringLiteral("Action `%1` has an incomplete or invalid payload and would be dropped on save.")
                .arg(entry.type);
        out.append(m);
    }
    for (const PhosphorRules::ValidationIssue& issue : probe.validationIssues()) {
        if (issue.actionType.isEmpty()) {
            // A placeholder slot the user has not given a type yet. The
            // editor's own completeness gate blocks saving it and names it
            // properly; surfacing it here as well would print an issue with
            // no action name in it.
            continue;
        }
        QVariantMap m;
        m[QStringLiteral("code")] = static_cast<int>(issue.code);
        m[QStringLiteral("actionIndex")] = issue.actionIndex;
        m[QStringLiteral("actionType")] = issue.actionType;
        // Friendly picker label for the same action, so the status bar can
        // name the action the way the picker did instead of echoing the raw
        // wire token ("excludePlacement") into user-facing prose. Falls back
        // to the wire token for an unknown type.
        m[QStringLiteral("actionLabel")] = RuleAuthoring::actionTypeLabel(issue.actionType);
        m[QStringLiteral("message")] = issue.message;
        out.append(m);
    }
    return out;
}

bool RuleController::matchIsContextOnly(const QVariantMap& matchJson) const
{
    // An empty / unparseable match collapses to the default catch-all, which
    // is context-only by definition (no leaves to fail against a context
    // query). The picker treats this as "every action type compatible" so the
    // user can start with any action and add window predicates afterwards.
    const QJsonObject obj = QJsonObject::fromVariantMap(matchJson);
    if (obj.isEmpty()) {
        return true;
    }
    const auto match = MatchExpression::fromJson(obj);
    if (!match) {
        return true;
    }
    return match->isContextOnly();
}

} // namespace PlasmaZones
