#include "WebServer.h"
#include "../config.h"
#include "../protocol/Commands.h"
#include "../protocol/TextProgram.h"
#include "../protocol/AA55Packet.h"
#include "../modes/ClockWeatherMode.h"
#include <ArduinoJson.h>

SignWebServer webServer;

static void sendPackets(SignBLE* ble, const std::vector<std::vector<uint8_t>>& pkts) {
    for (auto& pkt : pkts) {
        ble->send(pkt);
        delay(150);
    }
}

void SignWebServer::begin(SignBLE* ble, ModeManager* modes) {
    _ble = ble;
    _modes = modes;
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
    setupRoutes();
    _server.begin();
    Serial.printf("[HTTP] Server started on port %d\n", HTTP_PORT);
}

void SignWebServer::setupRoutes() {

    // ---------------------------------------------------------------
    // GET / — serve the web UI
    // ---------------------------------------------------------------
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/html", R"rawhtml(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>LED Sign</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,system-ui,sans-serif;background:#1a1a2e;color:#e0e0e0;padding:16px;max-width:480px;margin:0 auto}
h1{font-size:1.4em;text-align:center;margin-bottom:16px;color:#3282ff}
.card{background:#16213e;border-radius:12px;padding:16px;margin-bottom:12px}
.card h2{font-size:1em;margin-bottom:12px;color:#888}
.modes{display:grid;grid-template-columns:repeat(2,1fr);gap:8px}
.mode-btn{background:#0f3460;border:2px solid transparent;border-radius:10px;padding:12px 8px;text-align:center;cursor:pointer;transition:.2s;font-size:.85em;color:#e0e0e0}
.mode-btn:hover{background:#1a4a8a}.mode-btn.active{border-color:#3282ff;background:#1a4a8a}
.mode-btn .icon{font-size:1.5em;display:block;margin-bottom:4px}
.slider-row{display:flex;align-items:center;gap:12px}
.slider-row input[type=range]{flex:1;accent-color:#3282ff}
.slider-row .val{min-width:24px;text-align:center;font-weight:bold}
.field{margin-bottom:10px}
.field label{display:block;font-size:.85em;color:#888;margin-bottom:4px}
.field input[type=text],.field select{width:100%;background:#0f3460;border:1px solid #333;border-radius:6px;padding:8px;color:#e0e0e0;font-size:.9em}
.field input[type=color]{width:48px;height:32px;border:none;background:none;cursor:pointer}
.row{display:flex;gap:8px;align-items:end}
.row .field{flex:1}
button.send{background:#3282ff;color:#fff;border:none;border-radius:8px;padding:10px 16px;cursor:pointer;font-size:.9em;width:100%}
button.send:hover{background:#4a9aff}
.status{font-size:.75em;color:#666;text-align:center;margin-top:8px}
#status-dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:#444;margin-right:4px}
#status-dot.ok{background:#00ff88}
.hidden{display:none}
#preview{width:100%;border-radius:8px;image-rendering:pixelated;cursor:default;touch-action:none}
#preview.draggable{cursor:grab}
#preview.dragging{cursor:grabbing}
#custom-opts{margin-top:12px}
</style></head><body>
<h1>LED Sign Control</h1>
<div class="card"><h2>Mode</h2>
<div class="modes">
<div class="mode-btn active" data-mode="clock" onclick="setMode('clock')"><span class="icon">&#128336;</span>Clock</div>
<div class="mode-btn" data-mode="text" onclick="showPanel('text')"><span class="icon">&#9997;</span>Text</div>
<div class="mode-btn" data-mode="message" onclick="showPanel('message')"><span class="icon">&#128172;</span>Message</div>
<div class="mode-btn" data-mode="off" onclick="setMode('off')"><span class="icon">&#9211;</span>Off</div>
</div></div>
<div class="card" id="text-panel" style="display:none"><h2>Custom Text</h2>
<div class="field"><label>Text</label><input type="text" id="custom-text" placeholder="Hello world"></div>
<div class="row">
<div class="field"><label>Color</label><input type="color" id="custom-color" value="#3282ff"></div>
<div class="field"><label>Scroll</label><select id="custom-scroll"><option value="left">Scroll</option><option value="static">Static</option></select></div>
<div class="field"><label>Speed</label><input type="range" id="custom-speed" min="1" max="20" value="10"></div>
</div>
<button class="send" onclick="sendText()">Send Text</button></div>
<div class="card" id="msg-panel" style="display:none"><h2>Message</h2>
<div class="row">
<div class="field"><label>Message</label><input type="text" id="msg-text" placeholder="Message..."></div>
<div class="field"><label>Color</label><input type="color" id="msg-color" value="#ffffff"></div>
</div>
<button class="send" onclick="sendMsg()">Send Message</button></div>
<div class="card"><h2>Brightness</h2>
<div class="slider-row"><input type="range" id="bright" min="0" max="15" value="10" oninput="setBright(this.value)"><span class="val" id="bright-val">10</span></div></div>
<div class="card"><h2>Clock Layout</h2>
<div class="field"><select id="clock-layout" onchange="pickLayout(this.value)">
<option value="0">Normal</option>
<option value="1">Large - Status Bar</option>
<option value="2">Large - Stacked</option>
<option value="3">Custom (drag to arrange)</option>
</select></div>
<div class="row" style="margin:8px 0">
<div class="field"><label>Time Color</label><input type="color" id="time-color" value="#3282ff" onchange="setTimeColor(this.value)"></div>
</div>
<canvas id="preview"></canvas>
<div id="custom-opts" class="hidden">
<div class="row" style="margin-top:8px">
<div class="field"><label>Time Scale</label><select id="ct-scale" onchange="E.time.sc=+this.value;saveCustom()"><option value="2">2x</option><option value="3" selected>3x</option></select></div>
<div class="field"><label>Icon</label><select id="ci-size" onchange="E.icon.sz=+this.value;saveCustom()"><option value="0">Small</option><option value="1">Large</option></select></div>
<div class="field"><label>Time Color</label><input type="color" id="ct-color" value="#3282ff" onchange="E.time.color=this.value;saveCustom()"></div>
</div>
</div>
</div>
<div class="card"><h2>Font</h2>
<div class="field"><select id="font-sel" onchange="pickFont(this.value)">
<option value="0">Standard</option>
<option value="1">Rounded</option>
<option value="2">Block</option>
<option value="3">Detailed 5x7</option>
<option value="4">Standard + AA</option>
<option value="5">Rounded + AA</option>
<option value="6">Block + AA</option>
<option value="7">Detailed 5x7 + AA</option>
</select></div>
</div>
<div class="card">
<button class="send" onclick="showForecast()">Show Forecast</button></div>
<div class="card"><h2>Timer</h2>
<div id="timer-setup">
<div class="row">
<div class="field"><label>Hours</label><input type="number" id="cd-h" min="0" max="23" value="0" style="width:60px"></div>
<div class="field"><label>Min</label><input type="number" id="cd-m" min="0" max="59" value="5" style="width:60px"></div>
<div class="field"><label>Sec</label><input type="number" id="cd-s" min="0" max="59" value="0" style="width:60px"></div>
</div>
<div class="row" style="margin-top:8px;gap:8px">
<button class="send" style="flex:1" onclick="startCountdown()">Countdown</button>
<button class="send" style="flex:1;background:#0f3460" onclick="startStopwatch()">Stopwatch</button>
</div></div>
<div id="timer-active" style="display:none">
<div style="text-align:center;font-size:1.8em;font-family:monospace;margin:8px 0" id="timer-display">00:00</div>
<div class="row" style="gap:8px">
<button class="send" style="flex:1" id="timer-pause-btn" onclick="togglePause()">Pause</button>
<button class="send" style="flex:1;background:#c0392b" onclick="resetTimer()">Reset</button>
</div></div></div>
<div class="card"><h2>News Headlines</h2>
<div class="row">
<div class="field" style="flex:1"><label>RSS Feed URL</label><input type="text" id="rss-url" placeholder="https://feeds.example.com/rss"></div>
</div>
<div class="row" style="margin-top:8px;gap:8px">
<button class="send" style="flex:1" onclick="saveRss()">Save</button>
<button class="send" style="flex:1;background:#0f3460" onclick="fetchNews()">Fetch Now</button>
</div></div>
<div class="card"><h2>Notifications</h2>
<div class="row">
<div class="field" style="flex:1"><label>Proxy URL</label><input type="text" id="proxy-url" placeholder="http://192.168.1.x:8890"></div>
</div>
<button class="send" style="margin-top:8px" onclick="saveProxy()">Save</button>
<div class="row" style="margin-top:8px">
<div class="field" style="flex:1"><label>Quick Message</label><input type="text" id="notif-msg" placeholder="Someone is at the door!"></div>
</div>
<button class="send" style="margin-top:8px;background:#e67e22" onclick="sendNotif()">Send to Sign</button>
<div id="notif-list" style="margin-top:8px;font-size:.8em;color:#888"></div></div>
<div class="status"><span id="status-dot"></span><span id="status-text">Connecting...</span></div>
<script>
var wx={t:70,ic:'cloud'};
var curLayout=0;
var timeCol='#3282ff';
function setMode(m,extra){
 document.querySelectorAll('.mode-btn').forEach(b=>b.classList.toggle('active',b.dataset.mode===m));
 document.getElementById('text-panel').style.display='none';
 document.getElementById('msg-panel').style.display='none';
 let p=new URLSearchParams({mode:m});
 if(extra)Object.keys(extra).forEach(k=>p.set(k,extra[k]));
 fetch('/api/mode?'+p).then(r=>r.json()).then(d=>{console.log('mode:',d)}).catch(e=>console.error(e));
}
function showPanel(p){
 document.getElementById('text-panel').style.display=p==='text'?'block':'none';
 document.getElementById('msg-panel').style.display=p==='message'?'block':'none';
 document.querySelectorAll('.mode-btn').forEach(b=>b.classList.toggle('active',b.dataset.mode===p));
}
function sendText(){
 setMode('text',{text:document.getElementById('custom-text').value,
  color:document.getElementById('custom-color').value,
  scroll:document.getElementById('custom-scroll').value,
  speed:parseInt(document.getElementById('custom-speed').value)});
}
function sendMsg(){
 setMode('message',{text:document.getElementById('msg-text').value,
  color:document.getElementById('msg-color').value});
}
var brightTimer;
function setBright(v){
 document.getElementById('bright-val').textContent=v;
 clearTimeout(brightTimer);
 brightTimer=setTimeout(()=>fetch('/api/brightness?level='+v),300);
}
function showForecast(){fetch('/api/forecast')}

// --- Canvas preview & editor ---
var PX=8,GW=96,GH=16;
var cv=document.getElementById('preview');
var cc=cv.getContext('2d');
cv.width=GW*PX;cv.height=GH*PX;
var DG=[[7,5,5,5,7],[2,6,2,2,7],[7,1,7,4,7],[7,1,7,1,7],[5,5,7,1,1],[7,4,7,1,7],[7,4,7,5,7],[7,1,1,1,1],[7,5,7,5,7],[7,5,7,1,7]];
var PF={'0':[7,5,5,5,7],'1':[2,6,2,2,7],'2':[7,1,7,4,7],'3':[7,1,7,1,7],'4':[5,5,7,1,1],'5':[7,4,7,1,7],'6':[7,4,7,5,7],'7':[7,1,1,1,1],'8':[7,5,7,5,7],'9':[7,5,7,1,7],'F':[7,4,6,4,4]};
function dp(x,y,c){if(x>=0&&x<GW&&y>=0&&y<GH){cc.fillStyle=c;cc.fillRect(x*PX,y*PX,PX-1,PX-1);}}
function dd(n,x,y,s,c){var g=DG[n];for(var r=0;r<5;r++)for(var cl=0;cl<3;cl++)if(g[r]&(4>>cl))for(var a=0;a<s;a++)for(var b=0;b<s;b++)dp(x+cl*s+b,y+r*s+a,c);return 3*s;}
function dpf(ch,x,y,c){var g=PF[ch];if(!g)return 3;var w=3;for(var r=0;r<5;r++)for(var cl=0;cl<w;cl++)if(g[r]&(1<<(w-1-cl)))dp(x+cl,y+r,c);return w;}
function dps(s,x,y,c){var cx=x;for(var i=0;i<s.length;i++){if(i>0)cx++;cx+=dpf(s[i],cx,y,c);}return cx-x;}
function tc(t){return t<=32?'#0064ff':t<=50?'#00c8ff':t<=65?'#00ff64':t<=80?'#b4ff00':t<=90?'#ff8c00':'#ff2800';}
function swid(s){var w=0;for(var i=0;i<s.length;i++){if(i>0)w++;w+=3;}return w;}
function drawTime(hStr,mStr,x,y,sc,col){
 var cx=x;
 for(var i=0;i<hStr.length;i++){if(i>0)cx+=sc;cx+=dd(+hStr[i],cx,y,sc,col);}
 var lp=Math.max(1,sc-2),rp=sc,ds=sc;
 cx+=lp;
 var doff=Math.max(2,Math.floor(5*sc/4)),cy=y+Math.floor(5*sc/2);
 for(var a=0;a<ds;a++)for(var b=0;b<ds;b++){dp(cx+b,cy-doff+a,col);dp(cx+b,cy+doff-1+a,col);}
 cx+=ds+rp;
 for(var i=0;i<mStr.length;i++){if(i>0)cx+=sc;cx+=dd(+mStr[i],cx,y,sc,col);}
 return cx;
}
function timeW(hStr,sc){var lp=Math.max(1,sc-2),rp=sc,ds=sc;return hStr.length*(3*sc)+(hStr.length-1)*sc+lp+ds+rp+2*(3*sc)+sc;}

// 5x7 digit data for temp rendering
var D7=[[14,17,17,17,17,17,14],[4,12,4,4,4,4,14],[14,17,1,6,8,16,31],
 [14,17,1,6,1,17,14],[2,6,10,18,31,2,2],[31,16,30,1,1,17,14],
 [6,8,16,30,17,17,14],[31,1,2,4,8,8,8],[14,17,17,14,17,17,14],
 [14,17,17,15,1,2,12]];
var D7F=[31,16,16,30,16,16,16];
function dd7(ch,x,y,c){
 var g;if(ch>='0'&&ch<='9')g=D7[+ch];else if(ch=='F')g=D7F;
 else if(ch=='-'){for(var cl=1;cl<4;cl++)dp(x+cl,y+3,c);return 5;}
 else return 0;
 for(var r=0;r<7;r++)for(var cl=0;cl<5;cl++)if(g[r]&(1<<(4-cl)))dp(x+cl,y+r,c);return 5;
}
function dts(s,x,y,c){var cx=x;for(var i=0;i<s.length;i++){if(i>0)cx++;cx+=dd7(s[i],cx,y,c);}return cx-x;}
function tsw(s){var w=0;for(var i=0;i<s.length;i++){if(i>0)w++;w+=5;}return w;}

// 7x7 icon data
var I7={sun:[0x14,0x08,0x3E,0x5D,0x3E,0x08,0x14],moon:[0x1C,0x3C,0x3C,0x3C,0x3C,0x3C,0x1C],
 cloud:[0x00,0x1C,0x22,0x41,0x41,0x3E,0x00],rain:[0x1C,0x3E,0x7F,0x00,0x2A,0x15,0x2A],
 snow:[0x1C,0x3E,0x7F,0x00,0x2A,0x14,0x2A],storm:[0x1C,0x3E,0x7F,0x08,0x18,0x3C,0x08],
 fog:[0x7F,0x00,0x3E,0x00,0x7F,0x00,0x3E]};
function di7(x,y,c){
 var d=I7[wx.ic]||I7.cloud;
 for(var r=0;r<7;r++)for(var cl=0;cl<7;cl++)if(d[r]&(1<<(6-cl)))dp(x+cl,y+r,c);
}

// Old icon drawing (unused but kept for custom layout)
function drawIcon(x,y,sz,c){
 if(sz===0){di7(x,y,c);return{w:7,h:7};}
 else{di7(x,y,c);return{w:7,h:7};}
}

// Element positions for custom layout
var E={time:{x:5,y:0,sc:3,color:'#3282ff'},temp:{x:65,y:5},icon:{x:65,y:0,sz:0}};
var drag=null,doff={x:0,y:0};

function elBounds(){
 var d=new Date(),h=''+(d.getHours()%12||12),m=('0'+d.getMinutes()).slice(-2);
 var s=E.time.sc,tw=timeW(h,s);
 var ts=(''+wx.t+'F'),tw2=tsw(ts);
 return{
  time:{x:E.time.x,y:E.time.y,w:tw,h:5*s},
  temp:{x:E.temp.x,y:E.temp.y,w:tw2,h:7},
  icon:{x:E.icon.x,y:E.icon.y,w:7,h:7}
 };
}

function render(){
 cc.fillStyle='#0a0a1a';cc.fillRect(0,0,cv.width,cv.height);
 cc.strokeStyle='#151530';cc.lineWidth=1;
 for(var x=0;x<=GW;x++){cc.beginPath();cc.moveTo(x*PX,0);cc.lineTo(x*PX,GH*PX);cc.stroke();}
 for(var y=0;y<=GH;y++){cc.beginPath();cc.moveTo(0,y*PX);cc.lineTo(GW*PX,y*PX);cc.stroke();}

 var d=new Date(),h=''+(d.getHours()%12||12),m=('0'+d.getMinutes()).slice(-2);
 var ap=d.getHours()>=12?'PM':'AM';
 var tcol=timeCol,tpc=tc(wx.t),icol='#c8c8c8';

 if(curLayout===0){
  var sc=2,dh=10,ty=Math.max(0,Math.floor((GH-dh)/2));
  var tw=timeW(h,sc);
  var apw=9,gap=3,ts=''+wx.t+'F',tw2=tsw(ts);
  var tot=tw+1+apw+gap+7+2+tw2;
  var xs=Math.max(0,Math.floor((GW-tot)/2));
  var cx=drawTime(h,m,xs,ty,sc,tcol);
  dps(ap,cx+1,ty+dh-5,tcol);cx+=1+apw;
  di7(cx+gap,Math.max(0,Math.floor((GH-7)/2)),icol);
  dts(ts,cx+gap+7+2,Math.max(0,Math.floor((GH-7)/2)),tpc);
 }else if(curLayout===1){
  var sc=3,dh=15,ty=Math.max(0,Math.floor((GH-dh)/2));
  var tw=timeW(h,sc);
  di7(1,Math.max(0,Math.floor((GH-7)/2)),icol);
  drawTime(h,m,Math.max(0,Math.floor((GW-tw)/2)),ty,sc,tcol);
  var ts=''+wx.t+'F',tw2=tsw(ts);
  dts(ts,GW-tw2-1,Math.max(0,Math.floor((GH-7)/2)),tpc);
 }else if(curLayout===2){
  var sc=3,dh=15,ty=Math.max(0,Math.floor((GH-dh)/2));
  var tw=timeW(h,sc);
  var ts=''+wx.t+'F',tw2=tsw(ts);
  var tot=tw+3+Math.max(tw2,7);
  var xs=Math.max(0,Math.floor((GW-tot)/2));
  var cx=drawTime(h,m,xs,ty,sc,tcol);
  di7(cx+3,0,icol);
  dts(ts,cx+3,8,tpc);
 }else{
  tcol=E.time.color||'#3282ff';
  drawTime(h,m,E.time.x,E.time.y,E.time.sc,tcol);
  dts(''+wx.t+'F',E.temp.x,E.temp.y,tpc);
  di7(E.icon.x,E.icon.y,icol);
  var b=elBounds();
  cc.lineWidth=1;
  for(var k of['time','temp','icon']){
   var r=b[k];cc.strokeStyle=drag===k?'#ff0':'#3282ff44';
   cc.strokeRect(r.x*PX-1,r.y*PX-1,r.w*PX+2,r.h*PX+2);
   cc.fillStyle='#888';cc.font='9px sans-serif';cc.fillText(k,r.x*PX,r.y*PX-3);
  }
 }
}

function pickLayout(v){
 curLayout=+v;
 document.getElementById('custom-opts').className=curLayout===3?'':'hidden';
 cv.className=curLayout===3?'draggable':'';
 fetch('/api/settings?layout='+v);
 render();
}
function pickFont(v){
 var fid=(+v)%4;
 var aa=+v>=4?1:0;
 fetch('/api/settings?font='+fid+'&aa='+aa);
 render();
}
function setTimeColor(c){
 fetch('/api/settings?time_color='+encodeURIComponent(c));
 render();
}

function saveCustom(){
 var p='layout=3&ct_x='+E.time.x+'&ct_y='+E.time.y+'&ct_s='+E.time.sc
  +'&cp_x='+E.temp.x+'&cp_y='+E.temp.y
  +'&ci_x='+E.icon.x+'&ci_y='+E.icon.y+'&ci_sz='+E.icon.sz;
 fetch('/api/settings?'+p);
 render();
}

// Drag and drop
function canvasXY(e){
 var r=cv.getBoundingClientRect();
 var x=(e.clientX-r.left)/r.width*GW, y=(e.clientY-r.top)/r.height*GH;
 return{x:Math.floor(x),y:Math.floor(y)};
}
function hitTest(mx,my){
 if(curLayout!==3)return null;
 var b=elBounds();
 for(var k of['time','temp','icon']){
  var r=b[k];
  if(mx>=r.x-1&&mx<=r.x+r.w+1&&my>=r.y-1&&my<=r.y+r.h+1)return k;
 }
 return null;
}
cv.addEventListener('pointerdown',function(e){
 if(curLayout!==3)return;
 var p=canvasXY(e);var hit=hitTest(p.x,p.y);
 if(hit){drag=hit;doff={x:p.x-E[hit].x,y:p.y-E[hit].y};cv.className='dragging';cv.setPointerCapture(e.pointerId);}
});
cv.addEventListener('pointermove',function(e){
 if(!drag)return;
 var p=canvasXY(e);
 E[drag].x=Math.max(0,Math.min(GW-5,p.x-doff.x));
 E[drag].y=Math.max(0,Math.min(GH-5,p.y-doff.y));
 render();
});
cv.addEventListener('pointerup',function(e){
 if(drag){cv.className='draggable';drag=null;saveCustom();}
});

// Timer controls
var timerMode=0,timerRunning=false;
function startCountdown(){
 var s=+document.getElementById('cd-h').value*3600 + +document.getElementById('cd-m').value*60 + +document.getElementById('cd-s').value;
 if(s<=0)s=300;
 fetch('/api/timer?action=countdown&seconds='+s).then(r=>r.json()).then(updateTimerUI);
}
function startStopwatch(){fetch('/api/timer?action=stopwatch').then(r=>r.json()).then(updateTimerUI);}
function togglePause(){
 fetch('/api/timer?action='+(timerRunning?'pause':'resume')).then(r=>r.json()).then(updateTimerUI);
}
function resetTimer(){fetch('/api/timer?action=reset').then(r=>r.json()).then(updateTimerUI);}
function updateTimerUI(d){
 timerMode=d.timer_mode||0;timerRunning=d.timer_running||false;
 document.getElementById('timer-setup').style.display=timerMode===0?'block':'none';
 document.getElementById('timer-active').style.display=timerMode!==0?'block':'none';
 document.getElementById('timer-pause-btn').textContent=timerRunning?'Pause':'Resume';
 if(timerMode===1){var r=d.remaining||0;document.getElementById('timer-display').textContent=fmtTime(r);}
 else if(timerMode===2){document.getElementById('timer-display').textContent=fmtTime(d.elapsed||0);}
}
function fmtTime(s){
 var h=Math.floor(s/3600),m=Math.floor((s%3600)/60),sec=s%60;
 if(h>0)return ('0'+h).slice(-2)+':'+('0'+m).slice(-2)+':'+('0'+sec).slice(-2);
 return ('0'+m).slice(-2)+':'+('0'+sec).slice(-2);
}

// News controls
function saveRss(){fetch('/api/news?rss_url='+encodeURIComponent(document.getElementById('rss-url').value));}
function fetchNews(){fetch('/api/news?fetch=1');}

// Notification controls
function saveProxy(){fetch('/api/notify?proxy_url='+encodeURIComponent(document.getElementById('proxy-url').value));}
function sendNotif(){
 var msg=document.getElementById('notif-msg').value;
 if(msg)fetch('/api/notify?message='+encodeURIComponent(msg));
}

setInterval(render,1000);
render();

function poll(){
 fetch('/api/status').then(r=>r.json()).then(d=>{
  document.getElementById('status-dot').className=d.ble_state==='Ready'?'ok':'';
  document.getElementById('status-text').textContent=d.ble_state+' | '+d.mode+' | '+d.uptime_sec+'s';
  document.querySelectorAll('.mode-btn').forEach(b=>b.classList.toggle('active',b.dataset.mode===d.mode));
  if(d.clock_layout!=null){curLayout=d.clock_layout;document.getElementById('clock-layout').value=d.clock_layout;
   document.getElementById('custom-opts').className=curLayout===3?'':'hidden';
   cv.className=curLayout===3?'draggable':'';}
  if(d.font_id!=null){var fv=d.font_id+(d.font_aa?4:0);document.getElementById('font-sel').value=fv;}
  if(d.time_color){timeCol=d.time_color;document.getElementById('time-color').value=d.time_color;}
  if(d.temp!=null)wx.t=d.temp;
  if(d.weather_icon)wx.ic=d.weather_icon;
  if(d.ct_x!=null){E.time.x=d.ct_x;E.time.y=d.ct_y;E.time.sc=d.ct_s;
   E.temp.x=d.cp_x;E.temp.y=d.cp_y;E.icon.x=d.ci_x;E.icon.y=d.ci_y;E.icon.sz=d.ci_sz;
   document.getElementById('ct-scale').value=d.ct_s;
   document.getElementById('ci-size').value=d.ci_sz;}
  if(d.timer_mode!=null)updateTimerUI({timer_mode:d.timer_mode,timer_running:d.timer_running,elapsed:d.timer_elapsed,remaining:d.timer_remaining});
  if(d.rss_url)document.getElementById('rss-url').value=d.rss_url;
  if(d.proxy_url)document.getElementById('proxy-url').value=d.proxy_url;
  render();
 }).catch(()=>{});
 setTimeout(poll,3000);
}
poll();
</script></body></html>)rawhtml");
    });

    // ---------------------------------------------------------------
    // GET /api/status
    // ---------------------------------------------------------------
    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
        _requestCount++;
        JsonDocument doc;
        doc["ble_state"] = _ble->stateStr();
        doc["mode"] = _modes->modeStr();
        doc["commands_sent"] = _ble->commandCount();
        doc["http_requests"] = _requestCount;
        doc["brightness"] = _brightness;
        doc["uptime_sec"] = millis() / 1000;
        doc["free_heap"] = ESP.getFreeHeap();
        doc["wifi_rssi"] = WiFi.RSSI();
        doc["clock_layout"] = clockMode.getClockLayout();
        doc["temp"] = clockMode.currentTemp();
        doc["weather_icon"] = clockMode.currentIcon();
        doc["ct_x"] = clockMode.customTimeX();
        doc["ct_y"] = clockMode.customTimeY();
        doc["ct_s"] = clockMode.customTimeScale();
        doc["cp_x"] = clockMode.customTempX();
        doc["cp_y"] = clockMode.customTempY();
        doc["ci_x"] = clockMode.customIconX();
        doc["ci_y"] = clockMode.customIconY();
        doc["ci_sz"] = clockMode.customIconSize();
        doc["font_id"] = clockMode.getFontId();
        doc["font_aa"] = clockMode.getFontAA();
        char tcHex[8];
        snprintf(tcHex, sizeof(tcHex), "#%02x%02x%02x",
            clockMode.timeColorR(), clockMode.timeColorG(), clockMode.timeColorB());
        doc["time_color"] = tcHex;
        doc["timer_mode"] = (int)clockMode.timerMode();
        doc["timer_running"] = clockMode.timerRunning();
        doc["timer_elapsed"] = clockMode.timerElapsedSec();
        doc["timer_remaining"] = clockMode.timerRemainingSec();
        doc["rss_url"] = clockMode.getRssUrl();
        doc["proxy_url"] = clockMode.getProxyUrl();
        doc["notif_count"] = clockMode.notifCount();
        doc["calendar_url"] = clockMode.getCalendarUrl();
        doc["calendar_events"] = clockMode.calendarEventCount();
        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // ---------------------------------------------------------------
    // GET /api/mode?mode=X — mode switch via query params (no body parsing needed)
    // ---------------------------------------------------------------
    _server.on("/api/mode", HTTP_GET, [this](AsyncWebServerRequest* req) {
        _requestCount++;
        String mode = req->arg("mode");
        Serial.printf("[HTTP] Mode request: %s\n", mode.c_str());

        if (mode == "clock") {
            _modes->setMode(ModeManager::CLOCK_WEATHER);
            _ble->send(Commands::powerOn());
            clockMode.forceUpdate();
        } else if (mode == "off") {
            _modes->setMode(ModeManager::OFF);
            _ble->send(Commands::powerOff());
        } else if (mode == "text") {
            _modes->setMode(ModeManager::TEXT);
            String text = req->arg("text");
            String color = req->arg("color");
            String scroll = req->arg("scroll");
            int speed = req->arg("speed").toInt();
            if (color.isEmpty()) color = "#3282ff";
            if (scroll.isEmpty()) scroll = "left";
            if (speed == 0) speed = 10;

            uint8_t r = strtol(color.substring(1, 3).c_str(), nullptr, 16);
            uint8_t g = strtol(color.substring(3, 5).c_str(), nullptr, 16);
            uint8_t b = strtol(color.substring(5, 7).c_str(), nullptr, 16);

            int duration = req->arg("duration").toInt();
            if (duration == 0) duration = max(8, (int)(text.length() * 0.4f) + 5);
            TextProgramConfig cfg;
            cfg.scroll = scroll;
            cfg.speed = speed;
            cfg.fontSize = 16;
            cfg.duration = duration;
            TextSegment seg;
            seg.text = text + "          ";
            seg.r = r; seg.g = g; seg.b = b;
            cfg.segments.push_back(seg);
            auto pkts = buildTextProgram("", cfg);
            sendPackets(_ble, pkts);
        } else if (mode == "message") {
            String text = req->arg("text");
            String color = req->arg("color");
            if (color.isEmpty()) color = "#ffffff";
            int duration = req->arg("duration").toInt();
            if (duration == 0) duration = 30;

            uint8_t r = strtol(color.substring(1, 3).c_str(), nullptr, 16);
            uint8_t g = strtol(color.substring(3, 5).c_str(), nullptr, 16);
            uint8_t b = strtol(color.substring(5, 7).c_str(), nullptr, 16);

            TextProgramConfig cfg;
            cfg.scroll = "static";
            cfg.speed = 10;
            cfg.fontSize = 16;
            cfg.duration = duration;
            TextSegment seg;
            seg.text = text;
            seg.r = r; seg.g = g; seg.b = b;
            cfg.segments.push_back(seg);
            auto pkts = buildTextProgram("", cfg);
            sendPackets(_ble, pkts);
            _modes->setModeWithTimeout(ModeManager::MESSAGE, duration + 2);
        } else if (mode == "headlines") {
            _modes->setMode(ModeManager::NEWS_TICKER);
            // TODO: RSS fetch not yet implemented on ESP32
        } else if (mode == "dadjoke") {
            _modes->setMode(ModeManager::TEXT);
            // TODO: joke API not yet implemented on ESP32
        }

        JsonDocument resp;
        resp["ok"] = true;
        resp["mode"] = _modes->modeStr();
        String out;
        serializeJson(resp, out);
        req->send(200, "application/json", out);
    });

    // ---------------------------------------------------------------
    // GET /api/brightness?level=X
    // ---------------------------------------------------------------
    _server.on("/api/brightness", HTTP_GET, [this](AsyncWebServerRequest* req) {
        _requestCount++;
        int level = req->arg("level").toInt();
        if (level < 0) level = 0;
        if (level > 15) level = 15;
        _brightness = level;
        auto pkt = Commands::setBrightness((uint8_t)level);
        _ble->send(pkt);
        Serial.printf("[HTTP] Brightness set to %d\n", level);
        req->send(200, "application/json", R"({"ok":true})");
    });

    // ---------------------------------------------------------------
    // POST /api/forecast — trigger forecast flash
    // ---------------------------------------------------------------
    _server.on("/api/forecast", HTTP_GET, [this](AsyncWebServerRequest* req) {
        _requestCount++;
        clockMode.showForecast();
        req->send(200, "application/json", R"({"ok":true,"action":"forecast"})");
    });

    // ---------------------------------------------------------------
    // GET /api/settings?clockScale=X — update display settings
    // ---------------------------------------------------------------
    _server.on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest* req) {
        _requestCount++;
        if (req->hasArg("layout")) {
            int layout = req->arg("layout").toInt();
            clockMode.setClockLayout(layout);
            Serial.printf("[HTTP] Clock layout set to %d\n", layout);
        }
        if (req->hasArg("font")) {
            int fontId = req->arg("font").toInt();
            bool aa = req->arg("aa").toInt() != 0;
            clockMode.setFont(fontId, aa);
            Serial.printf("[HTTP] Font set to %d aa=%d\n", fontId, aa);
        }
        if (req->hasArg("time_color")) {
            String c = req->arg("time_color");
            if (c.startsWith("#") && c.length() == 7) {
                uint8_t r = strtol(c.substring(1, 3).c_str(), nullptr, 16);
                uint8_t g = strtol(c.substring(3, 5).c_str(), nullptr, 16);
                uint8_t b = strtol(c.substring(5, 7).c_str(), nullptr, 16);
                clockMode.setTimeColor(r, g, b);
                Serial.printf("[HTTP] Time color set to %s\n", c.c_str());
            }
        }
        if (req->hasArg("ct_x")) {
            clockMode.setCustomPositions(
                req->arg("ct_x").toInt(), req->arg("ct_y").toInt(),
                req->arg("ct_s").toInt(),
                req->arg("cp_x").toInt(), req->arg("cp_y").toInt(),
                req->arg("ci_x").toInt(), req->arg("ci_y").toInt(),
                req->arg("ci_sz").toInt()
            );
            Serial.println("[HTTP] Custom layout positions updated");
        }
        JsonDocument resp;
        resp["ok"] = true;
        resp["layout"] = clockMode.getClockLayout();
        String out;
        serializeJson(resp, out);
        req->send(200, "application/json", out);
    });

    // ---------------------------------------------------------------
    // POST /power/on, /power/off — direct bridge-compatible endpoints
    // ---------------------------------------------------------------
    _server.on("/power/on", HTTP_POST, [this](AsyncWebServerRequest* req) {
        _requestCount++;
        auto pkt = Commands::powerOn();
        bool ok = _ble->send(pkt);
        req->send(200, "application/json", ok ? R"({"ok":true})" : R"({"ok":false})");
    });

    _server.on("/power/off", HTTP_POST, [this](AsyncWebServerRequest* req) {
        _requestCount++;
        auto pkt = Commands::powerOff();
        bool ok = _ble->send(pkt);
        req->send(200, "application/json", ok ? R"({"ok":true})" : R"({"ok":false})");
    });

    // POST /brightness/<level> — bridge-compatible
    _server.on("^\\/brightness\\/(\\d+)$", HTTP_POST, [this](AsyncWebServerRequest* req) {
        _requestCount++;
        int level = req->pathArg(0).toInt();
        if (level < 0) level = 0;
        if (level > 15) level = 15;
        _brightness = level;
        auto pkt = Commands::setBrightness((uint8_t)level);
        bool ok = _ble->send(pkt);
        req->send(200, "application/json", ok ? R"({"ok":true})" : R"({"ok":false})");
    });

    // POST /delete
    _server.on("/delete", HTTP_POST, [this](AsyncWebServerRequest* req) {
        _requestCount++;
        auto pkt = Commands::deleteAll();
        bool ok = _ble->send(pkt);
        req->send(200, "application/json", ok ? R"({"ok":true})" : R"({"ok":false})");
    });

    // POST /raw/<hex>
    _server.on("^\\/raw\\/([0-9a-fA-F]+)$", HTTP_POST, [this](AsyncWebServerRequest* req) {
        _requestCount++;
        String hex = req->pathArg(0);
        std::vector<uint8_t> data;
        data.reserve(hex.length() / 2);
        for (size_t i = 0; i + 1 < hex.length(); i += 2) {
            char buf[3] = {hex[i], hex[i+1], 0};
            data.push_back((uint8_t)strtol(buf, nullptr, 16));
        }
        bool ok = _ble->send(data);
        JsonDocument doc;
        doc["ok"] = ok;
        doc["action"] = "raw";
        doc["bytes"] = data.size();
        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // POST /reconnect
    _server.on("/reconnect", HTTP_POST, [this](AsyncWebServerRequest* req) {
        _requestCount++;
        signBLE.begin();
        req->send(200, "application/json", R"({"ok":true,"action":"reconnect"})");
    });

    // GET /scroll?text=X&direction=left&speed=10 — legacy bridge-compatible
    _server.on("/scroll", HTTP_GET, [this](AsyncWebServerRequest* req) {
        _requestCount++;
        TextProgramConfig cfg;
        String text = req->arg("text");
        cfg.scroll = req->arg("direction");
        if (cfg.scroll.isEmpty()) cfg.scroll = "left";
        cfg.speed = req->arg("speed").toInt();
        if (cfg.speed == 0) cfg.speed = 10;
        cfg.fontSize = 16;
        TextSegment seg;
        seg.text = text;
        seg.r = 255; seg.g = 255; seg.b = 255;
        cfg.segments.push_back(seg);
        _modes->setMode(ModeManager::OFF);
        auto pkts = buildTextProgram("", cfg);
        sendPackets(_ble, pkts);
        req->send(200, "application/json", R"({"ok":true})");
    });

    // ---------------------------------------------------------------
    // Legacy mode shortcuts (bridge-compatible)
    // ---------------------------------------------------------------
    _server.on("/mode/clock", HTTP_POST, [this](AsyncWebServerRequest* req) {
        _requestCount++;
        _modes->setMode(ModeManager::CLOCK_WEATHER);
        req->send(200, "application/json", R"({"ok":true,"mode":"clock"})");
    });
    _server.on("/mode/off", HTTP_POST, [this](AsyncWebServerRequest* req) {
        _requestCount++;
        _modes->setMode(ModeManager::OFF);
        req->send(200, "application/json", R"({"ok":true,"mode":"off"})");
    });

    // GET /health
    _server.on("/health", HTTP_GET, [this](AsyncWebServerRequest* req) {
        JsonDocument doc;
        doc["uptime_sec"] = millis() / 1000;
        doc["free_heap"] = ESP.getFreeHeap();
        doc["ble_state"] = _ble->stateStr();
        doc["ble_commands"] = _ble->commandCount();
        doc["wifi_rssi"] = WiFi.RSSI();
        doc["http_requests"] = _requestCount;
        doc["mode"] = _modes->modeStr();
        String out;
        serializeJsonPretty(doc, out);
        req->send(200, "application/json", out);
    });

    // GET /status — bridge-compatible
    _server.on("/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
        _requestCount++;
        JsonDocument doc;
        doc["state"] = _ble->stateStr();
        doc["commands_sent"] = _ble->commandCount();
        doc["http_requests"] = _requestCount;
        doc["info"] = _ble->deviceInfo();
        doc["mode"] = _modes->modeStr();
        doc["uptime_sec"] = millis() / 1000;
        doc["free_heap"] = ESP.getFreeHeap();
        String out;
        serializeJsonPretty(doc, out);
        req->send(200, "application/json", out);
    });

    // ---------------------------------------------------------------
    // GET /api/timer?action=X — Timer / Stopwatch control
    // ---------------------------------------------------------------
    _server.on("/api/timer", HTTP_GET, [this](AsyncWebServerRequest* req) {
        _requestCount++;
        String action = req->arg("action");

        if (action == "countdown") {
            int sec = req->arg("seconds").toInt();
            if (sec <= 0) sec = 300;
            clockMode.startCountdown(sec);
        } else if (action == "stopwatch") {
            clockMode.startStopwatch();
        } else if (action == "pause") {
            clockMode.pauseTimer();
        } else if (action == "resume") {
            clockMode.resumeTimer();
        } else if (action == "reset") {
            clockMode.resetTimer();
        }

        JsonDocument resp;
        resp["ok"] = true;
        resp["timer_mode"] = (int)clockMode.timerMode();
        resp["timer_running"] = clockMode.timerRunning();
        resp["elapsed"] = clockMode.timerElapsedSec();
        resp["remaining"] = clockMode.timerRemainingSec();
        String out;
        serializeJson(resp, out);
        req->send(200, "application/json", out);
    });

    // ---------------------------------------------------------------
    // GET /api/news — News settings
    // ---------------------------------------------------------------
    _server.on("/api/news", HTTP_GET, [this](AsyncWebServerRequest* req) {
        _requestCount++;
        if (req->hasArg("rss_url")) {
            clockMode.setRssUrl(req->arg("rss_url"));
        }
        if (req->hasArg("fetch")) {
            clockMode.triggerNewsFetch();
        }

        JsonDocument resp;
        resp["ok"] = true;
        resp["rss_url"] = clockMode.getRssUrl();
        resp["headline_count"] = clockMode.notifCount();
        String out;
        serializeJson(resp, out);
        req->send(200, "application/json", out);
    });

    // ---------------------------------------------------------------
    // GET /api/calendar — Calendar settings
    // ---------------------------------------------------------------
    _server.on("/api/calendar", HTTP_GET, [this](AsyncWebServerRequest* req) {
        _requestCount++;
        if (req->hasArg("url")) {
            clockMode.setCalendarUrl(req->arg("url"));
        }

        JsonDocument resp;
        resp["ok"] = true;
        resp["calendar_url"] = clockMode.getCalendarUrl();
        resp["event_count"] = clockMode.calendarEventCount();
        JsonArray events = resp["events"].to<JsonArray>();
        for (int i = 0; i < clockMode.calendarEventCount(); i++)
            events.add(clockMode.calendarEvent(i));
        String out;
        serializeJson(resp, out);
        req->send(200, "application/json", out);
    });

    // ---------------------------------------------------------------
    // GET /api/notify — Notification proxy settings
    // ---------------------------------------------------------------
    _server.on("/api/notify", HTTP_GET, [this](AsyncWebServerRequest* req) {
        _requestCount++;
        if (req->hasArg("proxy_url")) {
            clockMode.setProxyUrl(req->arg("proxy_url"));
        }
        if (req->hasArg("message")) {
            clockMode.showNotification(req->arg("message"));
        }

        JsonDocument resp;
        resp["ok"] = true;
        resp["proxy_url"] = clockMode.getProxyUrl();
        resp["notif_count"] = clockMode.notifCount();
        JsonArray items = resp["notifications"].to<JsonArray>();
        for (int i = 0; i < clockMode.notifCount(); i++)
            items.add(clockMode.notifAt(i));
        String out;
        serializeJson(resp, out);
        req->send(200, "application/json", out);
    });

}

