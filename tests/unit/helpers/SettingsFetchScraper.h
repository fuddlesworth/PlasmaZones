// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file SettingsFetchScraper.h
 * @brief Scrapes every setting key the KWin effect and the editor fetch over D-Bus, from
 *        their own sources on disk.
 *
 * Split out of test_settings_registry_contract.cpp, which owns the assertions, to keep that
 * file under the project's 800-line cap.
 *
 * The contract this exists to enforce, and why it is worth this much machinery, is written
 * out in full above keysFetchedByEffect below. The short version: SettingsAdaptor resolves
 * keys through a HAND-MAINTAINED registry, so a fetch of an unregistered key compiles,
 * links, runs, and silently keeps its built-in default. This scrape is the only thing that
 * notices — and it has been broken, silently, EIGHT times, each time in a way that left it
 * green while a real key went unchecked. If you change it, make the unrecognised case LOUD.
 */

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <memory>
#include <utility>

namespace PlasmaZones::TestHelpers {

/// Source with its COMMENTS REMOVED.
///
/// The scrape works on raw text, so a comment that merely mentions `loadSettingAsync(`
/// inflates the call-site tally and fails the test pointing at prose. That is the safe
/// direction, but a tripwire that cries wolf is a tripwire someone eventually loosens —
/// and loosening this one is how it came to be broken repeatedly. Strip the comments and
/// it counts only code. String literals are left alone: a key IS a string literal, so they
/// are the one thing the scrape must still be able to see.
QString withoutComments(const QString& src)
{
    QString out;
    out.reserve(src.size());
    bool inLine = false;
    bool inBlock = false;
    bool inString = false;
    for (qsizetype i = 0; i < src.size(); ++i) {
        const QChar c = src.at(i);
        const QChar next = (i + 1 < src.size()) ? src.at(i + 1) : QChar();
        if (inLine) {
            if (c == QLatin1Char('\n')) {
                inLine = false;
                out.append(c);
            }
            continue;
        }
        if (inBlock) {
            if (c == QLatin1Char('*') && next == QLatin1Char('/')) {
                inBlock = false;
                ++i;
            }
            continue;
        }
        // STRING LITERALS ARE NOT COMMENTS, and this has to know the difference. A literal
        // containing "//" — a URL, a path, a glob, a snippet of GLSL — would otherwise look
        // like the start of a comment, and everything after it on the line (a real fetch,
        // say) would be silently deleted from what the scrape sees. Silently. No effect
        // source contains such a literal today; relying on that is exactly the kind of
        // luck this file has already run out of, repeatedly.
        if (inString) {
            if (c == QLatin1Char('\\')) {
                out.append(c);
                if (i + 1 < src.size()) {
                    out.append(src.at(i + 1)); // an escaped quote does not end the literal
                    ++i;
                }
                continue;
            }
            if (c == QLatin1Char('"')) {
                inString = false;
            }
            out.append(c);
            continue;
        }
        // A CHAR literal. '"' would otherwise open a phantom string that runs to the next
        // quote anywhere in the file, swallowing everything in between — including a real
        // fetch — from what the scrape sees.
        if (c == QLatin1Char('\'')) {
            out.append(c);
            for (++i; i < src.size(); ++i) {
                const QChar q = src.at(i);
                out.append(q);
                if (q == QLatin1Char('\\') && i + 1 < src.size()) {
                    out.append(src.at(++i));
                    continue;
                }
                if (q == QLatin1Char('\'')) {
                    break;
                }
            }
            continue;
        }
        // A RAW string, R"delim(...)delim". Its body may contain quotes, backslashes and
        // "//" freely, none of which mean anything — treating it as an ordinary string ends
        // it at the first inner quote, and everything after that on the line reads as a
        // comment and is deleted. The effect embeds GLSL, which is exactly where a raw
        // string shows up.
        if (c == QLatin1Char('R') && next == QLatin1Char('"')) {
            const qsizetype open = src.indexOf(QLatin1Char('('), i + 2);
            if (open > 0) {
                const QString delim = src.mid(i + 2, open - (i + 2));
                const QString close = QLatin1Char(')') + delim + QLatin1Char('"');
                const qsizetype end = src.indexOf(close, open + 1);
                if (end > 0) {
                    out.append(src.mid(i, end + close.size() - i));
                    i = end + close.size() - 1;
                    continue;
                }
            }
        }
        if (c == QLatin1Char('"')) {
            inString = true;
            out.append(c);
            continue;
        }
        if (c == QLatin1Char('/') && next == QLatin1Char('/')) {
            inLine = true;
            continue;
        }
        if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
            inBlock = true;
            ++i;
            continue;
        }
        out.append(c);
    }
    return out;
}

