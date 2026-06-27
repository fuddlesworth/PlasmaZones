// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "searchproviders.h"

#include "settingscontroller.h"
#include "rulecontroller.h"
#include "rulemodel.h"

#include <QModelIndex>
#include <QVariantMap>

using PhosphorControl::SearchEntry;

namespace PlasmaZones {

QVector<SearchEntry> LayoutsSearchProvider::searchEntries() const
{
    QVector<SearchEntry> out;
    if (m_controller == nullptr) {
        return out;
    }

    const QVariantList layouts = m_controller->layouts();
    out.reserve(layouts.size());
    for (const QVariant& v : layouts) {
        const QVariantMap m = v.toMap();
        const QString name = m.value(QStringLiteral("displayName")).toString();
        if (name.isEmpty()) {
            continue;
        }

        SearchEntry e;
        e.kind = SearchEntry::Kind::Entity;
        e.pageId = QStringLiteral("layouts");
        e.title = name;
        // Per-layout reveal anchor; LayoutGridDelegate registers "layout:<id>"
        // with the page (id matches the QML modelData.id), so selecting a
        // layouts search result scrolls to + pulses that exact card and expands
        // its group. Empty id → no anchor (page-level open only).
        const QString id = m.value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) {
            e.anchor = QStringLiteral("layout:") + id;
        }
        // No subtitle: SearchController auto-derives the page breadcrumb, keeping
        // these consistent with section/setting results.
        e.icon = QStringLiteral("view-grid-symbolic");
        out.push_back(e);
    }
    return out;
}

QVector<SearchEntry> RulesSearchProvider::searchEntries() const
{
    QVector<SearchEntry> out;
    if (m_controller == nullptr) {
        return out;
    }
    RuleController* page = m_controller->rulesPage();
    if (page == nullptr) {
        return out;
    }
    RuleModel* model = page->model();
    if (model == nullptr) {
        return out;
    }

    const int rows = model->rowCount();
    out.reserve(rows);
    for (int i = 0; i < rows; ++i) {
        const QModelIndex idx = model->index(i);
        const QString name = model->data(idx, RuleModel::NameRole).toString();
        if (name.isEmpty()) {
            continue;
        }

        SearchEntry e;
        e.kind = SearchEntry::Kind::Entity;
        e.pageId = QStringLiteral("rules");
        // Per-rule reveal anchor; the rule rows register "rule:<id>" with the
        // page (id is the QUuid-with-braces from IdRole, matching the QML side).
        const QString id = model->data(idx, RuleModel::IdRole).toString();
        if (!id.isEmpty()) {
            e.anchor = QStringLiteral("rule:") + id;
        }
        e.title = name;
        // The match summary is the meaningful per-rule context; when absent, leave
        // the subtitle empty so SearchController auto-derives the page breadcrumb.
        e.subtitle = model->data(idx, RuleModel::MatchSummaryRole).toString();
        e.icon = QStringLiteral("window-symbolic");
        out.push_back(e);
    }
    return out;
}

} // namespace PlasmaZones
