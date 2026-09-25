// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Shape-traversal contract — `extractValidTokens` is the single
// source of truth, called by both loadFromJson (the JSON-blob path)
// and applyParsedJson (the file-load / hot-reload path). Each consumes
// the returned validated map directly for its merge step, so the
// wrapped-vs-flat JSON layout rules AND the QColor accept set cannot
// drift between any pair of callers that inspect a parsed payload.
//
// extractValidTokens wraps extractTokensOrEmpty (shape probe) and
// adds the QColor-parse pass, returning the normalised QVariantMap
// both callers consume. The two file-backed entry points share the
// parse + applyParsedDocWithLoadError step (identical diagnostic
// wording): loadFromFile does its own preflight read to avoid
// TOCTOU and then calls applyParsedDocWithLoadError directly;
// reloadFromCurrentPath re-reads via readParseAndApply because the
// watcher signals "something changed" without the bytes, so the
// reload needs its own I/O pass before reaching the shared apply
// step.
//
// A previous version of this file open-coded the wrapped/flat
// traversal in both loadFromJson's validation arm and
// applyParsedJson and relied on a Q_ASSERT after the apply step to
// catch drift; in release builds the assert would compile out and
// a half-committed loadFromJson (watcher dropped, sourcePath
// cleared, paletteChanged NOT fired) would surface silently.
// Routing both call sites through one helper makes the divergence
// structurally impossible.
//
// If you add a new ApplyResult variant or change the shape rules,
// update extractValidTokens and its callers will stay correct by
// construction.

#include <PhosphorTheme/PaletteStore.h>

