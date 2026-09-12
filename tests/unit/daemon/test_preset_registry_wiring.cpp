// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_preset_registry_wiring.cpp
 * @brief `OverlayService::setPresetRegistry` must APPLY at set time, not only subscribe.
 *
 * The behaviour: the animator's per-role shader configs hold parameters flattened
 * against whichever preset registry was live when they were last built. The only other
 * thing that rebuilds them is `setSettings`, whose entire body sits behind
 * `if (m_settings != settings)` — and m_settings is ctor-owned while stop() never resets
 * it, so an init() re-run passes the same pointer and the whole block is skipped. Without
 * an apply at set time the second init left the OSD and popup show/hide legs tuned
 * against a destroyed store until a presetsChanged or a tree edit happened to arrive.
 *
 * Why this is a source scrape rather than a behaviour test, stated plainly because the
 * limitation is real: the apply fires ONLY on a second init (on the first, m_settings is
 * still null when setupShaderPresets runs), and standing up an OverlayService in a
 * fixture is not currently possible — its constructor requires a QGuiApplication, builds
 * a Wayland transport and a screen provider, and nothing in tests/unit constructs one
 * (only helpers/StubOverlayService.h, which implements the interface rather than the
 * class). So this pins a textual pattern, not a behaviour: it is brittle against
 * renaming, and it cannot see whether the call is reached, only that it is there.
 *
 * It is here because the defect class is severe and silent (stale tuning on every
 * surface the daemon animates, with nothing in the logs), and otherwise unguarded. The
 * honest replacement, if an OverlayService fixture is ever built for another reason, is
 * to set a registry twice and assert the animator's config for one role follows the
 * second one. Same shape and same reasoning as test_connect_sweep_ordering.cpp.
 */

#include <QFile>
#include <QString>
#include <QTest>

#include <algorithm>

namespace {

QString read(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(f.readAll());
}

/// The body of @p function in @p source, from its opening brace to the matching close.
/// Brace-counted rather than regex-matched so a nested lambda (which this function is
/// mostly made of) cannot end it early.
QString functionBody(const QString& source, const QString& signature)
{
    const qsizetype start = source.indexOf(signature);
    if (start < 0) {
        return {};
    }
    const qsizetype open = source.indexOf(QLatin1Char('{'), start);
    if (open < 0) {
        return {};
    }
    int depth = 0;
    for (qsizetype i = open; i < source.size(); ++i) {
        if (source.at(i) == QLatin1Char('{')) {
            ++depth;
        } else if (source.at(i) == QLatin1Char('}')) {
            --depth;
            if (depth == 0) {
                return source.mid(open, i - open + 1);
            }
        }
    }
    return {};
}

/// The brace depth of each occurrence of @p needle inside @p body, where the body's own
/// opening brace is depth 1.
///
/// Needed because `setPresetRegistry` calls applyShaderProfilesToAnimator TWICE: once at
/// set time, which is the behaviour this file pins, and once inside the presetsChanged
/// lambda, which is several levels deeper. A plain `contains` is satisfied by the lambda
/// alone — verified by deleting the set-time call and watching the naive check pass.
QList<int> depthsOf(const QString& body, const QString& needle)
{
    QList<int> depths;
    int depth = 0;
    for (qsizetype i = 0; i < body.size(); ++i) {
        if (body.at(i) == QLatin1Char('{')) {
            ++depth;
        } else if (body.at(i) == QLatin1Char('}')) {
            --depth;
        } else if (body.mid(i, needle.size()) == needle) {
            depths.append(depth);
        }
    }
    return depths;
}

} // namespace

class TestPresetRegistryWiring : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void setPresetRegistryAppliesAtSetTime()
    {
        const QString source = read(QStringLiteral(P_SOURCE_DIR "/src/daemon/overlayservice/shader.cpp"));
        QVERIFY2(!source.isEmpty(), "cannot read src/daemon/overlayservice/shader.cpp");

        const QString body = functionBody(source, QStringLiteral("void OverlayService::setPresetRegistry("));
        QVERIFY2(!body.isEmpty(), "setPresetRegistry not found — renamed or moved, and this guard needs updating");

        // The apply at SET TIME, identified by its brace depth: depth 1 is the function
        // body itself and 2 is the `if (m_settings)` guard around the call, while the
        // copy inside the presetsChanged lambda sits far deeper. Asserting only that the
        // name appears anywhere is satisfied by the lambda — checked by deleting the
        // set-time call, which left a `contains` check green.
        const QList<int> depths = depthsOf(body, QStringLiteral("applyShaderProfilesToAnimator("));
        QVERIFY2(!depths.isEmpty(), "setPresetRegistry does not call applyShaderProfilesToAnimator at all");
        QVERIFY2(*std::min_element(depths.cbegin(), depths.cend()) <= 2,
                 "setPresetRegistry must call applyShaderProfilesToAnimator at SET TIME, not only from the "
                 "presetsChanged handler: setSettings' body is gated on a pointer that never changes, so on an "
                 "init() re-run nothing else rebuilds the animator's per-role configs and they stay flattened "
                 "against the replaced registry");

        // And it still subscribes, so a LATER retune is picked up too. Both halves are
        // needed and deleting either leaves the other looking complete.
        QVERIFY2(body.contains(QStringLiteral("presetsChanged")),
                 "setPresetRegistry must also subscribe to presetsChanged, or a preset retuned after init never "
                 "reaches the daemon's surfaces");
    }

    void setPresetRegistrySeversTheOutgoingRegistry()
    {
        // The teardown half of the same function: the borrow is replaced on every
        // re-init, so the previous registry's connection has to go or the handler
        // stacks a second copy and fires twice per change.
        const QString source = read(QStringLiteral(P_SOURCE_DIR "/src/daemon/overlayservice/shader.cpp"));
        QVERIFY2(!source.isEmpty(), "cannot read src/daemon/overlayservice/shader.cpp");
        const QString body = functionBody(source, QStringLiteral("void OverlayService::setPresetRegistry("));
        QVERIFY2(!body.isEmpty(), "setPresetRegistry not found");
        QVERIFY2(body.contains(QStringLiteral("disconnect(")),
                 "setPresetRegistry must disconnect from the outgoing registry before overwriting the borrow");
    }
};

QTEST_MAIN(TestPresetRegistryWiring)
#include "test_preset_registry_wiring.moc"