QString readSource(const QString& path, QString* whyFailed)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *whyFailed = QStringLiteral("cannot open %1").arg(path);
        return {};
    }
    const QString stripped = withoutComments(QString::fromUtf8(f.readAll()));
    if (stripped.isEmpty()) {
        // Check what we RETURN, not what we read. Every caller treats {} as failure and
        // prints *whyFailed, and a file that is entirely comments (a licence header and
        // nothing else) reads non-empty but strips to nothing — so guarding the raw text
        // left the caller bailing out with a blank explanation.
        *whyFailed = QStringLiteral("%1 has no code in it").arg(path);
    }
    return stripped;
}

/// ── Source-shape helpers ────────────────────────────────────────────────────
///
/// The scrape identifies PLUMBING (a call that forwards a key it was handed, rather than
/// naming one) by WHERE THE CALL IS: inside the brace-matched body of a function that is
/// itself a fetcher. It used to identify plumbing by what the argument was CALLED, which
/// is not a property of plumbing at all — it is a property of an identifier. The exempt
/// set came out to the single token `name`, so any real fetch that happened to spell its
/// key variable `name` was waved through, the tally balanced, and the key went unchecked.
/// That was the eighth time this file was broken in exactly that shape. Position is a fact
/// about the code; a name is a coincidence.

/// Index of the brace/paren matching the one at @p open, or -1. String and char literals
/// are skipped so a brace inside GLSL or a quote inside a literal cannot throw off the count.
qsizetype matchDelimiter(const QString& s, qsizetype open, QChar closeCh)
{
    const QChar openCh = s.at(open);
    int depth = 0;
    for (qsizetype i = open; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            const QChar quote = c;
            for (++i; i < s.size(); ++i) {
                if (s.at(i) == QLatin1Char('\\')) {
                    ++i;
                    continue;
                }
                if (s.at(i) == quote) {
                    break;
                }
            }
            continue;
        }
        if (c == openCh) {
            ++depth;
        } else if (c == closeCh && --depth == 0) {
            return i;
        }
    }
    return -1;
}

/// Split an argument or parameter list on its TOP-LEVEL commas (a nested call, template
/// argument list, or braced initializer keeps its own commas).
QStringList splitTopLevel(const QString& args)
{
    QStringList out;
    int depth = 0;
    qsizetype start = 0;
    for (qsizetype i = 0; i < args.size(); ++i) {
        const QChar c = args.at(i);
        if (c == QLatin1Char('(') || c == QLatin1Char('[') || c == QLatin1Char('{') || c == QLatin1Char('<')) {
            ++depth;
        } else if (c == QLatin1Char(')') || c == QLatin1Char(']') || c == QLatin1Char('}') || c == QLatin1Char('>')) {
            --depth;
        } else if (c == QLatin1Char(',') && depth == 0) {
            out << args.mid(start, i - start).trimmed();
            start = i + 1;
        }
    }
    const QString last = args.mid(start).trimmed();
    if (!last.isEmpty()) {
        out << last;
    }
    return out;
}

