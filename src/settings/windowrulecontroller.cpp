// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowrulecontroller.h"

#include "dbusutils.h"
#include "windowruleauthoring.h"
#include "windowruletemplates.h"

#include "../core/logging.h"
#include "../pz_i18n.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorWindowRule/MatchExpression.h>
#include <PhosphorWindowRule/MatchTypes.h>
#include <PhosphorWindowRule/RuleAction.h>
#include <PhosphorWindowRule/WindowRuleSet.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace PlasmaZones {

namespace {

namespace ActionType = PhosphorWindowRule::ActionType;
using PhosphorWindowRule::MatchExpression;
using PhosphorWindowRule::RuleAction;
using PhosphorWindowRule::WindowRule;
using PhosphorWindowRule::WindowRuleSet;

// Priority bands live next to the seeded templates (windowruletemplates.h) so
// the renormalize logic here shares the exact bands the templates /
// empty-rule seeds land in — a single source of truth keeps section ordering
// (Advanced > Monitor > Application > Animation) consistent between newly-
// authored rules and post-reorder priority stamps.
using WindowRuleTemplates::kAdvancedBandBase;
using WindowRuleTemplates::kAnimationBandBase;
using WindowRuleTemplates::kApplicationBandBase;
using WindowRuleTemplates::kContextBandBase;

/// Convert a JSON map (QML's QVariantMap) into a WindowRule. Returns a
/// null-id rule on a malformed map.
WindowRule ruleFromVariant(const QVariantMap& map)
{
    const QJsonObject obj = QJsonObject::fromVariantMap(map);
    const auto rule = WindowRule::fromJson(obj);
    return rule.value_or(WindowRule{});
}

} // namespace

WindowRuleController::WindowRuleController(QObject* parent)
    : QObject(parent)
{
    // Re-fetch from the daemon whenever it broadcasts a rule change — but only
    // while the page is clean, so a daemon-side write doesn't silently stomp
    // the user's unsaved staged edits.
    //
    // `QDBusConnection::connect` accepts only the const-char* SLOT form (no
    // member-pointer overload), so the SLOT() macro is canonical here. Capture
    // the return so a failed subscription (e.g. service not yet up) is
    // diagnosable rather than a silent miss — the deferred reload below still
    // fetches the initial state, but without this subscription the page would
    // never re-sync on subsequent daemon-side writes.
    const bool subscribed = QDBusConnection::sessionBus().connect(
        QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
        QString(PhosphorProtocol::Service::Interface::WindowRules), QStringLiteral("rulesChanged"), this,
        SLOT(reload()));
    if (!subscribed) {
        qCWarning(lcConfig) << "WindowRuleController: failed to subscribe to org.plasmazones.WindowRules.rulesChanged "
                               "— page will not refresh on daemon-side writes";
    }
    // Kick off the initial fetch immediately — the call is asynchronous
    // (QDBusPendingCallWatcher), so the constructor returns without
    // blocking and the settings page can finish loading. The reply
    // handler repopulates the model on the next event-loop turn.
    reload();
}

WindowRuleController::~WindowRuleController()
{
    // Mirror the `connect()` from the constructor so the bus doesn't keep
    // a slot binding referencing this destroyed instance. Settings-app
    // lifetime is process-wide today, but the symmetric disconnect makes
    // the controller safe to instantiate from a transient owner (tests,
    // future KCM page where the controller is rebuilt on demand) without
    // leaking dangling slot dispatches.
    QDBusConnection::sessionBus().disconnect(QString(PhosphorProtocol::Service::Name),
                                             QString(PhosphorProtocol::Service::ObjectPath),
                                             QString(PhosphorProtocol::Service::Interface::WindowRules),
                                             QStringLiteral("rulesChanged"), this, SLOT(reload()));
}

void WindowRuleController::setScreenLookup(WindowRuleModel::LabelLookup fn)
{
    m_model.setScreenLabelLookup(std::move(fn));
}

void WindowRuleController::setActivityLookup(WindowRuleModel::LabelLookup fn)
{
    m_model.setActivityLabelLookup(std::move(fn));
}

void WindowRuleController::setLayoutLookup(WindowRuleModel::LabelLookup fn)
{
    m_layoutLookup = fn;
    // Also forward to the model so its `actionSummary` can resolve layoutId /
    // algorithm-token wire values when building the rule-list captions.
    m_model.setLayoutLabelLookup(std::move(fn));
}

