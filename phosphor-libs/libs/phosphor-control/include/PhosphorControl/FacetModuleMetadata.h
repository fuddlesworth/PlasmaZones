// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "phosphorcontrol_export.h"

#include <QJsonObject>
#include <QList>
#include <QLocale>
#include <QString>
#include <QStringList>

namespace PhosphorControl {

/// A top-level sidebar category a module defines.
struct PHOSPHORCONTROL_EXPORT FacetCategoryMetadata
{
    QString id;
    QString name;
    QString icon;
    int order = 0;
};

/// One page of a module, as its metadata describes it.
struct PHOSPHORCONTROL_EXPORT FacetPageMetadata
{
    /// Local to the module: `snapping` or `snapping/behavior`.
    QString id;
    QString name;
    QString icon;
    /// Empty for a page directly under the module, a local page id for a page
    /// under one of the module's own pages, or `<module>:<page>` for a page
    /// under another module's page.
    QString parent;
    QStringList keywords;
};

/// The metadata a Facet module embeds with Q_PLUGIN_METADATA:
///
///     {
///       "Id": "plasmazones", "ApiVersion": 1,
///       "Name": "KWin and Plasma", "Name[de]": "KWin und Plasma",
///       "Description": "...", "Icon": "org.phosphor_works.PlasmaZones",
///       "Category": "window-management", "Parent": "placement", "Order": 20,
///       "Keywords": ["kwin"], "TranslationDomain": "facet-module-plasmazones",
///       "Categories": [{"Id": "...", "Name": "...", "Icon": "...", "Order": 0}],
///       "Pages": [{"Id": "effect", "Name": "KWin Effect", "Icon": "...",
///                  "Parent": "", "Keywords": ["blur"]}]
///     }
///
/// `Name`, `Description` and `Keywords` may carry `[<locale>]` variants
/// (`Name[de]`, `Name[pt_BR]`), generated from the module's translations at
/// build time. parse() picks the best one for the locale.
struct PHOSPHORCONTROL_EXPORT FacetModuleMetadata
{
    QString id;
    int apiVersion = 0;
    QString name;
    QString description;
    QString icon;
    /// The category the module's node goes under. Empty means `apps`.
    QString category;
    /// Another module's id to nest this module's node under, or empty.
    QString parent;
    int order = 0;
    QStringList keywords;
    QString translationDomain;
    QList<FacetCategoryMetadata> categories;
    QList<FacetPageMetadata> pages;

    /// Parse @p json for @p locale. Every problem is appended to @p errors;
    /// the metadata is usable only when none was added.
    static FacetModuleMetadata parse(const QJsonObject& json, const QLocale& locale, QStringList* errors);

    /// The page with local id @p id, or nullptr.
    [[nodiscard]] const FacetPageMetadata* page(const QString& id) const;
};

} // namespace PhosphorControl