/// A function or lambda found in the source, with its body located exactly.
struct SourceFn
{
    QString name;
    QStringList keyParams; ///< parameters whose type is a QString (however spelled)
    qsizetype bodyStart = -1;
    qsizetype bodyEnd = -1;
};

/// Parameters of @p params whose TYPE is a QString in any spelling — `const QString&`,
/// `QString` by value, `const QString &` with the reference on the name, `QStringView`.
/// Pinning one spelling is how a wrapper escapes discovery entirely: undiscovered, its call
/// sites land in neither the numerator nor the denominator, and its keys simply vanish.
QStringList keyParamsOf(const QString& params)
{
    static const QRegularExpression paramRe(
        QLatin1String("(?:^|\\s)(?:const\\s+)?(QString|QStringView)\\s*&?\\s*([A-Za-z0-9_]+)\\s*$"));
    QStringList out;
    for (const QString& param : splitTopLevel(params)) {
        const auto m = paramRe.match(param);
        if (m.hasMatch()) {
            out << m.captured(2);
        }
    }
    return out;
}

/// Every function and named lambda in @p src, with brace-matched bodies.
QList<SourceFn> functionsIn(const QString& src)
{
    QList<SourceFn> fns;
    // A named lambda: `const auto f = [this](const QString& name, …) { … }`.
    static const QRegularExpression lambdaRe(QLatin1String("auto\\s+([A-Za-z0-9_]+)\\s*=\\s*\\[[^\\]]*\\]\\s*\\("));
    // A function definition, qualified or not. Deliberately loose: it over-matches call
    // sites too, which is harmless — a call has no body, so it is dropped below.
    static const QRegularExpression fnRe(QLatin1String("(?<![A-Za-z0-9_])(?:[A-Za-z0-9_]+::)?([A-Za-z0-9_]+)\\s*\\("));
    static const QSet<QString> keywords{QStringLiteral("if"),     QStringLiteral("for"),   QStringLiteral("while"),
                                        QStringLiteral("switch"), QStringLiteral("catch"), QStringLiteral("return")};

    for (const QRegularExpression* re : {&lambdaRe, &fnRe}) {
        auto it = re->globalMatch(src);
        while (it.hasNext()) {
            const auto m = it.next();
            const QString name = m.captured(1);
            if (keywords.contains(name)) {
                continue;
            }
            const qsizetype paren = m.capturedEnd(0) - 1; // the '(' the regex ended on
            const qsizetype close = matchDelimiter(src, paren, QLatin1Char(')'));
            if (close < 0) {
                continue;
            }
            // A BODY follows, not a `;`. What sits between is `const`, `override`, `-> T`,
            // `noexcept` — never a '(' (that would be a constructor's init list, which is
            // not a fetcher).
            qsizetype i = close + 1;
            bool body = false;
            for (; i < src.size(); ++i) {
                const QChar c = src.at(i);
                if (c == QLatin1Char('{')) {
                    body = true;
                    break;
                }
                if (c == QLatin1Char(';') || c == QLatin1Char('(') || c == QLatin1Char(')')) {
                    break;
                }
            }
            if (!body) {
                continue;
            }
            const qsizetype bodyEnd = matchDelimiter(src, i, QLatin1Char('}'));
            if (bodyEnd < 0) {
                continue;
            }
            fns << SourceFn{name, keyParamsOf(src.mid(paren + 1, close - paren - 1)), i, bodyEnd};
        }
    }
    return fns;
}

