// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ProfileStore's DIFF surface: turning a profile's sparse config delta and
// rule delta into the humanized, per-leaf rows the Profiles page renders.
// Split from profilestore.cpp (CRUD + inheritance resolution) by concern —
// nothing here touches disk; it only walks records the store half loaded.

#include "profilestore.h"

#include "config/configkeys.h"
#include "config/settingsvaluelabels.h"
#include "phosphor_i18n.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QVariantMap>

namespace PlasmaZones {

namespace {

/// A trigger ({modifier, mouseButton}) is a leaf, not a subtree: recursing into
/// it would surface raw Qt bitmasks, where the pair together reads as "Alt +
/// Right" once QML resolves it.
bool isTriggerObject(const QJsonObject& object)
{
    return object.contains(ConfigKeys::triggerModifierField())
        || object.contains(ConfigKeys::triggerMouseButtonField());
}

/// Label for one array element: prefer an identifying field the element already
/// carries (a profile-tree override names its animation path) so the row reads
/// as that entry rather than as a position, falling back to a 1-based index.
QString arraySegmentLabel(const QJsonValue& element, int index, QString* identityKey)
{
    if (element.isObject()) {
        const QJsonObject object = element.toObject();
        for (const QLatin1String field : {QLatin1String("path"), QLatin1String("id"), QLatin1String("name")}) {
            const QString value = object.value(field).toString();
            if (!value.isEmpty()) {
                *identityKey = field;
                return value;
            }
        }
    }
    identityKey->clear();
    return QString::number(index + 1);
}

} // namespace

QString ProfileStore::humanizeKey(const QString& key)
{
    // camelCase / PascalCase → spaced sentence case: "borderWidth" becomes
    // "Border width", so a delta row reads as prose rather than as a config key.
    QString out;
    out.reserve(key.size() + 4);
    for (int i = 0; i < key.size(); ++i) {
        const QChar c = key.at(i);
        if (i == 0) {
            out.append(c.toUpper());
        } else if (c.isUpper() && !key.at(i - 1).isUpper()) {
            out.append(QLatin1Char(' '));
            out.append(c.toLower());
        } else {
            out.append(c);
        }
    }
    return out;
}

QStringList ProfileStore::humanizeGroupSegments(const QString& group)
{
    // A dot-path group becomes one segment per level, each humanized:
    // "Snapping.Behavior.ZoneSpan" → ["Snapping", "Behavior", "Zone span"].
    // Segments rather than a joined breadcrumb, so the view can nest rows that
    // share a prefix under one parent instead of repeating (and eliding) the
    // whole path on every row.
    const QStringList parts = group.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    QStringList humanized;
    humanized.reserve(parts.size());
    for (const QString& part : parts) {
        humanized.append(humanizeKey(part));
    }
    return humanized;
}

void ProfileStore::appendLeafRows(const QString& rawGroup, const QString& rawKey, const QStringList& segments,
                                  const QVariantList& rawPath, const QJsonValue& before, const QJsonValue& after,
                                  int depth, QVariantList& rows, const QString& identityKey)
{
    // An unchanged subtree contributes nothing, which is what keeps the row
    // count proportional to the actual change rather than to the tree's size.
    if (before == after) {
        return;
    }

    // Depth cap: the schema bounds these shapes, but the files are hand-editable
    // and a pathological nesting should degrade to one row, not recurse forever.
    constexpr int maxDepth = 6;
    const bool structured = before.isObject() || after.isObject();
    const bool listed = before.isArray() || after.isArray();
    const bool trigger = (before.isObject() && isTriggerObject(before.toObject()))
        || (after.isObject() && isTriggerObject(after.toObject()));

    if (depth < maxDepth && structured && !trigger) {
        QStringList keys = before.toObject().keys();
        for (const QString& key : after.toObject().keys()) {
            if (!keys.contains(key)) {
                keys.append(key);
            }
        }
        keys.sort();
        for (const QString& key : keys) {
            // The identifying field already names this row; repeating it as a
            // leaf would report "window.move › Path: window.move".
            if (key == identityKey) {
                continue;
            }
            QVariantMap step;
            step.insert(QStringLiteral("key"), key);
            appendLeafRows(rawGroup, rawKey, segments + QStringList{humanizeKey(key)}, rawPath + QVariantList{step},
                           before.toObject().value(key), after.toObject().value(key), depth + 1, rows);
        }
        return;
    }

    if (depth < maxDepth && listed) {
        const QJsonArray beforeItems = before.toArray();
        const QJsonArray afterItems = after.toArray();
        // Pair elements by their identifying field, position only as the
        // fallback — the same axis the revert step addresses by. Positional
        // pairing would misreport a reordered array as pairs of unrelated
        // entries changing into each other.
        QList<bool> beforeClaimed;
        beforeClaimed.resize(beforeItems.size());
        for (int i = 0; i < afterItems.size(); ++i) {
            const QJsonValue afterItem = afterItems.at(i);
            QString identity;
            const QString segment = arraySegmentLabel(afterItem, i, &identity);
            int beforeIndex = -1;
            if (!identity.isEmpty()) {
                const QJsonValue identityValue = afterItem.toObject().value(identity);
                for (int b = 0; b < beforeItems.size(); ++b) {
                    if (!beforeClaimed.at(b) && beforeItems.at(b).toObject().value(identity) == identityValue) {
                        beforeIndex = b;
                        break;
                    }
                }
            } else if (i < beforeItems.size() && !beforeClaimed.at(i)) {
                beforeIndex = i;
            }
            const QJsonValue beforeItem = beforeIndex >= 0 ? beforeItems.at(beforeIndex) : QJsonValue();
            if (beforeIndex >= 0) {
                beforeClaimed[beforeIndex] = true;
            }
            QVariantMap step;
            if (!identity.isEmpty()) {
                step.insert(QStringLiteral("identityKey"), identity);
                step.insert(QStringLiteral("identityValue"), afterItem.toObject().value(identity).toVariant());
                // Tiebreak for a hand-edited file carrying two elements with
                // the same identity value: the revert prefers this position.
                step.insert(QStringLiteral("index"), i);
            } else {
                step.insert(QStringLiteral("index"), i);
            }
            appendLeafRows(rawGroup, rawKey, segments + QStringList{segment}, rawPath + QVariantList{step}, beforeItem,
                           afterItem, depth + 1, rows, identity);
        }
        // Elements only the parent carries (this profile dropped them).
        for (int b = 0; b < beforeItems.size(); ++b) {
            if (beforeClaimed.at(b)) {
                continue;
            }
            const QJsonValue beforeItem = beforeItems.at(b);
            QString identity;
            const QString segment = arraySegmentLabel(beforeItem, b, &identity);
            QVariantMap step;
            if (!identity.isEmpty()) {
                step.insert(QStringLiteral("identityKey"), identity);
                step.insert(QStringLiteral("identityValue"), beforeItem.toObject().value(identity).toVariant());
            } else {
                step.insert(QStringLiteral("index"), b);
            }
            appendLeafRows(rawGroup, rawKey, segments + QStringList{segment}, rawPath + QVariantList{step}, beforeItem,
                           QJsonValue(), depth + 1, rows, identity);
        }
        return;
    }

    QVariantMap row;
    row.insert(QStringLiteral("segments"), segments);
    row.insert(QStringLiteral("before"), before.toVariant());
    row.insert(QStringLiteral("after"), after.toVariant());
    // Machine address of this leaf inside the profile's delta, so the view's
    // per-row Revert can hand the row straight back to revertConfigChange.
    row.insert(QStringLiteral("rawGroup"), rawGroup);
    row.insert(QStringLiteral("rawKey"), rawKey);
    row.insert(QStringLiteral("rawPath"), rawPath);
    // How to present this value. `kind` tells the view which values it must
    // resolve itself against live state (a layout id, a connected monitor);
    // the *Text fields carry what this side could already resolve — an enum's
    // word, a number's unit — and are empty when it could not.
    const ValueDescriptor descriptor = SettingsValueLabels::descriptorFor(rawGroup, rawKey);
    row.insert(QStringLiteral("kind"), SettingsValueLabels::kindName(descriptor.kind));
    row.insert(QStringLiteral("beforeText"),
               SettingsValueLabels::displayText(rawGroup, rawKey, before.toVariant(), descriptor));
    row.insert(QStringLiteral("afterText"),
               SettingsValueLabels::displayText(rawGroup, rawKey, after.toVariant(), descriptor));
    rows.append(row);
}

QVariantList ProfileStore::configChanges(const QString& id) const
{
    const QHash<QUuid, Record> all = loadAll();
    const QUuid uid(id);
    if (!all.contains(uid)) {
        return {};
    }
    const Record& rec = all.value(uid);
    // The stored delta IS the set of overridden keys; the parent-resolved blob
    // supplies what each was before this profile changed it.
    const QUuid parent = rec.parent;
    const QJsonObject parentResolved = parent.isNull() || !all.contains(parent)
        ? (m_config.defaultConfig ? m_config.defaultConfig() : QJsonObject())
        : resolveConfig(parent, all);

    QVariantList rows;
    for (auto git = rec.configDelta.constBegin(); git != rec.configDelta.constEnd(); ++git) {
        const QJsonObject group = git.value().toObject();
        const QJsonObject baseGroup = parentResolved.value(git.key()).toObject();
        for (auto kit = group.constBegin(); kit != group.constEnd(); ++kit) {
            appendLeafRows(git.key(), kit.key(), humanizeGroupSegments(git.key()) + QStringList{humanizeKey(kit.key())},
                           QVariantList(), baseGroup.value(kit.key()), kit.value(), 0, rows);
        }
    }
    return rows;
}

QVariantList ProfileStore::ruleChanges(const QString& id) const
{
    const QHash<QUuid, Record> all = loadAll();
    const QUuid uid(id);
    if (!all.contains(uid)) {
        return {};
    }
    const Record& rec = all.value(uid);
    const QUuid parent = rec.parent;
    const QList<PhosphorRules::Rule> parentRules =
        parent.isNull() || !all.contains(parent) ? QList<PhosphorRules::Rule>() : resolveRules(parent, all);
    QHash<QUuid, PhosphorRules::Rule> parentById;
    for (const PhosphorRules::Rule& rule : parentRules) {
        parentById.insert(rule.id, rule);
    }

    // Title rules the way the Rules page does (unnamed and auto-stamped
    // context rules read as their match summary), with a generic last resort
    // so a row can never render blank.
    const auto titleOf = [this](const PhosphorRules::Rule& rule) {
        const QString title = m_config.ruleTitle ? m_config.ruleTitle(rule) : rule.name;
        return title.isEmpty() ? PhosphorI18n::tr("Unnamed rule") : title;
    };

    QVariantList rows;
    for (const PhosphorRules::Rule& rule : rec.ruleUpserts) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), rule.id.toString());
        row.insert(QStringLiteral("name"), titleOf(rule));
        row.insert(QStringLiteral("change"),
                   parentById.contains(rule.id) ? QStringLiteral("changed") : QStringLiteral("added"));
        rows.append(row);
    }
    for (const QUuid& removed : rec.ruleRemovedIds) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), removed.toString());
        // Name the rule as the PARENT knows it — this profile no longer carries it.
        row.insert(QStringLiteral("name"),
                   parentById.contains(removed) ? titleOf(parentById.value(removed)) : removed.toString());
        row.insert(QStringLiteral("change"), QStringLiteral("removed"));
        rows.append(row);
    }
    return rows;
}

} // namespace PlasmaZones
