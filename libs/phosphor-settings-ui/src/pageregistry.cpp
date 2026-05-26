// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorSettingsUi/PageRegistry.h"

#include <QDebug>

#include "PhosphorSettingsUi/PageController.h"

namespace PhosphorSettingsUi {

PageRegistry::PageRegistry(QObject* parent)
    : QObject(parent)
{
}

PageRegistry::~PageRegistry() = default;

void PageRegistry::registerPage(Entry entry)
{
    if (entry.id.isEmpty()) {
        qWarning() << "PageRegistry::registerPage: refusing to register page with empty id";
        return;
    }
    if (m_indexById.contains(entry.id)) {
        qWarning() << "PageRegistry::registerPage: duplicate page id" << entry.id << "— ignoring registration";
        return;
    }
    if (!entry.parentId.isEmpty() && !m_indexById.contains(entry.parentId)) {
        qWarning() << "PageRegistry::registerPage: page" << entry.id << "references unknown parent" << entry.parentId
                   << "— ignoring registration";
        return;
    }

    const QString id = entry.id;
    m_indexById.insert(id, m_pages.size());
    m_pages.append(std::move(entry));
    Q_EMIT pageRegistered(id);
}

bool PageRegistry::hasPage(const QString& id) const
{
    return m_indexById.contains(id);
}

PageController* PageRegistry::controller(const QString& id) const
{
    const auto it = m_indexById.constFind(id);
    if (it == m_indexById.constEnd()) {
        return nullptr;
    }
    return m_pages.at(it.value()).controller;
}

PageRegistry::Entry PageRegistry::entry(const QString& id) const
{
    const auto it = m_indexById.constFind(id);
    if (it == m_indexById.constEnd()) {
        return {};
    }
    return m_pages.at(it.value());
}

QList<PageRegistry::Entry> PageRegistry::topLevelPages() const
{
    QList<Entry> out;
    out.reserve(m_pages.size());
    for (const Entry& e : m_pages) {
        if (e.parentId.isEmpty()) {
            out.append(e);
        }
    }
    return out;
}

QList<PageRegistry::Entry> PageRegistry::childPages(const QString& parentId) const
{
    QList<Entry> out;
    for (const Entry& e : m_pages) {
        if (e.parentId == parentId) {
            out.append(e);
        }
    }
    return out;
}

QList<PageRegistry::Entry> PageRegistry::allPages() const
{
    return m_pages;
}

namespace {
QVariantMap entryToVariant(const PageRegistry::Entry& e)
{
    QVariantMap m;
    m.insert(QStringLiteral("id"), e.id);
    m.insert(QStringLiteral("parentId"), e.parentId);
    m.insert(QStringLiteral("title"), e.title);
    m.insert(QStringLiteral("iconSource"), e.iconSource);
    m.insert(QStringLiteral("qmlSource"), e.qmlSource);
    return m;
}
} // namespace

QVariantList PageRegistry::topLevelPagesData() const
{
    QVariantList out;
    for (const Entry& e : m_pages) {
        if (e.parentId.isEmpty()) {
            out.append(entryToVariant(e));
        }
    }
    return out;
}

QVariantList PageRegistry::childPagesData(const QString& parentId) const
{
    QVariantList out;
    for (const Entry& e : m_pages) {
        if (e.parentId == parentId) {
            out.append(entryToVariant(e));
        }
    }
    return out;
}

QVariantMap PageRegistry::pageData(const QString& id) const
{
    const auto it = m_indexById.constFind(id);
    if (it == m_indexById.constEnd()) {
        return {};
    }
    return entryToVariant(m_pages.at(it.value()));
}

} // namespace PhosphorSettingsUi
