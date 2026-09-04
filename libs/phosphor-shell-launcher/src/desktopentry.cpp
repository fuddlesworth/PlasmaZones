// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShellLauncher/DesktopEntry.h>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>

namespace PhosphorShellLauncher {

namespace {

// Unescape a Desktop Entry string value: \s \n \t \r \\ per the spec.
QString unescape(QStringView raw)
{
    QString out;
    out.reserve(raw.size());
    for (qsizetype i = 0; i < raw.size(); ++i) {
        const QChar c = raw[i];
        if (c != u'\\' || i + 1 >= raw.size()) {
            out.append(c);
            continue;
        }
        const QChar next = raw[++i];
        switch (next.unicode()) {
        case u's':
            out.append(u' ');
            break;
        case u'n':
            out.append(u'\n');
            break;
        case u't':
            out.append(u'\t');
            break;
        case u'r':
            out.append(u'\r');
            break;
        case u'\\':
            out.append(u'\\');
            break;
        default:
            // Unknown escape: keep both characters, the spec does not say
            // to drop them and a launcher should show something.
            out.append(c);
            out.append(next);
            break;
        }
    }
    return out;
}

// A `;`-separated list value. `\;` is a literal semicolon. A trailing
// separator (the spec's convention) yields no empty final element.
QStringList splitList(QStringView raw)
{
    QStringList out;
    QString current;
    for (qsizetype i = 0; i < raw.size(); ++i) {
        const QChar c = raw[i];
        if (c == u'\\' && i + 1 < raw.size() && raw[i + 1] == u';') {
            current.append(u';');
            ++i;
            continue;
        }
        if (c == u';') {
            if (!current.isEmpty()) {
                out.append(unescape(current));
            }
            current.clear();
            continue;
        }
        current.append(c);
    }
    if (!current.isEmpty()) {
        out.append(unescape(current));
    }
    return out;
}

bool parseBool(QStringView raw)
{
    return raw.trimmed() == u"true";
}

// The locale fallback order the spec prescribes for `Key[LOCALE]`:
// lang_COUNTRY.ENCODING@MODIFIER → lang_COUNTRY@MODIFIER → lang_COUNTRY →
// lang@MODIFIER → lang → unlocalised. Encoding is always ignored.
QStringList localeCandidates(const QString& locale)
{
    if (locale.isEmpty()) {
        return {};
    }
    QString base = locale;
    const qsizetype dot = base.indexOf(u'.');
    QString modifier;
    const qsizetype at = base.indexOf(u'@');
    if (at >= 0) {
        modifier = base.mid(at + 1);
        base.truncate(at);
    }
    if (dot >= 0 && dot < base.size()) {
        base.truncate(dot);
    }
    QString lang = base;
    QString country;
    const qsizetype under = base.indexOf(u'_');
    if (under >= 0) {
        lang = base.left(under);
        country = base.mid(under + 1);
    }

    QStringList out;
    if (!country.isEmpty() && !modifier.isEmpty()) {
        out.append(lang + u'_' + country + u'@' + modifier);
    }
    if (!country.isEmpty()) {
        out.append(lang + u'_' + country);
    }
    if (!modifier.isEmpty()) {
        out.append(lang + u'@' + modifier);
    }
    out.append(lang);
    return out;
}

// Pick the most specific localised value present for `key`.
QString localised(const QHash<QString, QString>& group, const QString& key, const QStringList& locales)
{
    for (const QString& loc : locales) {
        const auto it = group.constFind(key + u'[' + loc + u']');
        if (it != group.constEnd()) {
            return unescape(it.value());
        }
    }
    return unescape(group.value(key));
}

// The desktop-file id for a file under `root`: relative path, '/' → '-',
// suffix stripped.
QString idFor(const QDir& root, const QString& filePath)
{
    QString rel = root.relativeFilePath(filePath);
    if (rel.endsWith(QLatin1String(".desktop"))) {
        rel.chop(8);
    }
    rel.replace(u'/', u'-');
    return rel;
}

} // namespace

std::optional<DesktopEntry> DesktopEntry::parse(const QString& filePath, const QString& locale, const QString& id)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return std::nullopt;
    }

    // Collect the [Desktop Entry] group only. Other groups (actions,
    // KDE-specific extensions) are skipped, not rejected.
    QHash<QString, QString> group;
    bool inEntryGroup = false;
    bool sawEntryGroup = false;
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const QStringView view = QStringView(line).trimmed();
        if (view.isEmpty() || view.startsWith(u'#')) {
            continue;
        }
        if (view.startsWith(u'[')) {
            inEntryGroup = (view == u"[Desktop Entry]");
            sawEntryGroup = sawEntryGroup || inEntryGroup;
            continue;
        }
        if (!inEntryGroup) {
            continue;
        }
        const qsizetype eq = view.indexOf(u'=');
        if (eq <= 0) {
            continue;
        }
        group.insert(view.left(eq).trimmed().toString(), view.mid(eq + 1).trimmed().toString());
    }
    if (!sawEntryGroup) {
        return std::nullopt;
    }
    if (group.value(QStringLiteral("Type")) != QLatin1String("Application")) {
        return std::nullopt;
    }

    const QStringList locales = localeCandidates(locale);
    DesktopEntry entry;
    entry.id = id.isEmpty() ? idFor(QFileInfo(filePath).dir(), filePath) : id;
    entry.filePath = filePath;
    entry.name = localised(group, QStringLiteral("Name"), locales);
    if (entry.name.isEmpty()) {
        return std::nullopt;
    }
    entry.genericName = localised(group, QStringLiteral("GenericName"), locales);
    entry.comment = localised(group, QStringLiteral("Comment"), locales);
    entry.icon = unescape(group.value(QStringLiteral("Icon")));
    entry.exec = unescape(group.value(QStringLiteral("Exec")));
    entry.path = unescape(group.value(QStringLiteral("Path")));
    entry.keywords = splitList(localised(group, QStringLiteral("Keywords"), locales));
    entry.categories = splitList(group.value(QStringLiteral("Categories")));
    entry.terminal = parseBool(group.value(QStringLiteral("Terminal")));
    entry.noDisplay = parseBool(group.value(QStringLiteral("NoDisplay")));
    entry.hidden = parseBool(group.value(QStringLiteral("Hidden")));
    entry.onlyShowIn = splitList(group.value(QStringLiteral("OnlyShowIn")));
    entry.notShowIn = splitList(group.value(QStringLiteral("NotShowIn")));

    // TryExec: the spec says an entry whose TryExec is not installed is to
    // be treated as absent. Relative names resolve on PATH; absolute
    // paths are checked directly.
    const QString tryExec = unescape(group.value(QStringLiteral("TryExec")));
    if (!tryExec.isEmpty()) {
        const bool found = QDir::isAbsolutePath(tryExec) ? QFileInfo(tryExec).isExecutable()
                                                         : !QStandardPaths::findExecutable(tryExec).isEmpty();
        if (!found) {
            return std::nullopt;
        }
    }

    return entry;
}