/// The key a call NAMES, read from its argument list: a literal, or a SettingProperty
/// constant resolved through @p constants. Scans the WHOLE list rather than the first
/// argument, so a wrapper that takes its key second is still read. Empty when the call
/// names no key at all.
///
/// @p unresolved is set when the call names a SettingProperty the constants scrape could
/// not resolve — that is LOUD, never silent: the call has the right SHAPE, so the tally
/// balances perfectly while the key it fetches is never compared against the registry.
QString keyNamedBy(const QString& args, const QHash<QString, QString>& constants, QString* unresolved)
{
    static const QRegularExpression keyRe(
        QLatin1String("QStringLiteral\\(\"([A-Za-z0-9_]+)\"\\)"
                      "|PhosphorProtocol::Service::SettingProperty::([A-Za-z0-9_]+)"));
    const auto m = keyRe.match(args);
    if (!m.hasMatch()) {
        return {};
    }
    if (!m.captured(1).isEmpty()) {
        return m.captured(1);
    }
    const QString resolved = constants.value(m.captured(2));
    if (resolved.isEmpty()) {
        *unresolved = m.captured(2);
    }
    return resolved;
}

/// Is @p offset inside the body of any function in @p fns whose name is in @p names?
bool insideBodyOf(const QList<SourceFn>& fns, const QSet<QString>& names, qsizetype offset)
{
    for (const SourceFn& fn : fns) {
        if (names.contains(fn.name) && offset > fn.bodyStart && offset < fn.bodyEnd) {
            return true;
        }
    }
    return false;
}

