// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/qml/detail/PhosphorAnimatedValueImpl.h>

/**
 * @file PhosphorAnimatedTypeMacro.h
 * @brief Token-paste macro that materialises the cpp body of every
 *        per-T `PhosphorAnimated{Real,Point,Size,Rect}` wrapper.
 *
 * The four typed wrappers' cpps are byte-for-byte identical modulo
 * the value type — every accessor (from / to / value /
 * isAnimating / isComplete), every lifecycle-call dispatch (start /
 * startImpl / retarget / cancel / finish / advance / onSync), and
 * every ctor / dtor body. Hand-rolling them duplicated ~13 method
 * bodies per class across four classes. The
 * `PHOSPHOR_DEFINE_ANIMATED_TYPE` macro materialises the common cpp
 * shape from one source so adding a new typed wrapper or extending
 * the wrapper API is a one-edit change.
 *
 * ## Why ONLY the cpp body, not the header
 *
 * Tried to also macroise the header (Q_OBJECT class declaration)
 * but Qt's AUTOMOC pre-scan + dedup logic interacts badly with
 * macro-expanded `Q_OBJECT`s: AUTOMOC tracks the per-T headers as
 * needing moc, runs moc on each, but then refuses to include the
 * generated `moc_PhosphorAnimated{Real,Point,Size,Rect}.cpp` files
 * in `mocs_compilation.cpp` — apparently treating them as
 * duplicates of the (empty) `moc_PhosphorAnimatedTypeMacro.cpp`
 * generated for the macro definition header. Result: `fromChanged`
 * / `toChanged` / `valueChanged` become undefined link references.
 * Comment-marker tricks (`// Q_OBJECT`) are filtered by AUTOMOC's
 * comment-stripping pre-scan and don't help.
 *
 * Keeping the literal `Q_OBJECT` inside each per-T header means
 * AUTOMOC sees it the conventional way and emits the moc cpp into
 * `mocs_compilation.cpp` correctly. The class declarations stay
 * hand-written (~60 lines each, mostly Q_PROPERTY / Q_SIGNALS
 * declarations that AUTOMOC needs to see literally anyway), and
 * this file collapses the cpp side instead.
 *
 * ## Color is deliberately NOT routed through this macro
 *
 * `PhosphorAnimatedColor` carries TWO `AnimatedValue<QColor, Space>`
 * instances (Linear + OkLab) with per-space dispatch in every
 * accessor and an extra `colorSpace` Q_PROPERTY. Macroising it
 * would require either parameterising the dispatch (kills the
 * trivial generated body) or carving a second macro family that's
 * used in exactly one place. Color stays hand-written; the
 * `runWithDiffEmit` helper in `PhosphorAnimatedValueImpl.h` covers
 * Color's snapshot/diff-emit duplication separately.
 */

/**
 * @brief Define the per-T wrapper's trivial bodies — ctor, dtor (with
 *        the cancel-before-base-destruction safety dance), the six
 *        AnimatedValue passthrough getters, and the start/retarget/
 *        cancel/finish/advance/onSync orchestration that delegates to
 *        the helpers in `PhosphorAnimatedValueImpl.h`. Invoked from
 *        each per-T cpp at namespace scope.
 *
 * The dtor explicitly cancels `m_animatedValue` BEFORE the base-class
 * destruction path runs. Member destruction order is reverse of
 * declaration: at base-dtor time `m_animatedValue` has already gone,
 * but a render-thread `beforeSynchronizing` handler that fired
 * concurrently with our destruction could otherwise UAF on the gone
 * AnimatedValue<T>. The cancel here clears the running flag so the
 * next `onSync()` short-circuits, and the base dtor's
 * `disconnectSyncSignal()` drops the connection for future frames.
 *
 * @param ClassName  PascalCase class name (e.g. `PhosphorAnimatedReal`).
 * @param T          Value type (e.g. `qreal`, `QPointF`).
 * @param ParamT     How to spell @p T in a function parameter — `qreal`
 *                   for primitives, `const QPointF&` for compound
 *                   types. Picked at the invocation site so each
 *                   wrapper keeps its natural public API shape.
 */
#define PHOSPHOR_DEFINE_ANIMATED_TYPE(ClassName, T, ParamT)                                                            \
    ClassName::ClassName(QObject* parent)                                                                              \
        : PhosphorAnimation::PhosphorAnimatedValueBase(parent)                                                         \
    {                                                                                                                  \
    }                                                                                                                  \
                                                                                                                       \
    ClassName::~ClassName()                                                                                            \
    {                                                                                                                  \
        m_animatedValue.cancel();                                                                                      \
    }                                                                                                                  \
                                                                                                                       \
    T ClassName::from() const                                                                                          \
    {                                                                                                                  \
        return m_animatedValue.from();                                                                                 \
    }                                                                                                                  \
    T ClassName::to() const                                                                                            \
    {                                                                                                                  \
        return m_animatedValue.to();                                                                                   \
    }                                                                                                                  \
    T ClassName::value() const                                                                                         \
    {                                                                                                                  \
        return m_animatedValue.value();                                                                                \
    }                                                                                                                  \
    bool ClassName::isAnimating() const                                                                                \
    {                                                                                                                  \
        return m_animatedValue.isAnimating();                                                                          \
    }                                                                                                                  \
    bool ClassName::isComplete() const                                                                                 \
    {                                                                                                                  \
        return m_animatedValue.isComplete();                                                                           \
    }                                                                                                                  \
                                                                                                                       \
    bool ClassName::start(ParamT from, ParamT to)                                                                      \
    {                                                                                                                  \
        return startImpl(from, to, resolveClock());                                                                    \
    }                                                                                                                  \
                                                                                                                       \
    bool ClassName::startImpl(ParamT from, ParamT to, PhosphorAnimation::IMotionClock* clock)                          \
    {                                                                                                                  \
        return PhosphorAnimation::detail::startImpl(this, m_animatedValue, from, to, clock, profile().value());        \
    }                                                                                                                  \
                                                                                                                       \
    bool ClassName::retarget(ParamT to)                                                                                \
    {                                                                                                                  \
        return PhosphorAnimation::detail::retargetImpl(this, m_animatedValue, to);                                     \
    }                                                                                                                  \
                                                                                                                       \
    void ClassName::cancel()                                                                                           \
    {                                                                                                                  \
        PhosphorAnimation::detail::cancelImpl(this, m_animatedValue);                                                  \
    }                                                                                                                  \
                                                                                                                       \
    void ClassName::finish()                                                                                           \
    {                                                                                                                  \
        PhosphorAnimation::detail::finishImpl(this, m_animatedValue);                                                  \
    }                                                                                                                  \
                                                                                                                       \
    void ClassName::advance()                                                                                          \
    {                                                                                                                  \
        m_animatedValue.advance();                                                                                     \
    }                                                                                                                  \
                                                                                                                       \
    void ClassName::onSync()                                                                                           \
    {                                                                                                                  \
        m_animatedValue.advance();                                                                                     \
    }
