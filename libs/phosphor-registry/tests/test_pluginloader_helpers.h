// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

// Shared scaffolding for the PluginLoader test executables. Two
// translation units consume this header:
//
//   - test_phosphor_registry_pluginloader.cpp      (load + factory-
//                                                   error + intro-
//                                                   spection slots)
//   - test_phosphor_registry_pluginloader_lifecycle.cpp
//                                                  (rescan re-entry +
//                                                   destructor-pin
//                                                   slots)
//
// Each TU defines its own QTest subclass and QTEST_MAIN. The helpers
// here factor out the WarningCapture instrumentation and the plugin-
// fixture install routines so neither piece of scaffolding has to be
// edited in two places when the loader contract changes.
//
// The PHOSPHOR_REGISTRY_FAKE_PLUGIN_*_DIR macros consumed by the
// install helpers below are defined per-target via
// target_compile_definitions in tests/CMakeLists.txt; every test
// executable that includes this header MUST receive the full set of
// macros, otherwise the static_assert / #error guards trip at
// compile time. Centralising the install helpers here means we
// can't accidentally drift the macro list between executables.

#include <PhosphorRegistry/IBarWidgetFactory.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#ifndef PHOSPHOR_REGISTRY_FAKE_PLUGIN_DIR
#error "PHOSPHOR_REGISTRY_FAKE_PLUGIN_DIR must be defined by tests/CMakeLists.txt"
#endif
#ifndef PHOSPHOR_REGISTRY_FAKE_PLUGIN_SECONDARY_DIR
#error "PHOSPHOR_REGISTRY_FAKE_PLUGIN_SECONDARY_DIR must be defined by tests/CMakeLists.txt"
#endif
#ifndef PHOSPHOR_REGISTRY_FAKE_PLUGIN_IDMISMATCH_DIR
#error "PHOSPHOR_REGISTRY_FAKE_PLUGIN_IDMISMATCH_DIR must be defined by tests/CMakeLists.txt"
#endif
#ifndef PHOSPHOR_REGISTRY_FAKE_PLUGIN_NULLFACTORY_DIR
#error "PHOSPHOR_REGISTRY_FAKE_PLUGIN_NULLFACTORY_DIR must be defined by tests/CMakeLists.txt"
#endif
#ifndef PHOSPHOR_REGISTRY_FAKE_PLUGIN_NOENTRY_DIR
#error "PHOSPHOR_REGISTRY_FAKE_PLUGIN_NOENTRY_DIR must be defined by tests/CMakeLists.txt"
#endif

namespace PhosphorRegistryTestHelpers {

// Forward declaration so WarningCapture can friend the C-style
// callback without exposing its body before the class definition.
void warningCapturingHandler(QtMsgType type, const QMessageLogContext& context, const QString& message);

// File-scope WarningCapture instrumentation. Installs a Qt message
// handler that captures every qWarning emitted while it is in scope.
// We can't enforce the "no qWarning fires" contract via the process-
// wide QT_FATAL_WARNINGS env var because the sibling tests in this
// binary rely on QTest::ignoreMessage to suppress intentional
// warning paths (ignoresInvalidManifest, rejectsAbiVersionMismatch,
// ...), and Qt's default handler aborts on a fatal-warning trigger
// before the ignore-message infrastructure can match. Per-test
// instrumentation it is.
//
// qInstallMessageHandler takes a plain function pointer — the
// callback has no `this` to capture, so the sink the handler
// writes into has to be reachable via static storage. We route
// that through a single static "active capture" pointer (set in
// the ctor, restored in the dtor) and hold the QStringList* sink
// reference on the instance, so the sink is folded into the
// capture object itself rather than living as a separate file-
// scope global. Nesting two captures is structurally safe: the
// ctor saves whatever s_activeCapture was before it took over,
// and the dtor restores that prior pointer — so an inner scope's
// teardown hands control back to the outer scope's capture, not
// to nullptr. The Q_ASSERT in the ctor remains in place as a
// debug-build advisory that the test plan didn't intend to nest;
// the runtime behaviour is correct either way thanks to the
// save-and-restore.
class WarningCapture
{
public:
    explicit WarningCapture(QStringList& sink)
        : m_sink(&sink)
    {
        // The save/restore in m_priorActiveCapture (line below) +
        // ~WarningCapture's restoration makes nesting structurally
        // safe — the previous Q_ASSERT that forbade nesting in debug
        // builds contradicted that contract. Removed so debug and
        // release both follow the same documented nesting semantics.
        m_priorActiveCapture = s_activeCapture;
        s_activeCapture = this;
        m_priorHandler = qInstallMessageHandler(warningCapturingHandler);
    }
    ~WarningCapture()
    {
        qInstallMessageHandler(m_priorHandler);
        // Restore the capture that was active before this scope
        // opened (typically nullptr; an outer WarningCapture if
        // a test ever nests them). Unconditional nulling would
        // strand the outer scope's captured warnings on the floor
        // for the rest of its lifetime.
        s_activeCapture = m_priorActiveCapture;
    }
    Q_DISABLE_COPY_MOVE(WarningCapture)

