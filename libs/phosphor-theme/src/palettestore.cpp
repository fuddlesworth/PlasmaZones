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
#include <QTimer>

namespace PhosphorTheme {

PaletteStore::PaletteStore(QObject* parent)
    : QObject(parent)
    , m_palette(detail::defaultDarkPalette())
    , m_watcher(std::make_unique<QFileSystemWatcher>(this))
    , m_reloadDebounce(std::make_unique<QTimer>(this))
{
    // Hot-reload debounce. In-place editors issue truncate-then-write.
    // The truncate phase fires fileChanged before the body lands. A
    // naive immediate reload then sees an empty or partial file. That
    // reload emits loadError and the user is greeted by a false-positive
    // error flash. Coalescing all events inside a short window collapses
    // the truncate-plus-write pair into one reload of the final content.
    // 80 ms is long enough to cover a typical editor write. It is also
    // short enough that hot-reload still feels live.
    m_reloadDebounce->setSingleShot(true);
    m_reloadDebounce->setInterval(80);
    connect(m_reloadDebounce.get(), &QTimer::timeout, this, &PaletteStore::reloadFromCurrentPath);

    // Two events drive a reload:
    //
    //   fileChanged       Editor writes in place (truncate + rewrite). The
    //                     watch survives because the inode is the same;
    //                     just re-read.
    //
    //   directoryChanged  Editor atomically replaces the file via
    //                     temp-file + rename (vim's default, emacs's
    //                     backup-by-rename, our own writeAtomic via
    //                     QSaveFile). The unlink + create cycle drops the
    //                     fileChanged watch silently because its tracked
    //                     inode is gone. The directory watch fires AFTER
    //                     the new inode appears, at which point we can
    //                     re-arm the file watch and reload.
    //
    // Watching just the file path is therefore not sufficient on its own;
    // we always pair it with a parent-directory watch.
    connect(m_watcher.get(), &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
        if (QFileInfo::exists(path) && !m_watcher->files().contains(path)) {
            m_watcher->addPath(path);
        }
        m_reloadDebounce->start();
    });
    connect(m_watcher.get(), &QFileSystemWatcher::directoryChanged, this, [this](const QString&) {
        if (m_sourcePath.isEmpty()) {
            return;
        }
        if (QFileInfo::exists(m_sourcePath)) {
            if (!m_watcher->files().contains(m_sourcePath)) {
                m_watcher->addPath(m_sourcePath);
            }
            m_reloadDebounce->start();
        }
    });
}

PaletteStore::~PaletteStore() = default;

QVariantMap PaletteStore::defaultPalette()
{
    return detail::defaultDarkPalette();
}

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
    // Public entry point: caller passed a raw blob, so drop the watched
    // source per the header contract (sourcePath empty after a JSON-blob
    // load).
    return parseAndApplyJson(json, /*dropWatchedSource=*/true);
}