void WindowRuleController::setDirty(bool dirty)
{
    if (m_dirty == dirty) {
        return;
    }
    m_dirty = dirty;
    Q_EMIT dirtyChanged();
}

void WindowRuleController::setDaemonReachable(bool reachable)
{
    if (m_daemonReachable == reachable) {
        return;
    }
    m_daemonReachable = reachable;
    Q_EMIT daemonReachableChanged();
}

void WindowRuleController::setDaemonChangedWhileDirty(bool changed)
{
    if (m_daemonChangedWhileDirty == changed) {
        return;
    }
    m_daemonChangedWhileDirty = changed;
    Q_EMIT daemonChangedWhileDirtyChanged();
}

bool WindowRuleController::pushToDaemon(const QList<WindowRule>& rules)
{
    WindowRuleSet set;
    // setRules() drops invalid rules with a logged diagnostic and returns the
    // accepted count. Bail out BEFORE pushing if any rule was rejected — the
    // earlier code would push a truncated set to the daemon and only fail
    // the return, leaving the daemon persisting a divergent rule set the
    // model would never refresh (the next reload is guarded by the dirty
    // bit, which stays set on failure → permanent divergence).
    const int accepted = set.setRules(rules);
    if (accepted != rules.size()) {
        qCWarning(lcConfig) << "WindowRuleController::pushToDaemon: rejecting push —" << (rules.size() - accepted)
                            << "of" << rules.size() << "rules failed client-side validation; daemon NOT updated";
        return false;
    }
    const QJsonDocument doc(set.toJson());
    const QDBusMessage reply =
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::WindowRules),
                               QStringLiteral("setAllRules"), {QString::fromUtf8(doc.toJson(QJsonDocument::Compact))});
    if (reply.type() != QDBusMessage::ReplyMessage) {
        setDaemonReachable(false);
        return false;
    }
    setDaemonReachable(true);
    return !reply.arguments().isEmpty() && reply.arguments().constFirst().toBool();
}

void WindowRuleController::fetchAndLoad()
{
    // Asynchronous `getAllRules` — the reply handler repopulates the model.
    // Bound to `this` for parent, so a controller teardown before the reply
    // arrives cancels the watcher cleanly (no dangling lambda).
    auto* watcher = new QDBusPendingCallWatcher(
        PhosphorProtocol::ClientHelpers::asyncCall(QString(PhosphorProtocol::Service::Interface::WindowRules),
                                                   QStringLiteral("getAllRules")),
        this);
    // Tag THIS watcher with whether the call originated from revert(). A
    // counter-only scheme would let an in-flight initial-load watcher A
    // wrongly claim a revert tag posted by a concurrent watcher B — whichever
    // reply lands first decrements the counter regardless of which watcher
    // posted the increment. Capturing the bool by value at watcher
    // construction time binds the tag to the specific caller path.
    const bool fromRevert = m_pendingRevertFetches > 0;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, fromRevert](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QString> reply = *w;
        // Decrement *before* emitting so a chained revert→revertFinished→commit
        // handler sees the counter at the correct value.
        if (fromRevert) {
            --m_pendingRevertFetches;
        }
        if (reply.isError()) {
            // Daemon unreachable or method missing — leave the model and the
            // dirty bit untouched so a staged-but-unsavable edit isn't silently
            // dropped. `daemonReachable` flips so the page can surface the
            // warning banner.
            qCWarning(lcConfig) << "WindowRuleController::fetchAndLoad: getAllRules failed —"
                                << reply.error().message();
            setDaemonReachable(false);
            if (fromRevert) {
                Q_EMIT revertFinished(false);
            }
            return;
        }
        setDaemonReachable(true);
        const QString json = reply.value();
        WindowRuleSet set;
        if (!json.isEmpty()) {
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                set = WindowRuleSet::fromJson(doc.object()).value_or(WindowRuleSet{});
            }
            // Malformed JSON — daemon answered but with a corrupt store. Fall
            // through to the empty set so the page paints rather than holding
            // stale rules; the daemon log will carry the parse error.
        }
        m_model.setRules(set.rules());
        setDirty(false);
        setDaemonChangedWhileDirty(false);
        Q_EMIT rulesLoaded();
        if (fromRevert) {
            Q_EMIT revertFinished(true);
        }
    });
}

