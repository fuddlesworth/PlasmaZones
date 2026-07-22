// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTest>

#include "config/configdefaults.h"

namespace PlasmaZones {

/// Shared fixture for the v3 → v4 migration test split. Each per-concern
/// test QObject derives from this base to inherit the config/rules JSON I/O
/// helpers, the v3 config + assignments builders, and the rule-introspection
/// predicates the assertions rely on. Group-specific builders (animation,
/// exclusion, zone-rename) stay with their own test target.
class MigrationV3V4Fixture
{
protected:
    void writeJson(const QString& path, const QJsonObject& obj)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        const QByteArray bytes = QJsonDocument(obj).toJson();
        QCOMPARE(f.write(bytes), static_cast<qint64>(bytes.size()));
    }

    QJsonObject readJson(const QString& path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QJsonDocument::fromJson(f.readAll()).object();
    }

    /// The legacy assignments.json path. ConfigDefaults no longer exposes it
    /// (rules.json supersedes it in v4) — it sits beside rules.json
    /// in the same plasmazones config directory.
    static QString assignmentsPath()
    {
        return QFileInfo(ConfigDefaults::rulesFilePath()).absolutePath() + QStringLiteral("/assignments.json");
    }

    /// A v3 config.json carrying per-mode disable lists + a global default
    /// snapping layout.
    QJsonObject makeV3Config()
    {
        QJsonObject root;
        root.insert(QStringLiteral("_version"), 3);

        QJsonObject display;
        display.insert(QStringLiteral("SnappingDisabledMonitors"), QStringLiteral("DP-3"));
        display.insert(QStringLiteral("AutotileDisabledMonitors"), QStringLiteral("DP-3,HDMI-2"));
        display.insert(QStringLiteral("SnappingDisabledDesktops"), QStringLiteral("DP-1/4"));
        display.insert(QStringLiteral("AutotileDisabledActivities"), QStringLiteral("DP-1/act-uuid-7"));
        root.insert(QStringLiteral("Display"), display);

        // Global default snapping layout — feeds the gated default resolver
        // (no provider-default rule is emitted).
        QJsonObject windowHandling;
        windowHandling.insert(QStringLiteral("DefaultLayoutId"),
                              QStringLiteral("{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}"));
        QJsonObject behavior;
        behavior.insert(QStringLiteral("WindowHandling"), windowHandling);
        QJsonObject snapping;
        snapping.insert(QStringLiteral("Behavior"), behavior);
        root.insert(QStringLiteral("Snapping"), snapping);

        return root;
    }

    /// A v3 assignments.json fixture exercising every cascade level.
    QJsonObject makeAssignments()
    {
        QJsonObject root;

        // Exact: screen + desktop + activity, autotile mode.
        QJsonObject exact;
        exact.insert(QStringLiteral("Mode"), 1); // Autotile
        exact.insert(QStringLiteral("SnappingLayout"), QStringLiteral("{snap-exact}"));
        exact.insert(QStringLiteral("TilingAlgorithm"), QStringLiteral("dwindle"));
        root.insert(QStringLiteral("Assignment:DP-2:Desktop:2:Activity:work-uuid"), exact);

        // Screen + activity, snapping mode.
        QJsonObject scrAct;
        scrAct.insert(QStringLiteral("Mode"), 0); // Snapping
        scrAct.insert(QStringLiteral("SnappingLayout"), QStringLiteral("{snap-act}"));
        scrAct.insert(QStringLiteral("TilingAlgorithm"), QString());
        root.insert(QStringLiteral("Assignment:DP-2:Activity:play-uuid"), scrAct);

        // Screen + desktop, snapping mode.
        QJsonObject scrDesk;
        scrDesk.insert(QStringLiteral("Mode"), 0);
        scrDesk.insert(QStringLiteral("SnappingLayout"), QStringLiteral("{snap-desk}"));
        scrDesk.insert(QStringLiteral("TilingAlgorithm"), QString());
        root.insert(QStringLiteral("Assignment:DP-2:Desktop:3"), scrDesk);

        // Screen only (display default), autotile mode-only (empty tiling algo).
        QJsonObject scrOnly;
        scrOnly.insert(QStringLiteral("Mode"), 1); // Autotile, default algorithm
        scrOnly.insert(QStringLiteral("SnappingLayout"), QString());
        scrOnly.insert(QStringLiteral("TilingAlgorithm"), QString());
        root.insert(QStringLiteral("Assignment:DP-2"), scrOnly);

        // QuickLayouts — NOT a rule.
        QJsonObject quick;
        quick.insert(QStringLiteral("3"), QStringLiteral("{quick-layout-id}"));
        root.insert(QStringLiteral("QuickLayouts"), quick);

        return root;
    }

