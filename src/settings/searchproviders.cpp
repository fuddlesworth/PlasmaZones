// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "searchproviders.h"

#include "phosphor_i18n.h"
#include "settingscontroller.h"
#include "windowrulecontroller.h"
#include "windowrulemodel.h"

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

    const QString breadcrumb = PhosphorI18n::tr("Layouts");
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
        e.subtitle = breadcrumb;
        e.icon = QStringLiteral("view-grid-symbolic");
        out.push_back(e);
    }
    return out;
}

QVector<SearchEntry> WindowRulesSearchProvider::searchEntries() const
{
    QVector<SearchEntry> out;
    if (m_controller == nullptr) {
        return out;
    }
    WindowRuleController* page = m_controller->windowRulesPage();
    if (page == nullptr) {
        return out;
    }
    WindowRuleModel* model = page->model();
    if (model == nullptr) {
        return out;
    }

    const QString breadcrumb = PhosphorI18n::tr("Window Rules");
    const int rows = model->rowCount();
    out.reserve(rows);
    for (int i = 0; i < rows; ++i) {
        const QModelIndex idx = model->index(i);
        const QString name = model->data(idx, WindowRuleModel::NameRole).toString();
        if (name.isEmpty()) {
            continue;
        }

        SearchEntry e;
        e.kind = SearchEntry::Kind::Entity;
        e.pageId = QStringLiteral("window-rules");
        e.title = name;
        const QString summary = model->data(idx, WindowRuleModel::MatchSummaryRole).toString();
        e.subtitle = summary.isEmpty() ? breadcrumb : summary;
        e.icon = QStringLiteral("window-symbolic");
        out.push_back(e);
    }
    return out;
}

} // namespace PlasmaZones