QStringList DesktopEntry::execArgs() const
{
    // The spec's quoting: arguments are separated by spaces; an argument
    // containing spaces or reserved characters is double-quoted, and
    // inside quotes a backslash escapes " ` $ \. Field codes are then
    // removed from the resulting arguments, and %% is a literal percent.
    QStringList args;
    QString current;
    bool inQuotes = false;
    bool haveArg = false;
    for (qsizetype i = 0; i < exec.size(); ++i) {
        const QChar c = exec[i];
        if (inQuotes) {
            if (c == u'\\' && i + 1 < exec.size()) {
                current.append(exec[++i]);
            } else if (c == u'"') {
                inQuotes = false;
            } else {
                current.append(c);
            }
            continue;
        }
        if (c == u'"') {
            inQuotes = true;
            haveArg = true;
            continue;
        }
        if (c.isSpace()) {
            if (haveArg) {
                args.append(current);
                current.clear();
                haveArg = false;
            }
            continue;
        }
        current.append(c);
        haveArg = true;
    }
    if (haveArg) {
        args.append(current);
    }

    // Field codes. A whole argument that is one code (the common "%U")
    // disappears; a code embedded in an argument is cut out of it.
    QStringList out;
    for (const QString& arg : std::as_const(args)) {
        QString cleaned;
        cleaned.reserve(arg.size());
        for (qsizetype i = 0; i < arg.size(); ++i) {
            if (arg[i] != u'%' || i + 1 >= arg.size()) {
                cleaned.append(arg[i]);
                continue;
            }
            const QChar code = arg[++i];
            if (code == u'%') {
                cleaned.append(u'%');
            }
            // Every other code (f u F U d D n N i c k v m) is dropped.
        }
        if (!cleaned.isEmpty() || arg.isEmpty()) {
            // An argument that was ONLY a field code contributes nothing;
            // an argument that was empty to begin with ("") is kept.
            if (!(cleaned.isEmpty() && !arg.isEmpty())) {
                out.append(cleaned);
            }
        }
    }
    return out;
}

bool DesktopEntry::showsOn(const QStringList& currentDesktop) const
{
    if (!onlyShowIn.isEmpty()) {
        bool listed = false;
        for (const QString& de : currentDesktop) {
            if (onlyShowIn.contains(de)) {
                listed = true;
                break;
            }
        }
        if (!listed) {
            return false;
        }
    }
    for (const QString& de : currentDesktop) {
        if (notShowIn.contains(de)) {
            return false;
        }
    }
    return true;
}

QStringList DesktopEntryScanner::defaultDirectories()
{
    return QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
}

QList<DesktopEntry> DesktopEntryScanner::scan(const QStringList& directories, const QString& locale,
                                              const QStringList& currentDesktop)
{
    QList<DesktopEntry> out;
    QSet<QString> seen;
    for (const QString& dirPath : directories) {
        const QDir root(dirPath);
        if (!root.exists()) {
            continue;
        }
        QDirIterator it(dirPath, {QStringLiteral("*.desktop")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString filePath = it.next();
            const QString id = idFor(root, filePath);
            // First directory to define an id wins. Recorded BEFORE
            // parsing so a user's deliberately broken override still
            // shadows the system entry rather than letting it through.
            if (seen.contains(id)) {
                continue;
            }
            seen.insert(id);
            auto entry = DesktopEntry::parse(filePath, locale, id);
            if (!entry) {
                continue;
            }
            if (entry->noDisplay || entry->hidden || !entry->showsOn(currentDesktop)) {
                continue;
            }
            out.append(std::move(*entry));
        }
    }
    return out;
}

} // namespace PhosphorShellLauncher
