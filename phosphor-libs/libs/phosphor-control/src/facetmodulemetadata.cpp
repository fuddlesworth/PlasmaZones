// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorControl/FacetModuleMetadata.h"

#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

namespace PhosphorControl {

namespace {

const QRegularExpression& moduleIdPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$"));
    return pattern;
}

// Page ids may nest with '/', but never contain ':', which separates a module
// id from a page id in a cross-module parent.
const QRegularExpression& pageIdPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._/-]*$"));
    return pattern;
}

const QRegularExpression& categoryIdPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9][a-z0-9-]*$"));
    return pattern;
}

// The value of `key` for `locale`: `key[pt_BR]`, then `key[pt]`, then `key`.
QJsonValue localized(const QJsonObject& json, const QString& key, const QLocale& locale)
{
    const QString full = locale.name();
    const QString language = full.section(QLatin1Char('_'), 0, 0);
    for (const QString& variant : {full, language}) {
        if (variant.isEmpty()) {
            continue;
        }
        const auto it = json.constFind(key + QLatin1Char('[') + variant + QLatin1Char(']'));
        if (it != json.cend()) {
            return *it;
        }
    }
    return json.value(key);
}

QStringList stringList(const QJsonValue& value)
{
    QStringList list;
    const QJsonArray array = value.toArray();
    for (const QJsonValue& item : array) {
        if (item.isString() && !item.toString().isEmpty()) {
            list.append(item.toString());
        }
    }
    return list;
}

bool isCrossModuleParent(const QString& parent)
{
    const qsizetype colon = parent.indexOf(QLatin1Char(':'));
    if (colon <= 0) {
        return false;
    }
    return moduleIdPattern().match(parent.left(colon)).hasMatch()
        && pageIdPattern().match(parent.mid(colon + 1)).hasMatch();
}

} // namespace

