// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowrulecontroller.h"

#include "windowruleauthoring.h"
#include "windowruletemplates.h"

#include "../core/logging.h"
#include "../phosphor_i18n.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorWindowRules/MatchTypes.h>
#include <PhosphorWindowRules/WindowRuleSet.h>

#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QHash>
#include <QSet>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace PlasmaZones {

namespace {

using PhosphorWindowRules::WindowRule;
using PhosphorWindowRules::WindowRuleSet;

// Priority bands live next to the seeded templates (windowruletemplates.h) so
// the renormalize logic here shares the exact bands the templates /
// empty-rule seeds land in — a single source of truth keeps section ordering
// (Advanced > Monitor/Activity > Application > Animation; Monitor and Activity
// share the Context band) consistent between newly-authored rules and
// post-reorder priority stamps.
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
    : PhosphorControl::PageController(QStringLiteral("window-rules"), parent)
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
        qCWarning(lcConfig) << "WindowRuleController: failed to subscribe to org.plasmazones.WindowRules.rulesChanged "
                               "— will retry when the daemon becomes reachable";
    }
    m_rulesChangedSubscribed = subscribed;
    // Kick off the initial fetch immediately — the call is asynchronous
    // (QDBusPendingCallWatcher), so the constructor returns without
    // blocking and the settings page can finish loading. The reply
    // handler repopulates the model on the next event-loop turn.
    reload();
}

WindowRuleController::~WindowRuleController()
{
    // Mirror the `connect()` from the constructor — see subscribe
    // helper docstring above.
    unsubscribeRulesChanged();
}

WindowRuleController::RulesChangedSubscription WindowRuleController::rulesChangedSubscriptionArgs()
{
    // QLatin1String avoids the QString allocation that `QString(...)` of a
    // const-char* produced — the protocol constants are compile-time
    // literals, and the implicit-conversion to QString happens once at
    // the QDBus connect call boundary.
    return {QLatin1String(PhosphorProtocol::Service::Name), QLatin1String(PhosphorProtocol::Service::ObjectPath),
            QLatin1String(PhosphorProtocol::Service::Interface::WindowRules), QStringLiteral("rulesChanged")};
}

bool WindowRuleController::subscribeRulesChanged()
{
    const auto args = rulesChangedSubscriptionArgs();
    return QDBusConnection::sessionBus().connect(args.service, args.objectPath, args.interface, args.signalName, this,
                                                 SLOT(reload()));
}

void WindowRuleController::unsubscribeRulesChanged()
{
    const auto args = rulesChangedSubscriptionArgs();
    QDBusConnection::sessionBus().disconnect(args.service, args.objectPath, args.interface, args.signalName, this,
                                             SLOT(reload()));
    m_rulesChangedSubscribed = false;
}

// Label-lookup setters (setScreenLookup, setActivityLookup,
// setSnappingLayoutLookup, setTilingAlgorithmLookup, setShaderEffectLookup,
// setCurveLabelResolver) live in windowrulecontroller_lookups.cpp so this TU
// stays under the project's 800-line cap.

bool WindowRuleController::isDirty() const
{
    return m_dirty;
}

void WindowRuleController::apply()
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

void WindowRuleController::discard()
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
    // pattern leaked when ~WindowRuleController ran before
    // revertFinished arrived). The companion destroyed() guard would
    // be redundant here: the lambda's receiver IS `this`, so Qt
    // auto-removes the connection on `this` destruction.
    connect(
        this, &WindowRuleController::revertFinished, this,
        [this](bool success) {
            m_discardInFlight = false;
            Q_EMIT discardResult(success,
                                 success ? QString() : PhosphorI18n::tr("Failed to fetch the daemon's rule set."));
        },
        Qt::SingleShotConnection);
    revert();
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
    // Retry the rulesChanged subscription if a previous attempt failed.
    // Covers the "controller built before the daemon was up" scenario:
    // the initial subscribe() in the ctor returned false, but once the
    // daemon arrives the next setDaemonReachable(true) reattaches the
    // signal so subsequent daemon-side writes refresh the page.
    if (reachable && !m_rulesChangedSubscribed) {
        if (subscribeRulesChanged()) {
            m_rulesChangedSubscribed = true;
            qCInfo(lcConfig)
                << "WindowRuleController: rulesChanged subscription attached after daemon became reachable";
        }
    }
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

