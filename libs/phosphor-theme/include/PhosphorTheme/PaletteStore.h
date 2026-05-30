// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorTheme/IThemeService.h>
#include <PhosphorTheme/phosphortheme_export.h>

#include <QColor>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <optional>

class QFileSystemWatcher;
// QJsonDocument is referenced only by the class's private member
// signatures (applyParsedJson, applyParsedDocWithLoadError,
// extractTokensOrEmpty, extractValidTokens). C++ requires non-nested
// forward declarations at global / namespace scope rather than inside
// a class body, so it can't live in the `private:` section literally.
// Grouped here with the other private-only Qt forward decls
// (QFileSystemWatcher, QTimer — both private members) to keep the
// "private-API consumer" relationship obvious to readers scanning
// the file top.
class QJsonDocument;
class QTimer;

namespace PhosphorTheme {

// Concrete IThemeService backed by an in-memory QVariantMap. JSON files are
// loaded eagerly and watched for hot-reload; the file's `tokens` object
// (token name → "#RRGGBB" or "#AARRGGBB") merges into the active palette
// (existing tokens absent from the new payload survive) and
// `paletteChanged` fires once per reload that actually changed at least
// one token. Use `resetToDefaults()` for a true replace-with-defaults.
//
// Path handling: `loadFromFile(path)` canonicalises through
// QFileInfo::canonicalFilePath() before storing as the watched source,
// so symlinks are resolved to their target. A passed-in symlink to a
// palette file will have the underlying target file watched, not the
// symlink itself. This is by design: hot-reload should track the file
// whose contents change, not an indirection that may swap targets
// without notifying the watcher. When canonicalFilePath() returns
// empty (the path doesn't exist yet, or canonicalisation can't
// resolve the target), loadFromFile falls back to
// QFileInfo::absoluteFilePath() so the subsequent open() still
// surfaces the user-visible path in any error message. No base-dir
// restriction is applied; callers pass paths they trust (KCM, demo,
// scripted setup).
//
// QML side: registered as a QML singleton via `QML_ELEMENT QML_SINGLETON`,
// imported as `import Phosphor.Theme` → `PaletteStore.token("primary")`.
// QML files in Phosphor.Theme (Theme.qml, etc.) wrap PaletteStore in
// nicer property aliases so consumers write `Theme.primary` instead of
// `PaletteStore.token("primary")`.
//
// This is a per-engine instance, not a process global. The engine owns
// the lifetime; alternate implementations swap in via
// `qmlRegisterSingletonInstance` before module import.
class PHOSPHORTHEME_EXPORT PaletteStore : public QObject, public IThemeService
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // QML-friendly accessor exposing the full token map. Bindings on
    // `palette` re-evaluate when the active palette changes.
    Q_PROPERTY(QVariantMap palette READ palette NOTIFY paletteChanged)

    // The currently-watched JSON path, or empty if the active palette
    // came from `loadFromJson` / defaults.
    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY sourcePathChanged)