FacetModuleMetadata FacetModuleMetadata::parse(const QJsonObject& json, const QLocale& locale, QStringList* errors)
{
    QStringList problems;
    FacetModuleMetadata metadata;

    metadata.id = json.value(QLatin1String("Id")).toString();
    if (!moduleIdPattern().match(metadata.id).hasMatch()) {
        problems.append(QStringLiteral("\"Id\" must match [A-Za-z0-9][A-Za-z0-9._-]*"));
    }
    metadata.apiVersion = json.value(QLatin1String("ApiVersion")).toInt(0);
    metadata.name = localized(json, QStringLiteral("Name"), locale).toString();
    if (metadata.name.isEmpty()) {
        problems.append(QStringLiteral("\"Name\" is missing"));
    }
    metadata.description = localized(json, QStringLiteral("Description"), locale).toString();
    metadata.icon = json.value(QLatin1String("Icon")).toString();
    metadata.category = json.value(QLatin1String("Category")).toString();
    if (!metadata.category.isEmpty() && !categoryIdPattern().match(metadata.category).hasMatch()) {
        problems.append(QStringLiteral("\"Category\" %1 must match [a-z0-9][a-z0-9-]*").arg(metadata.category));
    }
    metadata.parent = json.value(QLatin1String("Parent")).toString();
    if (!metadata.parent.isEmpty()) {
        if (!moduleIdPattern().match(metadata.parent).hasMatch()) {
            problems.append(QStringLiteral("\"Parent\" %1 is not a module id").arg(metadata.parent));
        } else if (metadata.parent == metadata.id) {
            problems.append(QStringLiteral("\"Parent\" names the module itself"));
        }
    }
    metadata.order = json.value(QLatin1String("Order")).toInt(0);
    metadata.keywords = stringList(localized(json, QStringLiteral("Keywords"), locale));
    metadata.translationDomain = json.value(QLatin1String("TranslationDomain")).toString();

    QSet<QString> categoryIds;
    const QJsonArray categories = json.value(QLatin1String("Categories")).toArray();
    for (const QJsonValue& value : categories) {
        const QJsonObject object = value.toObject();
        FacetCategoryMetadata category;
        category.id = object.value(QLatin1String("Id")).toString();
        category.name = localized(object, QStringLiteral("Name"), locale).toString();
        category.icon = object.value(QLatin1String("Icon")).toString();
        category.order = object.value(QLatin1String("Order")).toInt(0);
        if (!categoryIdPattern().match(category.id).hasMatch()) {
            problems.append(QStringLiteral("category id %1 must match [a-z0-9][a-z0-9-]*").arg(category.id));
            continue;
        }
        if (category.name.isEmpty()) {
            problems.append(QStringLiteral("category %1 has no \"Name\"").arg(category.id));
            continue;
        }
        if (categoryIds.contains(category.id)) {
            problems.append(QStringLiteral("category %1 is defined twice").arg(category.id));
            continue;
        }
        categoryIds.insert(category.id);
        metadata.categories.append(category);
    }

    QList<FacetPageMetadata> declared;
    QSet<QString> pageIds;
    const QJsonArray pages = json.value(QLatin1String("Pages")).toArray();
    for (const QJsonValue& value : pages) {
        const QJsonObject object = value.toObject();
        FacetPageMetadata page;
        page.id = object.value(QLatin1String("Id")).toString();
        page.name = localized(object, QStringLiteral("Name"), locale).toString();
        page.icon = object.value(QLatin1String("Icon")).toString();
        page.parent = object.value(QLatin1String("Parent")).toString();
        page.keywords = stringList(localized(object, QStringLiteral("Keywords"), locale));
        if (!pageIdPattern().match(page.id).hasMatch()) {
            problems.append(QStringLiteral("page id %1 must match [A-Za-z0-9][A-Za-z0-9._/-]*").arg(page.id));
            continue;
        }
        if (page.name.isEmpty()) {
            problems.append(QStringLiteral("page %1 has no \"Name\"").arg(page.id));
            continue;
        }
        if (pageIds.contains(page.id)) {
            problems.append(QStringLiteral("page %1 is declared twice").arg(page.id));
            continue;
        }
        pageIds.insert(page.id);
        declared.append(page);
    }

    // Validate parents, then order the pages so every local parent comes
    // before its children, keeping declaration order otherwise.
    QHash<QString, QString> localParent;
    for (const FacetPageMetadata& page : std::as_const(declared)) {
        if (page.parent.isEmpty()) {
            continue;
        }
        if (page.parent.contains(QLatin1Char(':'))) {
            if (!isCrossModuleParent(page.parent)) {
                problems.append(
                    QStringLiteral("page %1 has parent %2, which is not <module>:<page>").arg(page.id, page.parent));
            }
            continue;
        }
        if (!pageIds.contains(page.parent)) {
            problems.append(
                QStringLiteral("page %1 has parent %2, which the module does not declare").arg(page.id, page.parent));
            continue;
        }
        localParent.insert(page.id, page.parent);
    }
    QSet<QString> placed;
    QSet<QString> visiting;
    bool cycle = false;
    const auto place = [&](const auto& self, const FacetPageMetadata& page) -> void {
        if (placed.contains(page.id) || cycle) {
            return;
        }
        if (visiting.contains(page.id)) {
            cycle = true;
            problems.append(QStringLiteral("page %1 is its own ancestor").arg(page.id));
            return;
        }
        visiting.insert(page.id);
        if (const auto parent = localParent.constFind(page.id); parent != localParent.cend()) {
            for (const FacetPageMetadata& candidate : std::as_const(declared)) {
                if (candidate.id == *parent) {
                    self(self, candidate);
                    break;
                }
            }
        }
        visiting.remove(page.id);
        if (!cycle && !placed.contains(page.id)) {
            placed.insert(page.id);
            metadata.pages.append(page);
        }
    };
    for (const FacetPageMetadata& page : std::as_const(declared)) {
        place(place, page);
    }

    if (errors) {
        errors->append(problems);
    }
    return metadata;
}

const FacetPageMetadata* FacetModuleMetadata::page(const QString& id) const
{
    for (const FacetPageMetadata& candidate : pages) {
        if (candidate.id == id) {
            return &candidate;
        }
    }
    return nullptr;
}

} // namespace PhosphorControl
