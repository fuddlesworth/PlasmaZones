// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';

window.PhosphorNotifications = {
  create({root, icon, getSettings, getQuiet, setQuiet, onChanged, onDismiss}) {
    const review = document.querySelector('#notification-preview-controls');
    const arrival = document.querySelector('#notification-arrival');
    const escape = value => String(value).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
    const apps = {
      system: {name:'Backups', icon:'shield', color:'var(--c4)'},
      element: {name:'Element', icon:'message', color:'var(--c3)'},
      dolphin: {name:'Dolphin', icon:'folder', color:'var(--c2)'},
      konsole: {name:'Konsole', icon:'terminal', color:'var(--c1)'}
    };
    const longMessage = 'I went through the notification mockup this morning. The grouped cards are much easier to scan, especially when a chat gets busy while a build is running.\n\nA few details to try: keep the app icon visible beside the sender, let attached pictures keep their original proportions, and make the full message available without opening the app. A short preview should still leave room for Reply and Dismiss.\n\nI also attached the wallpaper study so we can check the same notification against dark glass, Paper, and Ember. The softer violet background should work with all three. Let me know what you think when you have a moment.';
    const attachment = {image:'notification-attachment.png',imageAlt:'Violet wallpaper study with a pale moon above layered hills',imageName:'Wallpaper study.png'};
    const samples = [
      {id:'backup',app:'system',title:'Backup drive disconnected',body:'Reconnect Archive to finish your backup.',time:'Now',unread:true,urgent:true,action:'View backup'},
      {id:'maya',app:'element',title:'Maya · Shell design',body:'The new lock screen feels right. I left a few notes on the spacing.',time:'2 min',unread:true,action:'Open chat',reply:true},
      {id:'alex',app:'element',title:'Alex · Shell design',body:longMessage,time:'8 min',unread:false,action:'Open chat',reply:true},
      {id:'room',app:'element',title:'Design room',body:'Maya shared a wallpaper study.',time:'12 min',unread:false,action:'Open chat',...attachment},
      {id:'files',app:'dolphin',title:'12 files copied',body:'Design references → Pictures / Inspiration',time:'18 min',unread:false,action:'Open folder'},
      {id:'download',app:'dolphin',title:'Download complete',body:'phosphor-wallpapers.zip · 24.8 MB',time:'24 min',unread:false,action:'Show file'},
      {id:'build',app:'konsole',title:'Build finished',body:'All targets built successfully in 42 seconds.',time:'32 min',unread:true}
    ];
    let items = samples.map(item => ({...item}));
    let active = false, filter = 'all', expanded = new Set(), replyId = '', draft = '', scenario = 'everyday';
    let undo = null, message = '', incoming = 0, arrivalId = '';
    const expandedBodies = new Set();
    const surface = () => arrivalId?arrival:root;
    const unread = () => items.filter(item => item.unread).length;
    const actionButton = (action, label, glyph, extra = '') => `<button data-nc="${action}" data-nc-key="${action}" ${extra}>${glyph ? icon(glyph) : ''}${label}</button>`;

    function card(item, popup = false) {
      const app = apps[item.app], showBody = getSettings().notificationPreviews;
      const accessibleTitle = showBody ? item.title : `${app.name} notification`;
      const long = item.body.length > 180, full = expandedBodies.has(item.id);
      return `<article class="nc-card ${item.unread?'nc-unread':''} ${item.urgent?'nc-urgent':''}" data-notification="${item.id}" style="--app-color:${app.color}">
        <div class="nc-card-top"><span>${popup?`<span class="nc-arrival-icon">${icon(app.icon)}</span><span class="nc-arrival-app">${app.name}<small>New notification</small></span>`:item.urgent?`${icon(app.icon)} ${app.name}`:item.unread?'<i class="nc-unread-dot"></i> Unread':'Received'}</span><time>${item.time}</time>${actionButton(`${popup?'close-toast':'dismiss'}:${item.id}`,'','close-icon',`class="nc-icon-button nc-dismiss" aria-label="Dismiss ${escape(accessibleTitle)}" title="${popup?'Dismiss popup; keep in history':'Dismiss notification'}"`)}</div>
        <h3>${showBody?escape(item.title):'Notification preview hidden'}</h3>
        ${showBody?`<p class="nc-body ${long&&!full?'nc-body-clamped':''} ${full?'nc-body-expanded':''}">${escape(item.body)}</p>${long?actionButton(`body:${item.id}`,full?'Show less':'Read full message','chevron',`class="nc-body-toggle" aria-expanded="${full}"`):''}${item.image?`<a class="nc-attachment" href="${item.image}" target="_blank" rel="noopener" aria-label="Open ${escape(item.imageName)}"><img src="${item.image}" alt="${escape(item.imageAlt)}"><span>${icon('picture')} ${escape(item.imageName)} ${icon('arrow-right')}</span></a>`:''}`:'<p>Open the app to see the details.</p>'}
        ${replyId === item.id ? `<form class="nc-reply" data-reply-id="${item.id}"><label class="nc-sr-only" for="nc-reply-input">Reply to ${escape(accessibleTitle)}</label><input id="nc-reply-input" maxlength="500" placeholder="Write a reply…" value="${escape(draft)}" autocomplete="off"><button type="submit" data-nc-key="send" aria-label="Send demo reply" ${draft.trim()?'':'disabled'}>${icon('arrow-right')}</button>${actionButton('cancel-reply','','close-icon','aria-label="Cancel reply"')}</form>` :
          `<div class="nc-card-actions">${item.action?actionButton(`open:${item.id}`,item.action,'arrow-right','class="nc-open"'): '<span class="nc-history-label">Saved in history</span>'}${item.reply?actionButton(`reply:${item.id}`,'Reply',null,'class="nc-secondary"'):''}${item.unread?actionButton(`read:${item.id}`,'','check','class="nc-icon-button nc-mark" aria-label="Mark notification as read" title="Mark as read"'):''}</div>`}
      </article>`;
    }

    function group(appId, rows) {
      const app = apps[appId], open = expanded.has(appId), shown = open?rows:rows.slice(0,1);
      return `<section class="nc-group" style="--app-color:${app.color}" aria-label="${app.name} notifications">
        <div class="nc-group-heading"><span class="nc-app-icon">${icon(app.icon)}</span><h3>${app.name}</h3><span class="nc-group-count">${rows.length}</span>${actionButton(`dismiss-app:${appId}`,'','close-icon',`class="nc-icon-button" aria-label="Clear ${app.name} notifications" title="Clear ${app.name}"`)}</div>
        <div class="nc-group-cards">${shown.map(item=>card(item)).join('')}</div>
        ${rows.length>1?actionButton(`expand:${appId}`,open?'Show less':`${rows.length-1} earlier notification${rows.length>2?'s':''}`, 'chevron',`class="nc-expand" aria-expanded="${open}"`):''}
      </section>`;
    }

    function empty() {
      const filtered = filter === 'unread' && items.length;
      return `<div class="nc-empty"><div class="nc-empty-art" aria-hidden="true"><i></i><i></i><i></i><span>${icon('check')}</span></div><h3>${filtered?'You’re all caught up':'A little room to focus.'}</h3><p>${filtered?'Everything here has been read. Your history is still in All.':getQuiet()?'Do not disturb is on. New notifications will collect here quietly.':'New notifications will appear here. You can get back to your day.'}</p>${filtered?actionButton('filter:all','View all notifications','arrow-right'):''}</div>`;
    }

    function draw() {
      root.className = active&&!arrivalId?'notification-center material':'hidden';
      arrival.className = active&&arrivalId?'notification-arrival material':'hidden';
      review.classList.toggle('hidden',!active);
      review.querySelector('[data-notification-inbox]').setAttribute('aria-pressed',!arrivalId);
      review.querySelectorAll('[data-notification-scenario]').forEach(button => button.setAttribute('aria-pressed',button.dataset.notificationScenario===scenario));
      if (!active) { root.replaceChildren();arrival.replaceChildren();return; }
      if (arrivalId) {
        const key = arrival.contains(document.activeElement)?document.activeElement.dataset.ncKey:null;
        root.replaceChildren();
        const item=items.find(item=>item.id===arrivalId);
        arrival.innerHTML=item?`${card(item,true)}<div class="nc-arrival-footer"><span>${item.unread?'Saved in your notification center':message||'Marked as read'}</span>${actionButton('inbox','Open center','arrow-right')}</div>`:'';
        if(key) [...arrival.querySelectorAll('[data-nc-key]')].find(button=>button.dataset.ncKey===key)?.focus({preventScroll:true});
        return;
      }
      arrival.replaceChildren();
      const key = root.contains(document.activeElement)?document.activeElement.dataset.ncKey:null;
      const scroll = root.querySelector('.nc-scroll')?.scrollTop || 0;
      const rows = items.filter(item => filter === 'all' || item.unread);
      const urgent = rows.filter(item => item.urgent), ordinary = rows.filter(item => !item.urgent);
      let content = '';
      if (urgent.length) content += `<div class="nc-section-label"><span>Needs attention</span><i></i></div>${urgent.map(item=>card(item)).join('')}`;
      if (ordinary.length) {
        content += `<div class="nc-section-label"><span>Today</span><i></i><span>Sat 12</span></div>`;
        content += getSettings().notificationGrouping === 'app'
          ? Object.keys(apps).filter(id => ordinary.some(item => item.app === id)).map(id => group(id,ordinary.filter(item => item.app === id))).join('')
          : ordinary.map(item => `<div class="nc-timeline-entry" style="--app-color:${apps[item.app].color}"><div class="nc-timeline-app">${icon(apps[item.app].icon)}${apps[item.app].name}</div>${card(item)}</div>`).join('');
      }
      root.innerHTML = `<header class="nc-header"><div><span class="nc-eyebrow">YOUR INBOX</span><h2>Notifications <span>${items.length.toString().padStart(2,'0')}</span></h2></div><button class="nc-icon-button" data-dismiss aria-label="Close notification center" title="Close">${icon('close-icon')}</button></header>
        <div class="nc-quiet ${getQuiet()?'nc-quiet-on':''}"><span class="nc-quiet-icon">${icon('moon')}</span><div><b>Do not disturb</b><span>${getQuiet()?'On · new arrivals stay here quietly':'Off · show incoming notifications'}</span></div>${actionButton('quiet','',null,`class="nc-switch" role="switch" aria-checked="${getQuiet()}" aria-label="Do not disturb"`)}</div>
        <div class="nc-toolbar"><nav aria-label="Filter notifications">${actionButton('filter:all',`All <span>${items.length}</span>`,null,`aria-pressed="${filter==='all'}"`)}${actionButton('filter:unread',`Unread <span>${unread()}</span>`,null,`aria-pressed="${filter==='unread'}"`)}</nav>${actionButton('read-all','','check-all',`class="nc-icon-button" aria-label="Mark all as read" title="Mark all as read" ${unread()?'':'disabled'}`)}</div>
        <div class="nc-scroll" tabindex="0" aria-label="Notification history">${content || empty()}</div>
        <footer class="nc-footer"><div class="nc-feedback" role="status" aria-live="polite">${message?escape(message):`${icon('bell')} ${items.length?`${unread()} unread · ${items.length} total`:'Nothing waiting'}`}${undo?actionButton('undo','Undo clear',null):''}</div>${actionButton('clear-all','Clear all','trash',`class="nc-clear" ${items.length?'':'disabled'}`)}</footer>`;
      root.querySelector('.nc-scroll').scrollTop = scroll;
      if (key) [...root.querySelectorAll('[data-nc-key]')].find(button => button.dataset.ncKey===key)?.focus({preventScroll:true});
    }

    function update(fallback) {
      draw(); onChanged();
      if (fallback && !surface().contains(document.activeElement)) (surface().querySelector(fallback)||surface().querySelector('button'))?.focus({preventScroll:true});
    }

    function dismiss(predicate) {
      const removed = items.filter(predicate);
      if (!removed.length) return;
      const positions = new Map(items.map((item,index) => [item.id,index]));
      undo = {removed,positions};
      items = items.filter(item => !predicate(item));
      if (removed.some(item=>item.id===replyId)) { replyId='';draft=''; }
      message = `${removed.length} notification${removed.length===1?'':'s'} cleared`;
      update('[data-nc="undo"]');
    }

    function handleClick(event) {
      const button = event.target.closest('[data-nc]');
      if (!button || button.disabled) return;
      event.stopPropagation();
      const [action,id] = button.dataset.nc.split(':');
      const item = items.find(item => item.id === id);
      if (action==='inbox') { showInbox();root.querySelector('[data-nc="filter:all"]')?.focus();return; }
      if (action==='close-toast') { arrivalId='';onDismiss();return; }
      if (action==='dismiss') return dismiss(row=>row.id===id);
      if (action==='dismiss-app') return dismiss(row=>row.app===id);
      if (action==='clear-all') return dismiss(()=>true);
      if (action==='undo' && undo) {
        items = [...items,...undo.removed].sort((a,b)=>(undo.positions.get(a.id)??-1)-(undo.positions.get(b.id)??-1));
        message='Notifications restored';undo=null;
      } else if (action==='filter') { filter=id;replyId='';draft=''; }
      else if (action==='quiet') setQuiet(!getQuiet());
      else if (action==='read-all') { items.forEach(item=>item.unread=false);message='All marked as read'; }
      else if (action==='read' && item) item.unread=false;
      else if (action==='expand') { expanded.has(id)?expanded.delete(id):expanded.add(id); }
      else if (action==='body') { expandedBodies.has(id)?expandedBodies.delete(id):expandedBodies.add(id); }
      else if (action==='open' && item) { item.unread=false;message=`${item.action} previewed`; }
      else if (action==='reply' && item) { replyId=id;draft=''; }
      else if (action==='cancel-reply') { replyId='';draft=''; }
      update('[data-nc="filter:all"]');
      if (replyId) surface().querySelector('#nc-reply-input')?.focus();
    }
    function handleInput(event) {
      if (event.target.id!=='nc-reply-input') return;
      draft=event.target.value;
      surface().querySelector('.nc-reply [type=submit]').disabled=!draft.trim();
    }
    function handleSubmit(event) {
      if (!event.target.matches('.nc-reply')) return;
      event.preventDefault(); event.stopPropagation();
      if (!draft.trim()) return;
      const item=items.find(item=>item.id===replyId);
      if (item) item.unread=false;
      replyId='';draft='';message='Demo reply sent';
      update('[data-nc="filter:all"]');
    }
    function handleKey(event) {
      if (event.key==='Escape' && replyId) {
        event.preventDefault();event.stopPropagation();replyId='';draft='';update('[data-nc="filter:all"]');
      }
    }
    for(const element of [root,arrival]) {
      element.addEventListener('click',handleClick);
      element.addEventListener('input',handleInput);
      element.addEventListener('submit',handleSubmit);
      element.addEventListener('keydown',handleKey);
    }
    function showInbox() {
      arrivalId='';replyId='';draft='';update();
    }
    review.addEventListener('click', event => {
      const button=event.target.closest('button');
      if (!button) return;
      if(button.hasAttribute('data-notification-inbox')) { showInbox();return; }
      if (button.dataset.notificationScenario) {
        scenario=button.dataset.notificationScenario;
        items=scenario==='empty'?[]:samples.map(item=>({...item}));
        if (scenario==='busy') for(let i=0;i<23;i++) {
          const sample=samples[1+i%6];
          items.push({...sample,id:`busy-${i}`,time:`${35+i} min`,unread:i%3===0});
        }
        filter='all';expanded.clear();expandedBodies.clear();replyId='';draft='';undo=null;message='';arrivalId='';
      } else if (button.hasAttribute('data-notification-arrive')) {
        const type=document.querySelector('#notification-arrival-type').value;
        const item={...samples[1],id:`incoming-${++incoming}`,title:'Maya · Shell design',body:type==='long'||type==='rich'?longMessage:type==='picture'?'Here’s the wallpaper study. What do you think of the softer colors?':'The notification mockup is ready to look at.',time:'Now',unread:true,...(type==='picture'||type==='rich'?attachment:{})};
        items.unshift(item);
        message=undo?`${undo.removed.length} cleared · new arrival${getQuiet()?' saved quietly':''}`:getQuiet()?'New notification saved quietly':'New notification received';
        arrivalId=getQuiet()?'':item.id;replyId='';draft='';
      }
      update();
      if (button.dataset.notificationScenario) root.querySelector('.nc-scroll').scrollTop=0;
    });
    return {
      render(show) { active=show; if(!show){replyId='';draft='';arrivalId='';} draw(); },
      unread,
      showInbox,
      inboxOpen:()=>active&&!arrivalId,
      focus() { root.querySelector('[data-nc="filter:all"]')?.focus({preventScroll:true}); }
    };
  }
};
