// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';

window.PhosphorShortcuts = {
  create({root,review,icon,getContext,onContextMode,onDismiss}) {
    const esc = value => String(value??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
    const names = {tiling:'Tiling',scrolling:'Scrolling',snapping:'Snapping',general:'General',shell:'Shell',all:'All shortcuts'};
    const accents = {tiling:'var(--c2)',scrolling:'var(--c3)',snapping:'var(--c1)',general:'var(--c2)',shell:'var(--c4)',all:'var(--c2)'};
    const groupOrder = ['Focus','Arrange','Zones','Columns','Sizing','View','Tabs','Column size','Window size','Layouts','General','Virtual screens','Shell'];
    const groupIcons = {Focus:'eye',Arrange:'grid',Zones:'grid',Columns:'grid',Sizing:'tune',View:'eye',Tabs:'folder','Column size':'tune','Window size':'tune',Layouts:'grid',General:'keyboard','Virtual screens':'grid',Shell:'keyboard'};
    const state = {scope:'tiling',query:'',profile:'defaults',assigned:false,guide:true,large:false,layouts:true};
    let active=false;
    const openDetails = new Set();
    const custom = {
      focus_zone_left:['Meta+H','Alt+Shift+Left'],focus_zone_right:['Meta+L','Alt+Shift+Right'],
      focus_zone_up:['Meta+K','Alt+Shift+Up'],focus_zone_down:['Meta+J','Alt+Shift+Down'],
      toggle_window_float:['Meta+Shift+F'],scroll_focus_column_left_or_last:['Meta+Ctrl+Alt+Shift+Home'],
      scroll_focus_column_right_or_first:['Meta+Ctrl+Alt+Shift+End'],scroll_increase_column_width:[],
      'launcher.toggle':['Meta+Space'],'lock.lock':['Meta+Escape']
    };
    // Custom chords illustrate overrides, not recommendations or host settings.
    function catalog() {
      return PhosphorShortcutCatalog.map(row=>({...row,
        triggers:state.profile==='unassigned'?[]:state.profile==='custom'&&Object.hasOwn(custom,row.id)?custom[row.id]:row.triggers,
        changed:state.profile==='custom'&&Object.hasOwn(custom,row.id)
      }));
    }
    function applies(row) {
      if(row.modes==='layouts'&&!state.layouts)return false;
      if(state.scope==='all')return true;
      if(['general','shell'].includes(state.scope))return row.modes===state.scope;
      return row.modes==='all'||row.modes==='layouts'||row.modes===state.scope||row.modes==='managed'&&state.scope!=='snapping';
    }
    function results() {
      const normalize=value=>value.toLocaleLowerCase().replace(/\b(super|windows key)\b/g,'meta').replace(/\breturn\b/g,'enter').replace(/\bescape\b/g,'esc').replace(/\bpage up\b/g,'pgup').replace(/\b(page down|pgdn)\b/g,'pgdown');
      const tokens=normalize(state.query).split(/[\s+]+/).filter(Boolean);
      const modifiers=['meta','ctrl','alt','shift'];
      const hasModifier=tokens.some(token=>modifiers.includes(token));
      const isKey=token=>modifiers.includes(token)||['enter','esc','pgup','pgdown','home','end','tab','space'].includes(token)||hasModifier&&(/^[a-z0-9]$/.test(token)||['left','right','up','down'].includes(token));
      const keyTokens=tokens.filter(isKey),words=tokens.filter(token=>!isKey(token));
      return catalog().filter(row=>applies(row)&&(!state.assigned||row.triggers.length)&&
        (!keyTokens.length||row.triggers.some(chord=>keyTokens.every(token=>normalize(chord).split('+').includes(token))))&&
        words.every(token=>normalize(`${row.label} ${row.id} ${row.group} ${row.description||''} ${row.triggers.join(' ')} ${row.modes} ${row.triggers.length?'':'unassigned'}`).includes(token)));
    }
    function keys(chord,range=false) {
      const labels={Left:'←',Right:'→',Up:'↑',Down:'↓',Return:'Enter',Escape:'Esc',PgUp:'PgUp',PgDown:'PgDn'};
      // Preserve the literal plus key in a chord such as Meta++.
      const parts=chord.endsWith('++')?[...chord.slice(0,-2).split('+'),'+']:chord.split('+');
      return `<span class="sc-chord" aria-label="${esc(chord)}">${parts.map((part,i)=>`<kbd${range&&i===parts.length-1?' class="sc-range"':''} aria-hidden="true">${esc(labels[part]||part)}</kbd>`).join('')}</span>`;
    }
    function bindings(row) {
      return row.triggers.length?`<span class="sc-chords">${row.triggers.map(chord=>keys(chord)).join('<span class="sc-or">or</span>')}</span>`:
        `<span class="sc-unassigned">${row.external?'Set in compositor':'Unassigned'}</span>`;
    }
    function rowHTML(row) {
      const description=row.description||(row.modes==='tiling'?'Available with tiling algorithms that support a master area.':row.group==='Column size'||row.group==='Window size'?'Column size follows the strip’s axis. Window size follows the axis inside a column.':`Available in ${row.modes==='all'||row.modes==='general'||row.modes==='layouts'?'all placement modes':row.modes==='managed'?'tiling and scrolling':row.modes}.`);
      return `<details class="sc-row" data-sc-detail="${esc(row.id)}"${openDetails.has(row.id)?' open':''}><summary><span class="sc-row-label">${esc(row.label)}${row.changed?'<small>Customized</small>':''}</span>${bindings(row)}</summary>
        <div class="sc-detail"><p>${esc(description)}</p><code>${esc(row.id)}</code>${!row.triggers.length?`<p class="sc-binding-label">${row.external?'Assign this action in your compositor’s shortcut configuration.':'Assign a shortcut in Phosphor settings.'}</p>`:''}</div></details>`;
    }
    function familyHTML(rows) {
      if(rows.length===1||state.query)return rows.map(rowHTML).join('');
      const first=rows[0],prefix=first.triggers[0]?.split('+').slice(0,-1).join('+');
      const uniform=rows.every(row=>row.triggers.length===1&&row.triggers[0].split('+').slice(0,-1).join('+')===prefix);
      const allUnassigned=rows.every(row=>!row.triggers.length);
      const range=rows.length===9?'1–9':rows.map(row=>({left:'←',right:'→',up:'↑',down:'↓'}[row.id.split('_').at(-1)]||row.triggers[0]?.split('+').at(-1))).join(' ');
      const keyHTML=uniform?`<span class="sc-chords">${keys(`${prefix}+${range}`,true)}</span>`:`<span class="sc-unassigned">${allUnassigned?'Unassigned':`${rows.length} bindings`}</span>`;
      const id=`family:${first.family}`;
      return `<details class="sc-family" data-sc-detail="${id}"${openDetails.has(id)?' open':''}><summary><span class="sc-row-label">${esc(first.familyLabel)}${rows.some(row=>row.changed)?'<small>Customized</small>':''}</span>${keyHTML}</summary><div class="sc-family-rows">${rows.map(rowHTML).join('')}</div></details>`;
    }
    function groupHTML(group,rows) {
      const families=new Set();
      return `<section class="sc-group"><h3>${icon(groupIcons[group])}${esc(group)}<span>${rows.length}</span></h3>${rows.map(row=>{
        if(!row.family)return rowHTML(row);
        if(families.has(row.family))return '';families.add(row.family);
        return familyHTML(rows.filter(other=>other.family===row.family));
      }).join('')}</section>`;
    }
    function symbol(mode) {
      if(['tiling','scrolling','snapping'].includes(mode))return `<span class="sc-mode-symbol ${mode}" aria-hidden="true">${'<i></i>'.repeat(mode==='snapping'?4:3)}</span>`;
      return icon(mode==='shell'?'keyboard':mode==='general'?'tune':'search');
    }
    function guideHTML() {
      if(!state.guide||state.query||!['tiling','scrolling','snapping'].includes(state.scope))return '';
      const content={
        tiling:['A place for every window.','Move focus without disturbing the layout. Master controls apply to layouts with a master area.','focus_master','Focus master'],
        scrolling:['Keep your place in the flow.','Focus travels through the strip. Columns can share space, stack windows, or become tabs.','scroll_center_column','Center column'],
        snapping:['Your layout. Your call.','Send a window to a numbered zone, swap positions, or extend it across neighboring zones.','snap_to_zone_1','Move to zone 1']
      }[state.scope];
      const row=catalog().find(row=>row.id===content[2]);
      const tiles=state.scope==='tiling'?['Master','02','03']:state.scope==='snapping'?['01','02','03','04']:['01','02','03','04'];
      return `<div class="sc-guide"><div><span class="sc-overline">${names[state.scope]} / Field guide</span><h3>${content[0]}</h3><p>${content[1]}</p><div class="sc-guide-chord">${bindings(row)}<span>${content[3]}</span></div></div><div class="sc-diagram ${state.scope}" role="img" aria-label="${state.scope==='tiling'?'One master window beside two stacked windows':state.scope==='snapping'?'Four zones, with zone one selected': 'A scrolling strip with the second column selected'}">${tiles.map((label,i)=>`<div class="sc-tile ${i===(state.scope==='scrolling'?1:0)?'selected':''}"><span>${label}</span></div>`).join('')}</div></div>`;
    }
    function drawReview() {
      review.classList.toggle('hidden',!active);if(!active)return;
      review.innerHTML=`<label>Bindings <select id="sc-profile"><option value="defaults">Project defaults</option><option value="custom">Custom bindings + alternatives</option><option value="unassigned">Nothing assigned</option><option value="unavailable">Catalog unavailable</option></select></label>
        <label>Workspace mode <select id="sc-context-mode">${['tiling','scrolling','snapping'].map(mode=>`<option value="${mode}">${names[mode]}</option>`).join('')}</select></label>
        <button data-sc-layouts aria-pressed="${state.layouts}">Layout support</button><button data-sc-large aria-pressed="${state.large}">Large text</button><p>Reference only. Bindings are browser fixtures.</p>`;
      review.querySelector('#sc-profile').value=state.profile;
      review.querySelector('#sc-context-mode').value=contextMode;
    }
    let contextMode='tiling';
    function drawContent() {
      const rows=results(),count=root.querySelector('#sc-count'),body=root.querySelector('.sc-scroll');
      if(!body)return;
      const unavailable=state.profile==='unavailable';
      count.textContent=unavailable?'Catalog unavailable':`${rows.length} ${rows.length===1?'action':'actions'}${['tiling','scrolling','snapping'].includes(state.scope)?' · includes shared shortcuts':''}`;
      root.querySelector('[data-sc-clear]').hidden=!state.query;
      root.querySelector('.sc-search-hint').hidden=!!state.query;
      if(unavailable) {
        body.innerHTML=`<div class="sc-empty">${icon('keyboard')}<h3>Shortcuts aren’t available yet.</h3><p>The shortcut service couldn’t be reached. Try reconnecting to load your bindings.</p><button data-sc-retry>Try again</button></div>`;return;
      }
      if(!rows.length) {
        body.innerHTML=`<div class="sc-empty">${icon('search')}<h3>${state.query?'No matching shortcuts.':'No assigned shortcuts here.'}</h3><p>${state.query?`Try an action, a key, or another section. Searching ${names[state.scope]}.`:'Turn off “Assigned only” to see actions you can bind.'}</p>${state.query?'<button data-sc-reset>Clear search</button>':'<button data-sc-show-all>Show unassigned actions</button>'}${state.query&&state.scope!=='all'?'<button data-sc-scope="all">Search all shortcuts</button>':''}</div>`;return;
      }
      const note=state.scope==='shell'?'<div class="sc-note">Shell actions use compositor shortcuts. A missing binding means it hasn’t been provided here.</div>':'';
      body.innerHTML=`${guideHTML()}${note}<div class="sc-groups">${groupOrder.filter(group=>rows.some(row=>row.group===group)).map(group=>groupHTML(group,rows.filter(row=>row.group===group))).join('')}</div>`;
    }
    function draw() {
      root.classList.toggle('hidden',!active);drawReview();if(!active){root.innerHTML='';return;}
      root.dataset.large=String(state.large);
      const previousFocus=root.contains(document.activeElement)?document.activeElement:null;
      const focusId=previousFocus?.id;
      const scrollTop=root.querySelector('.sc-scroll')?.scrollTop||0;
      const context=getContext();
      root.innerHTML=`<div class="sc-scrim" data-sc-dismiss></div><section class="sc-sheet material" role="dialog" aria-modal="true" aria-labelledby="sc-title" style="--sc-accent:${accents[state.scope]}">
        <aside class="sc-sidebar"><div class="sc-brand"><span class="phosphor-mark" aria-hidden="true"></span><div><b>PHOSPHOR</b><small>At your fingertips.</small></div></div><span class="sc-overline">Browse shortcuts</span>
          <nav class="sc-navigation" aria-label="Shortcut sections">${Object.entries(names).map(([id,label],i)=>`${i===3?'<div class="sc-nav-divider"></div>':''}<button data-sc-scope="${id}" aria-pressed="${state.scope===id}" style="--sc-accent:${accents[id]}">${symbol(id)}${label}${id===contextMode?'<span class="sc-live-dot" title="Current workspace mode" aria-label="Current workspace mode"></span>':''}</button>`).join('')}</nav>
          <div class="sc-context"><div class="sc-mini-line"></div><span class="sc-overline">This workspace</span><b>${esc(context.workspace)} · ${names[contextMode]}</b><p>Browse any mode.<br>Your windows stay in place.</p></div></aside>
        <div class="sc-main"><header class="sc-heading"><div><h2 id="sc-title">Keyboard shortcuts</h2><p>Find your next move.</p></div><button class="close" data-sc-dismiss aria-label="Close shortcut reference">${icon('close-icon')}</button></header>
          <div class="sc-search">${icon('search')}<input id="sc-search" type="search" aria-label="Search ${names[state.scope]} shortcuts" placeholder="Find an action or key…" autocomplete="off" spellcheck="false" value="${esc(state.query)}"><span class="sc-search-hint sc-chords"><kbd>Ctrl</kbd><kbd>F</kbd></span><button data-sc-clear aria-label="Clear shortcut search">${icon('close-icon')}</button></div>
          <div class="sc-toolbar"><span id="sc-count" role="status" aria-live="polite"></span><label><input id="sc-assigned" type="checkbox"${state.assigned?' checked':''}>Assigned only</label><button data-sc-guide aria-pressed="${state.guide}"${!['tiling','scrolling','snapping'].includes(state.scope)?' hidden':''}>${icon('grid')}${state.guide?'Hide':'Show'} field guide</button></div>
          <div class="sc-scroll" tabindex="0" aria-label="${names[state.scope]} shortcut reference"></div>
          <footer class="sc-footer"><span>${icon('keyboard')}Meta is the Super / Windows key</span><span><kbd>Esc</kbd>${state.query?'Clear search':'Close reference'}</span></footer></div></section>`;
      drawContent();
      root.querySelector('.sc-scroll').scrollTop=scrollTop;
      if(focusId)root.querySelector(`#${focusId}`)?.focus({preventScroll:true});
    }
    function focusSearch() { root.querySelector('#sc-search')?.focus({preventScroll:true}); }
    function redrawAndFocus(selector) {draw();root.querySelector(selector)?.focus({preventScroll:true});}
    function clearSearch() {state.query='';draw();focusSearch();}
    root.addEventListener('input',event=>{
      if(event.target.id!=='sc-search')return;
      state.query=event.target.value;drawContent();root.querySelector('.sc-scroll').scrollTop=0;
      root.querySelector('.sc-footer>span:last-child').innerHTML=`<kbd>Esc</kbd>${state.query?'Clear search':'Close reference'}`;
    });
    root.addEventListener('change',event=>{
      if(event.target.id==='sc-assigned'){state.assigned=event.target.checked;drawContent();}
    });
    root.addEventListener('toggle',event=>{
      const id=event.target.dataset.scDetail;if(!id||!event.target.isConnected)return;
      if(event.target.open)openDetails.add(id);else openDetails.delete(id);
    },true);
    root.addEventListener('click',event=>{
      event.stopPropagation();
      const b=event.target.closest('button,[data-sc-dismiss]');if(!b)return;
      if(b.hasAttribute('data-sc-dismiss')){onDismiss();return;}
      if(b.dataset.scScope){state.scope=b.dataset.scScope;draw();root.querySelector('.sc-scroll').scrollTop=0;root.querySelector(`.sc-navigation [data-sc-scope="${state.scope}"]`)?.focus({preventScroll:true});}
      else if(b.hasAttribute('data-sc-clear')||b.hasAttribute('data-sc-reset'))clearSearch();
      else if(b.hasAttribute('data-sc-guide')){state.guide=!state.guide;redrawAndFocus('[data-sc-guide]');}
      else if(b.hasAttribute('data-sc-show-all')){state.assigned=false;redrawAndFocus('#sc-assigned');}
      else if(b.hasAttribute('data-sc-retry')){state.profile='defaults';draw();focusSearch();}
    });
    review.addEventListener('change',event=>{
      if(event.target.id==='sc-profile')state.profile=event.target.value;
      if(event.target.id==='sc-context-mode'){contextMode=event.target.value;state.scope=contextMode;state.query='';onContextMode(contextMode);}
      draw();review.querySelector(`#${event.target.id}`)?.focus({preventScroll:true});
    });
    review.addEventListener('click',event=>{
      const b=event.target.closest('button');if(!b)return;
      if(b.hasAttribute('data-sc-large'))state.large=!state.large;
      if(b.hasAttribute('data-sc-layouts'))state.layouts=!state.layouts;
      const selector=b.hasAttribute('data-sc-large')?'[data-sc-large]':'[data-sc-layouts]';
      draw();review.querySelector(selector)?.focus({preventScroll:true});
    });
    return {
      render(show) {
        const opening=show&&!active;active=show;
        if(opening){contextMode=getContext().mode;state.scope=contextMode;state.query='';}
        draw();if(opening)focusSearch();
      },
      focus:focusSearch,
      handleKey(event) {
        if(!active)return false;
        const inside=root.contains(event.target);
        if(event.key==='Escape'){event.preventDefault();if(state.query)clearSearch();else onDismiss();return true;}
        if(!inside)return true; // Review controls keep their native keyboard behavior.
        if((event.ctrlKey||event.metaKey)&&event.key.toLowerCase()==='f'){event.preventDefault();focusSearch();root.querySelector('#sc-search').select();return true;}
        if(event.key==='Tab') {
          const elements=[...root.querySelectorAll('button,input,summary,[tabindex="0"]')].filter(el=>el.getClientRects().length&&!el.disabled&&!el.hidden);
          const first=elements[0],last=elements.at(-1);
          if(event.shiftKey&&event.target===first){event.preventDefault();last.focus();}
          else if(!event.shiftKey&&event.target===last){event.preventDefault();first.focus();}
        }
        if(event.target.closest('.sc-navigation')&&['ArrowDown','ArrowUp','Home','End'].includes(event.key)){
          event.preventDefault();const items=[...root.querySelectorAll('.sc-navigation button')],index=items.indexOf(event.target);
          items[event.key==='Home'?0:event.key==='End'?items.length-1:(index+(event.key==='ArrowDown'?1:items.length-1))%items.length].focus();
        }
        return true;
      }
    };
  }
};
