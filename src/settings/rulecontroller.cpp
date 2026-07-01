// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rulecontroller.h"

#include "ruleauthoring.h"
#include "ruletemplates.h"

#include "../core/baselinerules.h"
#include "../core/logging.h"
#include "../phosphor_i18n.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/RuleSet.h>

#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QHash>
#include <QSet>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <limits>

namespace PlasmaZones {

namespace {

using PhosphorRules::Rule;
using PhosphorRules::RuleSet;

// Default priority bases by section, shared with the seeded templates
// (ruletemplates.h). Priority renormalization is flat global list-order and does
// NOT use these; they feed only `bandBaseForSection`, which seeds where a newly
// added rule inserts (a new Advanced rule starts above Application, etc.) before
// the user reorders freely.
using RuleTemplates::kAdvancedBandBase;
using RuleTemplates::kAnimationBandBase;
using RuleTemplates::kApplicationBandBase;
using RuleTemplates::kContextBandBase;

/// Convert a JSON map (QML's QVariantMap) into a Rule. Returns a
/// null-id rule on a malformed map.
Rule ruleFromVariant(const QVariantMap& map)
{
    const QJsonObject obj = QJsonObject::fromVariantMap(map);
    const auto rule = Rule::fromJson(obj);
    return rule.value_or(Rule{});
}

} // namespace

RuleController::RuleController(QObject* parent)
    : PhosphorControl::PageController(QStringLiteral("rules"), parent)
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
    // Subscribe to daemon-side rule mutations. The connect() and the
    // disconnect() in the dtor must use IDENTICAL (service, path,
    // interface, signal, slot) tuples — otherwise the bus keeps a
    // dangling slot binding referencing a destroyed object. Routing
    // both through `rulesChangedSubscriptionArgs()` makes that
    // symmetry mechanical: a future rename of the signal touches the
    // tuple in one place and connect+disconnect track each other.
    const bool subscribed = subscribeRulesChanged();
    if (!subscribed) {
        qCWarning(lcConfig) << "RuleController: failed to subscribe to org.plasmazones.Rules.rulesChanged "
                               "— will retry when the daemon becomes reachable";
    }
    m_rulesChangedSubscribed = subscribed;
    // Kick off the initial fetch immediately — the call is asynchronous
    // (QDBusPendingCallWatcher), so the constructor returns without
    // blocking and the settings page can finish loading. The reply
    // handler repopulates the model on the next event-loop turn.
    reload();
}

RuleController::~RuleController()
{
    // Mirror the `connect()` from the constructor — see subscribe
    // helper docstring above.
    unsubscribeRulesChanged();
}

RuleController::RulesChangedSubscription RuleController::rulesChangedSubscriptionArgs()
{
    // QLatin1String avoids the QString allocation that `QString(...)` of a
    // const-char* produced — the protocol constants are compile-time
    // literals, and the implicit-conversion to QString happens once at
    // the QDBus connect call boundary.
    return {QLatin1String(PhosphorProtocol::Service::Name), QLatin1String(PhosphorProtocol::Service::ObjectPath),
            QLatin1String(PhosphorProtocol::Service::Interface::Rules), QStringLiteral("rulesChanged")};
}

bool RuleController::subscribeRulesChanged()
{
    const auto args = rulesChangedSubscriptionArgs();
    return QDBusConnection::sessionBus().connect(args.service, args.objectPath, args.interface, args.signalName, this,
                                                 SLOT(reload()));
}

void RuleController::unsubscribeRulesChanged()
{
    const auto args = rulesChangedSubscriptionArgs();
    QDBusConnection::sessionBus().disconnect(args.service, args.objectPath, args.interface, args.signalName, this,
                                             SLOT(reload()));
    m_rulesChangedSubscribed = false;
}

// Label-lookup setters (setScreenLookup, setActivityLookup,
// setSnappingLayoutLookup, setTilingAlgorithmLookup, setShaderEffectLookup,
// setCurveLabelResolver) live in rulecontroller_lookups.cpp so this TU
// stays under the project's 800-line cap.

