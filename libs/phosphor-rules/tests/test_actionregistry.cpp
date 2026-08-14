// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRules/RuleAction.h>

#include <QJsonObject>
#include <QSet>
#include <QTest>

using namespace PhosphorRules;

namespace {

RuleAction makeAction(QLatin1StringView type, const QJsonObject& params = {})
{
    RuleAction a;
    a.type = QString(type);
    a.params = params;
    return a;
}

} // namespace

// `ActionRegistry` is a process-global singleton. `testRegisterCustomAction`
// registers a sentinel and unregisters it again (via `unregisterAction`) so it
// leaves the singleton pristine. As defence in depth against any future test
// that mutates the registry without cleaning up, no test here asserts an
// *absolute* `registeredTypes().size()` — that count is not guaranteed stable
// across the suite. Builtins are asserted individually instead, and
// `testBuiltinsRegistered` stays declared FIRST so it observes the registry
// before any test mutates it.
class TestActionRegistry : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testBuiltinsRegistered()
    {
        const ActionRegistry& reg = ActionRegistry::instance();
        // Assert each builtin individually — never an absolute
        // `registeredTypes().size()`, see the singleton-pollution note above.
        // Note a same-type DOUBLE registration across the two builtin TUs is
        // not detectable post-hoc (registerAction is register-or-replace and
        // the hash dedupes); a duplicate whose descriptor DIFFERS surfaces
        // through the per-type behaviour tests (slots, terminal flag, domain
        // pins) rather than through any count here.
        QVERIFY(reg.isRegistered(QString(ActionType::SetEngineMode)));
        QVERIFY(reg.isRegistered(QString(ActionType::SetSnappingLayout)));
        QVERIFY(reg.isRegistered(QString(ActionType::SetTilingAlgorithm)));
        QVERIFY(reg.isRegistered(QString(ActionType::SetScrollingTemplate)));
        QVERIFY(reg.isRegistered(QString(ActionType::DisableEngine)));
        QVERIFY(reg.isRegistered(QString(ActionType::Exclude)));
        QVERIFY(reg.isRegistered(QString(ActionType::ExcludePlacement)));
        QVERIFY(reg.isRegistered(QString(ActionType::ExcludeAnimations)));
        QVERIFY(reg.isRegistered(QString(ActionType::ExcludeDecorations)));
        QVERIFY(reg.isRegistered(QString(ActionType::Float)));
        QVERIFY(reg.isRegistered(QString(ActionType::OverrideAnimationShader)));
        QVERIFY(reg.isRegistered(QString(ActionType::OverrideAnimationTiming)));
        QVERIFY(reg.isRegistered(QString(ActionType::OverrideAnimationCurve)));
        QVERIFY(reg.isRegistered(QString(ActionType::SetOpacity)));
        QVERIFY(reg.isRegistered(QString(ActionType::RestorePosition)));
        QVERIFY(reg.isRegistered(QString(ActionType::LockContext)));
        QVERIFY(reg.isRegistered(QString(ActionType::DefaultLayoutAssignment)));
        QVERIFY(reg.isRegistered(QString(ActionType::SetOsdEnabled)));
        QVERIFY(reg.isRegistered(QString(ActionType::SetDragSelectorEnabled)));
    }

    void testSlots()
    {
        const ActionRegistry& reg = ActionRegistry::instance();

        QJsonObject mode;
        mode.insert(QStringLiteral("mode"), QStringLiteral("autotile"));
        QCOMPARE(reg.slotFor(makeAction(ActionType::SetEngineMode, mode)), QString(ActionSlot::EngineMode));

        QJsonObject layout;
        layout.insert(QStringLiteral("layoutId"), QStringLiteral("{x}"));
        QCOMPARE(reg.slotFor(makeAction(ActionType::SetSnappingLayout, layout)), QString(ActionSlot::Layout));

        QJsonObject algo;
        algo.insert(QStringLiteral("algorithm"), QStringLiteral("dwindle"));
        // setSnappingLayout and setTilingAlgorithm share the `layout` slot.
        QCOMPARE(reg.slotFor(makeAction(ActionType::SetTilingAlgorithm, algo)), QString(ActionSlot::Layout));

        QCOMPARE(reg.slotFor(makeAction(ActionType::Float)), QString(ActionSlot::Float));
        QCOMPARE(reg.slotFor(makeAction(ActionType::Exclude)), QString(ActionSlot::Manage));
        // The scoped exclusion siblings: ExcludePlacement deliberately shares
        // the Manage slot (same "unmanaged by placement" concept, and both are
        // terminal so neither ever fills it); ExcludeDecorations gets its own
        // declared-for-completeness slot. A copy-pasted wrong constantSlot is
        // exactly the failure this test exists to catch.
        QCOMPARE(reg.slotFor(makeAction(ActionType::ExcludePlacement)), QString(ActionSlot::Manage));
        QCOMPARE(reg.slotFor(makeAction(ActionType::ExcludeDecorations)), QString(ActionSlot::DecorationExclude));
    }

    void testTerminalFlagCompleteness()
    {
        // Canary over the LIVE registry: the terminal bit is the highest-
        // consequence descriptor field (a terminal action stops a resolve
        // walk), so pin the exact membership — isTerminal iff the type is one
        // of the four Exclude-family builtins. A future action registered
        // terminal by copy-paste, or an Exclude-family member losing the
        // flag, fails here rather than surfacing as a silent behaviour
        // change. Iterates registeredTypes() so no absolute count is
        // asserted (singleton-pollution note above); a non-builtin sentinel
        // registered terminal by another test in this process would trip
        // this canary, which is the correct outcome — tests that register
        // sentinels unregister them (see unregisterAction below).
        const ActionRegistry& reg = ActionRegistry::instance();
        const QSet<QString> terminalFamily = {QString(ActionType::Exclude), QString(ActionType::ExcludePlacement),
                                              QString(ActionType::ExcludeAnimations),
                                              QString(ActionType::ExcludeDecorations)};
        for (const QString& type : reg.registeredTypes()) {
            RuleAction probe;
            probe.type = type;
            QCOMPARE(reg.isTerminal(probe), terminalFamily.contains(type));
        }
    }

    void testAnimationSlotsAreEventScoped()
    {
        const ActionRegistry& reg = ActionRegistry::instance();

        QJsonObject open;
        open.insert(QStringLiteral("event"), QStringLiteral("window.open"));
        QJsonObject close;
        close.insert(QStringLiteral("event"), QStringLiteral("window.close"));

        const QString openShaderSlot = reg.slotFor(makeAction(ActionType::OverrideAnimationShader, open));
        const QString closeShaderSlot = reg.slotFor(makeAction(ActionType::OverrideAnimationShader, close));
        QCOMPARE(openShaderSlot, QStringLiteral("anim-shader:window.open"));
        QCOMPARE(closeShaderSlot, QStringLiteral("anim-shader:window.close"));
        QVERIFY(openShaderSlot != closeShaderSlot);

        // Shader and timing axes stay independent even for the same event.
        const QString openTimingSlot = reg.slotFor(makeAction(ActionType::OverrideAnimationTiming, open));
        QCOMPARE(openTimingSlot, QStringLiteral("anim-timing:window.open"));
        QVERIFY(openTimingSlot != openShaderSlot);
    }

    void testTerminalFlag()
    {
        const ActionRegistry& reg = ActionRegistry::instance();
        // The four Exclude-family actions are the terminal builtins — every
        // other builtin must be non-terminal, so evaluation continues past a
        // match.
        QVERIFY(reg.isTerminal(makeAction(ActionType::Exclude)));
        QVERIFY(reg.isTerminal(makeAction(ActionType::ExcludePlacement)));
        QVERIFY(reg.isTerminal(makeAction(ActionType::ExcludeAnimations)));
        QVERIFY(reg.isTerminal(makeAction(ActionType::ExcludeDecorations)));
        QVERIFY(!reg.isTerminal(makeAction(ActionType::Float)));
        QVERIFY(!reg.isTerminal(makeAction(ActionType::SetEngineMode)));
        QVERIFY(!reg.isTerminal(makeAction(ActionType::SetSnappingLayout)));
        QVERIFY(!reg.isTerminal(makeAction(ActionType::SetTilingAlgorithm)));
        QVERIFY(!reg.isTerminal(makeAction(ActionType::DisableEngine)));
        QVERIFY(!reg.isTerminal(makeAction(ActionType::OverrideAnimationShader)));
        QVERIFY(!reg.isTerminal(makeAction(ActionType::OverrideAnimationTiming)));
        QVERIFY(!reg.isTerminal(makeAction(ActionType::OverrideAnimationCurve)));
        QVERIFY(!reg.isTerminal(makeAction(ActionType::SetOpacity)));
        QVERIFY(!reg.isTerminal(makeAction(ActionType::RestorePosition)));
        QVERIFY(!reg.isTerminal(makeAction(ActionType::LockContext)));
        QVERIFY(!reg.isTerminal(makeAction(ActionType::DefaultLayoutAssignment)));
    }

    void testValidateAcceptsWellFormedRegisteredAction()
    {
        const ActionRegistry& reg = ActionRegistry::instance();
        // A registered type with a params payload its descriptor accepts.
        QJsonObject mode;
        mode.insert(QStringLiteral("mode"), QStringLiteral("autotile"));
        QVERIFY(reg.validate(makeAction(ActionType::SetEngineMode, mode)));

        QJsonObject opacity;
        opacity.insert(QStringLiteral("value"), 0.5);
        QVERIFY(reg.validate(makeAction(ActionType::SetOpacity, opacity)));
    }

    void testValidateRejectsRegisteredTypeWithBadParams()
    {
        const ActionRegistry& reg = ActionRegistry::instance();
        // Registered type, but the params fail the descriptor's predicate:
        // setEngineMode requires a non-empty `mode` string.
        QVERIFY(!reg.validate(makeAction(ActionType::SetEngineMode)));
        QJsonObject emptyMode;
        emptyMode.insert(QStringLiteral("mode"), QString());
        QVERIFY(!reg.validate(makeAction(ActionType::SetEngineMode, emptyMode)));

        // setOpacity requires a numeric `value` in [0, 1] — out of range fails.
        QJsonObject badOpacity;
        badOpacity.insert(QStringLiteral("value"), 1.5);
        QVERIFY(!reg.validate(makeAction(ActionType::SetOpacity, badOpacity)));
    }

    void testSetScrollingTemplateAction()
    {
        const ActionRegistry& reg = ActionRegistry::instance();
        // Same layoutId-keyed value shape as SetSnappingLayout, but its OWN
        // slot — sharing the layout slot would shadow the snapping half of
        // the lossless pair.
        QJsonObject layout;
        layout.insert(QStringLiteral("layoutId"), QStringLiteral("{x}"));
        QVERIFY(reg.validate(makeAction(ActionType::SetScrollingTemplate, layout)));
        QCOMPARE(reg.slotFor(makeAction(ActionType::SetScrollingTemplate, layout)),
                 QString(ActionSlot::ScrollingTemplate));

        // Missing and empty layoutId both fail, mirroring SetSnappingLayout's
        // validator: an empty template is expressed by OMITTING the action,
        // never by an empty param.
        QVERIFY(!reg.validate(makeAction(ActionType::SetScrollingTemplate)));
        QJsonObject emptyLayout;
        emptyLayout.insert(QStringLiteral("layoutId"), QString());
        QVERIFY(!reg.validate(makeAction(ActionType::SetScrollingTemplate, emptyLayout)));
    }

    void testRestorePositionAction()
    {
        const ActionRegistry& reg = ActionRegistry::instance();
        QVERIFY(reg.isRegistered(QString(ActionType::RestorePosition)));

        QJsonObject on;
        on.insert(QStringLiteral("value"), true);
        QJsonObject off;
        off.insert(QStringLiteral("value"), false);

        // Window-domain boolean action filling the dedicated restore-position slot.
        QCOMPARE(reg.slotFor(makeAction(ActionType::RestorePosition, on)), QString(ActionSlot::RestorePosition));
        QCOMPARE(reg.domainFor(makeAction(ActionType::RestorePosition, on)), ActionDomain::Window);
        QVERIFY(!reg.isTerminal(makeAction(ActionType::RestorePosition, on)));

        // Requires a boolean `value`; both true and false are well-formed.
        QVERIFY(reg.validate(makeAction(ActionType::RestorePosition, on)));
        QVERIFY(reg.validate(makeAction(ActionType::RestorePosition, off)));
        QVERIFY2(!reg.validate(makeAction(ActionType::RestorePosition)), "missing value must fail validation");
        QJsonObject notBool;
        notBool.insert(QStringLiteral("value"), 1);
        QVERIFY2(!reg.validate(makeAction(ActionType::RestorePosition, notBool)), "non-bool value must fail");
    }

    void testLockContextAction()
    {
        const ActionRegistry& reg = ActionRegistry::instance();
        QVERIFY(reg.isRegistered(QString(ActionType::LockContext)));

        QJsonObject on;
        on.insert(QStringLiteral("value"), true);
        QJsonObject off;
        off.insert(QStringLiteral("value"), false);

        // Context-domain boolean action filling the dedicated locked slot.
        // Non-terminal: a lock-only context rule composes with other context
        // slots (e.g. a separate gap rule) rather than short-circuiting.
        QCOMPARE(reg.slotFor(makeAction(ActionType::LockContext, on)), QString(ActionSlot::Locked));
        QCOMPARE(reg.domainFor(makeAction(ActionType::LockContext, on)), ActionDomain::Context);
        QVERIFY(!reg.isTerminal(makeAction(ActionType::LockContext, on)));

        // Requires a boolean `value`; both true and false are well-formed.
        QVERIFY(reg.validate(makeAction(ActionType::LockContext, on)));
        QVERIFY(reg.validate(makeAction(ActionType::LockContext, off)));
        QVERIFY2(!reg.validate(makeAction(ActionType::LockContext)), "missing value must fail validation");
        QJsonObject notBool;
        notBool.insert(QStringLiteral("value"), 1);
        QVERIFY2(!reg.validate(makeAction(ActionType::LockContext, notBool)), "non-bool value must fail");
    }

    void testDefaultLayoutAssignmentAction()
    {
        const ActionRegistry& reg = ActionRegistry::instance();
        QVERIFY(reg.isRegistered(QString(ActionType::DefaultLayoutAssignment)));

        QJsonObject allow;
        allow.insert(QStringLiteral("value"), true);
        QJsonObject suppress;
        suppress.insert(QStringLiteral("value"), false);

        // Context-domain boolean action filling its dedicated slot. Non-terminal:
        // a default-assignment-only context rule composes with other context
        // slots rather than short-circuiting (mirrors LockContext).
        QCOMPARE(reg.slotFor(makeAction(ActionType::DefaultLayoutAssignment, allow)),
                 QString(ActionSlot::DefaultAssignment));
        QCOMPARE(reg.domainFor(makeAction(ActionType::DefaultLayoutAssignment, allow)), ActionDomain::Context);
        QVERIFY(!reg.isTerminal(makeAction(ActionType::DefaultLayoutAssignment, allow)));

        // Requires a boolean `value`; both true (allow) and false (suppress) are
        // well-formed.
        QVERIFY(reg.validate(makeAction(ActionType::DefaultLayoutAssignment, allow)));
        QVERIFY(reg.validate(makeAction(ActionType::DefaultLayoutAssignment, suppress)));
        QVERIFY2(!reg.validate(makeAction(ActionType::DefaultLayoutAssignment)), "missing value must fail validation");
        QJsonObject notBool;
        notBool.insert(QStringLiteral("value"), 1);
        QVERIFY2(!reg.validate(makeAction(ActionType::DefaultLayoutAssignment, notBool)), "non-bool value must fail");
    }

    void testSetDragSelectorEnabledAction()
    {
        const ActionRegistry& reg = ActionRegistry::instance();
        QVERIFY(reg.isRegistered(QString(ActionType::SetDragSelectorEnabled)));

        QJsonObject show;
        show.insert(QStringLiteral("value"), true);
        QJsonObject hide;
        hide.insert(QStringLiteral("value"), false);

        // Context-domain boolean action filling its own dedicated slot.
        // Non-terminal: a selector-only context rule composes with the other
        // context slots rather than short-circuiting (mirrors SetOsdEnabled).
        QCOMPARE(reg.slotFor(makeAction(ActionType::SetDragSelectorEnabled, show)),
                 QString(ActionSlot::DragSelectorEnabled));
        // Its OWN slot, not the OSD one it was modelled on. The two overrides
        // are independent verdicts, so a copy-pasted constantSlot would make
        // an OSD rule and a selector rule clobber each other.
        QCOMPARE(reg.slotFor(makeAction(ActionType::SetOsdEnabled, show)), QString(ActionSlot::OsdEnabled));
        QVERIFY(reg.slotFor(makeAction(ActionType::SetDragSelectorEnabled, show))
                != reg.slotFor(makeAction(ActionType::SetOsdEnabled, show)));
        QCOMPARE(reg.domainFor(makeAction(ActionType::SetDragSelectorEnabled, show)), ActionDomain::Context);
        QVERIFY(!reg.isTerminal(makeAction(ActionType::SetDragSelectorEnabled, show)));

        // Requires a boolean `value`; both true (force the popup on) and false
        // (suppress it) are well-formed.
        QVERIFY(reg.validate(makeAction(ActionType::SetDragSelectorEnabled, show)));
        QVERIFY(reg.validate(makeAction(ActionType::SetDragSelectorEnabled, hide)));
        QVERIFY2(!reg.validate(makeAction(ActionType::SetDragSelectorEnabled)), "missing value must fail validation");
        QJsonObject notBool;
        notBool.insert(QStringLiteral("value"), 1);
        QVERIFY2(!reg.validate(makeAction(ActionType::SetDragSelectorEnabled, notBool)), "non-bool value must fail");
    }

    void testDisplayOrderUniqueWithinCategory()
    {
        // No consumer reads displayOrder today (the shipped picker sorts by
        // label), but the field exists for one, and two of the twelve overlay
        // orders come from loop tables — a collision would be silent until a
        // consumer appears and the rule editor reordered under it. Pin
        // (category, displayOrder) uniqueness across every registered type.
        const ActionRegistry& reg = ActionRegistry::instance();
        QHash<QString, QString> seen; // "category|order" -> first type
        for (const QString& type : reg.registeredTypes()) {
            const auto desc = reg.descriptor(type);
            QVERIFY2(desc.has_value(), qPrintable(type));
            const QString key = desc->category + QLatin1Char('|') + QString::number(desc->displayOrder);
            QVERIFY2(!seen.contains(key),
                     qPrintable(QStringLiteral("(%1, %2) collides: %3 vs %4")
                                    .arg(desc->category)
                                    .arg(desc->displayOrder)
                                    .arg(seen.value(key), type)));
            seen.insert(key, type);
        }
        QVERIFY2(seen.size() > 10, "The scan itself is broken (almost nothing registered).");
    }

    void testRegisterCustomAction()
    {
        ActionRegistry& reg = ActionRegistry::instance();
        // `_zz_` prefix so the type-id sorts to the end if anyone iterates
        // `registeredTypes()` in lexicographic order, and the underscore-led
        // name visually separates it from the production wire identifiers.
        // The sentinel is unregistered at the end of the test so it does not
        // leak into the process-global singleton for the rest of the binary's
        // lifetime — see the file-level comment for the cross-test
        // independence pattern.
        const QString customType = QStringLiteral("_zz_pwrTestCustomAction");
        QVERIFY(!reg.isRegistered(customType));

        reg.registerAction(ActionDescriptor{.type = customType,
                                            .slotFor =
                                                [](const QJsonObject&) {
                                                    return QStringLiteral("custom-slot");
                                                },
                                            .validate =
                                                [](const QJsonObject&) {
                                                    return true;
                                                },
                                            .terminal = false});
        QVERIFY(reg.isRegistered(customType));
        QCOMPARE(reg.slotFor(makeAction(QLatin1StringView("_zz_pwrTestCustomAction"))), QStringLiteral("custom-slot"));

        // Clean up: unregister the sentinel so the singleton is left pristine
        // for any later test (and so the binary does not carry a bespoke type).
        QVERIFY(reg.unregisterAction(customType));
        QVERIFY(!reg.isRegistered(customType));
    }

    void testValidateRejectsUnregistered()
    {
        const ActionRegistry& reg = ActionRegistry::instance();
        QVERIFY(!reg.validate(makeAction(QLatin1StringView("notRegistered"))));
    }

    /// Every registered param's `kind` must be one the settings layer's QML
    /// dispatcher recognises. The kind is a free-form QString that no compiler
    /// checks, and ActionRow.qml falls back to a plain TEXT FIELD for an
    /// unknown one — so a typo silently ships a numeric slot as free text,
    /// which is exactly what "pixels" did until this round. Failing here is
    /// far cheaper than noticing it in the rule editor.
    ///
    /// The list mirrors ActionRow.qml's dispatch plus the struct doc on
    /// ParamSchema. Adding a kind means adding it in BOTH places, and this
    /// canary is what makes the omission loud.
    void everyParamKindIsInTheKnownVocabulary()
    {
        static const QSet<QString> known = {
            QStringLiteral("string"),
            QStringLiteral("number"),
            QStringLiteral("percent"),
            QStringLiteral("enum"),
            QStringLiteral("bool"),
            QStringLiteral("color"),
            QStringLiteral("snappingLayout"),
            QStringLiteral("tilingAlgorithm"),
            QStringLiteral("scrollingTemplate"),
            QStringLiteral("animationEvent"),
            QStringLiteral("shaderEffect"),
            QStringLiteral("overlayShader"),
            QStringLiteral("zoneOrdinals"),
            QStringLiteral("curveEditor"),
            QStringLiteral("screenId"),
            QStringLiteral("virtualDesktop"),
            QStringLiteral("decorationChain"),
        };
        const ActionRegistry& reg = ActionRegistry::instance();
        QStringList offenders;
        int paramsSeen = 0;
        for (const QString& type : reg.registeredTypes()) {
            const auto desc = reg.descriptor(type);
            if (!desc) {
                continue;
            }
            for (const ParamSchema& p : desc->params) {
                ++paramsSeen;
                if (!known.contains(p.kind)) {
                    offenders.append(type + QLatin1Char('/') + p.key + QLatin1String(" = \"") + p.kind
                                     + QLatin1Char('"'));
                }
            }
        }
        QVERIFY2(paramsSeen > 0, "No descriptor params found: the scan itself is broken.");
        QVERIFY2(offenders.isEmpty(),
                 qPrintable(QStringLiteral("Param kinds the settings-layer dispatcher does not recognise, so their "
                                           "editors silently fall back to a text field: %1")
                                .arg(offenders.join(QStringLiteral(", ")))));
    }
};

QTEST_GUILESS_MAIN(TestActionRegistry)
#include "test_actionregistry.moc"