    QJsonArray rulesFromRules()
    {
        const QJsonObject root = readJson(ConfigDefaults::rulesFilePath());
        return root.value(QStringLiteral("rules")).toArray();
    }

    QList<QJsonObject> allRulesByPriority(const QJsonArray& rules, int priority)
    {
        QList<QJsonObject> out;
        for (const QJsonValue& v : rules) {
            const QJsonObject r = v.toObject();
            if (r.value(QStringLiteral("priority")).toInt() == priority) {
                out.append(r);
            }
        }
        return out;
    }

    // An assignment rule sets an engine mode and does NOT disable an engine.
    // Disable rules are all seeded at the Context band base (300) while
    // assignment rules sit just above it (301..306), so they never share a
    // priority; this predicate still lets a priority-keyed lookup target the
    // assignment rule unambiguously.
    bool isAssignmentRule(const QJsonObject& rule)
    {
        const QStringList types = actionTypes(rule);
        return types.contains(QLatin1String("setEngineMode")) && !types.contains(QLatin1String("disableEngine"));
    }

    bool hasAssignmentAtPriority(const QJsonArray& rules, int priority)
    {
        for (const QJsonObject& r : allRulesByPriority(rules, priority)) {
            if (isAssignmentRule(r)) {
                return true;
            }
        }
        return false;
    }

    // Returns the assignment rule at @p priority, skipping any disable rule.
    // A bare first-match-by-priority lookup would return whichever rule the
    // migration happened to emit first — an order dependency the assertion
    // should not rely on.
    QJsonObject findAssignmentRuleByPriority(const QJsonArray& rules, int priority)
    {
        for (const QJsonObject& r : allRulesByPriority(rules, priority)) {
            if (isAssignmentRule(r)) {
                return r;
            }
        }
        return {};
    }

    /// Collect the action `type` strings of a rule.
    QStringList actionTypes(const QJsonObject& rule)
    {
        QStringList types;
        for (const QJsonValue& v : rule.value(QStringLiteral("actions")).toArray()) {
            types.append(v.toObject().value(QStringLiteral("type")).toString());
        }
        types.sort();
        return types;
    }

    /// Flatten a rule's match expression to its leaf objects. A bare leaf
    /// match yields a one-element list; an All{} match yields its children.
    QList<QJsonObject> matchLeaves(const QJsonObject& rule)
    {
        const QJsonObject match = rule.value(QStringLiteral("match")).toObject();
        QList<QJsonObject> leaves;
        if (match.contains(QStringLiteral("field"))) {
            leaves.append(match); // a bare equality leaf
        } else if (match.contains(QStringLiteral("all"))) {
            for (const QJsonValue& v : match.value(QStringLiteral("all")).toArray()) {
                leaves.append(v.toObject());
            }
        }
        return leaves;
    }

    /// The string value of the `field == equals` leaf for @p field, or empty
    /// if the rule's match carries no such leaf.
    QString matchLeafValue(const QJsonObject& rule, const QString& field)
    {
        return matchLeafValueByOp(rule, field, QStringLiteral("equals"));
    }

    /// Locate the first leaf with `field == @p field` AND `op == @p op`, and
    /// return its `value` as a string. Generalises @ref matchLeafValue (which
    /// is hard-wired to `equals` for the assignment / disable rules) to the
    /// animation-rule case where the matcher is `contains`.
    QString matchLeafValueByOp(const QJsonObject& rule, const QString& field, const QString& op)
    {
        for (const QJsonObject& leaf : matchLeaves(rule)) {
            if (leaf.value(QStringLiteral("field")).toString() == field
                && leaf.value(QStringLiteral("op")).toString() == op) {
                return leaf.value(QStringLiteral("value")).toVariant().toString();
            }
        }
        return QString();
    }

    /// The `mode` token of a rule's single `disableEngine` action, or empty
    /// if the rule carries no disable action.
    QString disableActionMode(const QJsonObject& rule)
    {
        for (const QJsonValue& v : rule.value(QStringLiteral("actions")).toArray()) {
            const QJsonObject a = v.toObject();
            if (a.value(QStringLiteral("type")).toString() == QLatin1String("disableEngine")) {
                return a.value(QStringLiteral("mode")).toString();
            }
        }
        return QString();
    }

    /// All rules carrying a `disableEngine` action.
    QList<QJsonObject> disableRules(const QJsonArray& rules)
    {
        QList<QJsonObject> out;
        for (const QJsonValue& v : rules) {
            const QJsonObject r = v.toObject();
            if (!disableActionMode(r).isEmpty()) {
                out.append(r);
            }
        }
        return out;
    }
};

} // namespace PlasmaZones