bool RuleController::isDirty() const
{
    return m_dirty;
}

void RuleController::apply()
{
    // StagingDomain::apply contract: asyncCommit dispatches the
    // setAllRules push via QDBusPendingCallWatcher and emits
    // applyResult(ok, error) on the reply. The chrome's
    // ApplicationController::applyAllAsync waits on this signal so a
    // stuck daemon no longer freezes the Settings window — the user
    // sees a "Saving…" indicator and an error toast if the push
    // fails.
    asyncCommit(/*force=*/false);
}

void RuleController::discard()
{
    // Re-entrant guard: a second discard() while the first revert is
    // in flight would register a second one-shot lambda on
    // revertFinished — both fire on the next reply and emit
    // discardResult twice, breaking the StagingDomain contract that
    // promises one terminal signal per discard() invocation.
    if (m_discardInFlight) {
        Q_EMIT discardResult(false, PhosphorI18n::tr("Discard already in flight."));
        return;
    }
    m_discardInFlight = true;
    // Qt::SingleShotConnection self-disconnects on first fire — no
    // heap-allocated QMetaObject::Connection* needed (the prior
    // pattern leaked when ~RuleController ran before
    // revertFinished arrived). The companion destroyed() guard would
    // be redundant here: the lambda's receiver IS `this`, so Qt
    // auto-removes the connection on `this` destruction.
    connect(
        this, &RuleController::revertFinished, this,
        [this](bool success) {
            m_discardInFlight = false;
            Q_EMIT discardResult(success,
                                 success ? QString() : PhosphorI18n::tr("Failed to fetch the daemon's rule set."));
        },
        Qt::SingleShotConnection);
    revert();
}

void RuleController::setDirty(bool dirty)
{
    if (m_dirty == dirty) {
        return;
    }
    m_dirty = dirty;
    Q_EMIT dirtyChanged();
}

void RuleController::setDaemonReachable(bool reachable)
{
    if (m_daemonReachable == reachable) {
        return;
    }
    m_daemonReachable = reachable;
    // Retry the rulesChanged subscription if a previous attempt failed.
    // Covers the "controller built before the daemon was up" scenario:
    // the initial subscribe() in the ctor returned false, but once the
    // daemon arrives the next setDaemonReachable(true) reattaches the
    // signal so subsequent daemon-side writes refresh the page.
    if (reachable && !m_rulesChangedSubscribed) {
        if (subscribeRulesChanged()) {
            m_rulesChangedSubscribed = true;
            qCInfo(lcConfig) << "RuleController: rulesChanged subscription attached after daemon became reachable";
        }
    }
    Q_EMIT daemonReachableChanged();
}

void RuleController::setDaemonChangedWhileDirty(bool changed)
{
    if (m_daemonChangedWhileDirty == changed) {
        return;
    }
    m_daemonChangedWhileDirty = changed;
    Q_EMIT daemonChangedWhileDirtyChanged();
}

