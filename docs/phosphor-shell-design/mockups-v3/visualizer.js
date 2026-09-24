// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Design-only signal. No microphone, playback, or system audio is accessed.
window.PhosphorVisualizer = (() => {
  const reduced = matchMedia('(prefers-reduced-motion: reduce)');
  let frame = 0, lastPaint = 0, phase = 1.8, previous = 0, targets = [];
  let options = {playing:true,motion:true,style:'ribbon',glow:true};

  function envelope(x, time) {
    const bass = Math.exp(-Math.pow((x-.2)/.17,2));
    const mids = Math.exp(-Math.pow((x-.57)/.23,2));
    const pulse = .5 + .5*Math.sin(time*3.5);
    return (.12 + bass*(.3+pulse*.3) + mids*.28) * (.66+.2*Math.sin(x*32-time*4)+.14*Math.sin(x*63+time*2));
  }

  function draw(target, time, active) {
    const {canvas,ctx,colors,mini} = target, w=canvas.width, h=canvas.height;
    ctx.clearRect(0,0,w,h);
    const gradient=ctx.createLinearGradient(0,0,w,0);
    colors.forEach((color,i)=>gradient.addColorStop(i/3,color));
    ctx.strokeStyle=gradient;ctx.fillStyle=gradient;
    ctx.lineCap='round';ctx.lineJoin='round';
    ctx.shadowColor=colors[1];ctx.shadowBlur=options.glow&&!mini?8:0;
    const style=mini?'bars':options.style;
    const strength=active?1:.12;

    if(style==='bars') {
      const count=mini?10:42, gap=mini?2:3, width=w/count-gap;
      for(let i=0;i<count;i++) {
        const level=Math.max(mini?2:3,envelope(i/count,time)*h*.95*strength);
        const x=i*w/count;
        ctx.globalAlpha=.85;
        ctx.fillRect(x,h*.82-level,width,level);
        if(!mini){ctx.globalAlpha=.13;ctx.fillRect(x,h*.85,width,Math.min(level*.2,h*.14));}
      }
    } else if(style==='halo') {
      const cx=w/2,cy=h/2,r=h*.25,count=72;
      ctx.lineWidth=2;
      for(let i=0;i<count;i++) {
        const a=i/count*Math.PI*2-Math.PI/2;
        const level=envelope((i<count/2?i:count-i)/(count/2),time)*h*.28*strength;
        ctx.globalAlpha=.75;
        ctx.beginPath();ctx.moveTo(cx+Math.cos(a)*r,cy+Math.sin(a)*r);
        ctx.lineTo(cx+Math.cos(a)*(r+3+level),cy+Math.sin(a)*(r+3+level));ctx.stroke();
      }
      ctx.globalAlpha=.28;ctx.lineWidth=1;
      ctx.beginPath();ctx.ellipse(cx,cy,r-5,r-5,0,0,Math.PI*2);ctx.stroke();
      ctx.globalAlpha=.8;ctx.shadowBlur=0;
      ctx.beginPath();
      for(let i=0;i<=40;i++) {
        const x=cx-r*.62+i/40*r*1.24;
        const y=cy+Math.sin(i*.36-time*3)*Math.sin(i/40*Math.PI)*h*.045*strength;
        if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
      }
      ctx.stroke();
      // Fine side traces make the radial style part of the horizontal card.
      ctx.globalAlpha=.18;
      ctx.beginPath();ctx.moveTo(10,cy);ctx.lineTo(cx-r-h*.22,cy);ctx.moveTo(cx+r+h*.22,cy);ctx.lineTo(w-10,cy);ctx.stroke();
    } else {
      for(let layer=3;layer>=0;layer--) {
        ctx.beginPath();
        for(let i=0;i<=100;i++) {
          const x=i/100, taper=Math.pow(Math.sin(x*Math.PI),.65);
          const y=h*.5+Math.sin(x*13-time*2+layer*.56)*envelope(x,time+layer*.12)*h*.52*taper*strength;
          if(i===0)ctx.moveTo(x*w,y);else ctx.lineTo(x*w,y);
        }
        ctx.globalAlpha=layer===0?.95:.14+layer*.055;ctx.lineWidth=layer===0?2:1.3;
        ctx.stroke();
        if(layer===0){ctx.lineTo(w,h*.86);ctx.lineTo(0,h*.86);ctx.closePath();ctx.globalAlpha=.055;ctx.fill();}
      }
    }
    ctx.globalAlpha=1;ctx.shadowBlur=0;
  }

  function tick(now) {
    frame=0;
    if(now-lastPaint>=32) {
      if(previous)phase+=Math.min((now-previous)/1000,.1);
      previous=now;lastPaint=now;
      targets.forEach(target=>draw(target,phase,true));
    }
    frame=requestAnimationFrame(tick);
  }

  function sync(next = options) {
    options={...next};cancelAnimationFrame(frame);frame=0;previous=0;
    const shell=document.querySelector('#desktop');
    if(!shell)return;
    const css=getComputedStyle(shell);
    const colors=[1,2,3,4].map(i=>css.getPropertyValue(`--c${i}`).trim());
    targets=[...document.querySelectorAll('canvas[data-visualizer]')]
      .filter(canvas=>canvas.getClientRects().length)
      .map(canvas=>({canvas,ctx:canvas.getContext('2d'),mini:canvas.dataset.visualizer==='mini',colors}));
    const active=options.playing&&options.style!=='off';
    targets.forEach(target=>draw(target,phase,active));
    if(targets.length&&active&&options.motion&&!reduced.matches&&!document.hidden)frame=requestAnimationFrame(tick);
  }
  document.addEventListener('visibilitychange',()=>sync());
  reduced.addEventListener('change',()=>sync());
  return {sync};
})();