    QtMessageHandler priorHandler() const
    {
        return m_priorHandler;
    }

    QStringList* sink() const
    {
        return m_sink;
    }

private:
    // Single-active pointer the C-style message-handler callback
    // chains through to invoke the saved prior handler and write
    // into the caller-owned sink. Set in the ctor, restored to
    // the prior value in the dtor — nesting is structurally safe
    // because each instance remembers what was active before it
    // and hands control back on destruction. Kept private so the
    // only access path is through the friended handler below;
    // tests must use the WarningCapture RAII scope to interact
    // with it. `inline` so the header can be included from
    // multiple TUs in the same executable without an ODR violation.
    inline static WarningCapture* s_activeCapture = nullptr;
    QtMessageHandler m_priorHandler = nullptr;
    QStringList* m_sink = nullptr;
    WarningCapture* m_priorActiveCapture = nullptr;

    friend void warningCapturingHandler(QtMsgType type, const QMessageLogContext& context, const QString& message);
};

// DO NOT move warningCapturingHandler into a separate .cpp — the
// friend relationship above requires its definition to be co-located
// with the WarningCapture class declaration (the handler reaches
// into the private `s_activeCapture` static and the private accessors).
// Cross-TU semantics work today because the function is `inline` and
// the static is `inline static`, so ODR-merging across translation
// units in the same executable produces a single live copy. Pulling
// the body into a .cpp would either break the friend access (TU
// without the class header) or require re-friending in every TU,
// neither of which buys anything.
inline void warningCapturingHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    // Snapshot s_activeCapture once at entry. Two reads of the
    // global mid-handler would be vulnerable to a (hypothetical)
    // re-entrant install/uninstall between the type-check and the
    // chain-call: capture-as-nullptr after the type-check would
    // skip the sink append, then we would still try to invoke the
    // prior handler via the same now-stale pointer on the next
    // read — or vice versa. PluginLoader tests are GUI-thread-only
    // so this is more about correctness clarity than thread safety,
    // but a single read makes the control flow obviously consistent.
    WarningCapture* const active = WarningCapture::s_activeCapture;
    if (!active) {
        return;
    }
    if (type == QtWarningMsg && active->sink()) {
        active->sink()->append(message);
    }
    if (active->priorHandler()) {
        active->priorHandler()(type, context, message);
    }
}