bool WindowRuleController::pushToDaemonAsync(const QList<WindowRule>& rules)
{
    // Same up-front validation as the sync path — bail out BEFORE
    // dispatching so a partial-drop never reaches the daemon.
    WindowRuleSet set;
    const int accepted = set.setRules(rules);
    if (accepted != rules.size()) {
        qCWarning(lcConfig) << "WindowRuleController::pushToDaemonAsync: rejecting push —" << (rules.size() - accepted)
                            << "of" << rules.size() << "rules failed client-side validation; daemon NOT updated";
        return false;
    }
    const QJsonDocument doc(set.toJson());
    auto* watcher = new QDBusPendingCallWatcher(
        PhosphorProtocol::ClientHelpers::asyncCall(QString(PhosphorProtocol::Service::Interface::WindowRules),
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
            qCWarning(lcConfig) << "WindowRuleController::pushToDaemonAsync: setAllRules failed —"
                                << reply.error().message();
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
        setDirty(false);
        setDaemonChangedWhileDirty(false);
        Q_EMIT applyResult(true, QString());
    });
    return true;
}

void WindowRuleController::fetchAndLoad(bool fromRevert)
{
    // Asynchronous `getAllRules` — the reply handler repopulates the model.
    // Bound to `this` for parent, so a controller teardown before the reply
    // arrives cancels the watcher cleanly (no dangling lambda).
    auto* watcher = new QDBusPendingCallWatcher(
        PhosphorProtocol::ClientHelpers::asyncCall(QString(PhosphorProtocol::Service::Interface::WindowRules),
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
        bool jsonOk = true;
        if (!json.isEmpty()) {
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                set = WindowRuleSet::fromJson(doc.object()).value_or(WindowRuleSet{});
            } else {
                // Malformed JSON — daemon answered but with a corrupt store.
                // Do NOT stomp the existing model with an empty set: a single
                // corrupt reply could otherwise wipe the user's view of their
                // rules. Treat as transient transport error parallel to
                // reply.isError() above so the model stays intact and the
                // user can retry.
                qCWarning(lcConfig) << "WindowRuleController::fetchAndLoad: malformed daemon JSON —"
                                    << err.errorString();
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
        // it so the page can warn the user before a save silently
        // overwrites the daemon-side changes (lost update).
        setDaemonChangedWhileDirty(true);
        return;
    }
    fetchAndLoad();
}

void WindowRuleController::asyncCommit(bool force)
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
        qCWarning(lcConfig) << "WindowRuleController::asyncCommit: refusing to push — daemon rules changed while the "
                               "page had staged edits; review or force-overwrite required";
        Q_EMIT applyResult(false,
                           PhosphorI18n::tr("The daemon's window rules changed while you were editing. Review or use "
                                            "Save anyway to overwrite."));
        return;
    }
    if (!pushToDaemonAsync(m_model.rules())) {
        // The up-front client validation failed (a rule was rejected
        // before any wire dispatch). pushToDaemonAsync already logged a
        // diagnostic; surface a user-readable error so the chrome can
        // toast / banner the failure rather than leaving the page in a
        // silent-fail "still dirty" state.
        Q_EMIT applyResult(
            false,
            PhosphorI18n::tr("One or more window rules failed validation and could not be saved. See the "
                             "log for details."));
    }
    // pushToDaemonAsync's QDBusPendingCallWatcher emits applyResult on
    // the reply (success or transport error) — nothing to do here.
}

