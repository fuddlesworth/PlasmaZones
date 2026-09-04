// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// DesktopEntry parser + scanner over the fixture tree in fixtures/.
// Pins the spec behaviours a launcher actually depends on: localisation
// fallback, list splitting with escaped semicolons, Exec quoting and
// field-code stripping, TryExec rejection, NoDisplay/Hidden filtering,
// OnlyShowIn/NotShowIn against the current desktop, and first-directory-
// wins id precedence.

#include <PhosphorShellLauncher/DesktopEntry.h>

#include <QDir>
#include <QtTest/QtTest>

using PhosphorShellLauncher::DesktopEntry;
using PhosphorShellLauncher::DesktopEntryScanner;

namespace {
const QString kFixtures = QStringLiteral(PHOSPHOR_LAUNCHER_FIXTURES);

QString fixture(const char* rel)
{
    return QDir(kFixtures).filePath(QString::fromUtf8(rel));
}
} // namespace

class TestDesktopEntry : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesTheFieldsALauncherNeeds();
    void localisesWithTheSpecFallbackOrder();
    void localisesALocaleNameCarryingAScriptSubtag();
    void execArgsResolveQuotingAndDropFieldCodes();
    void rejectsWhatTheSpecSaysToTreatAsAbsent();
    void showsOnAppliesOnlyShowInThenNotShowIn();
    void scanDropsHiddenAndNoDisplay();
    void scanFirstDirectoryWinsPerId();
    void scanIdFoldsSubdirectoriesWithDashes();
};

void TestDesktopEntry::parsesTheFieldsALauncherNeeds()
{
    const auto e = DesktopEntry::parse(fixture("applications/firefox.desktop"), QString());
    QVERIFY(e.has_value());
    QCOMPARE(e->id, QStringLiteral("firefox"));
    QCOMPARE(e->name, QStringLiteral("Firefox"));
    QCOMPARE(e->genericName, QStringLiteral("Web Browser"));
    QCOMPARE(e->comment, QStringLiteral("Browse the World Wide Web"));
    QCOMPARE(e->icon, QStringLiteral("firefox"));
    QCOMPARE(e->exec, QStringLiteral("firefox %u"));
    QCOMPARE(e->keywords, (QStringList{QStringLiteral("Internet"), QStringLiteral("WWW"), QStringLiteral("Browser")}));
    QCOMPARE(e->categories, (QStringList{QStringLiteral("Network"), QStringLiteral("WebBrowser")}));
    QVERIFY(!e->terminal);
    QVERIFY(!e->noDisplay);
    QVERIFY(!e->hidden);
}

void TestDesktopEntry::localisesWithTheSpecFallbackOrder()
{
    const QString path = fixture("applications/kitty.desktop");
    // de_DE has no exact key; falls back to Name[de].
    auto de = DesktopEntry::parse(path, QStringLiteral("de_DE.UTF-8"));
    QVERIFY(de.has_value());
    QCOMPARE(de->name, QStringLiteral("Katze"));
    // Keywords localise the same way, and the list keeps an escaped
    // semicolon inside an item.
    QCOMPARE(de->keywords, (QStringList{QStringLiteral("terminal"), QStringLiteral("a;b")}));
    // An ESCAPED BACKSLASH before the separator is a different case, and the
    // one that a double unescape got wrong: `a\\;b;` is a keyword ending in a
    // backslash, then a separator, then "b". Reading the second backslash as
    // escaping the separator merges them into a single wrong keyword, and the
    // `a\;b` case above passes either way, so it cannot catch that.
    auto fr_kw = DesktopEntry::parse(path, QStringLiteral("fr_FR"));
    QVERIFY(fr_kw.has_value());
    QCOMPARE(fr_kw->keywords, (QStringList{QStringLiteral("a\\"), QStringLiteral("b")}));
    // An unrelated locale falls through to the unlocalised value.
    auto fr = DesktopEntry::parse(path, QStringLiteral("fr_FR"));
    QVERIFY(fr.has_value());
    QCOMPARE(fr->name, QStringLiteral("kitty"));
    QVERIFY(fr->terminal);
    // \s in a value is a space.
    QCOMPARE(fr->comment, QStringLiteral("Fast GPU terminal"));
}

// Qt's locale names carry a SCRIPT subtag for languages that need one, and
// the spec's key shape has no slot for it.
void TestDesktopEntry::localisesALocaleNameCarryingAScriptSubtag()
{
    const QString path = fixture("applications/kitty.desktop");
    auto sr = DesktopEntry::parse(path, QStringLiteral("sr_Latn_RS"));
    QVERIFY(sr.has_value());
    QCOMPARE(sr->name, QStringLiteral("Maca"));

    // The plain shape still works, and an unrelated locale still falls all
    // the way back to the unlocalised name.
    auto de = DesktopEntry::parse(path, QStringLiteral("de_AT"));
    QVERIFY(de.has_value());
    QCOMPARE(de->name, QStringLiteral("Katze"));
    auto none = DesktopEntry::parse(path, QStringLiteral("fi_FI"));
    QVERIFY(none.has_value());
    QCOMPARE(none->name, QStringLiteral("kitty"));
}