bool PaletteStore::parseAndApplyJson(const QByteArray& json, bool dropWatchedSource)
{
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    const auto root = doc.object();

    // Two accepted layouts:
    //   1. `{ "tokens": { "primary": "#RRGGBB", ... } }`, full document with
    //      room for metadata alongside the tokens (matugen-style output).
    //   2. `{ "primary": "#RRGGBB", ... }`, flat top-level map (hand-edited
    //      palette files where the wrapper feels like noise).
    // Layout 1 takes precedence if `tokens` is present.
    QJsonObject tokens;
    if (root.contains(QLatin1String("tokens"))) {
        // Caller signalled the wrapped layout. If the value isn't an
        // object the caller's intent is unambiguous but malformed. Fail
        // explicitly instead of silently treating sibling metadata keys
        // as tokens, which would change the palette in a surprising way.
        if (!root.value(QLatin1String("tokens")).isObject()) {
            return false;
        }
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

    if (dropWatchedSource) {
        // Drop the watched source so the documented contract holds:
        // after a public loadFromJson the palette is sourced from an
        // in-process blob, not a watched file, and a later on-disk edit
        // on a previously-watched path must NOT clobber the just-
        // applied tokens. reloadFromCurrentPath is the only caller that
        // passes false here so a successful hot-reload doesn't disarm
        // the watcher it was just triggered by.
        if (!m_watcher->files().isEmpty()) {
            m_watcher->removePaths(m_watcher->files());
        }
        if (!m_watcher->directories().isEmpty()) {
            m_watcher->removePaths(m_watcher->directories());
        }
        if (!m_sourcePath.isEmpty()) {
            m_sourcePath.clear();
            Q_EMIT sourcePathChanged();
        }
    }

    applyPalette(parsed);
    return true;
}

bool PaletteStore::loadFromFile(const QString& path)
{
    // Canonicalise once at the boundary. The watcher resolves relative
    // paths against CWD at watch-time, but reloadFromCurrentPath would
    // resolve m_sourcePath against CWD at reload-time. Any CWD change
    // between the load and a later reload (Qt FileDialogs, plugins,
    // KDE-service-launched daemons) would make the two resolve to
    // different files. Storing the absolute path closes that gap.
    const QString absolutePath = QFileInfo(path).absoluteFilePath();

    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Q_EMIT loadError(absolutePath, file.errorString());
        return false;
    }
    const auto data = file.readAll();
    file.close();

    // Bypass the public loadFromJson entry so the watcher-drop side
    // effect doesn't fire here: loadFromFile is the explicit "watch
    // this path" branch and arms the watcher below. Calling
    // loadFromJson would drop the just-armed watcher on a subsequent
    // load (and emit a spurious sourcePathChanged-to-empty).
    if (!parseAndApplyJson(data, /*dropWatchedSource=*/false)) {
        Q_EMIT loadError(absolutePath, QStringLiteral("invalid JSON or empty token map"));
        return false;
    }

    // Drop any pending debounce from the previous source so a queued
    // tick can't fire a redundant reload of the file we just loaded
    // synchronously.
    m_reloadDebounce->stop();

    // Replace any previous watch so the store never accumulates stale
    // paths across `loadFromFile` calls. Pair the file watch with a
    // parent-directory watch so atomic-rename saves still trigger a
    // reload after the unlink + create cycle drops the file watch
    // (see the constructor's directoryChanged handler).
    if (!m_watcher->files().isEmpty()) {
        m_watcher->removePaths(m_watcher->files());
    }
    if (!m_watcher->directories().isEmpty()) {
        m_watcher->removePaths(m_watcher->directories());
    }
    m_watcher->addPath(absolutePath);
    const QString parentDir = QFileInfo(absolutePath).absolutePath();
    if (!parentDir.isEmpty()) {
        m_watcher->addPath(parentDir);
    }

    if (m_sourcePath != absolutePath) {
        m_sourcePath = absolutePath;
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
    // The header contract is "stops any active filesystem watch". That
    // covers both the file watch and its paired parent-directory watch.
    // Leaking the directory watch would hold an inotify slot forever
    // even after the user dropped back to defaults.
    if (!m_watcher->files().isEmpty()) {
        m_watcher->removePaths(m_watcher->files());
    }
    if (!m_watcher->directories().isEmpty()) {
        m_watcher->removePaths(m_watcher->directories());
    }
    m_reloadDebounce->stop();
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
    // Merge over the current palette: tokens absent from the new payload
    // keep their previous value. This is the documented behaviour from
    // IThemeService::loadFromJson: missing tokens don't fall back to the
    // built-in default, because mid-session theme edits should preserve
    // any manual overrides the user had layered on top.
    //
    // Normalise every incoming value to a QColor before storing. QML and
    // matugen callers may hand us QString hex codes or QVariant(QColor);
    // collapsing both into a single QColor representation means token()
    // and palette[name] reads can rely on a uniform type. Values that
    // don't convert are dropped: a non-color in the palette would corrupt
    // downstream bindings worse than its absence.
    bool changed = false;
    for (auto it = tokens.constBegin(); it != tokens.constEnd(); ++it) {
        QColor c;
        if (it.value().userType() == QMetaType::QColor) {
            c = it.value().value<QColor>();
        } else if (it.value().canConvert<QString>()) {
            c = QColor(it.value().toString());
        }
        if (!c.isValid()) {
            continue;
        }
        const QVariant normalised = QVariant::fromValue(c);
        const auto existing = m_palette.constFind(it.key());
        if (existing == m_palette.constEnd() || existing.value() != normalised) {
            m_palette.insert(it.key(), normalised);
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