bool RuleController::pushToDaemonAsync(const QList<Rule>& rules)
{
    // Same up-front validation as the sync path — bail out BEFORE
    // dispatching so a partial-drop never reaches the daemon.
    RuleSet set;
    const int accepted = set.setRules(rules);
    if (accepted != rules.size()) {
        qCWarning(lcConfig) << "RuleController::pushToDaemonAsync: rejecting push —" << (rules.size() - accepted)
                            << "of" << rules.size() << "rules failed client-side validation; daemon NOT updated";
        return false;
    }
    const QJsonDocument doc(set.toJson());
    auto* watcher = new QDBusPendingCallWatcher(
        PhosphorProtocol::ClientHelpers::asyncCall(QString(PhosphorProtocol::Service::Interface::Rules),
                                                   QStringLiteral("setAllRules"),
                                                   {QString::fromUtf8(doc.toJson(QJsonDocument::Compact))}),
        this);
    // Flip the in-flight guard now so an asyncCommit re-entry during
    // the wire round-trip rejects rather than dispatching a second
    // setAllRules. Cleared in the reply lambda below before every
    // applyResult emit.
    m_asyncCommitInFlight = true;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        m_asyncCommitInFlight = false;
        QDBusPendingReply<bool> reply = *w;
        if (reply.isError()) {
            setDaemonReachable(false);
            qCWarning(lcConfig) << "RuleController::pushToDaemonAsync: setAllRules failed —" << reply.error().message();
            Q_EMIT applyResult(false, reply.error().message());
            return;
        }
        setDaemonReachable(true);
        // The daemon's bool reply tells us whether it accepted every
        // rule; treat anything other than `true` as a partial-drop
        // failure so the page can stay dirty for retry.
        const bool ok = reply.value();
        if (!ok) {
            Q_EMIT applyResult(false, PhosphorI18n::tr("The daemon rejected one or more rules."));
            return;
        }
        // The staged set is now the daemon's persisted set — re-baseline the
        // per-page dirty snapshot so baselinesDirty/userRulesDirty read clean.
        captureSavedSnapshot();
        setDirty(false);
        setDaemonChangedWhileDirty(false);
        Q_EMIT applyResult(true, QString());
    });
    return true;
}

void RuleController::fetchAndLoad(bool fromRevert)
{
    // Asynchronous `getAllRules` — the reply handler repopulates the model.
    // Bound to `this` for parent, so a controller teardown before the reply
    // arrives cancels the watcher cleanly (no dangling lambda).
    auto* watcher = new QDBusPendingCallWatcher(
        PhosphorProtocol::ClientHelpers::asyncCall(QString(PhosphorProtocol::Service::Interface::Rules),
                                                   QStringLiteral("getAllRules")),
        this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, fromRevert](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QString> reply = *w;
        if (reply.isError()) {
            // Daemon unreachable or method missing — leave the model and the
            // dirty bit untouched so a staged-but-unsavable edit isn't silently
            // dropped. `daemonReachable` flips so the page can surface the
            // warning banner.
            qCWarning(lcConfig) << "RuleController::fetchAndLoad: getAllRules failed —" << reply.error().message();
            setDaemonReachable(false);
            if (fromRevert) {
                Q_EMIT revertFinished(false);
            }
            return;
        }
        setDaemonReachable(true);
        const QString json = reply.value();
        RuleSet set;
        bool jsonOk = true;
        if (!json.isEmpty()) {
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                set = RuleSet::fromJson(doc.object()).value_or(RuleSet{});
            } else {
                // Malformed JSON — daemon answered but with a corrupt store.
                // Do NOT stomp the existing model with an empty set: a single
                // corrupt reply could otherwise wipe the user's view of their
                // rules. Treat as transient transport error parallel to
                // reply.isError() above so the model stays intact and the
                // user can retry.
                qCWarning(lcConfig) << "RuleController::fetchAndLoad: malformed daemon JSON —" << err.errorString();
                jsonOk = false;
            }
        }
        if (!jsonOk) {
            if (fromRevert) {
                Q_EMIT revertFinished(false);
            }
            return;
        }
        // If the user staged edits between dispatch and reply (e.g. an
        // initial-load fetch that races a fast user action), do not stomp
        // them on the non-revert path. Mirror reload()'s dirty-guard:
        // flag daemonChangedWhileDirty so the page can warn before a save
        // overwrites the daemon's newer set, and leave the staged
        // model intact. The explicit revert path is allowed to clobber.
        //
        // Do NOT emit rulesLoaded here — we did not actually load anything,
        // and downstream observers treat rulesLoaded as "the controller's
        // model now matches the daemon's authoritative set" which would be
        // false after this guard. Tests that synchronise on rulesLoaded
        // would otherwise read isDirty() before the explicit revert's
        // reply lands.
        if (!fromRevert && m_dirty) {
            setDaemonChangedWhileDirty(true);
            return;
        }
        m_model.setRules(set.rules());
        // Re-baseline the per-page dirty snapshot to the daemon's authoritative set.
        captureSavedSnapshot();
        setDirty(false);
        setDaemonChangedWhileDirty(false);
        Q_EMIT rulesLoaded();
        if (fromRevert) {
            Q_EMIT revertFinished(true);
        }
    });
}