#include "defaultpalette.h"
#include "phosphortheme_logging.h"

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
    // Stable objectName so the hot-reload test suite can fetch the
    // timer with `findChild<QTimer*>(QStringLiteral("paletteReloadDebounce"))`
    // instead of the index-fragile findChildren-then-first pattern. Tests
    // that scan the QObject child tree without an objectName would break
    // the moment any future code adds another QTimer child to PaletteStore.
    m_reloadDebounce->setObjectName(QStringLiteral("paletteReloadDebounce"));
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
    // applyParsedJson runs, so by the time any user slot wired to
    // paletteChanged / sourcePathChanged executes, the watcher
    // already reflects the new source. Stale-watcher reads inside a
    // signal slot are therefore not possible.
    connect(m_watcher.get(), &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
        // Guard against stale fileChanged events whose `path` belongs
        // to a previously-watched source. QFileSystemWatcher delivers
        // events asynchronously via the platform notifier (inotify on
        // Linux); a fileChanged for the OLD source can sit in the Qt
        // event queue while loadFromFile swaps m_sourcePath + removes
        // the file from the watcher's tracked paths. Without this
        // guard the lambda would re-add the stale path to the watcher
        // (resurrecting a watch that loadFromFile just dropped) and
        // schedule a debounce tick that, when it fires, would reload
        // from the NEW source — wasted I/O at best, or worse if a
        // subsequent loadFromJson cleared the source entirely.
        //
        // Raw string equality is safe here: m_sourcePath is set
        // exclusively by loadFromFile via QFileInfo::canonicalFilePath
        // (with absoluteFilePath fallback), and QFileSystemWatcher
        // hands back the same canonical string it was registered
        // with — no case-folding round-trip needed at delivery time.
        if (path != m_sourcePath) {
            return;
        }
        rearmFileIfMissing(path);
        m_reloadDebounce->start();
    });
    connect(m_watcher.get(), &QFileSystemWatcher::directoryChanged, this, [this](const QString&) {
        if (m_sourcePath.isEmpty()) {
            return;
        }
        // Re-arm the parent-directory watch if it was dropped. A
        // common operator pattern is `rm -rf <dir>; mkdir <dir>`
        // (live editing a palette pack), which destroys the parent
        // directory and recreates it. QFileSystemWatcher silently
        // drops the watch for a directory that ceases to exist;
        // without re-adding it on the next directoryChanged
        // delivery, a subsequent atomic-rename save inside the
        // recreated directory would never trigger any further
        // events. Re-arm covers both this case and the rename-over
        // case the constructor's original comment focused on.
        rearmDirIfMissing(QFileInfo(m_sourcePath).absolutePath());
        if (QFileInfo::exists(m_sourcePath)) {
            rearmFileIfMissing(m_sourcePath);
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
    // or memory corruption). Assert the invariant loudly in debug; the
    // qCWarning + default-return below are the belt-and-braces release
    // path where the assert compiles out.
    Q_ASSERT(it.value().userType() == QMetaType::QColor);
    if (it.value().userType() != QMetaType::QColor) {
        qCWarning(lcPhosphorTheme).noquote()
            << "phosphor-theme: PaletteStore::token(" << name << ") expected QColor but stored variant has typeId"
            << it.value().userType() << "; returning default QColor()";
        return QColor();
    }
    return it.value().value<QColor>();
}

// Public JSON-blob entry. The IThemeService contract says a failed
// load must not observably mutate state: if the JSON is malformed,
// the wrapped `tokens` key is non-object, or the token map yields
// no usable colors, a previously file-loaded store still sees its
// watcher armed and sourcePath populated when this returns false.
// We achieve that by extracting + validating the payload up-front,
// then committing in two ordered steps once success is irrevocable:
// (1) dropWatcherAndClearSourcePath emits sourcePathChanged, (2)
// applyPalette emits paletteChanged. The single extractValidTokens
// call is reused for the merge step so the validator and the merger
// see the exact same byte sequence (no double-parse).
//
// Signal-order convention: sourcePathChanged BEFORE paletteChanged
// matches resetToDefaults + loadFromFile. QML bindings that read
// both reach a coherent state on the first re-evaluation (new
// source against the new palette) instead of palette-against-stale-
// source on the first tick and only catching up on the second.
// Atomicity is preserved: extractValidTokens + the empty-map check
// have already committed success when control reaches the two
// emit-side helpers, and neither one can fail (applyPalette is a
// pure in-memory merge, dropWatcherAndClearSourcePath is QFileSystemWatcher
// removePaths + a string clear).
bool PaletteStore::loadFromJson(const QByteArray& json)
{
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    // Extract once. The returned map is the same one applyPalette
    // will merge below, so the validator and the applier consume
    // identical bytes by construction — no second QColor-parse pass.
    //
    // Contract dependency: when extractValidTokens returns Ok, the
    // optional is guaranteed engaged (see its declaration). The
    // extractErr != Ok check just below guards the only error escape,
    // so the parsed-> dereference afterwards needs no .has_value() guard.
    ApplyResult extractErr = ApplyResult::Ok;
    const auto parsed = extractValidTokens(doc, extractErr);
    if (extractErr != ApplyResult::Ok) {
        return false;
    }
    if (parsed->isEmpty()) {
        return false;
    }

    // Success is committed. Fire sourcePathChanged BEFORE
    // paletteChanged to match the resetToDefaults / loadFromFile
    // ordering convention (QML bindings see the new source on the
    // same tick that brings the new palette). Neither call can fail
    // — dropWatcherAndClearSourcePath only mutates watcher state +
    // m_sourcePath, and applyPalette is a pure in-memory merge —
    // so the atomic-on-failure contract for the public API holds:
    // every error path returns above this line.
    dropWatcherAndClearSourcePath();
    applyPalette(*parsed);
    return true;
}

QJsonObject PaletteStore::extractTokensOrEmpty(const QJsonDocument& doc, ApplyResult& outShapeError)
{
    // Two accepted layouts:
    //   1. `{ "tokens": { "primary": "#RRGGBB", ... } }` (wrapped,
    //      matugen-style — leaves room for sibling metadata keys).
    //   2. `{ "primary": "#RRGGBB", ... }` (flat top-level — hand-edited
    //      palette files where the wrapper feels like noise).
    // Layout 1 takes precedence if `tokens` is present.
    //
    // Shape failure: `tokens` key present but value isn't an object.
    // The caller's intent is unambiguous but malformed; fail explicitly
    // instead of silently falling through to flat parsing, which would
    // treat sibling metadata keys as tokens and change the palette
    // in a surprising way.
    outShapeError = ApplyResult::Ok;
    const auto root = doc.object();
    if (root.contains(QLatin1String("tokens"))) {
        const auto tokensValue = root.value(QLatin1String("tokens"));
        if (!tokensValue.isObject()) {
            outShapeError = ApplyResult::TokensKeyNotObject;
            return QJsonObject();
        }
        return tokensValue.toObject();
    }
    return root;
}

std::optional<QVariantMap> PaletteStore::extractValidTokens(const QJsonDocument& doc, ApplyResult& outError)
{
    // Single QColor-parse pass over the wrapped/flat token map. Both
    // the validator and the merger consume this normalised map so the
    // accept set is identical by construction (no per-call-site
    // QColor probes that could drift on a future regex tweak). The
    // returned map can be empty: callers must distinguish "shape
    // error" (outError != Ok, optional is nullopt) from "shape Ok but
    // no usable colors found" (outError == Ok, optional holds empty
    // map) — applyParsedJson maps the latter to NoUsableTokens.
    outError = ApplyResult::Ok;
    ApplyResult shapeError = ApplyResult::Ok;
    const QJsonObject tokens = extractTokensOrEmpty(doc, shapeError);
    if (shapeError != ApplyResult::Ok) {
        outError = shapeError;
        return std::nullopt;
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
    return parsed;
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
    // Clear m_sourcePath BEFORE stopping the debounce + removing
    // watch paths so any user slot that observes
    // sourcePathChanged sees an already-empty source — that lets
    // reloadFromCurrentPath's empty-source short-circuit cover
    // any racing debounce tick. Both watcher-dropping callers
    // (loadFromJson and resetToDefaults) route through this one
    // helper, so the sequence cannot drift between them by
    // construction.
    //
    // Also stop any pending debounce so a queued reload tick can't
    // run reloadFromCurrentPath against the (now empty) source.
    const bool sourcePathWasSet = !m_sourcePath.isEmpty();
    if (sourcePathWasSet) {
        m_sourcePath.clear();
    }
    m_reloadDebounce->stop();
    // QFileSystemWatcher::removePaths tolerates an empty list, so no
    // pre-check needed: hand it whatever it currently tracks.
    m_watcher->removePaths(m_watcher->files());
    m_watcher->removePaths(m_watcher->directories());
    if (sourcePathWasSet) {
        Q_EMIT sourcePathChanged();
    }
}

PaletteStore::ApplyResult PaletteStore::applyParsedJson(const QJsonDocument& doc)
{
    // Route through extractValidTokens (the single source of truth for
    // the wrapped-vs-flat shape rules AND the QColor accept set).
    // loadFromJson performs its own inline extractValidTokens + commit;
    // this function is the file-load / hot-reload entry that routes
    // through the same helper, so every path observes identical
    // accept-set behaviour.
    //
    // Contract dependency: when extractValidTokens returns Ok, the
    // optional is guaranteed engaged (see its declaration). The
    // err != Ok check just below guards the only error escape, so the
    // parsed-> dereference afterwards needs no .has_value() guard.
    ApplyResult err = ApplyResult::Ok;
    auto parsed = extractValidTokens(doc, err);
    if (err != ApplyResult::Ok) {
        return err;
    }
    if (parsed->isEmpty()) {
        return ApplyResult::NoUsableTokens;
    }

    applyPalette(*parsed);
    return ApplyResult::Ok;
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

    // Pre-flight parse: read + parse the file into a local
    // QJsonDocument so an outright I/O or parse failure doesn't
    // observably mutate sourcePath / the watcher. The previous order
    // (set m_sourcePath, parse, roll back on failure) emitted
    // sourcePathChanged twice (new → old) for a single failed load,
    // which QML bindings would see as two transient states.
    //
    // Single-read TOCTOU closure: we KEEP the parsed
    // QJsonDocument from this preflight and thread it directly
    // into applyParsedDocWithLoadError below. A previous version
    // re-read the file inside readParseAndApply after committing
    // sourcePath, which opened a delete/replace/truncate race: if
    // the file vanished between preflight close and the second
    // open, an I/O-failure branch would fire loadError with
    // sourcePath already committed — violating the header
    // contract "Outright I/O failures (cannot open, short read)
    // also leave sourcePath untouched". One read, one parse, one
    // apply: no second I/O step after sourcePath is mutated.
    //
    // Path-equality below uses a canonicalFilePath round-trip on
    // both sides so case-insensitive mounts (HFS+ bind-mount,
    // exFAT, NTFS surfaced through ntfs-3g, FAT32 USB sticks,
    // SMB/CIFS shares) treat the same logical file under different
    // capitalisations as equal. Phosphor is Linux/Wayland-only,
    // but case-insensitive filesystems do appear on Linux hosts
    // (removable media, network shares, Windows interop mounts)
    // and the watcher must not churn when a path arrives differing
    // only in case from the currently-loaded one. The canonical
    // round-trip also folds in any post-load symlink chain change.
    QJsonDocument preflightDoc;
    {
        QFile preflight(absolutePath);
        if (!preflight.open(QIODevice::ReadOnly | QIODevice::Text)) {
            Q_EMIT loadError(absolutePath, preflight.errorString());
            return false;
        }
        const auto preflightData = preflight.readAll();
        if (preflight.error() != QFileDevice::NoError) {
            Q_EMIT loadError(absolutePath, preflight.errorString());
            return false;
        }
        preflight.close();
        QJsonParseError err{};
        preflightDoc = QJsonDocument::fromJson(preflightData, &err);
        if (err.error != QJsonParseError::NoError || !preflightDoc.isObject()) {
            Q_EMIT loadError(absolutePath, QStringLiteral("invalid JSON"));
            return false;
        }
    }

    // Commit the path BEFORE applyParsedJson so QML bindings that
    // observe both palette and sourcePath see a coherent transition
    // (new path → new palette). Otherwise paletteChanged would fire
    // with sourcePath still pointing at the prior source, then
    // sourcePathChanged would catch up on the next event-loop tick.
    // Canonicalise the prior path before comparing so case-only
    // differences on case-insensitive mounts (and any post-load
    // symlink chain rewrite) don't read as a path move. The
    // incoming side is `absolutePath`, which we already resolved
    // through canonicalFilePath() (with an absoluteFilePath fallback)
    // at the top of this function, so no second round-trip needed.
    // When the prior path fails to canonicalise (it was unlinked)
    // the raw string is used as a deterministic fallback so the
    // comparison still terminates.
    //
    // Edge case (acknowledged, not addressed): a symlink that
    // re-points to a NEW target between load and the current call,
    // with the OLD target's path passed in, will now resolve to the
    // new target's canonical path and read as a path move. There is
    // no way to detect "symlink stayed at same target" from path
    // strings alone post-rewrite; treating it as a move drops and
    // re-arms the watcher, which is the safer behaviour anyway
    // (the new target is what the user is actually editing).
    const QString currentCanonical = m_sourcePath.isEmpty() ? QString() : QFileInfo(m_sourcePath).canonicalFilePath();
    const QString currentForCompare = currentCanonical.isEmpty() ? m_sourcePath : currentCanonical;
    const bool sourcePathMoved = (currentForCompare != absolutePath);
    if (sourcePathMoved) {
        m_sourcePath = absolutePath;
    }

    // Two-mode watcher arming:
    //
    //   Path moved: swap the watcher — drop the prior file +
    //   parent-directory watches, re-add for the new path.
    //
    //   Path NOT moved: the watcher is NORMALLY still armed for
    //   this path, but a silent drop is possible. The classic case
    //   is `rm -rf <parent>; mkdir <parent>` between loads on the
    //   same logical path: QFileSystemWatcher silently drops both
    //   the file and the parent-directory watches when their
    //   inodes vanish, and the directoryChanged handler that
    //   normally re-arms on recreation only fires while the
    //   process is actually receiving inotify events for the
    //   recreated parent — if the recreation happened while the
    //   PaletteStore was otherwise idle, the watcher silently
    //   stays disarmed. A subsequent same-path loadFromFile would
    //   then leave us with no hot-reload. Defend by always
    //   checking the expected entries are present and re-adding
    //   any missing ones. addPath is idempotent on already-tracked
    //   paths, so this is also safe in the steady "watcher is
    //   fine" case.
    //
    // Either way the watcher swap happens BEFORE applyParsedDocWithLoadError
    // so any user slot wired to paletteChanged that reads the
    // watcher observes the NEW state, not the stale one.
    if (sourcePathMoved) {
        // Swap: drop the prior watches first so the helper's
        // contains() guard doesn't suppress an addPath for the new
        // file (the prior file's entry would still be in the watcher's
        // tracked-files list when the helper ran).
        if (!m_watcher->files().isEmpty()) {
            m_watcher->removePaths(m_watcher->files());
        }
        if (!m_watcher->directories().isEmpty()) {
            m_watcher->removePaths(m_watcher->directories());
        }
    }
    // Same code path for both moved + not-moved cases — armWatchesFor
    // wraps addPath in the existence+contains guard pattern so the
    // call is safe whether the watcher is empty (post-removePaths)
    // or already armed (same-path re-call, possibly with a silently-
    // dropped entry to re-arm). Routing both branches through one
    // helper makes "the file + parent watches end up armed" the
    // structural invariant.
    armWatchesFor(absolutePath);

    // Drop any pending debounce from the previous source so a queued
    // tick can't fire a redundant reload of the file we just loaded
    // synchronously.
    m_reloadDebounce->stop();

    // Signal-order convention: sourcePathChanged fires BEFORE
    // paletteChanged. This matches loadFromJson + resetToDefaults
    // (every entry point that can mutate both signals routes
    // through the same convention). The convention is pinned by
    // tests test_palettestore.cpp::loadFromJson_signalOrderIsSourceBeforePalette
    // and test_palettestore.cpp::resetToDefaults_signalOrderIsSourceBeforePalette
    // so a regression that flipped the order in any caller would
    // fail the suite. QML bindings that read both reach a coherent
    // new-source / new-palette state on the first re-evaluation
    // instead of palette-against-stale-source on the first tick
    // and catching up on the second.
    if (sourcePathMoved) {
        Q_EMIT sourcePathChanged();
    }

    // Apply the preflight-parsed document directly — NO second
    // file read. Re-reading here would re-open the TOCTOU window:
    // if the file were unlinked/replaced/truncated between
    // preflight close and the second open, the I/O-failure branch
    // would fire loadError after sourcePath was already committed,
    // violating the header contract that I/O failures leave
    // sourcePath untouched. applyParsedDocWithLoadError emits the
    // same shape-failure diagnostics readParseAndApply would
    // (TokensKeyNotObject / NoUsableTokens) so the hot-reload and
    // fresh-load error wording remains identical.
    return applyParsedDocWithLoadError(absolutePath, preflightDoc) == ApplyResult::Ok;
}

PaletteStore::ApplyResult PaletteStore::applyParsedDocWithLoadError(const QString& absolutePath,
                                                                    const QJsonDocument& doc)
{
    // Apply an already-parsed document, fanning the typed
    // ApplyResult into the same user-visible loadError wording
    // readParseAndApply would have emitted. The split lets
    // loadFromFile thread its preflight QJsonDocument directly
    // into apply without a second file read (TOCTOU window closed)
    // while reloadFromCurrentPath still goes through
    // readParseAndApply (which has to re-read because the
    // watcher only signals "something changed" — not what).
    const ApplyResult result = applyParsedJson(doc);
    // Exhaustive switch — every ApplyResult variant has an explicit
    // arm. Adding a new variant surfaces a -Wswitch-enum warning
    // here AND trips Q_UNREACHABLE_RETURN below if the new variant
    // somehow falls through — so enum drift becomes a build error
    // rather than silently degrading to a permissive trailing
    // return. The Q_UNREACHABLE_RETURN replaces the prior trailing
    // `return result;` which let a missing case slip through with
    // no warning escalation.
    switch (result) {
    case ApplyResult::Ok:
        return ApplyResult::Ok;
    case ApplyResult::TokensKeyNotObject:
        Q_EMIT loadError(absolutePath, QStringLiteral("tokens key must be a JSON object"));
        return result;
    case ApplyResult::NoUsableTokens:
        Q_EMIT loadError(absolutePath, QStringLiteral("no usable color tokens"));
        return result;
    }
    Q_UNREACHABLE_RETURN(result);
}

void PaletteStore::readParseAndApply(const QString& absolutePath)
{
    // Hot-reload file-read-parse-apply path. Only
    // reloadFromCurrentPath calls here now: loadFromFile threads
    // its preflight QJsonDocument directly into
    // applyParsedDocWithLoadError to avoid the TOCTOU window
    // between path commit and a second read. The hot-reload path
    // must re-read because the watcher only signals "something
    // changed" without handing back the new bytes. Every failure mode
    // reports through loadError; there is no status to return.
    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Q_EMIT loadError(absolutePath, file.errorString());
        return;
    }
    const auto data = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        // readAll() returns whatever it managed to read on a short
        // read (truncated NFS mounts, signal-interrupted reads).
        // Without this check we'd silently parse a partial buffer as
        // if it were the full file and either mutate state from a
        // half-payload or report a misleading "invalid JSON" error
        // for what is actually an I/O failure. Mirrors the check in
        // TemplateEngine::renderFile.
        Q_EMIT loadError(absolutePath, file.errorString());
        return;
    }
    file.close();

    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        Q_EMIT loadError(absolutePath, QStringLiteral("invalid JSON"));
        return;
    }
    applyParsedDocWithLoadError(absolutePath, doc);
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
    // Tear the watcher down + clear sourcePath via the shared helper
    // so the watcher-tearing sequence (and the sourcePathChanged-
    // before-paletteChanged ordering it enforces) can't drift from
    // loadFromJson's path. See dropWatcherAndClearSourcePath.
    dropWatcherAndClearSourcePath();

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

    // applyPalette returns whether it emitted paletteChanged. If
    // it didn't (store already at defaults for every default key)
    // but we dropped at least one extra key above, we still need
    // to fire paletteChanged because the palette observably
    // changed. Reading the return value avoids the previous
    // connect/lambda/disconnect dance to observe the emit.
    const bool applyEmitted = applyPalette(defaults);
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
//
// Returns true iff at least one stored value changed (i.e.
// paletteChanged was emitted). resetToDefaults reads the return
// value to decide whether it needs to fire paletteChanged itself
// for an "extras dropped but defaults already in place" case,
// without resorting to a transient signal-spy connection to detect
// the emit.
bool PaletteStore::applyPalette(const QVariantMap& tokens)
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
        const int type = it.value().userType();
        if (type == QMetaType::QColor) {
            c = it.value().value<QColor>();
        } else if (type == QMetaType::QString) {
            // Narrow to QString rather than the broader canConvert<QString>:
            // canConvert returns true for ints/doubles/bools/QDateTime etc.,
            // each of which would round-trip through toString() into a
            // QColor() construction that always fails — wasted work that
            // obscures the producer's intent ("the value is a hex string").
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
    return changed;
}

void PaletteStore::reloadFromCurrentPath()
{
    if (m_sourcePath.isEmpty()) {
        return;
    }
    // The watcher stays armed throughout: this method is reached
    // only via the debounced timer, and dropWatcherAndClearSourcePath
    // is the sole watcher-tearing path (loadFromJson,
    // resetToDefaults). Route through the shared
    // readParseAndApply pipeline so the diagnostic wording on every
    // failure mode matches loadFromFile exactly — the user must see
    // the same "invalid JSON" / "tokens key must be a JSON object"
    // / "no usable color tokens" string whether they triggered the
    // load explicitly or via a hot-reload tick. readParseAndApply
    // returns void: reloadFromCurrentPath has no caller to surface
    // success/failure to; the loadError signal is the only
    // observable side-channel.
    readParseAndApply(m_sourcePath);
}

void PaletteStore::armWatchesFor(const QString& absolutePath)
{
    // Shared by both branches of loadFromFile. The contains() guards
    // make the call idempotent: in the "path moved" branch the prior
    // entries have already been removed by the caller so contains()
    // returns false and we addPath unconditionally; in the "path NOT
    // moved" branch the entries usually still exist and contains()
    // short-circuits the addPath — except when the watch was silently
    // dropped (rm-rf-then-mkdir of the parent while the store was
    // idle, NFS server bounce dropping inotify state), in which case
    // contains() returns false and we re-arm. Either way the post-
    // call invariant is "watcher armed against absolutePath + parent
    // directory", which is what every loadFromFile caller wants.
    //
    // The existence checks defend against a window where the file or
    // parent directory was unlinked between the loadFromFile preflight
    // and this re-arm. addPath against a non-existent path silently
    // fails (and QFileSystemWatcher logs a warning to stderr), so the
    // guard keeps the diagnostic noise off the user's journal in the
    // unlink-race case.
    rearmFileIfMissing(absolutePath);
    rearmDirIfMissing(QFileInfo(absolutePath).absolutePath());
}

void PaletteStore::rearmFileIfMissing(const QString& path)
{
    if (QFileInfo::exists(path) && !m_watcher->files().contains(path)) {
        m_watcher->addPath(path);
    }
}

void PaletteStore::rearmDirIfMissing(const QString& path)
{
    if (!path.isEmpty() && QFileInfo::exists(path) && !m_watcher->directories().contains(path)) {
        m_watcher->addPath(path);
    }
}

void PaletteStore::setDebounceIntervalForTest(int ms)
{
    // Test-only seam: shrink the 80ms debounce so tests resolve fast.
    // Clamp to >= 1ms because QTimer treats 0ms as "fire next tick"
    // which defeats the burst-coalescing under test.
    m_reloadDebounce->setInterval(ms < 1 ? 1 : ms);
}

} // namespace PhosphorTheme
