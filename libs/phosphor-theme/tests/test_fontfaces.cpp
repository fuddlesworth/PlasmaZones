// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// FontFaces resolves each role to an installed family: the first candidate
// the font database knows, else the caller's fallback, so Tokens never
// binds a family that does not exist.

#include <PhosphorTheme/FontFaces.h>

#include <QFontDatabase>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTest>

using PhosphorTheme::FontFaces;

class TestFontFaces : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void resolvePrefersTheFirstInstalledCandidate()
    {
        const QStringList families = QFontDatabase::families();
        if (families.size() < 2) {
            QSKIP("fewer than two font families installed");
        }
        // A name no database carries first, then a real family: the real
        // one wins, and the fallback is untouched.
        const QStringList candidates{QStringLiteral("Phosphor Nonexistent Face 9f3a"), families.at(1)};
        QCOMPARE(FontFaces::resolve(candidates, QStringLiteral("fallback")), families.at(1));
    }

    void resolveFallsBackWhenNothingIsInstalled()
    {
        const QStringList candidates{QStringLiteral("Phosphor Nonexistent Face 9f3a"),
                                     QStringLiteral("Phosphor Nonexistent Face 7c21")};
        QCOMPARE(FontFaces::resolve(candidates, QStringLiteral("fallback")), QStringLiteral("fallback"));
    }

    void facesAreNeverEmpty()
    {
        FontFaces faces;
        QVERIFY(!faces.ui().isEmpty());
        QVERIFY(!faces.mono().isEmpty());
        // The preferred flags agree with the database.
        QCOMPARE(faces.uiPreferred(), QFontDatabase::hasFamily(FontFaces::uiCandidates().first()));
        QCOMPARE(faces.monoPreferred(), QFontDatabase::hasFamily(FontFaces::monoCandidates().first()));
    }

    void refreshOnlySignalsOnChange()
    {
        FontFaces faces;
        QSignalSpy spy(&faces, &FontFaces::facesChanged);
        faces.refresh();
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TestFontFaces)

#include "test_fontfaces.moc"