void RuleController::reload()
{
    // A daemon `rulesChanged` broadcast routes here. If the user has unsaved
    // staged edits, re-fetching would stomp them — skip the refresh and keep
    // the staged set. The explicit revert() path calls fetchAndLoad()
    // directly so it bypasses this guard.
    if (m_dirty) {
        // The staged set is now divergent from the daemon's newer rules. Flag
        // it so the page can warn the user before a save silently
        // overwrites the daemon-side changes (lost update).
        setDaemonChangedWhileDirty(true);
        return;
    }
    fetchAndLoad();
}

void RuleController::asyncCommit(bool force)
{
    // Push the staged rule set to the daemon and emit applyResult on the
    // reply. Clean state is emitted as applyResult(true, "");
    // refusals + transport errors carry an explanatory message.
    //
    // Refuse re-entrant invocation while a prior setAllRules push is
    // outstanding — a second push would race the first reply (both
    // would call setDirty(false) and emit applyResult), and the
    // daemon receives two identical writes for one user action.
    if (m_asyncCommitInFlight) {
        Q_EMIT applyResult(false, PhosphorI18n::tr("A save is already in flight."));
        return;
    }
    if (!m_dirty) {
        Q_EMIT applyResult(true, QString());
        return;
    }
    if (m_daemonChangedWhileDirty && !force) {
        qCWarning(lcConfig) << "RuleController::asyncCommit: refusing to push — daemon rules changed while the "
                               "page had staged edits; review or force-overwrite required";
        Q_EMIT applyResult(false,
                           PhosphorI18n::tr("The daemon's rules changed while you were editing. Review or use "
                                            "Save anyway to overwrite."));
        return;
    }
    if (!pushToDaemonAsync(m_model.rules())) {
        // The up-front client validation failed (a rule was rejected
        // before any wire dispatch). pushToDaemonAsync already logged a
        // diagnostic; surface a user-readable error so the chrome can
        // toast / banner the failure rather than leaving the page in a
        // silent-fail "still dirty" state.
        Q_EMIT applyResult(false,
                           PhosphorI18n::tr("One or more rules failed validation and could not be saved. See the "
                                            "log for details."));
    }
    // pushToDaemonAsync's QDBusPendingCallWatcher emits applyResult on
    // the reply (success or transport error) — nothing to do here.
}

void RuleController::revert()
{
    // Discard staged edits by re-fetching the daemon's authoritative set —
    // bypass reload()'s dirty guard. fetchAndLoad() is async; the reply
    // handler clears the dirty bit only on success. A failed re-fetch keeps
    // the page dirty (and `daemonReachable` false) rather than silently
    // dropping the staged edits.
    //
    // Tag this fetch explicitly as fromRevert=true so the reply handler
    // emits revertFinished. SettingsController::load() listens once so
    // it can re-add the "rules" entry to the dirty-pages set on
    // a failed revert (its surrounding setNeedsSave(false) blanket-clears
    // every page unconditionally). The prior counter-based tagging was
    // spoofable by a concurrent daemon broadcast: a reload() arriving
    // mid-revert would read m_pendingRevertFetches > 0 and tag itself
    // as fromRevert, emitting a spurious revertFinished(true).
    fetchAndLoad(/*fromRevert=*/true);
}

// ── Per-page dirty split + baseline ops ──────────────────────────────────────

