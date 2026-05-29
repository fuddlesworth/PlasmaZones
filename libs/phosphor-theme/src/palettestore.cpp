// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTheme/PaletteStore.h>

#include "defaultpalette.h"

#include <QColor>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1String>
#include <QSet>
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
    //
    // Re-entry safety: loadFromFile swaps the watcher BEFORE
    // parseAndApplyJson runs, so by the time any user slot wired to
    // paletteChanged / sourcePathChanged executes, the watcher
    // already reflects the new source. Stale-watcher reads inside a
    // signal slot are therefore not possible.
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
    // applyPalette normalises every stored value to QColor before
    // insert, so a non-QColor variant here is a contract violation
    // (direct map mutation, deserialisation skipping the normaliser,
    // or a memory corruption indicator). Log instead of silently
    // returning a default QColor and hiding the bug downstream.
    if (it.value().userType() != QMetaType::QColor) {
        qWarning().noquote() << "phosphor-theme: PaletteStore::token(" << name
                             << ") expected QColor but stored variant has typeId" << it.value().userType()
                             << "; returning default QColor()";
        return QColor();
    }
    return it.value().value<QColor>();
}

// Single-line forwarder: the public JSON-blob entry drops any watched
// source per the IThemeService contract, then delegates to the shared
// parser. See parseAndApplyJson for the parse + merge logic.
bool PaletteStore::loadFromJson(const QByteArray& json)
{
    dropWatcherAndClearSourcePath();
    return parseAndApplyJson(json);
}