public:
    explicit PaletteStore(QObject* parent = nullptr);
    ~PaletteStore() override;
    Q_DISABLE_COPY_MOVE(PaletteStore)

    // The canonical built-in dark palette. Static accessor for callers
    // that need the defaults without paying for a PaletteStore instance
    // (which arms a QFileSystemWatcher + holds an inotify fd). Use this
    // for preset palettes, snapshot tests, and any read-only consumer.
    [[nodiscard]] static QVariantMap defaultPalette();

    // IThemeService.
    [[nodiscard]] QVariantMap palette() const override;
    [[nodiscard]] Q_INVOKABLE QColor token(const QString& name) const override;
    // loadFromJson is atomic across ALL three failure modes:
    //   - Parse failure (malformed JSON)
    //   - Shape mismatch (tokens key present but not a JSON object,
    //     mapped to ApplyResult::TokensKeyNotObject)
    //   - No-usable-tokens-found (parsed.isEmpty() after the
    //     QColor-parse pass, mapped to ApplyResult::NoUsableTokens)
    // On any of these the sourcePath, the watcher, and the active
    // palette are all unchanged (the implementation validates the
    // payload up-front, then commits only on success). A previously-
    // file-loaded store therefore keeps its watcher armed across a
    // failed loadFromJson call. Callers can treat any false return
    // as a pure no-op.
    Q_INVOKABLE bool loadFromJson(const QByteArray& json) override;
    // loadFromFile is INTENTIONALLY ASYMMETRIC with loadFromJson:
    // it commits the new sourcePath + swaps the watcher BEFORE
    // applying the parsed tokens, so a shape-failure return
    // (`tokens` key non-object, empty token map) still leaves
    // sourcePath pointing at the new file and the watcher armed
    // against it. Rationale: those failures are recoverable in
    // the user's editor without re-triggering load — the file
    // exists, the JSON parses, the contents just had no usable
    // tokens. Keeping the watcher armed means the next save in
    // the editor reloads automatically. Outright parse failures
    // (malformed JSON) still leave sourcePath untouched because
    // the parse runs before any commit. Outright I/O failures
    // (cannot open, short read) also leave sourcePath untouched
    // for the same reason — loadFromFile performs ONE read of
    // the file (preflight), parses the bytes into a
    // QJsonDocument, then threads that document directly into
    // the apply step. There is no second open/read after
    // sourcePath is committed, so an unlink-or-truncate race
    // between the preflight close and the apply step cannot
    // surface an I/O failure with sourcePath already mutated.
    // Callers that need atomic-commit semantics across every
    // failure mode should validate the file out-of-band and
    // route through loadFromJson instead.
    //
    // Note: on shape-failure paths (TokensKeyNotObject /
    // NoUsableTokens) the new sourcePath is still committed before
    // the failure surfaces, so `sourcePathChanged` may fire even
    // when this returns false. QML bindings observing both
    // sourcePath and loadError must be prepared to see the new
    // path arrive together with a loadError on the same edit.
    Q_INVOKABLE bool loadFromFile(const QString& path) override;
    // Fire-and-forget by IThemeService design: a token map that
    // normalises to zero usable colors is a no-op rather than an
    // error. The underlying applyPalette() returns a bool reporting
    // whether any token changed, but that signal is not propagated
    // through the IThemeService boundary because every QML/script
    // caller already observes the result via the paletteChanged
    // signal — duplicating the verdict in the return value would
    // force the interface to widen without any consumer needing it.
    Q_INVOKABLE void applyTokens(const QVariantMap& tokens) override;
    Q_INVOKABLE void resetToDefaults() override;

    [[nodiscard]] QString sourcePath() const;

    /// Test-only: override the hot-reload debounce interval (default
    /// 80 ms). Lets test_palettestore_hotreload.cpp resolve QTRY-style
    /// polling in O(milliseconds) instead of waiting on the production
    /// 80 ms window for every event. Values less than 1 ms are clamped
    /// to 1.
    ///
    /// CONTRACT: the `ForTest` suffix is convention only — there is no
    /// build-time guard preventing production code from calling this.
    /// Production consumers MUST NOT touch this method; doing so
    /// breaks the documented 80 ms coalescing behaviour relied on by
    /// downstream shells. The method stays public (rather than
    /// `friend class Test...`) because the test TU is built as a
    /// separate executable and friend-class extension across CMake
    /// targets is brittle. The `ForTest` suffix is the project
    /// convention for this trade-off (see DirectoryLoader::
    /// setDebounceIntervalForTest, etc.).
    void setDebounceIntervalForTest(int ms);

Q_SIGNALS:
    void paletteChanged();
    void sourcePathChanged();

    // Fires when `loadFromFile` failed to parse or read the file. The
    // active palette is unchanged. Wired to the demo's status bar so a
    // bad save is visible without crashing the shell.
    void loadError(const QString& path, const QString& reason);

