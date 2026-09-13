// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';

// Browser study only. The sample password stays in the input, is never persisted
// or sent anywhere, and is cleared on submit, state changes and leaving the view.
window.PhosphorLock = {
  create({root, icon, getSettings, isPlaying, togglePlayback, syncVisualizer, onUnlocked, onPhaseChanged}) {
    const review = document.querySelector('#lock-preview-controls');
    const feedback = document.querySelector('#lock-preview-feedback');
    let active = false, phase = 'ready', caps = false, layout = 'US', power = '', trackIndex = 0;
    const tracks = ['A Walk', 'Hours', 'Daydream'];
    let authTimer, releaseTimer;
    const field = () => root.querySelector('#lock-password');

    function cancelTimers() {
      clearTimeout(authTimer);
      clearTimeout(releaseTimer);
    }

    function setPhase(next, focus = true) {
      cancelTimers();
      phase = next;
      power = '';
      draw();
      if (next === 'typing') {
        field().value = 'demo';
        updateInput();
      }
      if (focus && !['checking', 'success'].includes(next)) field()?.focus({preventScroll:true});
    }

    function media() {
      if (!getSettings().lockMedia) return '';
      return `<div class="lock-media material" aria-label="Media controls">
        <div class="album" aria-hidden="true"></div>
        <div class="lock-track"><span class="lock-overline">${isPlaying() ? 'NOW PLAYING' : 'PAUSED'}</span><b>${tracks[trackIndex]}</b><span>Tycho · Dive</span></div>
        <button data-lock-track="previous" aria-label="Previous track">${icon('previous')}</button>
        <button data-lock-play aria-label="${isPlaying() ? 'Pause' : 'Play'} music">${isPlaying() ? 'Ⅱ' : '▶'}</button>
        <button data-lock-track="next" aria-label="Next track">${icon('next')}</button>
        ${getSettings().visualizer !== 'off' ? '<canvas class="lock-visualizer" data-visualizer="main" width="800" height="64" aria-label="Simulated audio visualizer" role="img"></canvas>' : ''}
      </div>`;
    }

    function powerMenu() {
      if (!power) return '';
      if (power === 'menu') return `<div class="lock-power-menu material" role="group" aria-label="Power actions">
        <span class="lock-overline">POWER</span>
        <button data-lock-power="sleep">${icon('moon')} Sleep</button>
        <button data-lock-power="restart">${icon('power')} Restart…</button>
        <button data-lock-power="shutdown">${icon('power')} Shut down…</button>
      </div>`;
      const label = power === 'restart' ? 'Restart' : 'Shut down';
      return `<div class="lock-power-menu lock-power-confirm material" role="group" aria-label="Confirm ${label.toLowerCase()}">
        <h3>${label} this device?</h3><p>Your session will end.</p>
        <div><button data-lock-power="cancel">Cancel</button><button data-lock-power="confirm">${label}</button></div>
      </div>`;
    }

    function draw() {
      onPhaseChanged(phase);
      root.classList.toggle('hidden', !active);
      review.classList.toggle('hidden', !active);
      if (!active) {
        root.replaceChildren();
        return;
      }
      const busy = phase === 'checking', success = phase === 'success', error = phase === 'error';
      root.dataset.phase = phase;
      root.innerHTML = `
        <div class="lock-atmosphere" aria-hidden="true"></div>
        <div class="lock-composition" aria-hidden="true"><i></i><i></i><i></i></div>
        <header class="lock-header"><div class="lock-brand"><span>φ</span> PHOSPHOR</div>
          <div class="lock-system"><span>${icon('lock')} Session locked</span><span>${icon('battery')} 82%</span></div>
        </header>
        <div class="lock-clock"><span class="lock-overline">SATURDAY, SEPTEMBER 12</span>
          <time datetime="2026-09-12T10:24:00-05:00">10<span>:</span>24</time>
          <div class="lock-clock-caption"><span class="lock-signature" aria-hidden="true"><i></i><i></i><i></i></span><span>Your windows, in place.</span></div>
        </div>
        <section class="lock-card material" aria-labelledby="lock-greeting" aria-busy="${busy}">
          <div class="lock-user"><div class="lock-avatar" aria-hidden="true">n<span></span></div><div><span class="lock-overline">${success ? 'SESSION READY' : 'WELCOME BACK'}</span><h2 id="lock-greeting">nlavender</h2></div><span class="lock-card-status" aria-hidden="true">${icon(success ? 'check' : 'lock')}</span></div>
          <form id="lock-form" novalidate autocomplete="off">
            <label for="lock-password">Password</label>
            <div class="lock-password-wrap">
              <input id="lock-password" name="prototype-password" type="password" placeholder="Enter your password" autocomplete="off" spellcheck="false" autocapitalize="off" aria-describedby="lock-message lock-caps" aria-invalid="${error}" ${busy || success ? 'disabled' : ''}>
              <button class="lock-submit" type="submit" aria-label="Unlock session" disabled>${busy ? '<span class="lock-spinner"></span>' : icon(success ? 'check' : 'arrow-right')}</button>
            </div>
            <div id="lock-message" class="lock-message" role="status" aria-live="polite">${error ? 'That password didn’t match. Try again.' : busy ? 'Unlocking your session…' : success ? 'You’re back.' : 'Enter to unlock'}</div>
          </form>
          <div class="lock-card-footer"><button data-lock-keyboard aria-label="Keyboard layout: ${layout === 'US' ? 'English US' : 'English UK'}">${icon('keyboard')} ${layout} <span>⌄</span></button>
            <span id="lock-caps" class="${caps ? '' : 'hidden'}" role="status">${icon('caps')} Caps Lock is on</span><span class="lock-escape">Esc to clear</span>
          </div>
        </section>
        <div class="lock-bottom-left">${media()}${getSettings().lockNotifications ? `<div class="lock-notifications">${icon('bell')}<span>3 notifications<span>Content hidden while locked</span></span></div>` : ''}</div>
        <footer class="lock-footer"><span>${icon('lock')} Your session stays here.</span><div>${powerMenu()}<button class="lock-power-button" data-lock-power="menu" aria-expanded="${!!power}">${icon('power')} Power</button></div></footer>`;
      root.querySelector('#lock-form').addEventListener('submit', submit);
      field().addEventListener('input', updateInput);
      review.querySelectorAll('[data-lock-state]').forEach(button => button.setAttribute('aria-pressed', button.dataset.lockState === phase));
      review.querySelector('[data-lock-caps]').setAttribute('aria-pressed', caps);
      syncVisualizer();
    }

    function updateInput() {
      root.querySelector('.lock-submit').disabled = !field().value || ['checking', 'success'].includes(phase);
      if (phase === 'error' && field().value) {
        phase = 'typing';
        root.dataset.phase = phase;
        field().setAttribute('aria-invalid', 'false');
        root.querySelector('#lock-message').textContent = 'Enter to unlock';
      }
      review.querySelectorAll('[data-lock-state]').forEach(button => button.setAttribute('aria-pressed', button.dataset.lockState === (field().value ? 'typing' : phase)));
    }

    function submit(event) {
      event.preventDefault();
      if (['checking', 'success'].includes(phase) || !field().value) return;
      const accepted = field().value === 'demo';
      field().value = '';
      setPhase('checking', false);
      authTimer = setTimeout(() => {
        if (!accepted) return setPhase('error');
        setPhase('success', false);
        releaseTimer = setTimeout(onUnlocked, getSettings().motion ? 550 : 0);
      }, 850);
    }

    root.addEventListener('click', event => {
      event.stopPropagation();
      const button = event.target.closest('button');
      if (!button) {
        if (power) { power = ''; draw(); }
        if (!event.target.closest('input')) field()?.focus({preventScroll:true});
        return;
      }
      if (button.hasAttribute('data-lock-play') || button.dataset.lockTrack) {
        if (button.hasAttribute('data-lock-play')) togglePlayback();
        else trackIndex = (trackIndex + (button.dataset.lockTrack === 'next' ? 1 : tracks.length - 1)) % tracks.length;
        root.querySelector('.lock-media').outerHTML = media();
        syncVisualizer();
        root.querySelector(button.hasAttribute('data-lock-play') ? '[data-lock-play]' : `[data-lock-track="${button.dataset.lockTrack}"]`)?.focus({preventScroll:true});
      }
      if (button.hasAttribute('data-lock-keyboard')) {
        layout = layout === 'US' ? 'UK' : 'US';
        button.innerHTML = `${icon('keyboard')} ${layout} <span>⌄</span>`;
        button.setAttribute('aria-label', `Keyboard layout: English ${layout}`);
        field()?.focus({preventScroll:true});
      }
      if (button.dataset.lockPower) {
        const action = button.dataset.lockPower;
        if (action === 'sleep' || action === 'confirm') {
          feedback.textContent = `${action === 'sleep' ? 'Sleep' : power === 'restart' ? 'Restart' : 'Shut down'} selected in the prototype. No system action was taken.`;
          power = '';
        } else if (action === 'cancel' || (action === 'menu' && power)) power = '';
        else power = action;
        draw();
        (root.querySelector('.lock-power-menu button') || root.querySelector('.lock-power-button'))?.focus({preventScroll:true});
      }
    });

    review.addEventListener('click', event => {
      const button = event.target.closest('button');
      if (button?.dataset.lockState) setPhase(button.dataset.lockState);
      if (button?.hasAttribute('data-lock-caps')) {
        caps = !caps;
        root.querySelector('#lock-caps').classList.toggle('hidden', !caps);
        button.setAttribute('aria-pressed', caps);
        field()?.focus({preventScroll:true});
      }
    });

    return {
      render(show) {
        const entering = show && !active;
        if (!show || entering) { cancelTimers(); phase = 'ready'; power = ''; caps = false; }
        active = show;
        // A settings change must not retain an authentication completion timer.
        if (show && !entering && phase !== 'ready') setPhase('ready', false);
        else draw();
        if (entering) field()?.focus({preventScroll:true});
      },
      handleKey(event) {
        if (!active) return false;
        if (event.target.closest('.review-toolbar,.review-header,#customizer,#lock-preview-controls')) return true;
        if (event.key === 'Tab') {
          const controls = [...root.querySelectorAll('button:not(:disabled),input:not(:disabled)')].filter(item => item.getClientRects().length);
          const first = controls[0], last = controls.at(-1);
          if (event.shiftKey && (event.target === first || !root.contains(event.target))) { event.preventDefault(); last?.focus(); }
          else if (!event.shiftKey && (event.target === last || !root.contains(event.target))) { event.preventDefault(); first?.focus(); }
        } else if (event.key === 'Escape') {
          event.preventDefault();
          setPhase('ready');
        } else if (!['checking', 'success'].includes(phase)) {
          if (event.key === 'CapsLock') {
            caps = event.getModifierState('CapsLock');
            root.querySelector('#lock-caps').classList.toggle('hidden', !caps);
            review.querySelector('[data-lock-caps]').setAttribute('aria-pressed', caps);
          }
          if (event.target !== field() && !event.ctrlKey && !event.metaKey && !event.altKey && event.key.length === 1 && !(event.key === ' ' && event.target.closest('button'))) {
            event.preventDefault();
            field()?.focus({preventScroll:true});
            field().value += event.key;
            updateInput();
          }
        }
        return true;
      }
    };
  }
};