/// Every setting key the effect fetches, scraped from its own sources rather than
/// duplicated here — a hand-copied list is exactly the thing that drifts.
///
/// WHAT counts as a fetch is discovered, not listed: the set is seeded with
/// loadSettingAsync and grows to include any function that hands one of its OWN key
/// parameters to a member of the set (the audio wrappers do exactly that). A wrapper added
/// tomorrow is covered without touching this file, which matters because the alternative —
/// a list of names here — is one more thing to forget, and forgetting it is what once took
/// the numerator and the denominator out of agreement.
///
/// Every fetch must NAME its key somewhere in its argument list, in one of two shapes:
///
///   loadSettingAsync(QStringLiteral("key"), …)   a literal
///   loadSettingAsync(SettingProperty::X, …)      a named constant, resolved from
///                                                ServiceConstants.h
///
/// and so must every call to a discovered wrapper. A fetch whose key the scrape cannot read
/// off the call site is UNACCOUNTED, and unaccounted is LOUD — including a key bound to a
/// local variable first, which is the shape that defeated the previous version of this file.
///
/// The one thing that is exempt is PLUMBING: a call that forwards a key it was itself
/// handed. That is recognised by POSITION (the call lies inside the brace-matched body of a
/// fetcher) and not by the NAME of the argument, which is not a property of plumbing at all.
///
/// @p unaccounted receives every call site that is neither. The caller asserts it is empty:
/// a fetch written in a shape this function does not know would otherwise simply not be
/// seen, and the test would pass green on precisely the bug it exists to catch. It has done
/// exactly that eight times.
QStringList keysFetchedByEffect(QString* whyFailed, QStringList* unaccounted)
{
    const QString constantsSrc =
        readSource(QStringLiteral(PLASMAZONES_PROTOCOL_INC_DIR "/PhosphorProtocol/ServiceConstants.h"), whyFailed);
    if (constantsSrc.isEmpty()) {
        return {};
    }

    // SettingProperty::Name -> "actualKey", from ServiceConstants.h — scoped to THAT
    // namespace block. The map is keyed on the bare identifier, and the header already has
    // identifiers that repeat across namespaces (ServiceName, ObjectPath, Interface). A
    // header-wide scrape would let a future SettingProperty member that shares a name with
    // any of them bind a fetch to the WRONG key, silently.
    const qsizetype nsStart = constantsSrc.indexOf(QLatin1String("namespace SettingProperty {"));
    if (nsStart < 0) {
        *whyFailed = QStringLiteral("ServiceConstants.h has no SettingProperty namespace — did it move?");
        return {};
    }
    // Brace-MATCHED, not "the first `\n}`": the moment anyone nests anything in the
    // namespace, the naive scan stops at the inner brace and silently truncates the slice.
    const qsizetype nsOpen = constantsSrc.indexOf(QLatin1Char('{'), nsStart);
    const qsizetype nsEnd = nsOpen >= 0 ? matchDelimiter(constantsSrc, nsOpen, QLatin1Char('}')) : -1;
    if (nsEnd < 0) {
        *whyFailed = QStringLiteral("SettingProperty namespace is not closed as expected");
        return {};
    }
    const QString settingPropertySrc = constantsSrc.mid(nsStart, nsEnd - nsStart);

    QHash<QString, QString> constants;
    static const QRegularExpression constRe(
        QLatin1String("inline constexpr QLatin1String ([A-Za-z0-9_]+)\\(\"([A-Za-z0-9_]+)\"\\)"));
    auto cit = constRe.globalMatch(settingPropertySrc);
    while (cit.hasNext()) {
        const auto m = cit.next();
        constants.insert(m.captured(1), m.captured(2));
    }
    // Prove the scrape SAW every constant in the block. Miss one — because clang-format
    // wrapped a long declaration, or someone wrote `constexpr auto` — and every fetch
    // through it silently stops being checked.
    const qsizetype declared = settingPropertySrc.count(QLatin1String("constexpr"));
    if (declared != constants.size()) {
        *whyFailed = QStringLiteral(
                         "scraped %1 of %2 SettingProperty constants — one is declared in a shape the "
                         "scrape does not recognise, and every fetch through it would go unchecked")
                         .arg(constants.size())
                         .arg(declared);
        return {};
    }

    // EVERY effect source, RECURSIVELY. A hand-written file list is a list to forget, and
    // so is a hand-written directory list. The extension filter is a DENY-list, not an
    // allow-list, for the same reason: a fetch that moves into a .ipp or .tpp template-impl
    // file would never be opened, and an unopened file has no tally and no failure — it
    // just quietly stops being checked.
    static const QSet<QString> notSource{QStringLiteral("md"),    QStringLiteral("txt"), QStringLiteral("json"),
                                         QStringLiteral("glsl"),  QStringLiteral("qml"), QStringLiteral("in"),
                                         QStringLiteral("cmake"), QStringLiteral("yml"), QStringLiteral("yaml"),
                                         QStringLiteral("png"),   QStringLiteral("svg"), QStringLiteral("ts")};
    QStringList files;
    QDirIterator it(QStringLiteral(PLASMAZONES_EFFECT_SRC_DIR), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (!notSource.contains(QFileInfo(path).suffix().toLower())) {
            files << path;
        }
    }
    if (files.isEmpty()) {
        *whyFailed = QStringLiteral("found no effect sources under %1").arg(QLatin1String(PLASMAZONES_EFFECT_SRC_DIR));
        return {};
    }

    // Read every source ONCE, then discover the fetchers across the WHOLE corpus before
    // scanning: a wrapper may be defined in one file and called from another.
    QHash<QString, QString> sources;
    QHash<QString, QList<SourceFn>> functions;
    for (const QString& path : files) {
        const QString src = readSource(path, whyFailed);
        if (src.isEmpty()) {
            return {};
        }
        sources.insert(path, src);
        functions.insert(path, functionsIn(src));
    }

    // Seed with the primitive. A function JOINS the set by handing one of its own key
    // parameters to a member of the set — which is what a wrapper IS. Iterated to a fixed
    // point so a wrapper of a wrapper is caught too.
    QSet<QString> fetchers{QStringLiteral("loadSettingAsync")};
    bool grew = true;
    while (grew) {
        grew = false;
        for (const QString& path : files) {
            const QString& src = sources[path];
            for (const SourceFn& fn : functions[path]) {
                if (fn.keyParams.isEmpty() || fetchers.contains(fn.name)) {
                    continue;
                }
                const QString body = src.mid(fn.bodyStart, fn.bodyEnd - fn.bodyStart);
                for (const QString& known : std::as_const(fetchers)) {
                    const QRegularExpression forwardRe(QStringLiteral("(?<![A-Za-z0-9_])%1\\s*\\(([^)]*)").arg(known));
                    auto fit = forwardRe.globalMatch(body);
                    bool forwards = false;
                    while (fit.hasNext() && !forwards) {
                        const QStringList args = splitTopLevel(fit.next().captured(1));
                        for (const QString& arg : args) {
                            if (fn.keyParams.contains(arg.trimmed())) {
                                forwards = true;
                                break;
                            }
                        }
                    }
                    if (forwards) {
                        fetchers.insert(fn.name);
                        grew = true;
                        break;
                    }
                }
            }
        }
    }
    if (fetchers.size() < 2) {
        // Only the seed. The effect HAS wrappers today, so this means their shape moved and
        // the scan is now blind to them — which is the silent hole this whole mechanism
        // exists to prevent.
        *whyFailed = QStringLiteral(
            "discovered no setting-fetch wrappers beyond loadSettingAsync — the wrapper "
            "shape has changed and their keys are no longer being checked");
        return {};
    }

    // Longest name first so `loadSettingAsync` cannot match as a prefix of a longer one.
    QStringList fetcherNames(fetchers.cbegin(), fetchers.cend());
    std::sort(fetcherNames.begin(), fetcherNames.end(), [](const QString& a, const QString& b) {
        return a.size() != b.size() ? a.size() > b.size() : a < b;
    });
    const QRegularExpression callRe(
        QStringLiteral("(?<![A-Za-z0-9_])(?:%1)\\s*\\(").arg(fetcherNames.join(QLatin1Char('|'))));
    // A SIGNATURE, not a call: a declaration or definition of a fetcher. Its parameter list
    // names the type. A call's arguments never contain the bare token `QString` (QStringLiteral
    // does not match — the boundary fails on the 'L').
    static const QRegularExpression signatureRe(QLatin1String("\\bQString\\b|\\bQStringView\\b"));

    QStringList keys;
    for (const QString& path : files) {
        const QString& src = sources[path];
        auto cit2 = callRe.globalMatch(src);
        while (cit2.hasNext()) {
            const auto m = cit2.next();
            const qsizetype paren = m.capturedEnd(0) - 1;
            const qsizetype close = matchDelimiter(src, paren, QLatin1Char(')'));
            if (close < 0) {
                *unaccounted
                    << QStringLiteral("%1: a fetch call's argument list is not closed").arg(QFileInfo(path).fileName());
                continue;
            }
            const QString args = src.mid(paren + 1, close - paren - 1);
            if (args.trimmed().isEmpty() || signatureRe.match(args).hasMatch()) {
                continue; // the fetcher's own declaration or definition — it names no key
            }
            QString unresolved;
            const QString key = keyNamedBy(args, constants, &unresolved);
            if (!unresolved.isEmpty()) {
                *unaccounted << QStringLiteral(
                                    "%1: cannot resolve SettingProperty::%2 — is it declared in "
                                    "ServiceConstants.h in the shape the scrape expects?")
                                    .arg(QFileInfo(path).fileName(), unresolved);
                continue;
            }
            if (!key.isEmpty()) {
                keys << key;
                continue;
            }
            // Names no key. The ONLY acceptable reason is that it is forwarding a key it was
            // handed, which is true exactly when it sits inside a fetcher's own body.
            if (insideBodyOf(functions[path], fetchers, m.capturedStart(0))) {
                continue;
            }
            *unaccounted << QStringLiteral(
                                "%1: a fetch at offset %2 names no key the scrape can read, and is not "
                                "inside a fetcher's body — bind the key at the call site, or its value is "
                                "never checked against the registry")
                                .arg(QFileInfo(path).fileName())
                                .arg(m.capturedStart(0));
        }
    }

    if (keys.isEmpty()) {
        *whyFailed = QStringLiteral("scraped 0 keys — has the call shape changed?");
    }
    return keys;
}

