// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';

window.PhosphorTray = (() => {
  const apps = [
    {id:'discord',name:'Discord',symbol:'message',color:'c3',status:'Connected'},
    {id:'steam',name:'Steam',symbol:'gamepad',color:'c2',status:'Online'},
    {id:'nextcloud',name:'Nextcloud',symbol:'cloud',color:'c1',status:'Everything is up to date'},
    {id:'kdeconnect',name:'KDE Connect',menuOnly:true,symbol:'phone',color:'c2',status:'Pixel 9 connected'},
    {id:'obs',name:'OBS Studio',symbol:'record',color:'c4',status:'Not recording'},
    {id:'keepass',name:'KeePassXC',symbol:'key',color:'c1',status:'Database locked'},
    {id:'syncthing',name:'Syncthing',symbol:'refresh',color:'c1',status:'Up to date'},
    {id:'torrent',name:'qBittorrent',symbol:'download',color:'c2',status:'No active transfers'},
    {id:'signal',name:'Signal',symbol:'message',color:'c2',status:'Connected'},
    {id:'element',name:'Element',symbol:'message',color:'c1',status:'Connected'},
    {id:'dropbox',name:'Dropbox',symbol:'cloud',color:'c2',status:'Up to date'},
    {id:'copyq',name:'CopyQ',symbol:'grid',color:'c1',status:'Clipboard ready'},
    {id:'flameshot',name:'Flameshot',symbol:'picture',color:'c4',status:'Ready to capture'},
    {id:'kmail',name:'KMail',symbol:'message',color:'c2',status:'Inbox up to date'},
    {id:'telegram',name:'Telegram',symbol:'message',color:'c2',status:'Connected'},
    {id:'mullvad',name:'Mullvad VPN',symbol:'shield',color:'c3',status:'Connected'},
    {id:'updates',name:'Updates',symbol:'download',color:'c2',status:'System up to date'},
    {id:'kdewallet',name:'KDE Wallet',symbol:'key',color:'c3',status:'Wallet locked'},
    {id:'remmina',name:'Remmina',symbol:'grid',color:'c1',status:'No active sessions'},
    {id:'resilio',name:'Resilio Sync',symbol:'refresh',color:'c4',status:'Up to date'}
  ];
  const defaults={trayIcons:'symbolic',trayLimit:3,trayAttention:true,trayOrder:apps.map(a=>a.id),
    trayVisibility:Object.fromEntries(apps.map(a=>[a.id,['discord','steam'].includes(a.id)?'pinned':'auto']))};
  const copy=value=>JSON.parse(JSON.stringify(value));
  const esc=value=>String(value).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  function preferences(value={}) {
    const result=copy(defaults);
    if(['symbolic','color'].includes(value.trayIcons))result.trayIcons=value.trayIcons;
    if([0,1,2,3,4].includes(value.trayLimit))result.trayLimit=value.trayLimit;
    if(typeof value.trayAttention==='boolean')result.trayAttention=value.trayAttention;
    if(Array.isArray(value.trayOrder)&&value.trayOrder.length===apps.length&&new Set(value.trayOrder).size===apps.length&&value.trayOrder.every(id=>apps.some(a=>a.id===id)))result.trayOrder=[...value.trayOrder];
    for(const a of apps)if(['pinned','auto','overflow','hidden'].includes(value.trayVisibility?.[a.id]))result.trayVisibility[a.id]=value.trayVisibility[a.id];
    return result;
  }
  function create({root,review,desktop,icon,getSettings,patchSettings,onShow,onDismiss,onBarChanged,notify}) {
    let active=false,page='overflow',scenario='everyday',menuId=null,submenu=null,origin='drawer',query='',dragged=null;
    let returnApp=null,fittedLimit=Infinity;
    let paused=false,discordMuted=false,discordScope='mentions',steamStatus='Online',recording=false,recordingPaused=false,notice='';
    const absent=new Set();
    const app=id=>apps.find(a=>a.id===id);
    const prefs=()=>preferences(getSettings());
    const ordered=()=>prefs().trayOrder.map(app).filter(a=>!absent.has(a.id));
    const running=()=>scenario==='empty'?[]:ordered().slice(0,scenario==='few'?2:scenario==='many'?apps.length:8);
    const attention=a=>scenario==='attention'&&a.id==='nextcloud';
    const busy=a=>(scenario==='recording'&&a.id==='obs')||(a.id==='obs'&&recording);
    const status=a=>attention(a)?'Sign-in required':a.id==='nextcloud'&&paused?'Sync paused':busy(a)?(recordingPaused?'Recording paused':'Recording · 02:18'):a.id==='steam'?steamStatus:a.id==='discord'&&discordMuted?'Notifications muted':a.status;
    function barApps() {
      const s=prefs(),available=running().filter(a=>s.trayVisibility[a.id]!=='hidden'&&s.trayVisibility[a.id]!=='overflow');
      const important=available.filter(a=>s.trayAttention&&(attention(a)||busy(a)));
      return [...important,...available.filter(a=>s.trayVisibility[a.id]==='pinned'&&!important.includes(a))].slice(0,Math.min(s.trayLimit,fittedLimit));
    }
    const overflowApps=()=>running().filter(a=>prefs().trayVisibility[a.id]!=='hidden'&&!barApps().some(b=>b.id===a.id));
    const glyph=a=>`<span class="tray-app-icon" style="--tray-app-color:var(--${a.color})">${icon(a.symbol)}</span>`;
    const btn=(action,label,content=label,extra='')=>`<button type="button" data-tray="${action}" aria-label="${esc(label)}" ${extra}>${content}</button>`;
    const small=(action,label,symbol,extra='')=>btn(action,label,icon(symbol),`class="tray-icon-button" ${extra}`);
    function barMarkup() {
      const pins=barApps(),more=overflowApps(),needsAttention=more.some(a=>attention(a)||busy(a));
      return `<div class="bar-tray" role="group" aria-label="System tray" data-tray-icons="${prefs().trayIcons}">${pins.map(a=>btn('app:'+a.id,`${a.name}, ${status(a)}. Right-click for menu.`,`${glyph(a)}${attention(a)||busy(a)?'<i class="tray-status-dot"></i>':''}`,`class="tray-bar-app ${busy(a)?'tray-recording':''}" data-tray-app="${a.id}" title="${esc(a.name+' · '+status(a))}" aria-haspopup="menu" aria-expanded="${active&&menuId===a.id}"`)).join('')}${btn('overflow',`Background apps, ${more.length} in overflow${needsAttention?', attention needed':''}`,`${icon('chevron')}${needsAttention?'<i class="tray-status-dot"></i>':''}`,`class="tray-overflow-trigger" aria-expanded="${active&&page==='overflow'}" aria-controls="tray" title="Background apps"`)}</div>`;
    }
    function header(title,kicker='SYSTEM TRAY',back=false) {
      return `<header class="tray-heading">${back?small('back','Back to background apps','arrow-left'):'<span class="tray-heading-icon">'+icon('tray-icon')+'</span>'}<div><span class="tray-kicker">${kicker}</span><h2>${title}</h2></div>${!back?small('settings','Arrange system tray','tune'):''}${small('close','Close system tray','close-icon')}</header>`;
    }
    function tile(a) {
      return `<div class="tray-app-tile ${menuId===a.id?'selected':''} ${attention(a)?'attention':''}">${btn('app:'+a.id,`${a.name}, ${status(a)}`,`${glyph(a)}<span>${a.name}</span><small>${attention(a)?'Needs attention':busy(a)?'Recording':prefs().trayVisibility[a.id]==='pinned'?'Pinned · in overflow':''}</small>`,`class="tray-app-launch" data-tray-app="${a.id}" title="${esc(status(a))}"`)}${small('menu:'+a.id,`${a.name} menu`,'more',`aria-haspopup="menu" aria-expanded="${menuId===a.id}"`)}</div>`;
    }
    function drawer() {
      const more=overflowApps(),attn=running().find(a=>attention(a)&&prefs().trayVisibility[a.id]!=='hidden');
      return `<div class="tray-drawer material" role="dialog" aria-label="Background apps">${header('Background apps')}<div class="tray-drawer-body">${attn?`<button class="tray-attention-card" data-tray="menu:${attn.id}">${glyph(attn)}<span><strong>${attn.name} needs you</strong><small>Sign in to resume syncing your files.</small></span>${icon('arrow-right')}</button>`:''}${more.length?`<div class="tray-grid" aria-label="Apps in overflow">${more.map(tile).join('')}</div>`:`<div class="tray-empty">${icon('tray-icon')}<h3>${running().length?'Everything is within reach.':'A quiet background.'}</h3><p>${running().length?'Your visible apps are already in the bar. Change what appears in Arrange tray.':'Apps will appear here when they run in the background.'}</p></div>`}</div><footer class="tray-footer"><span>${more.length} in overflow${barApps().length?' · '+barApps().length+' in bar':''}</span>${btn('settings','Arrange tray',`${icon('tune')} Arrange tray`)}</footer></div>`;
    }
    function option(label,value,key,values) {
      return `<label class="tray-setting"><span>${label}</span><select data-tray-pref="${key}" aria-label="${label}">${values.map(([id,name])=>`<option value="${id}" ${String(value)===String(id)?'selected':''}>${name}</option>`).join('')}</select></label>`;
    }
    function settingsPanel() {
      const s=prefs(),list=running().filter(a=>a.name.toLowerCase().includes(query.toLowerCase()));
      return `<div class="tray-drawer tray-settings material" role="dialog" aria-label="Arrange system tray">${header('Arrange tray','MAKE IT YOURS',true)}<div class="tray-settings-body"><div class="tray-preview"><span class="tray-kicker">IN YOUR BAR</span><div data-tray-icons="${s.trayIcons}">${barApps().map(glyph).join('')}${icon('chevron')}</div></div>
        ${option('Icon appearance',s.trayIcons,'trayIcons',[['symbolic','Shell tint'],['color','Accent colors']])}${option('Maximum bar icons',s.trayLimit,'trayLimit',[[0,'Overflow only'],[1,'1 icon'],[2,'2 icons'],[3,'3 icons'],[4,'4 icons']])}
        <label class="tray-attention-setting"><span>Surface important activity<small>Temporarily show apps that need attention or are recording. Always-hidden apps stay hidden.</small></span><input type="checkbox" data-tray-pref="trayAttention" ${s.trayAttention?'checked':''}></label>
        <div class="tray-arrange-title"><h3>Your apps</h3><span>Drag to reorder</span></div><label class="tray-search">${icon('search')}<input id="tray-search" type="search" placeholder="Find an app" aria-label="Find tray app" value="${esc(query)}"></label>
        <div class="tray-arrange-list">${list.map(a=>`<div class="tray-arrange-row" draggable="true" data-tray-drag="${a.id}"><span class="tray-grip" aria-hidden="true">⠿</span>${glyph(a)}<span class="tray-arrange-name">${a.name}<small>${status(a)}</small></span><select data-tray-visibility="${a.id}" aria-label="${a.name} visibility">${[['pinned','Pin to bar'],['auto','Automatic'],['overflow','Overflow only'],['hidden','Always hide']].map(([id,label])=>`<option value="${id}" ${s.trayVisibility[a.id]===id?'selected':''}>${label}</option>`).join('')}</select><div class="tray-reorder">${small('earlier:'+a.id,`Move ${a.name} earlier`,'chevron',s.trayOrder.indexOf(a.id)===0?'disabled':'')}${small('later:'+a.id,`Move ${a.name} later`,'chevron',s.trayOrder.indexOf(a.id)===s.trayOrder.length-1?'disabled':'')}</div></div>`).join('')||'<p class="tray-no-results">No matching apps.</p>'}</div>
        <p class="tray-help">Pinned apps fill the bar first. Automatic apps stay in overflow until they need you. Icons that don’t fit stay in overflow.</p></div><footer class="tray-footer">${btn('reset','Reset tray','Reset tray')}${btn('done','Done','Done','class="tray-primary"')}</footer></div>`;
    }
    function item(id,label,symbol='',extra={}) {return {id,label,symbol,...extra};}
    function menuRows(a) {
      if(submenu==='projector')return [item('display-1','Display 1','grid'),item('display-2','Display 2','grid')];
      if(submenu==='status')return ['Online','Away','Invisible','Offline'].map(value=>item('status:'+value,value,'',{radio:true,checked:steamStatus===value}));
      if(submenu==='notifications')return [item('mute','Mute notifications','',{check:true,checked:discordMuted}),item('mentions','Only mentions','',{radio:true,checked:discordScope==='mentions'}),item('all-messages','All messages','',{radio:true,checked:discordScope==='all-messages'}),item('notification-settings','Notification settings…','tune')];
      if(a.id==='nextcloud')return [item('open','Open Nextcloud','cloud'),item('folder','Open sync folder','folder'),null,item('pause',paused?'Resume syncing':'Pause syncing',paused?'arrow-right':'pause'),item('sync','Sync now','refresh',{disabled:paused||attention(a)}),...(attention(a)?[item('sign-in','Sign in…','key')]:[]),null,item('settings','Settings…','tune'),item('quit','Quit Nextcloud','power')];
      if(a.id==='discord')return [item('open','Open Discord','message'),null,item('notifications','Notifications','bell',{child:true}),item('mute','Mute notifications','',{check:true,checked:discordMuted}),null,item('settings','Settings…','tune'),item('quit','Quit Discord','power')];
      if(a.id==='steam')return [item('open','Open Steam','gamepad'),item('library','Library','grid'),item('downloads','Downloads','download'),null,item('status','Set status','',{child:true,detail:steamStatus}),null,item('settings','Settings…','tune'),item('quit','Exit Steam','power')];
      if(a.id==='obs')return [item('open','Show OBS Studio','record'),null,item('record',busy(a)?'Stop recording':'Start recording','record'),item('pause-recording',recordingPaused?'Resume recording':'Pause recording','pause',{disabled:!busy(a)}),item('projector','Fullscreen projector','grid',{child:true}),null,item('quit','Exit OBS Studio','power')];
      return [item('open','Open '+a.name,a.symbol),...(a.id==='kdeconnect'?[item('ping','Ring my phone','bell'),item('send','Send a file…','folder')]:[]),null,item('settings','Settings…','tune'),item('quit','Quit '+a.name,'power')];
    }
    function menuMarkup() {
      const a=app(menuId);if(!a)return '';
      const rows=menuRows(a),title=submenu?{status:'Set status',notifications:'Notifications',projector:'Fullscreen projector'}[submenu]:a.name;
      return `<div class="tray-app-menu material" role="dialog" aria-label="${esc(a.name)} menu"><header class="tray-menu-heading">${submenu?small('menu-back',`Back to ${a.name} menu`,'arrow-left'):glyph(a)}<div><h3>${title}</h3><p>${submenu?a.name:status(a)}</p></div>${small('menu-close','Close app menu','close-icon')}</header><div class="tray-menu-items" role="menu" aria-label="${esc(title)}">${rows.map(r=>!r?'<div role="separator" class="tray-menu-separator"></div>':btn('action:'+r.id,r.label,`<span class="tray-menu-mark">${r.check?(r.checked?icon('check'):''):r.radio?`<i class="${r.checked?'checked':''}"></i>`:r.symbol?icon(r.symbol):''}</span><span>${r.label}</span>${r.detail?'<small>'+r.detail+'</small>':''}${r.child?icon('chevron'):''}`,`role="${r.check?'menuitemcheckbox':r.radio?'menuitemradio':'menuitem'}" ${r.check||r.radio?'aria-checked="'+r.checked+'"':''} ${r.child?'aria-haspopup="menu"':''} ${r.disabled?'disabled':''} class="tray-menu-item"`)).join('')}</div>${!submenu?`<footer class="tray-menu-footer">${btn('pin:'+a.id,prefs().trayVisibility[a.id]==='pinned'?'Unpin from bar':'Pin to bar',`${icon('pin')} ${prefs().trayVisibility[a.id]==='pinned'?'Unpin from bar':'Pin to bar'}`,`aria-pressed="${prefs().trayVisibility[a.id]==='pinned'}"`)}${small('settings','Tray settings','tune')}</footer>`:''}${notice?'<div class="tray-inline-notice" role="status">'+esc(notice)+'</div>':''}</div>`;
    }
    function position() {
      if(!active)return;
      const d=desktop.getBoundingClientRect(),scale=d.width/desktop.offsetWidth;
      const rect=e=>{const r=e.getBoundingClientRect();return {left:(r.left-d.left)/scale,right:(r.right-d.left)/scale,top:(r.top-d.top)/scale,bottom:(r.bottom-d.top)/scale};};
      const bar=rect(desktop.querySelector('#bar')),trigger=desktop.querySelector('.tray-overflow-trigger');
      const anchor=trigger?rect(trigger):{left:desktop.offsetWidth-50,right:desktop.offsetWidth-20};
      const bottom=getSettings().edge==='bottom',edge=bottom?desktop.offsetHeight-bar.top+12:bar.bottom+12;
      const shelf=root.querySelector('.tray-drawer'),menu=root.querySelector('.tray-app-menu');
      if(shelf){shelf.style.left=`${Math.max(16,Math.min(desktop.offsetWidth-shelf.offsetWidth-16,anchor.right-shelf.offsetWidth))}px`;shelf.style[bottom?'bottom':'top']=`${edge}px`;shelf.style[bottom?'top':'bottom']='auto';}
      if(menu){
        const source=origin==='bar'?desktop.querySelector(`.tray-bar-app[data-tray-app="${menuId}"]`):null;
        const appRect=source?rect(source):anchor;
        let left=appRect.right-menu.offsetWidth,top=bottom?bar.top-12-menu.offsetHeight:bar.bottom+12;
        if(shelf){const sr=rect(shelf),tile=root.querySelector(`.tray-app-tile:has([data-tray-app="${menuId}"])`);left=sr.left-menu.offsetWidth-12;if(left<16)left=sr.right+12;top=tile?rect(tile).top:sr.top;}
        menu.style.left=`${Math.max(16,Math.min(desktop.offsetWidth-menu.offsetWidth-16,left))}px`;
        menu.style.top=`${Math.max(bottom?16:edge,Math.min((bottom?bar.top-12:desktop.offsetHeight-16)-menu.offsetHeight,top))}px`;
      }
    }
    function renderReview() {
      review.classList.toggle('hidden',!active);
      if(!active)return;
      review.innerHTML=`<span class="tray-kicker">TRAY STUDIES</span><nav aria-label="Tray example">${[['everyday','Everyday'],['attention','Needs attention'],['recording','Recording'],['many','20 apps'],['few','All in bar'],['empty','No apps']].map(([id,label])=>`<button data-tray-scenario="${id}" aria-pressed="${scenario===id}">${label}</button>`).join('')}</nav><button data-tray-demo="menu">App menu</button><button data-tray-demo="settings">Arrange tray</button><p>Sample apps · actions are simulated</p>`;
    }
    function draw(focusKey) {
      if(!active)return;
      const focused=focusKey||root.contains(document.activeElement)&&document.activeElement.dataset.tray;
      const field=root.contains(document.activeElement)&&document.activeElement.dataset.trayVisibility;
      const scroll=root.querySelector('.tray-settings-body')?.scrollTop||0;
      updateBar();
      root.innerHTML=(page==='settings'?settingsPanel():page==='overflow'?drawer():'')+menuMarkup();
      root.dataset.trayIcons=prefs().trayIcons;position();
      const body=root.querySelector('.tray-settings-body');if(body)body.scrollTop=scroll;
      if(focused){const target=root.querySelector(`[data-tray="${focused}"]`);(target?.disabled?target.closest('.tray-arrange-row')?.querySelector('button:not(:disabled)'):target)?.focus({preventScroll:true});}
      else if(field)root.querySelector(`[data-tray-visibility="${field}"]`)?.focus({preventScroll:true});
    }
    function updateBar(){
      let tray=desktop.querySelector('.bar-tray');if(!tray){position();return;}
      fittedLimit=prefs().trayLimit;tray.outerHTML=barMarkup();
      // A maximum is not a reservation: other widgets retain their space.
      for(let i=0;i<4&&fittedLimit>0;i++) {
        tray=desktop.querySelector('.bar-tray');const region=tray.closest('.ap-live-region');if(!region)break;
        const box=region.getBoundingClientRect();
        const clipped=[...region.children].some(e=>{const r=e.getBoundingClientRect();return r.left<box.left-1||r.right>box.right+1;});
        if(!clipped)break;
        fittedLimit--;tray.outerHTML=barMarkup();
      }
      position();
    }
    function save(patch,focusKey){patchSettings(patch);updateBar();draw(focusKey);}
    function openMenu(id,from='drawer') {
      if(!app(id)||!running().some(a=>a.id===id))return;
      if(!active)onShow();
      page=from==='bar'?'menu':'overflow';origin=from;returnApp=from==='bar'?id:null;menuId=id;submenu=null;notice='';draw();
      root.querySelector('.tray-menu-item:not(:disabled)')?.focus({preventScroll:true});
    }
    function closeMenu(){const id=menuId;menuId=null;submenu=null;notice='';if(page==='menu')onDismiss();else draw('menu:'+id);}
    function openSettings(){if(!active)onShow();page='settings';menuId=null;submenu=null;draw('back');}
    function act(id) {
      const a=app(menuId);if(!a)return;
      notice='';
      if(['status','notifications','projector'].includes(id)){submenu=id;draw();root.querySelector('.tray-menu-item:not(:disabled)')?.focus();return;}
      if(id.startsWith('status:')){steamStatus=id.slice(7);submenu=null;draw('action:status');updateBar();return;}
      if(id==='mute'){discordMuted=!discordMuted;draw('action:mute');updateBar();return;}
      if(id==='pause'){paused=!paused;draw('action:pause');updateBar();return;}
      if(id==='record'){recording=!busy(a);recordingPaused=false;if(scenario==='recording')scenario='everyday';notice=recording?'Recording started in the preview.':'Recording stopped in the preview.';draw('action:record');updateBar();renderReview();return;}
      if(id==='pause-recording'){recordingPaused=!recordingPaused;draw();updateBar();return;}
      if(id==='quit'){if(busy(a)){notice='Stop the recording before closing OBS Studio.';draw('action:record');return;}absent.add(a.id);menuId=null;submenu=null;page='overflow';updateBar();draw('settings');notify(`${a.name} closed in the preview`);return;}
      if(id==='sign-in'){scenario='everyday';renderReview();notice='Signed in. Syncing resumed in the preview.';draw();updateBar();return;}
      if(id==='mentions'||id==='all-messages'){discordScope=id;draw('action:'+id);return;}
      onDismiss();notify(`${a.name}: ${id==='open'?'app opened':id==='folder'?'sync folder opened':id==='sync'?'sync started':id==='ping'?'phone ringing':id==='send'?'file chooser opened':id==='settings'?'settings opened':id.replaceAll('-',' ')} in the preview`);
    }
    desktop.addEventListener('click',e=>{
      const target=e.target.closest('[data-tray]');if(!target)return;
      e.stopPropagation();const [action,id]=target.dataset.tray.split(/:(.*)/s);
      if(action==='overflow'){if(active&&page==='overflow'&&!menuId)onDismiss();else{if(!active)onShow();page='overflow';returnApp=null;menuId=null;submenu=null;draw('settings');}return;}
      if(action==='app'){if(app(id).menuOnly)openMenu(id,target.closest('#bar')?'bar':'drawer');else {onDismiss();notify(`${app(id).name} opened in the preview`);}}
      else if(action==='menu')openMenu(id,'drawer');
      else if(action==='settings')openSettings();
      else if(action==='close')onDismiss();
      else if(action==='menu-close')closeMenu();
      else if(action==='menu-back'){const old=submenu;submenu=null;draw('action:'+old);}
      else if(action==='back'||action==='done'){page='overflow';query='';draw('settings');}
      else if(action==='pin'){const s=prefs();s.trayVisibility[id]=s.trayVisibility[id]==='pinned'?'auto':'pinned';save({trayVisibility:s.trayVisibility},'pin:'+id);}
      else if(action==='action')act(id);
      else if(action==='reset')save(copy(defaults),'reset');
      else if(action==='earlier'||action==='later'){const order=prefs().trayOrder,index=order.indexOf(id),next=index+(action==='earlier'?-1:1);if(next>=0&&next<order.length){[order[index],order[next]]=[order[next],order[index]];save({trayOrder:order},action+':'+id);}}
    });
    desktop.addEventListener('contextmenu',e=>{
      const target=e.target.closest('[data-tray-app]');if(!target)return;
      e.preventDefault();e.stopPropagation();openMenu(target.dataset.trayApp,target.closest('#bar')?'bar':'drawer');
    });
    root.addEventListener('change',e=>{
      if(e.target.dataset.trayPref){const key=e.target.dataset.trayPref,value=e.target.type==='checkbox'?e.target.checked:key==='trayLimit'?Number(e.target.value):e.target.value;save({[key]:value});root.querySelector(`[data-tray-pref="${key}"]`)?.focus({preventScroll:true});}
      if(e.target.dataset.trayVisibility){const values=prefs().trayVisibility;values[e.target.dataset.trayVisibility]=e.target.value;save({trayVisibility:values});}
    });
    root.addEventListener('input',e=>{if(e.target.id==='tray-search'){query=e.target.value;const caret=e.target.selectionStart;draw();const input=root.querySelector('#tray-search');input.focus();if(input.type!=='search')input.setSelectionRange(caret,caret);}});
    root.addEventListener('dragstart',e=>{const row=e.target.closest('[data-tray-drag]');if(!row)return;dragged=row.dataset.trayDrag;e.dataTransfer.setData('text/plain',dragged);e.dataTransfer.effectAllowed='move';});
    root.addEventListener('dragover',e=>{if(dragged&&e.target.closest('[data-tray-drag]')){e.preventDefault();e.dataTransfer.dropEffect='move';}});
    root.addEventListener('drop',e=>{const row=e.target.closest('[data-tray-drag]');if(!row||!dragged)return;e.preventDefault();if(row.dataset.trayDrag===dragged){dragged=null;return;}const order=prefs().trayOrder.filter(id=>id!==dragged);order.splice(order.indexOf(row.dataset.trayDrag),0,dragged);save({trayOrder:order});dragged=null;});
    root.addEventListener('dragend',()=>{dragged=null;});
    review.addEventListener('click',e=>{
      const scenarioButton=e.target.closest('[data-tray-scenario]'),demo=e.target.closest('[data-tray-demo]');
      if(scenarioButton){scenario=scenarioButton.dataset.trayScenario;absent.clear();recording=false;recordingPaused=false;paused=false;menuId=null;submenu=null;page='overflow';onBarChanged();updateBar();draw();renderReview();review.querySelector(`[data-tray-scenario="${scenario}"]`)?.focus();}
      if(demo){if(demo.dataset.trayDemo==='settings')openSettings();else {if(!running().length){scenario='everyday';absent.clear();onBarChanged();renderReview();}openMenu('steam','bar');}}
    });
    window.addEventListener('resize',position);
    return {barMarkup,position,openSettings,openMenu,restoreFocus(){(returnApp&&desktop.querySelector(`.tray-bar-app[data-tray-app="${returnApp}"]`)||desktop.querySelector('.tray-overflow-trigger')||document.querySelector('.view-switch [data-view="tray"]'))?.focus({preventScroll:true});returnApp=null;},render(show){if(show&&!active){page='overflow';menuId=null;submenu=null;}active=show;root.className=show?'tray-root':'hidden';if(show)draw();else root.replaceChildren();renderReview();updateBar();},
      focus(){root.querySelector('.tray-app-launch,.tray-heading [data-tray="settings"]')?.focus({preventScroll:true});},
      handleKey(e){
        const target=e.target.closest('[data-tray-app]');
        if(target&&(e.key==='ContextMenu'||e.shiftKey&&e.key==='F10')){e.preventDefault();openMenu(target.dataset.trayApp,target.closest('#bar')?'bar':'drawer');return true;}
        if(!active||e.target.closest('#customizer,.review-toolbar,.review-header,#tray-preview-controls'))return false;
        if(e.key==='Escape'){e.preventDefault();if(submenu){const old=submenu;submenu=null;draw('action:'+old);}else if(menuId)closeMenu();else if(page==='settings'){page='overflow';draw('settings');}else onDismiss();return true;}
        const inMenu=e.target.closest('[role="menu"]');
        if(inMenu&&['ArrowDown','ArrowUp','Home','End','ArrowLeft','ArrowRight'].includes(e.key)){
          e.preventDefault();const items=[...inMenu.querySelectorAll('button:not(:disabled)')],i=items.indexOf(document.activeElement);
          if(e.key==='ArrowLeft'){if(submenu){const old=submenu;submenu=null;draw('action:'+old);}else closeMenu();}
          else if(e.key==='ArrowRight'){if(e.target.getAttribute('aria-haspopup')==='menu')e.target.click();}
          else items[e.key==='Home'?0:e.key==='End'?items.length-1:(i+(e.key==='ArrowDown'?1:items.length-1))%items.length]?.focus();
          return true;
        }
        if(e.target.matches('.tray-app-launch')&&['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','Home','End'].includes(e.key)){e.preventDefault();const items=[...root.querySelectorAll('.tray-app-launch')],i=items.indexOf(e.target),delta={ArrowLeft:-1,ArrowRight:1,ArrowUp:-3,ArrowDown:3}[e.key];items[e.key==='Home'?0:e.key==='End'?items.length-1:Math.max(0,Math.min(items.length-1,i+delta))]?.focus();return true;}
        return false;
      }
    };
  }
  return {defaults,preferences,create};
})();
