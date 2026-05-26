// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "phosphorsettingsui_export.h"

namespace PhosphorSettingsUi {

class PageController;

/**
 * Holds the catalogue of pages an application has registered.
 *
 * Pages are organised as a tree: each entry has an optional parentId.
 * Top-level pages (parentId empty) are the entries the sidebar root
 * displays; child pages are listed under their parent for drill-down.
 *
 * The registry stores raw weak references to PageController objects —
 * ownership lives with whoever constructed them (typically the
 * ApplicationController owning a parent QObject chain). Registry entries
 * are never moved or removed at runtime; the catalogue is built at
 * application start and is read-only thereafter. Dynamic page registration
 * (post-startup `registerPage` calls) is supported and fires
 * `pageRegistered`; dynamic removal is intentionally NOT — apps that need
 * plugin-style hot-unload should rebuild the controller.
 */
class PHOSPHORSETTINGSUI_EXPORT PageRegistry : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PageRegistry)
    QML_UNCREATABLE("PageRegistry is owned by ApplicationController.")

public:
    struct Entry
    {
        QString id;
        QString parentId; // empty == top-level
        QString title; // already translated by caller
        QString iconSource; // freedesktop icon name or QML asset URL; optional
        QUrl qmlSource; // page QML file URL
        PageController* controller = nullptr;
        /// When true, the Sidebar renders this entry as an inline-expandable
        /// category header rather than a drill-down target — its children
        /// appear indented under it (toggleable) instead of replacing the
        /// list. Useful for shallow grouping ("Display", "Rules", `*-cat`
        /// buckets) inside an otherwise drill-down sidebar.
        bool isCollapsible = false;
        /// When true, the Sidebar draws a horizontal divider line in the
        /// row slot immediately after this entry. Used to break up the
        /// vertical rhythm of long sidebars without grouping items under
        /// a category header. Suppressed while a search filter is active
        /// — dividers are navigation ornament, not match metadata.
        bool hasDividerAfter = false;
    };

    explicit PageRegistry(QObject* parent = nullptr);
    ~PageRegistry() override;

    /** Register a page. Emits `pageRegistered(id)` on success. Warns
     *  and silently skips the entry on any of: empty id, duplicate id,
     *  unknown parentId, null controller. The intent is to surface
     *  programmer errors in the log without aborting startup; the
     *  PageRegistry's tree just won't contain the misconfigured
     *  entry — downstream Sidebar / Breadcrumbs / page-router lookups
     *  for it will return empty. */
    void registerPage(Entry entry);

    Q_INVOKABLE bool hasPage(const QString& id) const;
    Q_INVOKABLE PhosphorSettingsUi::PageController* controller(const QString& id) const;
    Entry entry(const QString& id) const;

    QList<Entry> topLevelPages() const;
    QList<Entry> childPages(const QString& parentId) const;
    QList<Entry> allPages() const;

    // QML-facing accessors. Return dicts with keys: id, parentId, title,
    // iconSource, qmlSource. Used by Sidebar.qml + Breadcrumbs.qml to drive
    // their Repeaters without needing a custom QAbstractListModel.
    Q_INVOKABLE QVariantList topLevelPagesData() const;
    Q_INVOKABLE QVariantList childPagesData(const QString& parentId) const;
    Q_INVOKABLE QVariantMap pageData(const QString& id) const;

Q_SIGNALS:
    void pageRegistered(const QString& id);

private:
    QList<Entry> m_pages;
    QHash<QString, int> m_indexById;
};

} // namespace PhosphorSettingsUi
