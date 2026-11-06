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
    /// The page's section anchors, used by search and deep links. Every anchor
    /// is also a place other modules can add a section: `before:<anchor>` and
    /// `after:<anchor>`.
    QStringList anchors;
};

/// A named place on one of the module's pages that other modules may add a
/// section to, beyond the implicit `top`, `bottom`, `before:<anchor>` and
/// `after:<anchor>` slots every page has. The reserved id `anchors` declares
/// the version of a page's implicit slots instead.
struct PHOSPHORCONTROL_EXPORT FacetExtensionPointMetadata
{
    /// The module's local page id.
    QString page;
    QString id;
    /// Bumped whenever the slot moves or its meaning changes, so extensions
    /// built for the old one are skipped rather than shown in the wrong place.
    int version = 1;
};

/// A section this module adds to another module's page.
struct PHOSPHORCONTROL_EXPORT FacetExtensionMetadata
{
    /// Local to the module. Facet knows it as `<module>:<id>`.
    QString id;
    /// The target page, `<module>:<page>`.
    QString target;
    /// `top`, `bottom`, `before:<anchor>`, `after:<anchor>`, or the id of one
    /// of the target page's extension points.
    QString slot;
    /// The version of the slot the extension was built for.
    int targetVersion = 1;
    QString name;
    QStringList keywords;
    int order = 0;
};

/// A static search target on one of the module's pages: a setting or section
/// at one of the page's anchors. Facet indexes these from the metadata, so
/// search finds them before the module loads.
struct PHOSPHORCONTROL_EXPORT FacetSearchEntryMetadata
{
    /// The module's local page id.
    QString page;
    /// One of that page's anchors.
    QString anchor;
    /// `"Kind": "Section"`; a setting otherwise.
    bool section = false;
    QString title;
    QStringList keywords;
    /// The row shows only in advanced mode (SearchEntry::advancedOnly).
    bool advancedOnly = false;
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
///                  "Parent": "", "Keywords": ["blur"], "Anchors": ["blur"]}],
///       "ExtensionPoints": [{"Page": "effect", "Id": "outline", "Version": 1}],
///       "Extensions": [{"Id": "snap-assist", "Target": "placement:snapping/behavior",
///                       "Slot": "after:drag-modifier", "TargetVersion": 1,
///                       "Name": "KWin snap assist", "Keywords": ["assist"], "Order": 10}],
///       "Search": [{"Page": "effect", "Anchor": "blur", "Kind": "Setting", "Title": "Blur",
///                   "Keywords": ["frost"], "AdvancedOnly": false}],
///       "SearchProviders": false
///     }
///
/// `Name`, `Description`, `Keywords` and a search entry's `Title` may carry
/// `[<locale>]` variants (`Name[de]`, `Name[pt_BR]`), generated from the
/// module's translations at build time by phosphor_add_facet_module().
/// parse() picks the best one for the locale. Anchors match
/// `[A-Za-z0-9][A-Za-z0-9._-]*`, and a search entry's anchor must be one its
/// page lists. `SearchProviders` says the module registers dynamic search
/// providers, so Facet loads it the first time the user searches.
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
    QList<FacetExtensionPointMetadata> extensionPoints;
    QList<FacetExtensionMetadata> extensions;
    QList<FacetSearchEntryMetadata> search;
    bool searchProviders = false;

    /// Parse @p json for @p locale. Every problem is appended to @p errors;
    /// the metadata is usable only when none was added.
    static FacetModuleMetadata parse(const QJsonObject& json, const QLocale& locale, QStringList* errors);

    /// The page with local id @p id, or nullptr.
    [[nodiscard]] const FacetPageMetadata* page(const QString& id) const;
    /// The extension with local id @p id, or nullptr.
    [[nodiscard]] const FacetExtensionMetadata* extension(const QString& id) const;

    /// The version slot @p slot of the local page @p pageId offers, or 0
    /// when the page has no such slot. `top` and `bottom` always exist,
    /// `before:<a>` and `after:<a>` exist for each of the page's anchors, and
    /// all of them carry the version of the page's `anchors` extension point
    /// (1 when it declares none). A named slot carries its own version.
    [[nodiscard]] int slotVersion(const QString& pageId, const QString& slot) const;
};

} // namespace PhosphorControl