// Install one of the templated fake-plugin fixtures into
// pluginRoot/<subdir>. fixtureDir is the build-tree path to the
// pre-built .so + manifest.json (defined by tests/CMakeLists.txt
// PHOSPHOR_REGISTRY_FAKE_PLUGIN_*_DIR macros). soBasename is the .so's
// filename WITHOUT extension (e.g.
// "libphosphor_registry_test_fake_plugin"). The .so is copied into
// the destination as <subdir>.so so the loader's manifest-id-vs-
// directory-basename rule resolves cleanly. Out-param destDir receives
// the installed plugin directory path on success. Returns false on any
// setup failure so the caller can QVERIFY the result — Qt's QVERIFY
// macro inside a helper only returns from the helper, so wrapping the
// checks in throwaway lambdas (the previous shape) silently swallowed
// failures and left the test running against an incomplete fixture.
[[nodiscard]] inline bool installPluginFixture(const QString& pluginRoot, const QString& subdir,
                                               const QString& fixtureDir, const QString& soBasename, QString& destDir)
{
    destDir = QDir(pluginRoot).absoluteFilePath(subdir);
    if (!QDir().mkpath(destDir)) {
        qWarning().noquote() << "installPluginFixture: mkpath failed for" << destDir;
        return false;
    }
    const QString fixtureSo = QDir(fixtureDir).absoluteFilePath(soBasename + QStringLiteral(".so"));
    const QString fixtureManifest = QDir(fixtureDir).absoluteFilePath(QStringLiteral("manifest.json"));
    if (!QFileInfo::exists(fixtureSo)) {
        qWarning().noquote() << "installPluginFixture: missing fixture .so:" << fixtureSo;
        return false;
    }
    if (!QFileInfo::exists(fixtureManifest)) {
        qWarning().noquote() << "installPluginFixture: missing fixture manifest:" << fixtureManifest;
        return false;
    }

    const QString destSo = QDir(destDir).absoluteFilePath(subdir + QStringLiteral(".so"));
    const QString destManifest = QDir(destDir).absoluteFilePath(QStringLiteral("manifest.json"));

    if (!QFile::copy(fixtureSo, destSo)) {
        qWarning().noquote() << "installPluginFixture: failed to copy" << fixtureSo << "->" << destSo;
        return false;
    }
    if (!QFile::copy(fixtureManifest, destManifest)) {
        qWarning().noquote() << "installPluginFixture: failed to copy" << fixtureManifest << "->" << destManifest;
        return false;
    }
    return true;
}

[[nodiscard]] inline bool installFakePlugin(const QString& pluginRoot, const QString& subdir, QString& destDir)
{
    return installPluginFixture(pluginRoot, subdir, QStringLiteral(PHOSPHOR_REGISTRY_FAKE_PLUGIN_DIR),
                                QStringLiteral("libphosphor_registry_test_fake_plugin"), destDir);
}

[[nodiscard]] inline bool installFakePluginSecondary(const QString& pluginRoot, const QString& subdir, QString& destDir)
{
    // Distinct manifest id ("fake-plugin-secondary") so the loader can
    // hold BOTH the primary and secondary fixtures in m_plugins at the
    // same time. Required by the constFind-early-continue regression
    // test, which needs the outer scan loop's snapshot to contain two
    // ids so the inner rescan can clear one of them between iterations.
    return installPluginFixture(pluginRoot, subdir, QStringLiteral(PHOSPHOR_REGISTRY_FAKE_PLUGIN_SECONDARY_DIR),
                                QStringLiteral("libphosphor_registry_test_fake_plugin_secondary"), destDir);
}

[[nodiscard]] inline bool installFakePluginIdMismatch(const QString& pluginRoot, QString& destDir)
{
    return installPluginFixture(pluginRoot, QStringLiteral("id-mismatch-plugin"),
                                QStringLiteral(PHOSPHOR_REGISTRY_FAKE_PLUGIN_IDMISMATCH_DIR),
                                QStringLiteral("libphosphor_registry_test_fake_plugin_idmismatch"), destDir);
}

[[nodiscard]] inline bool installFakePluginNullFactory(const QString& pluginRoot, QString& destDir)
{
    return installPluginFixture(pluginRoot, QStringLiteral("null-factory-plugin"),
                                QStringLiteral(PHOSPHOR_REGISTRY_FAKE_PLUGIN_NULLFACTORY_DIR),
                                QStringLiteral("libphosphor_registry_test_fake_plugin_nullfactory"), destDir);
}

[[nodiscard]] inline bool installFakePluginNoEntry(const QString& pluginRoot, QString& destDir)
{
    return installPluginFixture(pluginRoot, QStringLiteral("no-entry-plugin"),
                                QStringLiteral(PHOSPHOR_REGISTRY_FAKE_PLUGIN_NOENTRY_DIR),
                                QStringLiteral("libphosphor_registry_test_fake_plugin_noentry"), destDir);
}

} // namespace PhosphorRegistryTestHelpers