void WindowRuleController::reload()
{
    // A daemon `rulesChanged` broadcast routes here. If the user has unsaved
    // staged edits, re-fetching would stomp them — skip the refresh and keep
    // the staged set. The explicit revert() path calls fetchAndLoad()
    // directly so it bypasses this guard.
    if (m_dirty) {
        // The staged set is now divergent from the daemon's newer rules. Flag
        // it so the page can warn the user before a commit() silently
        // overwrites the daemon-side changes (lost update).
        setDaemonChangedWhileDirty(true);
        return;
    }
    fetchAndLoad();
}

bool WindowRuleController::commit(bool force)
{
    if (!m_dirty) {
        return true;
    }
    if (m_daemonChangedWhileDirty && !force) {
        // The daemon's rules changed under the staged edits. Pushing now would
        // silently overwrite the daemon's newer set (lost update). Refuse and
        // keep the page dirty — the page surfaces `daemonChangedWhileDirty` so
        // the user can review/revert or force the overwrite via commit(true).
        qCWarning(lcConfig) << "WindowRuleController::commit: refusing to push — daemon rules changed while the page "
                               "had staged edits; review or force-overwrite required";
        return false;
    }
    if (!pushToDaemon(m_model.rules())) {
        // The push failed (daemon down or a partial drop). Keep the dirty bit
        // set so the user can retry — the bool return tells
        // SettingsController::save() to keep the page dirty.
        return false;
    }
    setDirty(false);
    setDaemonChangedWhileDirty(false);
    return true;
}

void WindowRuleController::revert()
{
    // Discard staged edits by re-fetching the daemon's authoritative set —
    // bypass reload()'s dirty guard. fetchAndLoad() is async; the reply
    // handler clears the dirty bit only on success. A failed re-fetch keeps
    // the page dirty (and `daemonReachable` false) rather than silently
    // dropping the staged edits.
    //
    // Tag this fetch so the reply handler can emit `revertFinished(success)`.
    // SettingsController::load() listens once so it can re-add the
    // "window-rules" entry to the dirty-pages set on a failed revert (its
    // surrounding setNeedsSave(false) blanket-clears every page unconditionally).
    ++m_pendingRevertFetches;
    fetchAndLoad();
}

void WindowRuleController::renormalizePriorities()
{
    // Walk the model's list order and re-stamp priority so list order maps
    // monotonically onto evaluation order. Higher list index ⇒ lower
    // priority within the rule's band; the bands keep the section ordering
    // (Advanced > Monitor > Application > Animation) stable.
    //
    // This is invoked ONLY when list order changes — on a drag-reorder
    // (moveRule) and when a new rule is appended (addRuleFromJson). An in-place
    // edit (updateRuleFromJson) never reorders the list, so it does not call
    // this: the Advanced editor exposes `priority` directly, and an explicit
    // priority edit there is honoured verbatim rather than being overwritten by
    // the band-derived value. A non-priority in-place edit likewise leaves
    // priority untouched, so list order and PriorityRole stay consistent.
    //
    // Only the priority changes — push the new values through setPriorities()
    // so the model emits a single dataChanged(PriorityRole) instead of a full
    // reset (which would tear down and rebuild every QML delegate).
    const QList<WindowRule>& rules = m_model.rules();
    const int n = rules.size();
    QList<int> priorities;
    priorities.resize(n);
    // Bands are spaced 100 apart (Animation=100, Application=200,
    // Context=300, Advanced=500). The per-rule offset is BAND-LOCAL — each
    // band re-starts the offset countdown from the band-width cap (99) and
    // decrements as later entries within the same band are encountered. This
    // gives a contiguous "earlier list index ⇒ higher priority within the
    // band" property: the global `i` would only do that if every band were
    // dense, but with sparse bands (one Animation rule near the bottom of
    // the list) the global index dropped the bottom rule below other rules
    // in its own band. Bands run independent counters.
    //
    // Each band's offset starts at `kBandWidth - 1` and decrements; capped
    // at 0 so a band with >100 rules ties at the floor (list order is
    // preserved by setPriorities's stable application).
    constexpr int kBandWidth = 100;
    const auto baseFor = [](const WindowRule& rule) {
        switch (WindowRuleModel::sectionFor(rule)) {
        case WindowRuleModel::Section::Advanced:
            return kAdvancedBandBase;
        case WindowRuleModel::Section::Monitor:
        case WindowRuleModel::Section::Activity:
            return kContextBandBase;
        case WindowRuleModel::Section::Application:
            return kApplicationBandBase;
        case WindowRuleModel::Section::Animation:
            return kAnimationBandBase;
        }
        return kAnimationBandBase;
    };

    // Per-band offset counter — base address → next available offset. Each
    // band seeds at `kBandWidth - 1` on first encounter and decrements as
    // later entries within the same band are stamped. Absence in the hash
    // (`find() == end()`) signals not-yet-seeded; the explicit
    // find/insert flow disambiguates from the value-initialised 0 that
    // `operator[]` on QHash would otherwise produce.
    QHash<int, int> nextOffset;
    for (int i = 0; i < n; ++i) {
        const int base = baseFor(rules.at(i));
        auto it = nextOffset.find(base);
        if (it == nextOffset.end()) {
            it = nextOffset.insert(base, kBandWidth - 1);
        }
        const int offset = qMax(0, it.value());
        it.value() = offset - 1;
        priorities[i] = base + offset;
    }
    m_model.setPriorities(priorities);
}