namespace {
// Partition helpers: managed rules are the appearance baselines; the rest are
// user rules. Order is preserved so the user-rules comparison catches reorders.
QList<PhosphorRules::Rule> managedSubset(const QList<PhosphorRules::Rule>& rules)
{
    QList<PhosphorRules::Rule> out;
    for (const PhosphorRules::Rule& r : rules) {
        if (r.managed)
            out.append(r);
    }
    return out;
}
QList<PhosphorRules::Rule> userSubset(const QList<PhosphorRules::Rule>& rules)
{
    QList<PhosphorRules::Rule> out;
    for (const PhosphorRules::Rule& r : rules) {
        if (!r.managed)
            out.append(r);
    }
    return out;
}
} // namespace

void RuleController::captureSavedSnapshot()
{
    m_savedRules = m_model.rules();
}

bool RuleController::baselinesDirty() const
{
    return managedSubset(m_model.rules()) != managedSubset(m_savedRules);
}

bool RuleController::userRulesDirty() const
{
    return userSubset(m_model.rules()) != userSubset(m_savedRules);
}

void RuleController::recomputeDirtyFromSnapshot()
{
    setDirty(baselinesDirty() || userRulesDirty());
}

void RuleController::upsertRule(const PhosphorRules::Rule& rule)
{
    if (m_model.contains(rule.id))
        m_model.updateRule(rule);
    else
        m_model.addRule(rule);
}

void RuleController::resetBaselines()
{
    // Rewrite the three managed baselines to their factory definitions. Managed
    // rules stay in the System section, so updateRule replaces them in place
    // (no priority renormalize). Staged — the global Save flushes it.
    upsertRule(makeBaselineBorderRule());
    upsertRule(makeBaselineTitleBarRule());
    upsertRule(makeBaselineGapRule());
    recomputeDirtyFromSnapshot();
    Q_EMIT baselinesChanged();
}

void RuleController::discardBaselineEdits()
{
    // Restore each managed baseline from the last synced snapshot, leaving every
    // user rule untouched.
    const QList<PhosphorRules::Rule> savedBaselines = managedSubset(m_savedRules);
    for (const PhosphorRules::Rule& saved : savedBaselines)
        upsertRule(saved);
    recomputeDirtyFromSnapshot();
    Q_EMIT baselinesChanged();
}

int RuleController::bandBaseForSection(RuleModel::Section section)
{
    switch (section) {
    case RuleModel::Section::Advanced:
        return kAdvancedBandBase;
    case RuleModel::Section::Monitor:
    case RuleModel::Section::Activity:
        return kContextBandBase;
    case RuleModel::Section::Application:
        return kApplicationBandBase;
    case RuleModel::Section::Animation:
        return kAnimationBandBase;
    case RuleModel::Section::System:
        // Managed System rules keep their pinned INT_MIN priority — they are
        // never renumbered by renormalizePriorities (the managed guard skips
        // them). Report INT_MIN so a System rule seeds below every user rule.
        return std::numeric_limits<int>::min();
    }
    return kAnimationBandBase;
}

