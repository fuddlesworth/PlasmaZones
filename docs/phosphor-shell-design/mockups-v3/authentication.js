// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';

// Browser fixtures only. Responses are cleared on submit and never leave the
// input, enter storage, or reach a system authentication service.
window.PhosphorAuth = {
  create({root, review, desktop, icon, getSettings, onDismiss}) {
    const examples = {
      file: {label:'Protected file', app:'Kate', icon:'terminal', title:'Save a protected file', message:'Kate needs administrator permission to save this file.', resource:'/etc/hosts', action:'org.kde.ktexteditor.katetextbuffer.savefile', program:'/usr/bin/kate'},
      software: {label:'Install software', app:'Discover', icon:'download', title:'Install software', message:'Discover needs administrator permission to install software for all users.', resource:'3 packages · 42 MB', action:'org.freedesktop.packagekit.package-install', program:'/usr/bin/plasma-discover'},
      accounts: {label:'Choose an account', app:'System Settings', icon:'tune', title:'Change the system time', message:'Choose an administrator account to allow this change.', action:'org.freedesktop.timedate1.set-time', program:'/usr/bin/systemsettings', identities:['nlavender','root']},
      code: {label:'Verification code', app:'Kate', icon:'terminal', title:'One more step', message:'Enter your verification code to finish authenticating this request.', resource:'Save /etc/hosts', action:'org.kde.ktexteditor.katetextbuffer.savefile', program:'/usr/bin/kate', prompt:'Verification code', echo:true},
      background: {label:'No app window', app:'System service', icon:'shield', title:'Authentication required', message:'Administrator permission is required to reload the system configuration.', action:'org.freedesktop.systemd1.reload-daemon', program:'Application information unavailable'},
      long: {label:'Long request', app:'System Settings', icon:'network', title:'Update the shared network connection', message:'Administrator permission is required to change the connection used by everyone on this computer. This includes the network address, routing, and DNS settings for the shared Ethernet connection.', resource:'Wired connection 1', action:'org.freedesktop.NetworkManager.settings.modify.system', program:'/usr/bin/systemsettings'}
    };
    const states = {ready:'Ready',typing:'Typing',error:'Incorrect password',checking:'Authenticating',success:'Authenticated',cancelled:'Cancelled',unavailable:'Service unavailable'};
    const esc = value => String(value).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
    let active = false, phase = 'ready', example = 'file', identity = 'nlavender', caps = false, details = false, large = false;
    let authTimer, releaseTimer;
    const field = () => root.querySelector('#auth-password');
    const request = () => examples[example];
    function clearTimers() { clearTimeout(authTimer); clearTimeout(releaseTimer); }

    function drawReview() {
      review.classList.toggle('hidden', !active);
      if (!active) return;
      review.innerHTML = `<span>PREVIEW REQUEST</span>
        <label>Example <select id="auth-example">${Object.entries(examples).map(([id,item])=>`<option value="${id}" ${id===example?'selected':''}>${item.label}</option>`).join('')}</select></label>
        <label>State <select id="auth-state">${Object.entries(states).map(([id,label])=>`<option value="${id}" ${id===phase?'selected':''}>${label}</option>`).join('')}</select></label>
        <button data-auth-caps aria-pressed="${caps}">Caps Lock</button><button data-auth-large aria-pressed="${large}">Large text</button>
        <button data-auth-reopen>New request</button><p>Demo only. Use <code>${request().echo?'123456':'demo'}</code>; no system changes.</p>`;
    }

    function status() {
      if (phase === 'error') return {symbol:'shield',text:request().echo?'That code didn’t match. Try again.':'That password didn’t match. Try again.'};
      if (phase === 'checking') return {symbol:'spinner',text:'Checking your credentials…'};
      if (phase === 'success') return {symbol:'check',text:'Authenticated. Returning to your app.'};
      if (phase === 'unavailable') return {symbol:'shield',text:'Authentication is unavailable. Try again or cancel this request.'};
      return {symbol:'',text:'Authenticate to continue.'};
    }

    function updateMessage() {
      const message = root.querySelector('#auth-message'), value = status();
      if (!message) return;
      message.innerHTML = `${value.symbol==='spinner'?'<span class="auth-spinner" aria-hidden="true"></span>':value.symbol?icon(value.symbol):''}<span>${value.text}</span>`;
      root.querySelector('.auth-card').dataset.phase = phase;
      field()?.setAttribute('aria-invalid', String(phase === 'error'));
    }

    function draw() {
      root.classList.toggle('hidden', !active);
      desktop.dataset.authText = large ? 'large' : 'normal';
      drawReview();
      if (!active) { root.replaceChildren(); return; }
      const item = request(), busy = phase === 'checking', success = phase === 'success', unavailable = phase === 'unavailable';
      const identities = item.identities || [identity];
      root.innerHTML = `<div class="auth-scrim" aria-hidden="true"></div>
        <section class="auth-card material" role="dialog" aria-modal="true" aria-labelledby="auth-title" aria-describedby="auth-description" data-phase="${phase}">
          <header class="auth-brand"><span class="phosphor-mark" aria-hidden="true"></span><span>AUTHENTICATION</span><span class="auth-security" aria-hidden="true">${icon('shield')}</span></header>
          <div class="auth-content">
            <div class="auth-requester"><span class="auth-app-icon" aria-hidden="true">${icon(item.icon)}</span><div><b>${esc(item.app)}</b><span>${example==='background'?'Background request':'Requesting permission'}</span></div></div>
            <h2 id="auth-title">${item.title}</h2><p id="auth-description" class="auth-description">${item.message}</p>
            ${item.resource?`<p class="auth-resource"><code>${esc(item.resource)}</code></p>`:''}
            <form id="auth-form" autocomplete="off" novalidate>
              <div class="auth-account"><span class="auth-avatar" aria-hidden="true">${esc(identity[0])}</span><div><label ${identities.length>1?'for="auth-identity"':''}>Authenticate as</label>
                ${identities.length>1?`<select id="auth-identity" ${busy||success?'disabled':''}>${identities.map(name=>`<option ${name===identity?'selected':''}>${esc(name)}</option>`).join('')}</select>`:`<strong>${esc(identity)}</strong>`}</div></div>
              <label class="auth-field-label" for="auth-password">${item.prompt||'Password'}</label>
              <div class="auth-password-wrap"><input id="auth-password" type="${item.echo?'text':'password'}" ${item.echo?'inputmode="numeric"':''} autocomplete="off" spellcheck="false" autocapitalize="off" placeholder="${item.echo?'Enter your code':'Enter your password'}" aria-describedby="auth-message auth-caps" aria-invalid="${phase==='error'}" ${busy||success||unavailable?'disabled':''}>
                ${item.echo?'':`<button class="auth-reveal" type="button" aria-label="Show password" aria-pressed="false" ${busy||success||unavailable?'disabled':''}>${icon('eye')}</button>`}</div>
              <div class="auth-meta"><span>${icon('keyboard')} English (US)</span><span id="auth-caps" ${!caps?'hidden':''}>${icon('caps')} Caps Lock is on</span></div>
              <div id="auth-message" class="auth-message" role="status" aria-live="polite" aria-atomic="true"></div>
            </form>
            <details class="auth-details" ${details?'open':''}><summary>Request details ${icon('chevron')}</summary><dl><dt>Action</dt><dd>${esc(item.action)}</dd><dt>Application</dt><dd>${esc(item.program)}</dd></dl></details>
          </div>
          <footer class="auth-actions"><span class="auth-keyhint"><kbd>Esc</kbd> to ${success?'close':'cancel'}</span><button class="auth-cancel" type="button">${success?'Close':'Cancel'}</button>
            <button class="auth-submit" type="submit" form="auth-form" ${unavailable?'':'disabled'}>${success?icon('check'):''}${busy?'Authenticating…':success?'Authenticated':unavailable?'Try again':'Authenticate'}</button></footer>
        </section>`;
      updateMessage();
      root.querySelector('.auth-details').addEventListener('toggle', event => { details = event.target.open; });
      root.querySelector('#auth-form').addEventListener('submit', submit);
      field().addEventListener('input', updateInput);
    }

    function focus() {
      if (!active) return;
      (field()?.disabled ? root.querySelector('.auth-cancel') : field())?.focus({preventScroll:true});
    }

    function setPhase(next, focusInput = true) {
      clearTimers();
      if (next === 'cancelled') { dismiss('cancelled'); return; }
      phase = next;
      draw();
      if (next === 'typing') { field().value = request().echo ? '123456' : 'demo'; updateInput(); }
      if (focusInput) focus();
    }

    function updateInput() {
      if (!field() || field().disabled) return;
      phase = field().value ? 'typing' : 'ready';
      root.querySelector('.auth-submit').disabled = !field().value;
      updateMessage();
      review.querySelector('#auth-state').value = phase;
    }

    function dismiss(reason) {
      clearTimers();
      if (field()) field().value = '';
      active = false;
      draw();
      onDismiss(reason);
    }

    function submit(event) {
      event.preventDefault();
      if (phase === 'unavailable') { setPhase('ready'); return; }
      if (!active || ['checking','success'].includes(phase) || !field().value) return;
      const accepted = field().value === (request().echo ? '123456' : 'demo');
      field().value = '';
      setPhase('checking');
      authTimer = setTimeout(() => {
        if (!active) return;
        if (!accepted) { setPhase('error'); return; }
        setPhase('success', false);
        releaseTimer = setTimeout(() => dismiss('success'), getSettings().motion ? 400 : 0);
      }, 900);
    }

    root.addEventListener('click', event => {
      event.stopPropagation();
      if (event.target.closest('.auth-cancel')) dismiss(phase==='success'?'success':'cancelled');
      const reveal = event.target.closest('.auth-reveal');
      if (reveal && !reveal.disabled) {
        const showing = field().type === 'password';
        field().type = showing ? 'text' : 'password';
        reveal.setAttribute('aria-pressed', String(showing));
        reveal.setAttribute('aria-label', showing ? 'Hide password' : 'Show password');
      }
    });
    root.addEventListener('change', event => {
      if (event.target.id !== 'auth-identity') return;
      identity = event.target.value;
      setPhase('ready');
    });
    review.addEventListener('change', event => {
      if (event.target.id === 'auth-example') {
        example = event.target.value; identity = 'nlavender'; details = false; caps = false;
        setPhase('ready');
      } else if (event.target.id === 'auth-state') setPhase(event.target.value);
    });
    review.addEventListener('click', event => {
      const button = event.target.closest('button');
      if (!button) return;
      if (button.hasAttribute('data-auth-reopen')) { details = false; setPhase('ready'); }
      if (button.hasAttribute('data-auth-caps')) {
        caps = !caps;
        root.querySelector('#auth-caps').hidden = !caps;
        button.setAttribute('aria-pressed', String(caps));
        focus();
      }
      if (button.hasAttribute('data-auth-large')) {
        large = !large;
        desktop.dataset.authText = large ? 'large' : 'normal';
        button.setAttribute('aria-pressed', String(large));
      }
    });

    return {
      render(show) {
        // Palette and study changes restyle the existing dialog. Keeping its
        // DOM also preserves the active field and an in-flight Cancel button.
        if (show === active) return;
        clearTimers(); phase = 'ready'; caps = false; details = false; identity = 'nlavender';
        active = show;
        draw();
        if (show) focus();
      },
      focus,
      handleKey(event) {
        if (!active) return false;
        if (event.target.closest('.review-toolbar,.review-header,#customizer,#auth-preview-controls')) return true;
        if (event.key === 'Escape') {
          event.preventDefault();
          dismiss(phase==='success'?'success':'cancelled');
        } else if (event.key === 'Tab') {
          const controls = [...root.querySelectorAll('button:not(:disabled),input:not(:disabled),select:not(:disabled),summary')].filter(item=>item.getClientRects().length);
          const first = controls[0], last = controls.at(-1);
          if (event.shiftKey && (event.target===first || !root.contains(event.target))) { event.preventDefault(); last?.focus(); }
          else if (!event.shiftKey && (event.target===last || !root.contains(event.target))) { event.preventDefault(); first?.focus(); }
        } else if (event.key === 'CapsLock') {
          caps = event.getModifierState('CapsLock');
          root.querySelector('#auth-caps').hidden = !caps;
          review.querySelector('[data-auth-caps]').setAttribute('aria-pressed', String(caps));
        }
        return true;
      }
    };
  }
};