QVariantMap WindowRuleController::newEmptyRule(const QString& subject) const
{
    return WindowRuleTemplates::newEmptyRule(subject);
}

QVariantList WindowRuleController::ruleTemplates() const
{
    return WindowRuleTemplates::ruleTemplates();
}

QVariantMap WindowRuleController::newRuleFromTemplate(const QString& templateId) const
{
    return WindowRuleTemplates::newRuleFromTemplate(templateId);
}

QString WindowRuleController::addRuleFromJson(const QVariantMap& ruleJson)
{
    // The editor sheet may hand back a map with no id (a never-stored rule).
    // Inject a fresh UUID into the JSON *before* WindowRule::fromJson runs —
    // fromJson rejects an id-less object outright, so generating the id after
    // the fact would silently drop the name / match / actions the user built.
    QJsonObject obj = QJsonObject::fromVariantMap(ruleJson);
    const QString idStr = obj.value(QLatin1String("id")).toString();
    if (idStr.isEmpty() || QUuid::fromString(idStr).isNull()) {
        obj.insert(QLatin1String("id"), QUuid::createUuid().toString());
    }
    const auto parsed = WindowRule::fromJson(obj);
    if (!parsed || !parsed->isValid()) {
        qCWarning(lcConfig) << "WindowRuleController::addRuleFromJson: rejecting malformed rule payload";
        return QString();
    }
    if (!m_model.addRule(*parsed)) {
        qCWarning(lcConfig) << "WindowRuleController::addRuleFromJson: model rejected rule" << parsed->id.toString()
                            << "(id collision or invalid)";
        return QString();
    }
    // Re-stamp priorities so two rules added back-to-back in the same band do
    // not collide on the band-base priority — list order maps onto evaluation
    // order exactly as it does after a drag-reorder.
    renormalizePriorities();
    setDirty(true);
    return parsed->id.toString();
}

bool WindowRuleController::updateRuleFromJson(const QVariantMap& ruleJson)
{
    const WindowRule rule = ruleFromVariant(ruleJson);
    if (rule.id.isNull()) {
        // ruleFromVariant() yields a null-id rule when WindowRule::fromJson
        // rejects the payload (malformed map, missing/invalid id).
        qCWarning(lcConfig) << "WindowRuleController::updateRuleFromJson: rejecting malformed rule payload";
        return false;
    }
    const WindowRuleModel::UpdateResult result = m_model.updateRule(rule);
    switch (result) {
    case WindowRuleModel::UpdateResult::NotFound:
        qCWarning(lcConfig) << "WindowRuleController::updateRuleFromJson: no rule with id" << rule.id.toString();
        return false;
    case WindowRuleModel::UpdateResult::Unchanged:
        // A genuine no-op save — the rule is unchanged, so do not dirty the
        // page. Still report success: the caller's "save" intent succeeded.
        return true;
    case WindowRuleModel::UpdateResult::Applied:
        setDirty(true);
        return true;
    }
    return false;
}

