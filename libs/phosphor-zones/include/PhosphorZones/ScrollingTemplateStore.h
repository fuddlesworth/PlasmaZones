// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "phosphorzones_export.h"

#include <PhosphorZones/ScrollingTemplate.h>

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QUuid>

namespace PhosphorZones {

/**
 * @brief Load/save store for scrolling templates
 *
 * The persistence peer of LayoutRegistry's layout loading, for the
 * ScrollingTemplate object: JSON files in "plasmazones/scrolling-templates"
 * under every GenericDataLocation. System directories load first and the
 * user directory last, so a user file sharing a bundled template's id
 * SHADOWS it (edit-a-bundled-template writes a user copy; deleting that
 * copy resurfaces the original). Templates get their own directory rather
 * than sharing the layouts directory, so the layout loader's sidecar
 * skip-list stays untouched.
 *
 * Mutations write the USER directory only and re-announce via
 * @ref templatesChanged. The store is process-local: the daemon owns the
 * authoritative instance behind the D-Bus CRUD verbs, and the settings/KCM
 * processes instantiate their own read view exactly as they do for the
 * local LayoutRegistry.
 */
class PHOSPHORZONES_EXPORT ScrollingTemplateStore : public QObject
{
    Q_OBJECT

public:
    explicit ScrollingTemplateStore(QObject* parent = nullptr);

    /// Rescan every data location (system first, user last). Emits
    /// templatesChanged when the loaded set differs.
    void loadTemplates();

    /// All templates, name-sorted (case-insensitive) for stable UI order.
    QList<ScrollingTemplate> templates() const;
    /// The template with @p id, or an invalid template.
    ScrollingTemplate templateById(const QUuid& id) const;
    bool contains(const QUuid& id) const
    {
        return m_templates.contains(id);
    }
    int count() const
    {
        return m_templates.size();
    }

    /// Persist @p templ (create or update). A null id is assigned a fresh
    /// UUID. Normalizes first; refuses (returns null id) when the template
    /// is invalid after normalization or the write fails. Returns the
    /// stored id.
    QUuid saveTemplate(ScrollingTemplate templ);

    /// Delete the USER file for @p id. A bundled (system) template shadowed
    /// by the user file resurfaces on the rescan this triggers; a pure
    /// system template is refused (returns false) — the UI disables delete
    /// for those.
    bool removeTemplate(const QUuid& id);

    /// Store a copy of @p id under a fresh UUID with @p newName (or a
    /// "name (copy)" derivative when empty). Returns the new id, null on
    /// failure.
    QUuid duplicateTemplate(const QUuid& id, const QString& newName = QString());

Q_SIGNALS:
    /// The loaded template set changed (load, save, remove, duplicate).
    void templatesChanged();

private:
    QString userTemplateDirectory() const;
    QString userTemplateFilePath(const QUuid& id) const;
    bool writeTemplateFile(const ScrollingTemplate& templ);

    QHash<QUuid, ScrollingTemplate> m_templates;
};

} // namespace PhosphorZones
