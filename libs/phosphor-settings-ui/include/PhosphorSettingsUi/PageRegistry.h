// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

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
 * application start and is read-only thereafter.
 */
class PHOSPHORSETTINGSUI_EXPORT PageRegistry : public QObject
{
    Q_OBJECT

public:
    struct Entry
    {
        QString id;
        QString parentId; // empty == top-level
        QString title; // already translated by caller
        QString iconSource; // freedesktop icon name or QML asset URL; optional
        QUrl qmlSource; // page QML file URL
        PageController* controller = nullptr;
    };

    explicit PageRegistry(QObject* parent = nullptr);
    ~PageRegistry() override;

    /** Register a page. Asserts that id is unique and that parentId, if
     *  non-empty, refers to a previously-registered page. */
    void registerPage(Entry entry);

    bool hasPage(const QString& id) const;
    PageController* controller(const QString& id) const;
    Entry entry(const QString& id) const;

    QList<Entry> topLevelPages() const;
    QList<Entry> childPages(const QString& parentId) const;
    QList<Entry> allPages() const;

Q_SIGNALS:
    void pageRegistered(const QString& id);

private:
    QList<Entry> m_pages;
    QHash<QString, int> m_indexById;
};

} // namespace PhosphorSettingsUi