bool WindowRuleController::removeRule(const QString& ruleId)
{
    if (!m_model.removeRule(QUuid::fromString(ruleId))) {
        return false;
    }
    setDirty(true);
    return true;
}

QString WindowRuleController::duplicateRule(const QString& ruleId)
{
    const WindowRule source = m_model.ruleById(QUuid::fromString(ruleId));
    if (source.id.isNull()) {
        return QString();
    }
    WindowRule clone = source;
    clone.id = QUuid::createUuid();
    // Auto-suffix the name when the source has one so the two rules don't
    // share an identical label in the list. An empty source name stays empty
    // — the matchSummary will distinguish the clone in that case (and the
    // user typically renames immediately on duplicate anyway).
    if (!source.name.isEmpty()) {
        clone.name = PzI18n::tr("%1 (copy)").arg(source.name);
    }
    if (!m_model.addRule(clone)) {
        qCWarning(lcConfig) << "WindowRuleController::duplicateRule: model rejected clone" << clone.id.toString();
        return QString();
    }
    // The model appends to the end; reorder so the clone sits just after the
    // source — preserves the user's mental model ("the new rule is next to
    // the one I copied") instead of dropping it at the bottom of the list.
    m_model.moveRule(clone.id, source.id);
    // moveRule "before source" puts the clone immediately ABOVE the source.
    // Shift it once more to the next slot below the source — find the rule
    // that currently follows source and move the clone before that. If the
    // source was the LAST rule in the list, there is no rule after it; move
    // the clone to the end of the list instead (an empty beforeId means
    // "send to end"), otherwise the off-by-one leaves order [..., clone,
    // source] instead of the intended [..., source, clone].
    const auto& rules = m_model.rules();
    for (int i = 0; i < rules.size(); ++i) {
        if (rules.at(i).id == source.id) {
            if (i + 1 < rules.size()) {
                const QUuid afterSource = rules.at(i + 1).id;
                if (afterSource != clone.id) {
                    m_model.moveRule(clone.id, afterSource);
                }
            } else {
                // Source is the last rule — push clone to the end.
                m_model.moveRule(clone.id, QUuid());
            }
            break;
        }
    }
    setDirty(true);
    return clone.id.toString();
}

bool WindowRuleController::setRuleEnabled(const QString& ruleId, bool enabled)
{
    WindowRule rule = m_model.ruleById(QUuid::fromString(ruleId));
    if (rule.id.isNull() || rule.enabled == enabled) {
        return false;
    }
    rule.enabled = enabled;
    // The enabled-flag flip is guaranteed a real change (guarded above), so
    // anything other than Applied is a model-level failure.
    if (m_model.updateRule(rule) != WindowRuleModel::UpdateResult::Applied) {
        return false;
    }
    setDirty(true);
    return true;
}

bool WindowRuleController::moveRule(const QString& ruleId, const QString& beforeRuleId)
{
    if (!m_model.moveRule(QUuid::fromString(ruleId), QUuid::fromString(beforeRuleId))) {
        return false;
    }
    renormalizePriorities();
    setDirty(true);
    return true;
}

QVariantMap WindowRuleController::ruleJson(const QString& ruleId) const
{
    const WindowRule rule = m_model.ruleById(QUuid::fromString(ruleId));
    return rule.id.isNull() ? QVariantMap{} : rule.toJson().toVariantMap();
}

QVariantList WindowRuleController::sections() const
{
    // Canonical display order — Monitor & Layout first, Advanced last. The
    // enum values are emitted as data so QML never hardcodes them.
    static const QList<WindowRuleModel::Section> kOrder = {
        WindowRuleModel::Section::Monitor,   WindowRuleModel::Section::Application, WindowRuleModel::Section::Activity,
        WindowRuleModel::Section::Animation, WindowRuleModel::Section::Advanced,
    };
    QVariantList out;
    for (WindowRuleModel::Section s : kOrder) {
        QVariantMap entry;
        entry[QStringLiteral("value")] = static_cast<int>(s);
        entry[QStringLiteral("label")] = WindowRuleModel::sectionLabel(s);
        out.append(entry);
    }
    return out;
}

