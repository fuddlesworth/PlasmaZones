// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';

window.PhosphorStatusIcons = (() => {
  const catalog = [
    ['wifi','Wi-Fi','wifi','When connected','Your wireless connection'],
    ['audio','Sound','volume','When muted','Volume and output device'],
    ['bluetooth','Bluetooth','bluetooth','When connected','Your connected devices'],
    ['battery','Battery','battery','When present','Charge and time remaining'],
    ['microphone','Microphone','microphone','When in use','Input activity and mute'],
    ['nightlight','Night light','moon','When on','Warmer display colors'],
    ['focus','Do not disturb','bell','When on','Keep notifications quiet'],
    ['airplane','Airplane mode','airplane','When on','Wireless radio state'],
    ['power','Power profile','tune','When not balanced','Performance and energy use']
  ];
  const ids = catalog.map(([id])=>id);
  const defaults = {statusOrder:ids,statusVisibility:{wifi:'always',audio:'always',bluetooth:'hidden',battery:'auto',microphone:'auto',nightlight:'hidden',focus:'auto',airplane:'auto',power:'hidden'},statusLimit:4,statusBatteryPercent:true};
  function preferences(value={}) {
    const order=Array.isArray(value.statusOrder)?[...new Set(value.statusOrder.filter(id=>ids.includes(id)))]:[];
    const visibility=Object.fromEntries(ids.map(id=>[id,['always','auto','hidden'].includes(value.statusVisibility?.[id])?value.statusVisibility[id]:defaults.statusVisibility[id]]));
    return {statusOrder:[...order,...ids.filter(id=>!order.includes(id))],statusVisibility:visibility,statusLimit:[2,3,4,5,6].includes(value.statusLimit)?value.statusLimit:4,statusBatteryPercent:typeof value.statusBatteryPercent==='boolean'?value.statusBatteryPercent:true};
  }
  function create({root,review,desktop,icon,getSettings,patchSettings,service,getView,onShow,onDismiss,onControls,onBarChanged}) {
    let active=false,page='settings',selected='audio',origin='audio',dragged='',example='everyday';
    const esc=value=>String(value).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
    const prefs=()=>preferences(getSettings());
    const entry=id=>catalog.find(row=>row[0]===id);
    const state=id=>service.statusSnapshot()[id];
    const eligible=()=>prefs().statusOrder.filter(id=>prefs().statusVisibility[id]==='always'||prefs().statusVisibility[id]==='auto'&&state(id).active);
    const shown=()=>eligible().slice(0,prefs().statusLimit);
    const overflow=()=>eligible().slice(prefs().statusLimit);
    const glyph=id=>`<span class="si-glyph ${state(id).off?'si-off':''}">${icon(entry(id)[2])}${state(id).alert?'<i aria-hidden="true"></i>':''}</span>`;
    const button=(action,label,extra='',classes='si-action')=>`<button type="button" data-si="${action}" ${extra} class="${classes}">${label}</button>`;
    const small=(action,label,symbol)=>button(action,icon(symbol),`aria-label="${label}"`,'si-icon-button');
    function barMarkup(preview=false) {
      const visible=shown(),more=overflow();
      return `<div class="status-cluster si-cluster ${preview?'si-preview-cluster':''}" role="group" aria-label="System status">${visible.map(id=>`<button type="button" data-status-icon="${id}" data-view="controls" aria-label="${esc(entry(id)[1]+', '+state(id).summary+'. Open quick settings.')}" title="${esc(entry(id)[1]+' · '+state(id).summary+'\nRight-click for controls')}" aria-haspopup="dialog" aria-expanded="${getView()==='controls'||active&&page==='menu'&&selected===id}">${glyph(id)}${id==='battery'&&prefs().statusBatteryPercent&&state(id).percent!==null?`<span class="si-percent">${state(id).percent}%</span>`:''}</button>`).join('')}${more.length?button('overflow',icon('more'),`aria-label="${more.length} more status icons" aria-expanded="${active&&page==='overflow'}"`,'si-overflow'):''}${!visible.length?`<button type="button" data-view="controls" aria-label="Open quick settings" title="Quick settings">${icon('tune')}</button>`:''}</div>`;
    }
    function save(patch,focus) {
      patchSettings(patch);onBarChanged();draw(focus);
    }
    function header(title,subtitle,symbol) {
      return `<header class="si-heading"><span class="si-heading-icon">${icon(symbol)}</span><div><span class="si-kicker">QUICK SETTINGS</span><h2>${title}</h2><p>${esc(subtitle)}</p></div>${small('close','Close status icons','close-icon')}</header>`;
    }
    function settingsPanel() {
      const p=prefs();
      return `<div class="si-panel si-settings material" role="dialog" aria-label="Status icons">${header('Status icons','Choose what stays in view.','tune')}<div class="si-body"><div class="si-live-preview"><div><span class="si-kicker">IN YOUR BAR</span><p>Updates as you arrange</p></div>${barMarkup(true)}</div><div class="si-preferences"><label><span>Visible icons<small>Extra icons stay in the more menu.</small></span><select data-si-pref="statusLimit" aria-label="Maximum visible status icons">${[2,3,4,5,6].map(n=>`<option value="${n}" ${p.statusLimit===n?'selected':''}>Up to ${n}</option>`).join('')}</select></label><label><span>Battery percentage</span><input type="checkbox" data-si-pref="statusBatteryPercent" ${p.statusBatteryPercent?'checked':''}></label></div><div class="si-list-heading"><h3>Your indicators</h3><span>Drag to reorder</span></div><div class="si-arrange-list">${p.statusOrder.map((id,index)=>{const c=entry(id);return `<div class="si-arrange-row" data-si-drag="${id}" draggable="true"><span class="si-grip" aria-hidden="true">⠿</span><span class="si-row-symbol">${glyph(id)}</span><span class="si-row-copy"><b>${c[1]}</b><small>${c[4]}</small></span><select data-si-visibility="${id}" aria-label="${c[1]} visibility">${[['always','Always'],['auto',c[3]],['hidden','Hidden']].map(([value,label])=>`<option value="${value}" ${p.statusVisibility[id]===value?'selected':''}>${label}</option>`).join('')}</select><span class="si-reorder">${button('up:'+id,'↑',`aria-label="Move ${c[1]} earlier" ${index===0?'disabled':''}`,'si-icon-button')}${button('down:'+id,'↓',`aria-label="Move ${c[1]} later" ${index===ids.length-1?'disabled':''}`,'si-icon-button')}</span></div>`;}).join('')}</div><div class="si-hint">${icon('mouse')}<p><b>One group. Individual controls.</b>Left-click opens Quick settings. Right-click an icon for its controls, or focus it and press Shift+F10.</p></div></div><footer class="si-footer">${button('reset','Reset icons','','si-text-button')}<span>Changes apply immediately</span>${button('close','Done','','si-primary')}</footer></div>`;
    }
    function menuItem(action,label,symbol,extra='') {
      return button(action,`${icon(symbol)}<span>${label}</span>`,extra,'si-menu-item');
    }
    function menuPanel() {
      const s=state(selected),c=entry(selected),all=service.statusSnapshot();
      const toggle=(action,label,on)=>menuItem(action,label,on?'check':c[2],`role="menuitemcheckbox" aria-checked="${on}" ${!s.available?'disabled':''}`);
      let controls='',items='';
      if(selected==='audio') {
        controls=`<div class="si-volume"><label for="si-volume">Volume <output>${s.off?'Muted':all.audio.volume+'%'}</output></label><input id="si-volume" type="range" min="0" max="100" value="${all.audio.volume}" style="--value:${all.audio.volume}%" aria-label="Volume" ${!s.available?'disabled':''}></div>`;
        items=toggle('toggle:audio','Mute sound',s.off)+`<div class="si-menu-label">OUTPUT DEVICE</div>`+all.audio.outputs.map(name=>menuItem('output:'+name,name,name===all.audio.device?'check':'speaker',`role="menuitemradio" aria-checked="${name===all.audio.device}" ${!s.available?'disabled':''}`)).join('');
      } else if(selected==='wifi') items=toggle('toggle:wifi','Wi-Fi',!s.off);
      else if(selected==='bluetooth') items=toggle('toggle:bluetooth','Bluetooth',!s.off);
      else if(selected==='microphone') items=toggle('toggle:microphone','Mute microphone',s.off);
      else if(selected==='battery') {
        controls=`<div class="si-battery-charge"><div><span style="width:${s.percent??0}%"></span></div><p>${esc(s.summary)}</p></div>`;
        items=menuItem('profile:saver','Use Power saver','battery',`role="menuitem" ${!all.power.available?'disabled':''}`);
      } else if(selected==='power') items=[['saver','Power saver'],['balanced','Balanced'],['performance','Performance']].map(([id,label])=>menuItem('profile:'+id,label,all.power.profile===id?'check':'tune',`role="menuitemradio" aria-checked="${all.power.profile===id}" ${!s.available?'disabled':''}`)).join('');
      else items=toggle('toggle:'+selected,c[1],s.active);
      const detail=selected==='focus'?'notifications':selected;
      const detailLabel={wifi:'Choose a network',bluetooth:'Connect a device',audio:'Sound controls',microphone:'Input controls',focus:'Notification center'}[selected]||c[1]+' details';
      return `<div class="si-panel si-menu material" role="dialog" aria-label="${c[1]} controls">${header(c[1],s.summary,c[2])}${controls}<div class="si-menu-items" role="menu" aria-label="${c[1]} actions">${items}<div class="si-separator" role="separator"></div>${menuItem('details:'+detail,detailLabel,'arrow-right','role="menuitem"')}</div><footer class="si-menu-footer">${menuItem('settings','Arrange status icons','tune')}</footer></div>`;
    }
    function overflowPanel() {
      return `<div class="si-panel si-overflow-panel material" role="dialog" aria-label="More status icons">${header('More status','Your other indicators.','more')}<div class="si-overflow-list">${overflow().map(id=>button('menu:'+id,`${glyph(id)}<span><b>${entry(id)[1]}</b><small>${esc(state(id).summary)}</small></span>${icon('arrow-right')}`,`data-status-icon="${id}" aria-label="${entry(id)[1]} controls"`,'si-overflow-row')).join('')||'<p class="si-empty">All your visible icons fit in the bar.</p>'}</div><footer class="si-menu-footer">${menuItem('settings','Arrange status icons','tune')}</footer></div>`;
    }
    function position() {
      if(!active)return;
      const panel=root.firstElementChild,anchor=desktop.querySelector(`#bar [data-status-icon="${origin}"]`)||desktop.querySelector('#bar .si-overflow')||desktop.querySelector('#bar .status-cluster')||desktop.querySelector('#bar');
      if(!panel||!anchor)return;
      const bounds=desktop.getBoundingClientRect(),a=anchor.getBoundingClientRect(),scale=bounds.width/desktop.offsetWidth;
      const left=(a.right-bounds.left)/scale-panel.offsetWidth,top=(a.bottom-bounds.top)/scale+12;
      panel.style.left=Math.max(18,Math.min(desktop.offsetWidth-panel.offsetWidth-18,left))+'px';
      panel.style.top=getSettings().edge==='bottom'?Math.max(18,(a.top-bounds.top)/scale-panel.offsetHeight-12)+'px':Math.min(top,desktop.offsetHeight-panel.offsetHeight-18)+'px';
    }
    function draw(focusKey) {
      if(!active)return;
      const scroll=root.querySelector('.si-body')?.scrollTop||0;
      root.innerHTML=page==='settings'?settingsPanel():page==='overflow'?overflowPanel():menuPanel();
      if(root.querySelector('.si-body'))root.querySelector('.si-body').scrollTop=scroll;
      position();
      if(focusKey)root.querySelector(focusKey)?.focus({preventScroll:true});
    }
    function open(next,id) {
      page=next;if(id){selected=id;origin=id;}onShow();if(!active)return;draw();
      history.replaceState(null,'',location.hash.split('/').slice(0,2).join('/')+'/'+(page==='menu'?selected:page));
      root.querySelector(page==='menu'?'[role^="menuitem"]:not(:disabled)':page==='settings'?'[data-si-pref]':'.si-overflow-row')?.focus({preventScroll:true});
    }
    function reorder(id,before) {
      if(id===before)return;
      const order=prefs().statusOrder.filter(value=>value!==id);order.splice(before?order.indexOf(before):order.length,0,id);
      save({statusOrder:order},`[data-si-visibility="${id}"]`);
    }
    function handleAction(action) {
      const [name,...parts]=action.split(':'),id=parts.join(':');
      if(name==='close'){onDismiss();return;}
      if(name==='settings'||name==='overflow'||name==='menu'){open(name,id);return;}
      if(name==='details'){onControls(id);return;}
      if(name==='reset'){save(preferences(defaults),'[data-si="reset"]');return;}
      if(name==='toggle'||name==='output'||name==='profile'){service.statusAction(name,id);onBarChanged();draw(`[data-si="${action}"]`);return;}
      if(name==='up'||name==='down') {
        const order=prefs().statusOrder,index=order.indexOf(id),next=index+(name==='up'?-1:1);
        if(next<0||next>=order.length)return;
        [order[index],order[next]]=[order[next],order[index]];save({statusOrder:order},`[data-si-visibility="${id}"]`);
      }
    }
    desktop.addEventListener('click',e=>{const indicator=e.target.closest('#bar [data-status-icon]');if(indicator)origin=indicator.dataset.statusIcon;const button=e.target.closest('[data-si]');if(!button)return;e.preventDefault();e.stopPropagation();handleAction(button.dataset.si);});
    desktop.addEventListener('contextmenu',e=>{const target=e.target.closest('[data-status-icon]');if(!target&&!e.target.closest('.status-cluster'))return;e.preventDefault();e.stopPropagation();if(target)open('menu',target.dataset.statusIcon);else open('settings');});
    root.addEventListener('change',e=>{
      const id=e.target.dataset.siVisibility,key=e.target.dataset.siPref;
      if(id)save({statusVisibility:{...prefs().statusVisibility,[id]:e.target.value}},`[data-si-visibility="${id}"]`);
      if(key)save({[key]:e.target.type==='checkbox'?e.target.checked:Number(e.target.value)},`[data-si-pref="${key}"]`);
    });
    root.addEventListener('input',e=>{
      if(e.target.id!=='si-volume')return;
      service.statusAction('volume',Number(e.target.value));onBarChanged();
      root.querySelector('.si-volume output').textContent=state('audio').off?'Muted':e.target.value+'%';
      root.querySelector('.si-heading p').textContent=state('audio').summary;
      e.target.style.setProperty('--value',e.target.value+'%');
    });
    root.addEventListener('dragstart',e=>{const row=e.target.closest('[data-si-drag]');if(!row)return;dragged=row.dataset.siDrag;e.dataTransfer.setData('text/plain',dragged);e.dataTransfer.effectAllowed='move';});
    root.addEventListener('dragover',e=>{if(dragged&&e.target.closest('[data-si-drag]')){e.preventDefault();e.dataTransfer.dropEffect='move';}});
    root.addEventListener('drop',e=>{const row=e.target.closest('[data-si-drag]');if(!row||!dragged)return;e.preventDefault();reorder(dragged,row.dataset.siDrag);dragged='';});
    root.addEventListener('dragend',()=>{dragged='';});
    review.addEventListener('click',e=>{
      const target=e.target.closest('button');if(!target)return;
      if(target.dataset.siExample){example=target.dataset.siExample;service.statusScenario(example);onBarChanged();draw();renderReview();}
      if(target.dataset.siPreview)open(target.dataset.siPreview==='settings'?'settings':'menu',target.dataset.siPreview==='settings'?null:target.dataset.siPreview);
    });
    function renderReview() {
      review.className=active?'si-review':'hidden';
      if(active)review.innerHTML=`<span>PREVIEW STATE</span><nav aria-label="Status example">${[['everyday','Everyday'],['meeting','In a call'],['travel','Travel'],['desktop','Desktop']].map(([id,label])=>`<button data-si-example="${id}" aria-pressed="${example===id}">${label}</button>`).join('')}</nav><button data-si-preview="wifi">Wi-Fi menu</button><button data-si-preview="audio">Sound menu</button><button data-si-preview="settings">Arrange icons</button><p>Simulated devices. Your system stays unchanged.</p>`;
    }
    window.addEventListener('resize',position);
    return {barMarkup,position,openSettings:()=>open('settings'),openMenu:id=>open('menu',ids.includes(id)?id:'audio'),
      render(show){active=show;root.className=show?'si-root':'hidden';if(show)draw();else root.replaceChildren();renderReview();},
      restoreFocus(){(desktop.querySelector(`#bar [data-status-icon="${origin}"]`)||desktop.querySelector('#bar .status-cluster button'))?.focus({preventScroll:true});},
      handleKey(e){
        const indicator=e.target.closest('[data-status-icon]');
        if(indicator&&(e.key==='ContextMenu'||e.shiftKey&&e.key==='F10')){e.preventDefault();open('menu',indicator.dataset.statusIcon);return true;}
        if(!indicator&&e.target.closest('#bar .status-cluster')&&(e.key==='ContextMenu'||e.shiftKey&&e.key==='F10')){e.preventDefault();open('settings');return true;}
        if(!active||e.target.closest('.review-toolbar,.review-header,#customizer,#status-preview-controls'))return false;
        if(e.key==='Escape'){e.preventDefault();onDismiss();return true;}
        if(e.target.closest('[role="menu"]')&&['ArrowDown','ArrowUp','Home','End'].includes(e.key)) {
          e.preventDefault();const items=[...root.querySelectorAll('[role^="menuitem"]:not(:disabled)')],i=items.indexOf(e.target);
          items[e.key==='Home'?0:e.key==='End'?items.length-1:(i+(e.key==='ArrowUp'?items.length-1:1))%items.length]?.focus();return true;
        }
        return false;
      }
    };
  }
  return {defaults,preferences,create};
})();