void RuleController::renormalizePriorities()
{
    // Priority is a single GLOBAL list-order sequence: earlier list row ⇒ higher
    // priority, full stop. There are no priority bands — a rule's precedence is
    // purely its position in the one flat list, and the user can freely
    // interleave rules from what used to be separate "sections" (sections are
    // now only a display label + a default-seeding hint, see
    // bandSeededInsertIndex). The evaluator only ever reads this integer
    // (descending, list-order tie-break), so a flat global sequence evaluates
    // identically to the old banded scheme — it just lets cross-section drags
    // actually change who wins.
    //
    // Invoked ONLY when list order changes — on a drag-reorder (moveRule) and
    // when a rule is added (addRuleFromJson). An in-place edit
    // (updateRuleFromJson) never reorders the list, so it does not call this:
    // the Advanced editor exposes `priority` directly and an explicit edit there
    // is honoured verbatim until the next reorder. Only the priority changes —
    // push through setPriorities() so the model emits a single
    // dataChanged(PriorityRole) instead of a full reset.
    const QList<Rule>& rules = m_model.rules();
    const int n = rules.size();
    QList<int> priorities;
    priorities.resize(n);
    // Spaced by kStep so a manual Advanced-editor priority edit has room to land
    // between two neighbours before the next reorder renumbers. Start from the
    // top (highest priority = first row) and step down.
    constexpr int kStep = 16;
    int userCount = 0;
    for (int i = 0; i < n; ++i) {
        if (!rules.at(i).managed)
            ++userCount;
    }
    int rank = userCount;
    for (int i = 0; i < n; ++i) {
        // Managed rules (the baseline appearance rule) carry a pinned priority
        // (INT_MIN) that fixes them below every user rule regardless of list
        // position — never re-stamp them, or they'd jump above user rules.
        if (rules.at(i).managed) {
            priorities[i] = rules.at(i).priority;
            continue;
        }
        priorities[i] = rank * kStep;
        --rank;
    }
    m_model.setPriorities(priorities);
}

int RuleController::bandSeededInsertIndex(const Rule& rule) const
{
    // Seed the new rule's DEFAULT position by band so defaults stay sensible (a
    // new Advanced rule starts high, a new Animation rule starts low) even
    // though priority is now one free-form global sequence. Insert above the
    // first existing rule of a strictly lower band, or above the managed block.
    // Existing same-band rules are stepped over, so the new rule lands at the
    // bottom of its own tier (just above the next lower band), keeping
    // earlier-added rules ahead of it within the tier. The user can drag it
    // anywhere afterwards.
    const int band = bandBaseForSection(RuleModel::sectionFor(rule));
    const QList<Rule>& rules = m_model.rules();
    for (int i = 0; i < rules.size(); ++i) {
        const Rule& r = rules.at(i);
        if (r.managed)
            return i;
        if (bandBaseForSection(RuleModel::sectionFor(r)) < band)
            return i;
    }
    return rules.size();
}

QVariantMap RuleController::newEmptyRule(const QString& subject) const
{
    return RuleTemplates::newEmptyRule(subject);
}

QVariantList RuleController::ruleTemplates() const
{
    return RuleTemplates::ruleTemplates();
}

QVariantMap RuleController::newRuleFromTemplate(const QString& templateId) const
{
    return RuleTemplates::newRuleFromTemplate(templateId);
}

QString RuleController::addRuleFromJson(const QVariantMap& ruleJson)
{
    // The editor sheet may hand back a map with no id (a never-stored rule).
    // Inject a fresh UUID into the JSON *before* Rule::fromJson runs —
    // fromJson rejects an id-less object outright, so generating the id after
    // the fact would silently drop the name / match / actions the user built.
    QJsonObject obj = QJsonObject::fromVariantMap(ruleJson);
    const QString idStr = obj.value(QLatin1String("id")).toString();
    if (idStr.isEmpty() || QUuid::fromString(idStr).isNull()) {
        obj.insert(QLatin1String("id"), QUuid::createUuid().toString());
    }
    const auto parsed = Rule::fromJson(obj);
    if (!parsed || !parsed->isValid()) {
        qCWarning(lcConfig) << "RuleController::addRuleFromJson: rejecting malformed rule payload";
        return QString();
    }
    // Insert at the band-seeded position so a new rule's DEFAULT precedence
    // reflects its section (Advanced high, Animation low) rather than always
    // landing at the bottom of the flat list. The user can drag it afterwards.
    if (!m_model.addRuleAt(*parsed, bandSeededInsertIndex(*parsed))) {
        qCWarning(lcConfig) << "RuleController::addRuleFromJson: model rejected rule" << parsed->id.toString()
                            << "(id collision or invalid)";
        return QString();
    }
    // Re-stamp priorities so list order maps onto evaluation order exactly as it
    // does after a drag-reorder.
    renormalizePriorities();
    setDirty(true);
    return parsed->id.toString();
}