QVariantList WindowRuleController::rulesSnapshot() const
{
    // Read every field through the model's own data() + role enum so the
    // section / summary logic stays in exactly one place and QML never has to
    // reference raw `Qt.UserRole + N` integers.
    QVariantList out;
    const int n = m_model.rowCount();
    for (int i = 0; i < n; ++i) {
        const QModelIndex idx = m_model.index(i, 0);
        QVariantMap entry;
        entry[QStringLiteral("ruleId")] = m_model.data(idx, WindowRuleModel::IdRole);
        entry[QStringLiteral("name")] = m_model.data(idx, WindowRuleModel::NameRole);
        entry[QStringLiteral("enabled")] = m_model.data(idx, WindowRuleModel::EnabledRole);
        entry[QStringLiteral("priority")] = m_model.data(idx, WindowRuleModel::PriorityRole);
        entry[QStringLiteral("section")] =
            static_cast<int>(m_model.data(idx, WindowRuleModel::SectionRole).value<WindowRuleModel::Section>());
        entry[QStringLiteral("matchSummary")] = m_model.data(idx, WindowRuleModel::MatchSummaryRole);
        entry[QStringLiteral("actionSummary")] = m_model.data(idx, WindowRuleModel::ActionSummaryRole);
        entry[QStringLiteral("conditionCount")] = m_model.data(idx, WindowRuleModel::ConditionCountRole);
        entry[QStringLiteral("actionCount")] = m_model.data(idx, WindowRuleModel::ActionCountRole);
        entry[QStringLiteral("isComposite")] = m_model.data(idx, WindowRuleModel::IsCompositeRole);
        // ScreenIdsRole is computed in the model's data() — no per-row
        // by-id lookup, so the snapshot stays O(n).
        entry[QStringLiteral("screenIds")] = m_model.data(idx, WindowRuleModel::ScreenIdsRole);
        entry[QStringLiteral("validationIssueCount")] = m_model.data(idx, WindowRuleModel::ValidationIssueCountRole);
        out.append(entry);
    }
    return out;
}

QVariantList WindowRuleController::monitorOverview(const QVariantList& screens) const
{
    // Per-screen accumulator — built in a single pass over the rule set so the
    // total cost is O(rules × actions + screens) rather than O(screens × rules
    // × actions). A screen with no pinned rule simply never gets an entry and
    // falls through to the default "not assigned" tile below.
    struct Summary
    {
        int ruleCount = 0;
        bool tilingEnabled = true;
        QString snappingLayout;
        QString tilingAlgorithm;
        // Mode the rule's `SetEngineMode` action selects (if any). Used to
        // disambiguate which layout name the overview tile should show
        // when a rule carries BOTH a snapping layout and a tiling
        // algorithm — the engine mode is the source of truth for which
        // engine actually runs.
        QString engineMode;
    };
    QHash<QString, Summary> byScreen;

    for (const WindowRule& rule : m_model.rules()) {
        // Only context-only rules that pin a monitor count toward a tile.
        if (!rule.match.isContextOnly()) {
            continue;
        }
        const QStringList screenIds = WindowRuleModel::screenIdsOf(rule.match);
        if (screenIds.isEmpty()) {
            continue;
        }
        for (const QString& screenId : screenIds) {
            Summary& s = byScreen[screenId];
            ++s.ruleCount;
            for (const RuleAction& a : rule.actions) {
                if (a.type == ActionType::DisableEngine) {
                    s.tilingEnabled = false;
                } else if (a.type == ActionType::SetEngineMode && s.engineMode.isEmpty()) {
                    s.engineMode = a.params.value(QLatin1String("mode")).toString();
                } else if (a.type == ActionType::SetSnappingLayout && s.snappingLayout.isEmpty()) {
                    s.snappingLayout = a.params.value(QLatin1String("layoutId")).toString();
                } else if (a.type == ActionType::SetTilingAlgorithm && s.tilingAlgorithm.isEmpty()) {
                    s.tilingAlgorithm = a.params.value(QLatin1String("algorithm")).toString();
                }
            }
        }
    }

    QVariantList out;
    for (const QVariant& sv : screens) {
        const QVariantMap screen = sv.toMap();
        // Settings screen maps key the connector under "name" (and sometimes
        // "id"); accept either so the overview never silently drops a tile.
        QString screenId = screen.value(QStringLiteral("name")).toString();
        if (screenId.isEmpty()) {
            screenId = screen.value(QStringLiteral("id")).toString();
        }
        if (screenId.isEmpty()) {
            continue;
        }

        const auto it = byScreen.constFind(screenId);
        const bool assigned = it != byScreen.constEnd();
        const Summary summary = assigned ? *it : Summary{};

        QVariantMap tile;
        tile[QStringLiteral("screenId")] = screenId;
        // Pick the layout token that matches the rule's engine mode. When a
        // rule sets BOTH a snapping layout AND a tiling algorithm (legal —
        // they live in independent slots) the engine mode decides which
        // one is actually visible. Without an explicit engine mode we
        // prefer the snapping layout (the more common case) and fall back
        // to the algorithm so the tile is never blank when only one is
        // set.
        QString layoutLabel;
        if (summary.engineMode == QLatin1String("autotile") && !summary.tilingAlgorithm.isEmpty()) {
            layoutLabel = summary.tilingAlgorithm;
        } else if (summary.engineMode == QLatin1String("snapping") && !summary.snappingLayout.isEmpty()) {
            layoutLabel = summary.snappingLayout;
        } else if (!summary.snappingLayout.isEmpty()) {
            layoutLabel = summary.snappingLayout;
        } else {
            layoutLabel = summary.tilingAlgorithm;
        }
        // The token is the raw layoutId / algorithm name from the rule's
        // action params — resolve it to a user-facing label when a lookup
        // is wired so the tile reads "BSP" instead of "{25828c9b-…}".
        if (m_layoutLookup && !layoutLabel.isEmpty()) {
            const QString resolved = m_layoutLookup(layoutLabel);
            if (!resolved.isEmpty()) {
                layoutLabel = resolved;
            }
        }
        tile[QStringLiteral("layoutName")] = layoutLabel;
        tile[QStringLiteral("tilingEnabled")] = summary.tilingEnabled;
        tile[QStringLiteral("ruleCount")] = summary.ruleCount;
        tile[QStringLiteral("assigned")] = assigned;
        out.append(tile);
    }
    return out;
}