private:
    /// Merge variant. Takes an already-parsed JsonDocument so
    /// callers that validated the document up-front (loadFromFile,
    /// reloadFromCurrentPath) can route through the same merge
    /// logic without paying for a second QJsonDocument::fromJson
    /// and without losing context about WHICH validation step
    /// failed (parse vs token-map empty). Returns a typed error
    /// code so the caller can surface the distinction in loadError.
    enum class ApplyResult {
        Ok,
        TokensKeyNotObject,
        NoUsableTokens,
    };
    /// Single source of truth for the wrapped-vs-flat shape
    /// traversal. Given a parsed JSON document, returns the tokens
    /// object the merge step would iterate (empty QJsonObject on
    /// shape error). Every payload-inspecting code path routes
    /// through this helper so the shape rules can't drift: a future
    /// failure mode added here surfaces in every caller in lockstep.
    /// See the file-top comment in palettestore.cpp for the rationale.
    ///
    /// On shape error (`tokens` key present but not an object) the
    /// returned object is empty AND outShapeError is set to
    /// TokensKeyNotObject. On a syntactically fine but empty /
    /// no-usable-color payload the returned object may be non-empty
    /// (the caller walks its entries to determine usability) and
    /// outShapeError stays Ok — the NoUsableTokens verdict only
    /// surfaces after the entry walk.
    static QJsonObject extractTokensOrEmpty(const QJsonDocument& doc, ApplyResult& outShapeError);
    /// Shared extraction + normalisation step. Walks the wrapped/flat
    /// shape via extractTokensOrEmpty, then QColor-parses every
    /// string entry exactly once and caches the validated map.
    ///
    /// loadFromJson calls this directly and reuses the cached map
    /// for the merge step — no second parse pass. applyParsedJson
    /// also routes through it so the file-load + hot-reload paths
    /// share the same accept set as the JSON-blob path. On shape
    /// error returns the matching ApplyResult variant (and nullopt);
    /// on an otherwise valid payload returns the normalised
    /// QVariantMap (empty if no usable colors were found — callers
    /// map that to NoUsableTokens).
    static std::optional<QVariantMap> extractValidTokens(const QJsonDocument& doc, ApplyResult& outError);
    [[nodiscard]] ApplyResult applyParsedJson(const QJsonDocument& doc);
    /// Apply an already-parsed document and emit `loadError` on the
    /// shape-failure variants with the same wording readParseAndApply
    /// surfaces. Used by loadFromFile to thread the preflight
    /// QJsonDocument directly into the apply step — no second file
    /// read between path commit and apply, so a delete/replace/truncate
    /// race between preflight close and apply cannot surface an I/O
    /// failure with sourcePath already mutated. (TOCTOU window
    /// closed: one read, one parse, one apply.)
    ApplyResult applyParsedDocWithLoadError(const QString& absolutePath, const QJsonDocument& doc);
    /// Post-path-resolution pipeline shared by reloadFromCurrentPath
    /// (and only reloadFromCurrentPath now): open + short-read guard
    /// + close + parse + applyParsedDocWithLoadError with identical
    /// error wording on every failure mode. The hot-reload path
    /// re-reads the file because the watcher only signals "something
    /// changed" — the bytes that triggered the event are not
    /// available without reading the file. loadFromFile bypasses this
    /// helper to avoid the TOCTOU window between its preflight read
    /// and a re-read.
    ///
    /// Returns void: the sole caller has nothing to surface a status
    /// to, and every failure mode (I/O, parse, shape, no-usable-tokens)
    /// is already reported through the loadError signal — the only
    /// observable side-channel. An ApplyResult here would have had to
    /// overload one of the parse-shape variants onto I/O failures,
    /// which the enum does not model.
    void readParseAndApply(const QString& absolutePath);
    /// Disarms the active filesystem watch (file + parent directory),
    /// cancels any pending debounced reload, and clears m_sourcePath
    /// (emitting sourcePathChanged if it was non-empty). The
    /// implementation counterpart to IThemeService::loadFromJson's
    /// "in-process blob, not a watched file" contract.
    void dropWatcherAndClearSourcePath();
    /// Merges incoming tokens over the current palette. Existing
    /// keys absent from `tokens` are preserved; values are
    /// normalised to QColor. Emits paletteChanged when at least
    /// one stored value differs from the incoming one. Pure
    /// in-memory merge: does NOT touch the watcher, the source
    /// path, or trigger any file I/O. Returns true iff at least
    /// one stored value changed (i.e. paletteChanged was emitted),
    /// so callers like resetToDefaults can compose the merge result
    /// with their own "did anything else change?" signal without
    /// resorting to a connect/disconnect dance to observe the emit.
    bool applyPalette(const QVariantMap& tokens);
    void reloadFromCurrentPath();
    /// Arm the file + parent-directory watches for `absolutePath`.
    /// Both branches of loadFromFile (path moved + path NOT moved)
    /// converge here so the addPath-with-existence-and-contains-guard
    /// pattern can't drift between them. The "path moved" branch
    /// removes prior entries BEFORE calling; the "path NOT moved"
    /// branch relies on addPath being a no-op when the path is
    /// already tracked AND on the contains() guards inside this
    /// helper to re-arm any silently-dropped watch (rm-rf-then-mkdir
    /// of the parent while the store was idle, NFS server bounce
    /// dropping inotify state). The helper itself does not remove
    /// prior entries — callers handle the swap when they need it.
    void armWatchesFor(const QString& absolutePath);
    /// Re-arm the watcher's tracked-files list for `path` iff `path`
    /// exists on disk AND is not already tracked. Used from the
    /// fileChanged/directoryChanged handlers and from armWatchesFor to
    /// converge the three sites that previously duplicated the
    /// exists()+contains()+addPath triple inline. The existence guard
    /// keeps QFileSystemWatcher's "addPath against missing file"
    /// stderr warning off the user's journal.
    void rearmFileIfMissing(const QString& path);
    /// Same as rearmFileIfMissing but for parent-directory watches
    /// (m_watcher->directories()). Empty paths are silently ignored
    /// since QFileInfo::absolutePath of an empty source path is
    /// itself empty.
    void rearmDirIfMissing(const QString& path);

    QVariantMap m_palette;
    QString m_sourcePath;
    std::unique_ptr<QFileSystemWatcher> m_watcher;
    std::unique_ptr<QTimer> m_reloadDebounce;
};

} // namespace PhosphorTheme
