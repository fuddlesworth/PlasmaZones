// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ProfileStore's REVERT surface: the write-side dual of profilestore_diff.cpp.
// The diff half enumerates a profile's overrides as addressed rows; this half
// consumes one such row and surgically restores the parent's value at its
// address. Split from profilestore.cpp (CRUD + inheritance resolution) by
// concern, the same way the diff surface is.

#include "profilestore.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>

namespace PlasmaZones {

QJsonValue ProfileStore::revertValueAtPath(const QJsonValue& current, const QJsonValue& parentValue,
                                           const QVariantList& path, int index)
{
    if (index >= path.size()) {
        // The addressed leaf itself: the parent's value IS the revert target
        // (Undefined when the parent has no value either, which the caller
        // turns into a removal).
        return parentValue;
    }

    const QVariantMap step = path.at(index).toMap();

    if (step.contains(QStringLiteral("key"))) {
        const QString key = step.value(QStringLiteral("key")).toString();
        QJsonObject obj = current.toObject();
        const QJsonValue parentChild =
            parentValue.isObject() ? parentValue.toObject().value(key) : QJsonValue(QJsonValue::Undefined);
        if (!obj.contains(key)) {
            // The profile dropped this whole branch (its leaves all read
            // "Unset"): restore the parent's branch whole rather than
            // resurrecting a fragment of it around one leaf.
            if (parentChild.isUndefined()) {
                return current;
            }
            obj.insert(key, parentChild);
            return obj;
        }
        const QJsonValue child = revertValueAtPath(obj.value(key), parentChild, path, index + 1);
        if (child.isUndefined()) {
            obj.remove(key);
        } else {
            obj.insert(key, child);
        }
        // A container emptied by the revert only stays when the parent also
        // carries one here; otherwise it is a leftover husk, not an override.
        if (obj.isEmpty() && !parentValue.isObject()) {
            return QJsonValue(QJsonValue::Undefined);
        }
        return obj;
    }

    // Array step: locate the element in BOTH arrays — by its identifying field
    // when it has one (positions shift as siblings come and go), by position
    // otherwise (mirroring how the rows were paired for display).
    QJsonArray arr = current.toArray();
    const QJsonArray parentArr = parentValue.isArray() ? parentValue.toArray() : QJsonArray();
    int pos = -1;
    int parentPos = -1;
    if (step.contains(QStringLiteral("identityKey"))) {
        const QString field = step.value(QStringLiteral("identityKey")).toString();
        const QJsonValue identity = QJsonValue::fromVariant(step.value(QStringLiteral("identityValue")));
        // The recorded position breaks ties between elements sharing one
        // identity value (only possible in a hand-edited file): prefer the
        // match at the row's own index, fall back to the first match.
        const int preferred = step.contains(QStringLiteral("index")) ? step.value(QStringLiteral("index")).toInt() : -1;
        const auto findIn = [&](const QJsonArray& a, int preferredIndex) {
            int first = -1;
            for (int i = 0; i < a.size(); ++i) {
                if (a.at(i).toObject().value(field) == identity) {
                    if (i == preferredIndex) {
                        return i;
                    }
                    if (first < 0) {
                        first = i;
                    }
                }
            }
            return first;
        };
        pos = findIn(arr, preferred);
        parentPos = findIn(parentArr, -1);
    } else {
        const int i = step.value(QStringLiteral("index")).toInt();
        pos = i < arr.size() ? i : -1;
        parentPos = i < parentArr.size() ? i : -1;
    }

    const QJsonValue parentElem = parentPos >= 0 ? parentArr.at(parentPos) : QJsonValue(QJsonValue::Undefined);
    if (pos < 0) {
        // Element missing from the profile's value: restore the parent's whole
        // element (same whole-branch rule as the object case).
        if (!parentElem.isUndefined()) {
            arr.insert(qMin(parentPos, arr.size()), parentElem);
        }
        return arr;
    }
    QJsonValue element = revertValueAtPath(arr.at(pos), parentElem, path, index + 1);
    if (!element.isUndefined() && parentElem.isUndefined() && step.contains(QStringLiteral("identityKey"))) {
        // The revert stripped everything but the element's identifying field
        // (the parent has no such element to fall back to): an entry that only
        // names itself overrides nothing, so it leaves with its last value.
        const QJsonObject remaining = element.toObject();
        const QString field = step.value(QStringLiteral("identityKey")).toString();
        if (remaining.isEmpty() || (remaining.size() == 1 && remaining.contains(field))) {
            element = QJsonValue(QJsonValue::Undefined);
        }
    }
    if (element.isUndefined()) {
        arr.removeAt(pos);
    } else {
        arr.replace(pos, element);
    }
    // Same husk rule as the object case: an emptied array only stays when the
    // parent carries one here.
    if (arr.isEmpty() && !parentValue.isArray()) {
        return QJsonValue(QJsonValue::Undefined);
    }
    return arr;
}

bool ProfileStore::revertConfigChange(const QString& id, const QVariantMap& change)
{
    QHash<QUuid, Record> all = loadAll();
    const QUuid uid(id);
    if (!all.contains(uid)) {
        return false;
    }
    Record rec = all.value(uid);

    const QString group = change.value(QStringLiteral("rawGroup")).toString();
    const QString key = change.value(QStringLiteral("rawKey")).toString();
    if (!rec.configDelta.contains(group)) {
        return false;
    }
    QJsonObject deltaGroup = rec.configDelta.value(group).toObject();
    if (!deltaGroup.contains(key)) {
        return false;
    }

    // The same parent-resolved baseline configChanges diffed against, so the
    // revert lands exactly on the value the row's FROM pill showed.
    const QUuid parent = rec.parent;
    const QJsonObject parentResolved = parent.isNull() || !all.contains(parent)
        ? (m_config.defaultConfig ? m_config.defaultConfig() : QJsonObject())
        : resolveConfig(parent, all);
    const QJsonValue parentValue = parentResolved.value(group).toObject().value(key);

    const QJsonValue reverted =
        revertValueAtPath(deltaGroup.value(key), parentValue, change.value(QStringLiteral("rawPath")).toList(), 0);
    if (reverted.isUndefined() || reverted == parentValue) {
        // Nothing about this key differs from the parent any more: the delta
        // stays sparse, so the key (and an emptied group) leaves it entirely.
        deltaGroup.remove(key);
    } else {
        deltaGroup.insert(key, reverted);
    }
    if (deltaGroup.isEmpty()) {
        rec.configDelta.remove(group);
    } else {
        rec.configDelta.insert(group, deltaGroup);
    }

    if (!writeProfileRecord(rec)) {
        return false;
    }
    notifyProfilesChanged();
    return true;
}

bool ProfileStore::revertRuleChange(const QString& id, const QString& ruleId)
{
    QHash<QUuid, Record> all = loadAll();
    const QUuid uid(id);
    const QUuid rid(ruleId);
    if (!all.contains(uid) || rid.isNull()) {
        return false;
    }
    Record rec = all.value(uid);

    bool touched = false;
    for (int i = 0; i < rec.ruleUpserts.size(); ++i) {
        if (rec.ruleUpserts.at(i).id == rid) {
            rec.ruleUpserts.removeAt(i);
            touched = true;
            break;
        }
    }
    if (touched) {
        // An ADDED rule (no parent version to fall back to) also leaves the
        // stored order, so the file carries no dead id. A CHANGED rule keeps
        // its slot — the parent's version takes it back over.
        const QUuid parent = rec.parent;
        const QList<PhosphorRules::Rule> parentRules =
            parent.isNull() || !all.contains(parent) ? QList<PhosphorRules::Rule>() : resolveRules(parent, all);
        const bool parentHasRule =
            std::any_of(parentRules.constBegin(), parentRules.constEnd(), [&](const PhosphorRules::Rule& r) {
                return r.id == rid;
            });
        if (!parentHasRule) {
            rec.ruleOrder.removeAll(rid);
        }
    } else if (rec.ruleRemovedIds.contains(rid)) {
        // Un-drop a parent rule. Its id is absent from the stored order (the
        // order was captured while the rule was dropped), so resolveRules
        // appends it after the ordered rules — the next capture renormalizes.
        rec.ruleRemovedIds.removeAll(rid);
        touched = true;
    }
    if (!touched) {
        return false;
    }

    if (!writeProfileRecord(rec)) {
        return false;
    }
    notifyProfilesChanged();
    return true;
}

} // namespace PlasmaZones