bool RuleController::updateRuleFromJson(const QVariantMap& ruleJson)
{
    Rule rule = ruleFromVariant(ruleJson);
    if (rule.id.isNull()) {
        // ruleFromVariant() yields a null-id rule when Rule::fromJson
        // rejects the payload (malformed map, missing/invalid id).
        qCWarning(lcConfig) << "RuleController::updateRuleFromJson: rejecting malformed rule payload";
        return false;
    }
    // The baseline appearance rule is editable (the Appearance page rewrites
    // its actions) but its identity is app-owned: a save must never demote it
    // to a user rule, retarget its catch-all match, or unpin its priority.
    // Force-preserve those from the stored rule regardless of the payload.
    const Rule existing = m_model.ruleById(rule.id);
    if (existing.managed) {
        rule.managed = true;
        rule.priority = existing.priority;
        rule.match = existing.match;
    }
    const RuleModel::UpdateResult result = m_model.updateRule(rule);
    switch (result) {
    case RuleModel::UpdateResult::NotFound:
        qCWarning(lcConfig) << "RuleController::updateRuleFromJson: no rule with id" << rule.id.toString();
        return false;
    case RuleModel::UpdateResult::Unchanged:
        // A genuine no-op save — the rule is unchanged, so do not dirty the
        // page. Still report success: the caller's "save" intent succeeded.
        return true;
    case RuleModel::UpdateResult::Applied:
        setDirty(true);
        return true;
    case RuleModel::UpdateResult::AppliedSectionChanged:
        // The edit reclassified the rule's section (e.g. adding an animation
        // action moves a Monitor rule to Animation). Section no longer feeds
        // priority (only list position does) and the in-place edit leaves the
        // row where it was, so this re-stamp is normally a no-op. It is kept to
        // re-sync PriorityRole with model row order in case an earlier explicit
        // priority edit (the Applied path, which does not renormalize) had
        // diverged the two. A same-section edit keeps any explicit priority
        // verbatim.
        renormalizePriorities();
        setDirty(true);
        return true;
    }
    return false;
}

bool RuleController::removeRule(const QString& ruleId)
{
    const QUuid id = QUuid::fromString(ruleId);
    if (id.isNull()) {
        // Distinguish "you sent garbage" from "rule doesn't exist" — both
        // return false but the garbage path deserves a warning so a buggy
        // caller doesn't silently fail in production.
        if (!ruleId.isEmpty()) {
            qCWarning(lcConfig) << "RuleController::removeRule: invalid UUID:" << ruleId;
        }
        return false;
    }
    if (!m_model.removeRule(id)) {
        return false;
    }
    setDirty(true);
    return true;
}