void PaletteStore::dropWatcherAndClearSourcePath()
{
    // The IThemeService::loadFromJson contract says the palette is
    // sourced from an in-process blob, not a watched file, after a
    // public JSON-blob load. Drop both the file and the paired
    // parent-directory watch so a later on-disk edit on a previously-
    // watched path doesn't clobber the just-applied tokens, and clear
    // m_sourcePath so QML bindings re-evaluate.
    //
    // Also stop any pending debounce so a queued reload tick can't
    // run reloadFromCurrentPath against the (now empty) source. This
    // mirrors resetToDefaults's debounce-stop and keeps the two
    // watcher-dropping paths symmetric.
    m_reloadDebounce->stop();
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

bool PaletteStore::parseAndApplyJson(const QByteArray& json)
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
    //
    // canonicalFilePath also resolves symlinks (e.g. a desktop-theme
    // pack ships theme.json as a symlink into the package data dir),
    // so the watcher tracks the file whose contents actually change
    // rather than an indirection that may be re-pointed without
    // notifying QFileSystemWatcher. canonicalFilePath returns empty
    // when the path doesn't exist; fall back to absoluteFilePath in
    // that case so the open() below still reports the user-visible
    // path in the error message.
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    const QString absolutePath = canonical.isEmpty() ? info.absoluteFilePath() : canonical;

    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Q_EMIT loadError(absolutePath, file.errorString());
        return false;
    }
    const auto data = file.readAll();
    file.close();

    // Parse + validate FIRST against a local map so a failed parse
    // doesn't observably mutate sourcePath / the watcher. The
    // previous order (set m_sourcePath, parse, roll back on failure)
    // emitted sourcePathChanged twice (new → old) for a single
    // failed load, which QML bindings would see as two transient
    // states.
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        Q_EMIT loadError(absolutePath, QStringLiteral("invalid JSON or empty token map"));
        return false;
    }

    // Commit the path BEFORE parseAndApplyJson so QML bindings that
    // observe both palette and sourcePath see a coherent transition
    // (new path → new palette). Otherwise paletteChanged would fire
    // with sourcePath still pointing at the prior source, then
    // sourcePathChanged would catch up on the next event-loop tick.
    const bool sourcePathMoved = (m_sourcePath != absolutePath);
    if (sourcePathMoved) {
        m_sourcePath = absolutePath;
    }

    // Swap the watcher BEFORE running parseAndApplyJson so any user
    // slot wired to paletteChanged that reads the watcher observes
    // the NEW state, not the stale one. Pair the file watch with a
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

    // Drop any pending debounce from the previous source so a queued
    // tick can't fire a redundant reload of the file we just loaded
    // synchronously.
    m_reloadDebounce->stop();

    if (sourcePathMoved) {
        Q_EMIT sourcePathChanged();
    }

    // Parse the (already-validated) payload. parseAndApplyJson can
    // still return false on an empty token map, in which case we
    // emit loadError but leave sourcePath + the watcher pointed at
    // the new file — the file exists, the JSON is syntactically
    // fine, the contents just had no usable tokens. That is the
    // shape a user can fix in-editor without re-triggering load,
    // so keeping the watcher armed is the right call.
    if (!parseAndApplyJson(data)) {
        Q_EMIT loadError(absolutePath, QStringLiteral("invalid JSON or empty token map"));
        return false;
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
    // Clear m_sourcePath BEFORE stopping the debounce so a future
    // refactor that reorders the watcher-drop or signal emit still
    // benefits from reloadFromCurrentPath's empty-source short-
    // circuit. The header contract is "stops any active filesystem
    // watch" — that covers both the file watch and its paired
    // parent-directory watch. Leaking the directory watch would
    // hold an inotify slot forever even after the user dropped
    // back to defaults.
    const bool sourcePathWasSet = !m_sourcePath.isEmpty();
    if (sourcePathWasSet) {
        m_sourcePath.clear();
    }
    m_reloadDebounce->stop();
    if (!m_watcher->files().isEmpty()) {
        m_watcher->removePaths(m_watcher->files());
    }
    if (!m_watcher->directories().isEmpty()) {
        m_watcher->removePaths(m_watcher->directories());
    }
    if (sourcePathWasSet) {
        Q_EMIT sourcePathChanged();
    }

    // True replace-with-defaults: drop any user-extended tokens
    // that aren't part of the canonical default set, then apply
    // the defaults. The previous implementation did
    // `m_palette.clear(); applyPalette(defaults);` which always
    // fired paletteChanged because every insert into the empty map
    // looked like a fresh change — even when resetToDefaults was
    // called on a store already holding exactly the defaults. The
    // contract is "no paletteChanged unless the palette actually
    // changed", so compute the diff honestly.
    const QVariantMap defaults = detail::defaultDarkPalette();

    // Drop any token in the current palette that's absent from the
    // defaults (matugen-extended brand_*, hand-edited custom keys).
    // Track whether a key was actually removed so we can decide
    // whether to emit paletteChanged when applyPalette below
    // happens to be a no-op (store was already at defaults).
    bool droppedExtras = false;
    QSet<QString> defaultsKeys;
    defaultsKeys.reserve(defaults.size());
    for (auto it = defaults.constBegin(); it != defaults.constEnd(); ++it) {
        defaultsKeys.insert(it.key());
    }
    const QList<QString> existingKeys = m_palette.keys();
    for (const QString& key : existingKeys) {
        if (!defaultsKeys.contains(key)) {
            m_palette.remove(key);
            droppedExtras = true;
        }
    }

    // applyPalette emits paletteChanged itself if any value
    // changed. If it doesn't (store already at defaults for every
    // default key) but we dropped at least one extra key above,
    // we still need to fire paletteChanged because the palette
    // observably changed. Use a one-shot connection to detect
    // applyPalette's emit so we don't double-fire.
    bool applyEmitted = false;
    const auto conn = connect(
        this, &PaletteStore::paletteChanged, this,
        [&applyEmitted]() {
            applyEmitted = true;
        },
        Qt::DirectConnection);
    applyPalette(defaults);
    disconnect(conn);
    if (droppedExtras && !applyEmitted) {
        Q_EMIT paletteChanged();
    }
}

QString PaletteStore::sourcePath() const
{
    return m_sourcePath;
}

// Merge incoming tokens over the current palette. The watcher-drop
// side effect that used to live here moved into the dedicated
// dropWatcherAndClearSourcePath() helper — applyPalette is now
// purely a token-merge primitive with no watcher coupling.
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
    // Route through parseAndApplyJson WITHOUT dropping the watcher
    // (parseAndApplyJson no longer drops the watcher at all — that's
    // dropWatcherAndClearSourcePath's job now, and only loadFromJson
    // calls it). A successful hot-reload therefore keeps the watcher
    // armed, so the next on-disk edit fires too.
    if (!parseAndApplyJson(data)) {
        Q_EMIT loadError(m_sourcePath, QStringLiteral("invalid JSON or empty token map"));
    }
}

} // namespace PhosphorTheme
