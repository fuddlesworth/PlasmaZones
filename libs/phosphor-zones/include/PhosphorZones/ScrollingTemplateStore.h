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

    /// The data-location subdirectory templates live in, relative to
    /// GenericDataLocation. The single authority for the spelling; UI code
    /// that opens the folder must use this rather than inlining the path.
    static QString templateSubdirectory();

    /// Absolute path of the USER template directory (GenericDataLocation +
    /// templateSubdirectory()). Static because it reads nothing off the store:
    /// UI code that has to open or create the folder can call it without an
    /// instance, and must, rather than re-joining the two parts itself.
    static QString userTemplateDirectory();

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
    /// is invalid after normalization or the write fails. A save that would
    /// store a byte-identical entry writes nothing and emits nothing, and
    /// still returns the stored id. Writes to &lt;id&gt;.json in the user
    /// directory and retires every other user file declaring the same id, so a
    /// hand-placed duplicate cannot win the next rescan and revert the save.
    /// Returns the stored id.
    QUuid saveTemplate(ScrollingTemplate templ);

    /// Delete the USER files for @p id. Both candidates are removed: the
    /// entry's own sourcePath when that path lies inside the user directory (a
    /// hand-placed user file need not be named after its id) and the id-derived
    /// path, since those can be two different files carrying the same id.
    /// A bundled (system) template shadowed by a user file resurfaces on the
    /// rescan this triggers; a template whose loaded entry came from a system
    /// location is refused (returns false) — the UI disables delete for those.
    /// Returns true when no user file for @p id remains afterwards, which
    /// includes the case where the files were already gone: the requested
    /// state holds either way. Returns false when a file survives (a remove
    /// that failed, which is logged) and when there was no user file to target
    /// at all, since nothing was deleted and the entry is still visible.
    bool removeTemplate(const QUuid& id);

    /// Store a copy of @p id under a fresh UUID with @p newName (or a
    /// "name (Copy)" derivative when empty). Returns the new id, null on
    /// failure.
    QUuid duplicateTemplate(const QUuid& id, const QString& newName = QString());

Q_SIGNALS:
    /// The loaded template set changed (load, save, remove, duplicate).
    void templatesChanged();

private:
    QString userTemplateFilePath(const QUuid& id) const;
    bool writeTemplateFile(const ScrollingTemplate& templ);

    QHash<QUuid, ScrollingTemplate> m_templates;
};

} // namespace PhosphorZones