QString RuleController::duplicateRule(const QString& ruleId)
{
    const Rule source = m_model.ruleById(QUuid::fromString(ruleId));
    if (source.id.isNull()) {
        return QString();
    }
    Rule clone = source;
    clone.id = QUuid::createUuid();
    // A duplicate of the baseline appearance rule is an ordinary user rule —
    // the managed flag (non-deletable, pinned priority) does not carry over.
    clone.managed = false;
    // Auto-suffix the name when the source has one so the two rules don't
    // share an identical label in the list. An empty source name stays empty
    // — the matchSummary will distinguish the clone in that case (and the
    // user typically renames immediately on duplicate anyway).
    if (!source.name.isEmpty()) {
        // Walk existing rules ONCE into a name → present set, then
        // probe candidate names against the set so duplicating into a
        // list with M existing duplicates is O(N + M) instead of
        // O(N · M) (the prior shape walked all N rules per candidate
        // probe). Three duplicates of the same source still land as
        // "X (copy)", "X (copy) 2", "X (copy) 3".
        QSet<QString> takenNames;
        const auto& existing = m_model.rules();
        takenNames.reserve(existing.size());
        for (const Rule& r : existing)
            takenNames.insert(r.name);
        const QString base = PhosphorI18n::tr("%1 (copy)").arg(source.name);
        QString candidate = base;
        int n = 2;
        while (takenNames.contains(candidate))
            candidate = base + QStringLiteral(" %1").arg(n++);
        clone.name = candidate;
    }
    // Locate the source's slot ONCE and insert the clone directly into
    // sourceIndex+1 via addRuleAt — the old shape fired addRule (append)
    // + up to two moveRule() calls + a final renormalize, producing
    // four model signals per Duplicate click. Now one beginInsertRows/
    // endInsertRows pair carries everything.
    int sourceIndex = -1;
    const auto& rules = m_model.rules();
    for (int i = 0; i < rules.size(); ++i) {
        if (rules.at(i).id == source.id) {
            sourceIndex = i;
            break;
        }
    }
    // Source vanished between ruleById() and this lookup (concurrent
    // teardown / race) — bail out instead of inserting at a guessed
    // index that could land in the wrong section.
    if (sourceIndex < 0) {
        qCWarning(lcConfig) << "RuleController::duplicateRule: source rule disappeared during clone"
                            << source.id.toString();
        return QString();
    }
    if (!m_model.addRuleAt(clone, sourceIndex + 1)) {
        qCWarning(lcConfig) << "RuleController::duplicateRule: model rejected clone" << clone.id.toString();
        return QString();
    }
    // The clone inherited the source's priority verbatim. Re-stamp the global
    // list-order priorities so the clone, inserted directly after the source,
    // gets a distinct rank and evaluates immediately after it instead of tying on
    // the inherited integer. Mirrors the renormalize call addRuleFromJson does.
    renormalizePriorities();
    setDirty(true);
    return clone.id.toString();
}

bool RuleController::setRuleEnabled(const QString& ruleId, bool enabled)
{
    Rule rule = m_model.ruleById(QUuid::fromString(ruleId));
    if (rule.id.isNull()) {
        return false;
    }
    if (rule.enabled == enabled) {
        // No-op: the rule is already in the requested state. Report success so
        // a QML caller toggling a switch into its current state doesn't surface
        // a spurious "save failed" toast.
        return true;
    }
    rule.enabled = enabled;
    // The enabled-flag flip is guaranteed a real change (guarded above), so
    // anything other than Applied is a model-level failure.
    if (m_model.updateRule(rule) != RuleModel::UpdateResult::Applied) {
        return false;
    }
    setDirty(true);
    return true;
}

bool RuleController::moveRule(const QString& ruleId, const QString& beforeRuleId)
{
    // Capture order BEFORE the move so a no-op move (drop a rule on its
    // own slot, or the slot immediately after — the model accepts both
    // and returns true) doesn't flip the dirty flag. The model has its
    // own no-op short-circuit but reports success either way; the user-
    // visible "drag-drop back to where it started" should never pollute
    // the dirty state.
    QList<QUuid> before;
    before.reserve(m_model.rules().size());
    for (const Rule& r : m_model.rules()) {
        before.append(r.id);
    }
    if (!m_model.moveRule(QUuid::fromString(ruleId), QUuid::fromString(beforeRuleId))) {
        return false;
    }
    bool actuallyMoved = false;
    const auto& after = m_model.rules();
    if (after.size() != before.size()) {
        actuallyMoved = true;
    } else {
        for (int i = 0; i < after.size(); ++i) {
            if (after.at(i).id != before.at(i)) {
                actuallyMoved = true;
                break;
            }
        }
    }
    if (!actuallyMoved) {
        return true;
    }
    renormalizePriorities();
    setDirty(true);
    return true;
}

QVariantMap RuleController::ruleJson(const QString& ruleId) const
{
    const Rule rule = m_model.ruleById(QUuid::fromString(ruleId));
    return rule.id.isNull() ? QVariantMap{} : rule.toJson().toVariantMap();
}

} // namespace PlasmaZones