void WindowRuleController::revert()
{
    // Discard staged edits by re-fetching the daemon's authoritative set —
    // bypass reload()'s dirty guard. fetchAndLoad() is async; the reply
    // handler clears the dirty bit only on success. A failed re-fetch keeps
    // the page dirty (and `daemonReachable` false) rather than silently
    // dropping the staged edits.
    //
    // Tag this fetch explicitly as fromRevert=true so the reply handler
    // emits revertFinished. SettingsController::load() listens once so
    // it can re-add the "window-rules" entry to the dirty-pages set on
    // a failed revert (its surrounding setNeedsSave(false) blanket-clears
    // every page unconditionally). The prior counter-based tagging was
    // spoofable by a concurrent daemon broadcast: a reload() arriving
    // mid-revert would read m_pendingRevertFetches > 0 and tag itself
    // as fromRevert, emitting a spurious revertFinished(true).
    fetchAndLoad(/*fromRevert=*/true);
}

void WindowRuleController::renormalizePriorities()
{
    // Walk the model's list order and re-stamp priority so list order maps
    // monotonically onto evaluation order. Higher list index ⇒ lower
    // priority within the rule's band; the bands keep the section ordering
    // (Advanced > Monitor/Activity > Application > Animation; Monitor and
    // Activity share the Context band) stable.
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
    const QUuid id = QUuid::fromString(ruleId);
    if (id.isNull()) {
        // Distinguish "you sent garbage" from "rule doesn't exist" — both
        // return false but the garbage path deserves a warning so a buggy
        // caller doesn't silently fail in production.
        if (!ruleId.isEmpty()) {
            qCWarning(lcConfig) << "WindowRuleController::removeRule: invalid UUID:" << ruleId;
        }
        return false;
    }
    if (!m_model.removeRule(id)) {
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
        // Walk existing rules ONCE into a name → present set, then
        // probe candidate names against the set so duplicating into a
        // list with M existing duplicates is O(N + M) instead of
        // O(N · M) (the prior shape walked all N rules per candidate
        // probe). Three duplicates of the same source still land as
        // "X (copy)", "X (copy) 2", "X (copy) 3".
        QSet<QString> takenNames;
        const auto& existing = m_model.rules();
        takenNames.reserve(existing.size());
        for (const WindowRule& r : existing)
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
        qCWarning(lcConfig) << "WindowRuleController::duplicateRule: source rule disappeared during clone"
                            << source.id.toString();
        return QString();
    }
    if (!m_model.addRuleAt(clone, sourceIndex + 1)) {
        qCWarning(lcConfig) << "WindowRuleController::duplicateRule: model rejected clone" << clone.id.toString();
        return QString();
    }
    // The clone inherited the source's priority verbatim; without
    // re-stamping the two would collide on the band-base value and the
    // daemon's section-ordering would treat them as indistinguishable.
    // Mirrors the renormalize call addRuleFromJson does after appending.
    renormalizePriorities();
    setDirty(true);
    return clone.id.toString();
}

bool WindowRuleController::setRuleEnabled(const QString& ruleId, bool enabled)
{
    WindowRule rule = m_model.ruleById(QUuid::fromString(ruleId));
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
    if (m_model.updateRule(rule) != WindowRuleModel::UpdateResult::Applied) {
        return false;
    }
    setDirty(true);
    return true;
}

bool WindowRuleController::moveRule(const QString& ruleId, const QString& beforeRuleId)
{
    // Capture order BEFORE the move so a no-op move (drop a rule on its
    // own slot, or the slot immediately after — the model accepts both
    // and returns true) doesn't flip the dirty flag. The model has its
    // own no-op short-circuit but reports success either way; the user-
    // visible "drag-drop back to where it started" should never pollute
    // the dirty state.
    QList<QUuid> before;
    before.reserve(m_model.rules().size());
    for (const WindowRule& r : m_model.rules()) {
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

QVariantMap WindowRuleController::ruleJson(const QString& ruleId) const
{
    const WindowRule rule = m_model.ruleById(QUuid::fromString(ruleId));
    return rule.id.isNull() ? QVariantMap{} : rule.toJson().toVariantMap();
}

} // namespace PlasmaZones