void TestDesktopEntry::execArgsResolveQuotingAndDropFieldCodes()
{
    DesktopEntry e;
    e.exec = QStringLiteral("firefox %u");
    QCOMPARE(e.execArgs(), QStringList{QStringLiteral("firefox")});

    // A quoted argument with a space survives as one argument; the
    // backslash-escaped quote inside it is literal; %% is a percent; a
    // code embedded in an argument is cut out of it.
    e.exec = QStringLiteral("\"/opt/my app/run\" --name=\"say \\\"hi\\\"\" 100%% --file=%f --icon %i %F");
    QCOMPARE(e.execArgs(),
             (QStringList{QStringLiteral("/opt/my app/run"), QStringLiteral("--name=say \"hi\""),
                          QStringLiteral("100%"), QStringLiteral("--file="), QStringLiteral("--icon")}));

    e.exec.clear();
    QVERIFY(e.execArgs().isEmpty());
}

void TestDesktopEntry::rejectsWhatTheSpecSaysToTreatAsAbsent()
{
    // Not an application.
    QVERIFY(!DesktopEntry::parse(fixture("applications/not-an-app.desktop"), QString()).has_value());
    // TryExec names a binary that does not exist on any PATH.
    QVERIFY(!DesktopEntry::parse(fixture("applications/missing-tryexec.desktop"), QString()).has_value());
    // Unreadable.
    QVERIFY(!DesktopEntry::parse(fixture("applications/does-not-exist.desktop"), QString()).has_value());
}

void TestDesktopEntry::showsOnAppliesOnlyShowInThenNotShowIn()
{
    DesktopEntry e;
    QVERIFY(e.showsOn({QStringLiteral("KDE")}));
    QVERIFY(e.showsOn({}));

    e.onlyShowIn = {QStringLiteral("GNOME")};
    QVERIFY(!e.showsOn({QStringLiteral("KDE")}));
    QVERIFY(e.showsOn({QStringLiteral("GNOME")}));
    // XDG_CURRENT_DESKTOP can list several; any match admits.
    QVERIFY(e.showsOn({QStringLiteral("KDE"), QStringLiteral("GNOME")}));
    // OnlyShowIn set and no current desktop known: not shown.
    QVERIFY(!e.showsOn({}));

    e.onlyShowIn.clear();
    e.notShowIn = {QStringLiteral("KDE")};
    QVERIFY(!e.showsOn({QStringLiteral("KDE")}));
    QVERIFY(e.showsOn({QStringLiteral("GNOME")}));
}

void TestDesktopEntry::scanDropsHiddenAndNoDisplay()
{
    const auto entries = DesktopEntryScanner::scan({fixture("applications")}, QString(), {QStringLiteral("KDE")});
    QStringList ids;
    for (const auto& e : entries) {
        ids.append(e.id);
    }
    QVERIFY(ids.contains(QStringLiteral("firefox")));
    QVERIFY(ids.contains(QStringLiteral("kitty")));
    // NoDisplay=true.
    QVERIFY(!ids.contains(QStringLiteral("hidden-tool")));
    // Hidden=true, which the spec calls "deleted". A separate leg of the
    // same filter, and previously no fixture set it, so that leg could be
    // removed with the whole suite green.
    QVERIFY(!ids.contains(QStringLiteral("deleted-tool")));
    // OnlyShowIn=GNOME against a KDE session.
    QVERIFY(!ids.contains(QStringLiteral("gnome-only")));
    // Not an application / missing TryExec never make it either.
    QVERIFY(!ids.contains(QStringLiteral("not-an-app")));
    QVERIFY(!ids.contains(QStringLiteral("missing-tryexec")));
}

void TestDesktopEntry::scanFirstDirectoryWinsPerId()
{
    // applications-local defines firefox too, with a different Name. Listed
    // first, it shadows the system one; listed second, it is ignored.
    auto local = DesktopEntryScanner::scan({fixture("applications-local"), fixture("applications")}, QString(),
                                           {QStringLiteral("KDE")});
    int firefoxes = 0;
    QString name;
    for (const auto& e : local) {
        if (e.id == QStringLiteral("firefox")) {
            ++firefoxes;
            name = e.name;
        }
    }
    QCOMPARE(firefoxes, 1);
    QCOMPARE(name, QStringLiteral("Firefox (local override)"));

    auto system = DesktopEntryScanner::scan({fixture("applications"), fixture("applications-local")}, QString(),
                                            {QStringLiteral("KDE")});
    // Counted, not asserted inside an `if`. An assertion guarded by the id
    // passes when the id is absent entirely, so a scan that returned nothing
    // satisfied the reversed half of this test.
    int systemFirefoxes = 0;
    QString systemName;
    for (const auto& e : system) {
        if (e.id == QStringLiteral("firefox")) {
            ++systemFirefoxes;
            systemName = e.name;
        }
    }
    QCOMPARE(systemFirefoxes, 1);
    QCOMPARE(systemName, QStringLiteral("Firefox"));
}

void TestDesktopEntry::scanIdFoldsSubdirectoriesWithDashes()
{
    const auto entries = DesktopEntryScanner::scan({fixture("applications")}, QString(), {QStringLiteral("KDE")});
    bool found = false;
    for (const auto& e : entries) {
        if (e.id == QStringLiteral("vendor-nested-tool")) {
            found = true;
            QCOMPARE(e.name, QStringLiteral("Nested Tool"));
        }
    }
    QVERIFY(found);
}

QTEST_GUILESS_MAIN(TestDesktopEntry)

#include "test_desktopentry.moc"
