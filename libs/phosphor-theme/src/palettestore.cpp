// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTheme/PaletteStore.h>

#include "defaultpalette.h"

#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1String>
#include <QString>
#include <QStringLiteral>

namespace PhosphorTheme {

PaletteStore::PaletteStore(QObject* parent)
    : QObject(parent)
    , m_palette(detail::defaultDarkPalette())
    , m_watcher(std::make_unique<QFileSystemWatcher>(this))
{
    // Some editors (vim/emacs default save flow) atomically replace the
    // file: write to a temp + rename, which fires `fileChanged` once and
    // then the watch is gone because the inode it tracked has been
    // unlinked. Re-arm via the directory watch as a fallback so the next
    // save still hot-reloads.
    connect(m_watcher.get(), &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
        reloadFromCurrentPath();
        // Re-add the path — after atomic-rename saves the watch
        // pointed at a now-deleted inode. QFileSystemWatcher
        // silently drops it; addPath rebinds to the new inode.
        if (QFileInfo::exists(path) && !m_watcher->files().contains(path)) {
            m_watcher->addPath(path);
        }
    });
}

PaletteStore::~PaletteStore() = default;

QVariantMap PaletteStore::palette() const
{
    return m_palette;
}

QColor PaletteStore::token(const QString& name) const
{
    const auto it = m_palette.constFind(name);
    if (it == m_palette.constEnd()) {
        return QColor();
    }
    return it.value().value<QColor>();
}

bool PaletteStore::loadFromJson(const QByteArray& json)
{
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    const auto root = doc.object();

    // Two accepted layouts:
    //   1. `{ "tokens": { "primary": "#RRGGBB", ... } }` — full document with
    //      room for metadata alongside the tokens (matugen-style output).
    //   2. `{ "primary": "#RRGGBB", ... }` — flat top-level map (hand-edited
    //      palette files where the wrapper feels like noise).
    // Layout 1 takes precedence if `tokens` is present.
    QJsonObject tokens;
    if (root.contains(QLatin1String("tokens")) && root.value(QLatin1String("tokens")).isObject()) {
        tokens = root.value(QLatin1String("tokens")).toObject();
    } else {
        tokens = root;
    }

    QVariantMap parsed;
    for (auto it = tokens.constBegin(); it != tokens.constEnd(); ++it) {
        const auto& val = it.value();
        if (!val.isString()) {
            // Tokens are colors; non-strings are either matugen metadata
            // we don't care about (the "$schema" / "version" keys) or
            // user mistakes we can't usefully repair. Silently skip.
            continue;
        }
        QColor c(val.toString());
        if (!c.isValid()) {
            continue;
        }
        parsed.insert(it.key(), c);
    }

    if (parsed.isEmpty()) {
        return false;
    }

    applyPalette(parsed);
    return true;
}

bool PaletteStore::loadFromFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Q_EMIT loadError(path, file.errorString());
        return false;
    }
    const auto data = file.readAll();
    file.close();

    if (!loadFromJson(data)) {
        Q_EMIT loadError(path, QStringLiteral("invalid JSON or empty token map"));
        return false;
    }

    // Bind the watcher to this file. Replacing any previous watch means
    // the store never accumulates stale paths across `loadFromFile` calls.
    if (!m_watcher->files().isEmpty()) {
        m_watcher->removePaths(m_watcher->files());
    }
    if (!m_watcher->directories().isEmpty()) {
        m_watcher->removePaths(m_watcher->directories());
    }
    m_watcher->addPath(path);

    if (m_sourcePath != path) {
        m_sourcePath = path;
        Q_EMIT sourcePathChanged();
    }
    return true;
}

void PaletteStore::applyTokens(const QVariantMap& tokens)
{
    if (tokens.isEmpty()) {
        return;
    }
    applyPalette(tokens);
}

void PaletteStore::resetToDefaults()
{
    if (!m_watcher->files().isEmpty()) {
        m_watcher->removePaths(m_watcher->files());
    }
    if (!m_sourcePath.isEmpty()) {
        m_sourcePath.clear();
        Q_EMIT sourcePathChanged();
    }
    applyPalette(detail::defaultDarkPalette());
}

QString PaletteStore::sourcePath() const
{
    return m_sourcePath;
}

void PaletteStore::applyPalette(const QVariantMap& tokens)
{
    // Merge over the current palette — tokens absent from the new payload
    // keep their previous value. This is the documented behaviour from
    // IThemeService::loadFromJson: missing tokens don't fall back to the
    // built-in default, because mid-session theme edits should preserve
    // any manual overrides the user had layered on top.
    bool changed = false;
    for (auto it = tokens.constBegin(); it != tokens.constEnd(); ++it) {
        const auto existing = m_palette.constFind(it.key());
        if (existing == m_palette.constEnd() || existing.value() != it.value()) {
            m_palette.insert(it.key(), it.value());
            changed = true;
        }
    }
    if (changed) {
        Q_EMIT paletteChanged();
    }
}

void PaletteStore::reloadFromCurrentPath()
{
    if (m_sourcePath.isEmpty()) {
        return;
    }
    QFile file(m_sourcePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Q_EMIT loadError(m_sourcePath, file.errorString());
        return;
    }
    const auto data = file.readAll();
    file.close();
    if (!loadFromJson(data)) {
        Q_EMIT loadError(m_sourcePath, QStringLiteral("invalid JSON or empty token map"));
    }
}

} // namespace PhosphorTheme