QStringList WindowRuleController::ruleScreenIds(const QString& ruleId) const
{
    const WindowRule rule = m_model.ruleById(QUuid::fromString(ruleId));
    if (rule.id.isNull()) {
        return {};
    }
    return WindowRuleModel::screenIdsOf(rule.match);
}

QVariantList WindowRuleController::matchFields() const
{
    return WindowRuleAuthoring::matchFields();
}

QVariantList WindowRuleController::operatorsForField(int fieldValue) const
{
    return WindowRuleAuthoring::operatorsForField(fieldValue);
}

QVariantList WindowRuleController::actionTypes() const
{
    return WindowRuleAuthoring::actionTypes();
}

QVariantMap WindowRuleController::defaultPayloadFor(const QString& typeWire) const
{
    return WindowRuleAuthoring::defaultPayloadFor(typeWire);
}

QVariantList WindowRuleController::validationIssuesForJson(const QVariantMap& ruleJson) const
{
    // Build a partial rule from the variant map — enough to run the semantic
    // compatibility check without requiring a full `WindowRule::fromJson`
    // (which would refuse a rule mid-edit: no id, no actions yet). The
    // validator only consults `match` and `actions`, so reconstruct just those.
    const QJsonObject obj = QJsonObject::fromVariantMap(ruleJson);

    WindowRule probe;
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
    const QJsonValue actionsValue = obj.value(QLatin1String("actions"));
    if (actionsValue.isArray()) {
        for (const QJsonValue& v : actionsValue.toArray()) {
            if (!v.isObject()) {
                continue;
            }
            if (const auto action = RuleAction::fromJson(v.toObject())) {
                probe.actions.append(*action);
            }
            // Malformed actions are silently dropped — the editor will fail
            // its own structural gates (param fields empty etc.) before save.
        }
    }

    QVariantList out;
    for (const PhosphorWindowRule::ValidationIssue& issue : probe.validationIssues()) {
        QVariantMap m;
        m[QStringLiteral("code")] = static_cast<int>(issue.code);
        m[QStringLiteral("actionIndex")] = issue.actionIndex;
        m[QStringLiteral("actionType")] = issue.actionType;
        m[QStringLiteral("message")] = issue.message;
        out.append(m);
    }
    return out;
}

bool WindowRuleController::matchIsContextOnly(const QVariantMap& matchJson) const
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