/// Every setting key the EDITOR fetches. Same contract as the effect, same failure when it
/// breaks — the editor is a separate process reading the same registry over the same bus,
/// so an unregistered key answers with an error and the editor silently keeps its built-in
/// default. That is how a gap overlay renders with the wrong inset and nothing says why.
///
/// The editor's PRIMITIVES are discovered the same way the effect's wrappers are, but from
/// the other end: any function whose body issues the `getSetting` / `getSettings` D-Bus call
/// is a fetcher. That covers today's query*Setting / querySettingsBatch helpers, and it also
/// covers a generic bus caller written tomorrow — the editor already has one
/// (ShaderDbusQueries::callSettings) that dispatches adaptor methods, and nothing but this
/// stops someone pointing it at getSetting where no scrape would ever see the key.
///
/// A fetcher's call sites must NAME their keys: a literal in the argument list, or a named
/// QStringList of literals declared in the same file (the batch shape). Every element of
/// that list must be a literal — not merely one of them.
QStringList keysFetchedByEditor(QString* whyFailed, QStringList* unaccounted)
{
    static const QSet<QString> notSource{QStringLiteral("md"),  QStringLiteral("txt"), QStringLiteral("json"),
                                         QStringLiteral("qml"), QStringLiteral("in"),  QStringLiteral("cmake"),
                                         QStringLiteral("ts"),  QStringLiteral("png"), QStringLiteral("svg")};
    QStringList files;
    QDirIterator it(QStringLiteral(PLASMAZONES_EDITOR_SRC_DIR), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (!notSource.contains(QFileInfo(path).suffix().toLower())) {
            files << path;
        }
    }
    if (files.isEmpty()) {
        *whyFailed = QStringLiteral("found no editor sources under %1").arg(QLatin1String(PLASMAZONES_EDITOR_SRC_DIR));
        return {};
    }

    QHash<QString, QString> sources;
    QHash<QString, QList<SourceFn>> functions;
    for (const QString& path : files) {
        const QString src = readSource(path, whyFailed);
        if (src.isEmpty()) {
            return {};
        }
        sources.insert(path, src);
        functions.insert(path, functionsIn(src));
    }

    // The D-Bus read methods. Wherever one of these is NAMED, a setting is being fetched.
    static const QRegularExpression dbusReadRe(QLatin1String("QStringLiteral\\(\"(getSetting|getSettings)\"\\)"));
    static const QRegularExpression literalRe(QLatin1String("QStringLiteral\\(\"([A-Za-z0-9_]+)\"\\)"));

    QStringList keys;
    QSet<QString> fetchers;
    // Pass 1: find the fetchers, and pick up any key named AT the bus call itself (a
    // generic caller pointed straight at getSetting, which no wrapper wraps).
    for (const QString& path : files) {
        const QString& src = sources[path];
        auto dit = dbusReadRe.globalMatch(src);
        while (dit.hasNext()) {
            const auto m = dit.next();
            bool enclosed = false;
            for (const SourceFn& fn : functions[path]) {
                if (m.capturedStart(0) > fn.bodyStart && m.capturedStart(0) < fn.bodyEnd) {
                    fetchers.insert(fn.name);
                    enclosed = true;
                }
            }
            if (!enclosed) {
                *unaccounted << QStringLiteral("%1: a getSetting call sits outside any function the scrape can see")
                                    .arg(QFileInfo(path).fileName());
            }
            // A key named AT the bus call, rather than at a wrapper's call site. That is what
            // a generic caller pointed straight at getSetting looks like, and the key is
            // right there to be read — so read it, instead of merely reporting that something
            // unreadable happened. The enclosing call is the one whose argument list holds the
            // method name; anything else in that list that looks like a key IS the key.
            const qsizetype callOpen = src.lastIndexOf(QLatin1Char('('), m.capturedStart(0));
            const qsizetype callClose = callOpen >= 0 ? matchDelimiter(src, callOpen, QLatin1Char(')')) : -1;
            if (callClose > m.capturedEnd(0)) {
                const QString callArgs = src.mid(callOpen + 1, callClose - callOpen - 1);
                auto lit = literalRe.globalMatch(callArgs);
                while (lit.hasNext()) {
                    const QString literal = lit.next().captured(1);
                    if (literal != m.captured(1)) { // not the method name itself
                        keys << literal;
                    }
                }
            }
        }
    }
    if (fetchers.isEmpty()) {
        *whyFailed = QStringLiteral("found no editor setting-fetch helpers — has the D-Bus call shape changed?");
        return {};
    }

    // Pass 2: every CALL to a fetcher, from outside that fetcher's own body, must name its
    // keys.
    QStringList fetcherNames(fetchers.cbegin(), fetchers.cend());
    std::sort(fetcherNames.begin(), fetcherNames.end(), [](const QString& a, const QString& b) {
        return a.size() != b.size() ? a.size() > b.size() : a < b;
    });
    const QRegularExpression callRe(
        QStringLiteral("(?<![A-Za-z0-9_])(?:%1)\\s*\\(").arg(fetcherNames.join(QLatin1Char('|'))));
    static const QRegularExpression signatureRe(QLatin1String("\\bQString\\b|\\bQStringList\\b|\\bQVariantList\\b"));

    for (const QString& path : files) {
        const QString& src = sources[path];
        auto cit = callRe.globalMatch(src);
        while (cit.hasNext()) {
            const auto m = cit.next();
            const qsizetype paren = m.capturedEnd(0) - 1;
            const qsizetype close = matchDelimiter(src, paren, QLatin1Char(')'));
            if (close < 0) {
                continue;
            }
            const QString args = src.mid(paren + 1, close - paren - 1);
            if (args.trimmed().isEmpty() || signatureRe.match(args).hasMatch()) {
                continue; // the helper's own declaration / definition — it names no key
            }
            if (insideBodyOf(functions[path], fetchers, m.capturedStart(0))) {
                continue; // plumbing: one helper calling another
            }
            // A key named right here.
            const auto lm = literalRe.match(args);
            if (lm.hasMatch()) {
                keys << lm.captured(1);
                continue;
            }
            // Otherwise it must pass a named QStringList of literals declared in this file.
            static const QRegularExpression identRe(QLatin1String("^\\s*([A-Za-z0-9_]+)\\s*$"));
            const auto im = identRe.match(args);
            const QString listName = im.hasMatch() ? im.captured(1) : QString();
            const qsizetype decl =
                listName.isEmpty() ? -1 : src.indexOf(QStringLiteral("QStringList %1 = {").arg(listName));
            const qsizetype open = decl >= 0 ? src.indexOf(QLatin1Char('{'), decl) : -1;
            const qsizetype end = open >= 0 ? matchDelimiter(src, open, QLatin1Char('}')) : -1;
            if (end < 0) {
                *unaccounted << QStringLiteral(
                                    "%1: a batch fetch passes something the scrape cannot read as a list "
                                    "of literal keys, so its keys are never checked")
                                    .arg(QFileInfo(path).fileName());
                continue;
            }
            // EVERY element must be a literal, not merely one of them. A list that mixes
            // QStringLiteral with a QLatin1String or a constant would otherwise contribute
            // the literals it happens to have and drop the rest, silently.
            const QStringList elements = splitTopLevel(src.mid(open + 1, end - open - 1));
            for (const QString& element : elements) {
                const auto em = literalRe.match(element);
                if (!em.hasMatch()) {
                    *unaccounted << QStringLiteral(
                                        "%1: batch key list %2 has an element the scrape cannot read as a "
                                        "literal key (%3) — it would go unchecked")
                                        .arg(QFileInfo(path).fileName(), listName, element);
                    continue;
                }
                keys << em.captured(1);
            }
        }
    }

    if (keys.isEmpty()) {
        *whyFailed = QStringLiteral("scraped 0 editor keys — has the call shape changed?");
    }
    return keys;
}

} // namespace PlasmaZones::TestHelpers
