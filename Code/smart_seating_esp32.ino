// RUN THE CODE IN ARDIUNO-IDE

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <SPIFFS.h>
#include <time.h>

#define WIFI_SSID "OPPO K12x 5G"
#define WIFI_PASS "david1234"

// Sensor pins 
const int SEAT_PIN[4] = {15, 5, 18, 19}; 

// Web objects
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// Seat state tracking
bool seatState[4] = {false, false, false, false}; // true = occupied
bool lastSeatState[4] = {false, false, false, false};
const char *historyFile = "/history.txt";

// NTP- Network Time Protocol,
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 5 * 3600 + 30 * 60; // IST offset +5:30
const int   daylightOffset_sec = 0;
unsigned long lastBroadcast = 0;
const unsigned long BROADCAST_INTERVAL = 500; // ms

// Forward
String getISOTime();
void appendHistory(int seatIndex, bool occupied);
void broadcastAllState();
String seatStateJson();

//summary helper-Total number ,etc.
String summaryJson() {
  int occ = 0;
  for (int i = 0; i < 4; i++) if (seatState[i]) occ++;
  int freeSeats = 4 - occ;
  String s = "{";
  s += "\"type\":\"summary\",";
  s += "\"total\":4,";
  s += "\"free\":" + String(freeSeats) + ",";
  s += "\"occupied\":" + String(occ);
  s += "}";
  return s;
}


// HTML / JS UI as one big raw string (updated JS to use request_history + summary)
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>Neon Seat Occupancy — 4 Seats</title>
  <style>
    :root{
      --bg: linear-gradient(180deg,#0f1724 0%, #071022 100%);
      --card-bg: rgba(255,255,255,0.03);
      --muted: rgba(255,255,255,0.6);
      --accent: #7dd3fc;
      --accent-2: #c7b8ff;
      --occupied: #ff6b6b;
      --free: #7efc82;
      font-family: "Montserrat", Arial, sans-serif;
    }
    html,body{height:100%;margin:0;background:var(--bg);color:#e6eef8}
    .wrap{max-width:980px;margin:32px auto;padding:20px}
    h1{font-size:22px;margin:0 0 8px 0;letter-spacing:0.6px}
    p.sub{color:var(--muted);margin:6px 0 22px 0}
    .grid{display:grid;grid-template-columns:repeat(4,1fr);gap:18px;margin-bottom:18px}
    .card{
      background: linear-gradient(135deg, rgba(255,255,255,0.03), rgba(255,255,255,0.01));
      border-radius:16px;padding:16px;backdrop-filter: blur(6px);
      box-shadow: 0 6px 18px rgba(0,0,0,0.6), inset 0 1px 0 rgba(255,255,255,0.02);
      position:relative;overflow:hidden;
    }
    .seat-id{font-size:12px;color:var(--muted);margin-bottom:8px}
    .seat-status{font-size:18px;font-weight:700;margin-bottom:10px}
    .dot{width:18px;height:18px;border-radius:50%;display:inline-block;vertical-align:middle;margin-right:8px;box-shadow:0 3px 12px rgba(0,0,0,0.6)}
    .occupied{background: linear-gradient(135deg,var(--occupied), #ff9b9b); box-shadow: 0 6px 20px rgba(255,107,107,.18); }
    .free{background: linear-gradient(135deg,var(--free), #b3ffcc);box-shadow: 0 6px 20px rgba(126,252,130,.12);}
    .small{font-size:12px;color:var(--muted)}
    .controls{display:flex;gap:8px;align-items:center;margin-bottom:12px}
    .btn{background:transparent;border:1px solid rgba(255,255,255,0.06);padding:8px 12px;border-radius:10px;color:var(--muted);cursor:pointer}
    .btn:hover{transform:translateY(-2px)}
    .history{background:rgba(255,255,255,0.02);border-radius:12px;padding:12px;max-height:320px;overflow:auto}
    .history-entry{padding:8px;border-bottom:1px dashed rgba(255,255,255,0.02);font-size:13px;color:var(--muted)}
    .topbar{display:flex;justify-content:space-between;align-items:center;margin-bottom:12px}
    .logo{display:flex;gap:10px;align-items:center}
    .logo .mark{width:44px;height:44px;border-radius:12px;background:conic-gradient(from 120deg,#7dd3fc,#c7b8ff,#ffd6e0);box-shadow:0 6px 18px rgba(0,0,0,0.6)}
    .statusLine{font-size:13px;color:var(--muted)}
    @media (max-width:720px){.grid{grid-template-columns:repeat(2,1fr)}}
    #summaryText { font-size:14px; color: var(--muted); margin-top:6px; text-align:right; }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="topbar">
      <div class="logo">
       
        <div>
          <h1>Hall Seat Occupancy System — 4 Seats</h1>
          <div class="small">Real-time updates </div>
        </div>
      </div>
      <div style="text-align:right">
        <div id="summaryText">Total: 4 | Free: — | Occupied: —</div>
        <div class="statusLine" id="connStatus">Connecting...</div>
      </div>
    </div>

    <div class="controls">
      <button class="btn" onclick="refreshHistory()">Refresh history</button>
      <button class="btn" onclick="clearHistory()">Clear history</button>
      <div style="flex:1"></div>
      <div class="small" id="lastSync">Time: —</div>
    </div>

    <div class="grid" id="seatGrid">
      <!-- seat cards injected by JS -->
    </div>

    <h3 style="margin-top:18px">History</h3>
    <div class="history" id="historyBox">
      Loading history...
    </div>
  </div>

<script>
const seats = [
  {id:1, name:"Seat 1"},
  {id:2, name:"Seat 2"},
  {id:3, name:"Seat 3"},
  {id:4, name:"Seat 4"}
];

const seatGrid = document.getElementById('seatGrid');
const connStatus = document.getElementById('connStatus');
const historyBox = document.getElementById('historyBox');
const lastSync = document.getElementById('lastSync');
const summaryText = document.getElementById('summaryText');

function createCard(s){
  const div = document.createElement('div');
  div.className = 'card';
  div.id = 'card-'+s.id;
  div.innerHTML = `<div class="seat-id">${s.name}</div>
    <div class="seat-status"><span class="dot free" id="dot-${s.id}"></span><span id="label-${s.id}">FREE</span></div>
    <div class="small" id="time-${s.id}">—</div>`;
  return div;
}

function initGrid(){
  seatGrid.innerHTML = '';
  seats.forEach(s => seatGrid.appendChild(createCard(s)));
}

initGrid();

let socket;
function startWS(){
  const host = location.hostname;
  const port = 81;
  const url = 'ws://' + host + ':' + port;
  socket = new WebSocket(url);

  socket.onopen = () => {
    connStatus.textContent = 'Connected';
    connStatus.style.color = '#7efc82';
    // request initial state & history & summary
    socket.send(JSON.stringify({type:'request_state'}));
    socket.send(JSON.stringify({type:'request_history'}));
  };
  socket.onerror = (err) => {
    connStatus.textContent = 'Socket error';
    connStatus.style.color = '#ffb86b';
    console.error('WebSocket error', err);
  };
  socket.onclose = () => {
    connStatus.textContent = 'Disconnected — retrying...';
    connStatus.style.color = '#ff6b6b';
    setTimeout(startWS, 2000);
  };
  socket.onmessage = (msg) => {
    try {
      const payload = JSON.parse(msg.data);
      handleMessage(payload);
    } catch(e) {
      console.error('Invalid JSON', e, msg.data);
    }
  };
}

function handleMessage(p){
  if(!p || !p.type) return;
  if(p.type === 'state'){
    // {type:'state', seats:[0/1,...], timestamp: '...'}
    p.seats.forEach((v, idx) => {
      updateSeat(idx+1, v === 1, p.timestamp);
    });
    if(p.timestamp) lastSync.textContent = 'Last update: ' + p.timestamp;
  } else if(p.type === 'history'){
    loadHistoryIntoUI(p.items || []);
  } else if(p.type === 'summary'){
    summaryText.textContent = `Total: ${p.total} | Free: ${p.free} | Occupied: ${p.occupied}`;
  } else if(p.type === 'ack'){
    // ignore
  }
}

function updateSeat(id, occupied, ts){
  const dot = document.getElementById('dot-'+id);
  const label = document.getElementById('label-'+id);
  const tdiv = document.getElementById('time-'+id);
  if(occupied){
    dot.classList.remove('free'); dot.classList.add('occupied');
    label.textContent = 'OCCUPIED';
  } else {
    dot.classList.remove('occupied'); dot.classList.add('free');
    label.textContent = 'FREE';
  }
  if(ts) tdiv.textContent = ts;
}

function refreshHistory(){
  // prefer websocket request if alive
  if(socket && socket.readyState === WebSocket.OPEN){
    socket.send(JSON.stringify({type:'request_history'}));
    return;
  }
  // fallback to HTTP
  fetch('/history').then(r => r.text()).then(txt => {
    const lines = txt.split('\n').filter(Boolean);
    const items = lines.map(l => {
      try { return JSON.parse(l); } catch(e){ return null; }
    }).filter(Boolean);
    // history endpoint returns oldest->newest; display newest first
    loadHistoryIntoUI(items.reverse());
  }).catch(e => {
    historyBox.innerHTML = 'Failed to load history';
  });
}

function loadHistoryIntoUI(items){
  if(!items || items.length === 0){
    historyBox.innerHTML = '<div class="small">No history</div>';
    return;
  }
  historyBox.innerHTML = '';
  items.forEach(it => {
    if(!it) return;
    const el = document.createElement('div');
    el.className = 'history-entry';
    el.innerHTML = `<strong>${it.time}</strong> — Seat ${it.seat} — ${it.occupied ? 'OCCUPIED' : 'FREE'}`;
    historyBox.appendChild(el);
  });
}

function clearHistory(){
  if(!confirm('Clear history on device?')) return;
  fetch('/clear-history').then(r => r.text()).then(txt => {
    refreshHistory();
    alert('History cleared');
  }).catch(e => alert('Failed to clear'));
}

window.addEventListener('load', () => {
  startWS();
  refreshHistory();
});
</script>
</body>
</html>
)rawliteral";

/////////////////////
// Helper / Server handlers
/////////////////////

void handleRoot() {
  server.sendHeader("Content-Type", "text/html; charset=utf-8");
  server.send(200, "text/html", INDEX_HTML);
}

void handleHistory() {
  if (!SPIFFS.exists(historyFile)) {
    server.send(200, "text/plain", "");
    return;
  }
  File f = SPIFFS.open(historyFile, FILE_READ);
  if(!f){
    server.send(500, "text/plain", "failed");
    return;
  }
  String out;
  while(f.available()){
    out += (char)f.read();
  }
  f.close();
  server.send(200, "text/plain", out);
}

void handleClearHistory() {
  if (SPIFFS.exists(historyFile)) {
    SPIFFS.remove(historyFile);
  }
  server.send(200, "text/plain", "ok");
}

/////////////////////
// WebSocket callbacks
/////////////////////
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_TEXT: {
      String msg = String((char*)payload);
      // simple detection for request types (client sends JSON like {"type":"request_state"})
      if(msg.indexOf("request_state") >= 0) {
        String json = seatStateJson();
        webSocket.sendTXT(num, json);
        // also send summary
        String sum = summaryJson();
        webSocket.sendTXT(num, sum);
      } else if(msg.indexOf("request_history") >= 0) {
        // build history JSON array from file
        String out;
        if (SPIFFS.exists(historyFile)) {
          File f = SPIFFS.open(historyFile, FILE_READ);
          if(f){
            while(f.available()) out += (char)f.read();
            f.close();
          }
        }
        String json = "{\"type\":\"history\",\"items\":[";
        int start = 0;
        int idx = 0;
        while(true){
          int pos = out.indexOf('\n', start);
          if(pos < 0) break;
          String line = out.substring(start, pos);
          if(line.length() > 5) {
            if(idx > 0) json += ",";
            json += line;
            idx++;
          }
          start = pos + 1;
        }
        json += "]}";
        webSocket.sendTXT(num, json);
      }
      break;
    }
    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(num);
      Serial.printf("[WS] Client %u connected from %s\n", num, ip.toString().c_str());
      // send immediate state and history
      String json = seatStateJson();
      webSocket.sendTXT(num, json);

      // send summary as well
      String sum = summaryJson();
      webSocket.sendTXT(num, sum);

      // send history too (small delay to ensure client ready)
      delay(20);
      String out;
      if (SPIFFS.exists(historyFile)) {
        File f = SPIFFS.open(historyFile, FILE_READ);
        if(f){
          while(f.available()) out += (char)f.read();
          f.close();
        }
      }
      String j2 = "{\"type\":\"history\",\"items\":[";
      int start = 0;
      int idx = 0;
      while(true){
        int pos = out.indexOf('\n', start);
        if(pos < 0) break;
        String line = out.substring(start, pos);
        if(line.length() > 5) {
          if(idx > 0) j2 += ",";
          j2 += line;
          idx++;
        }
        start = pos + 1;
      }
      j2 += "]}";
      webSocket.sendTXT(num, j2);

      break;
    }
    case WStype_DISCONNECTED:
      Serial.printf("[WS] Client %u disconnected\n", num);
      break;
    default: break;
  }
}

/////////////////
// Utilities
/////////////////
String seatStateJson() {
  String s = "{";
  s += "\"type\":\"state\",";
  s += "\"seats\":[";
  for(int i=0;i<4;i++){
    s += seatState[i] ? "1" : "0";
    if(i<3) s+= ",";
  }
  s += "],";
  s += "\"timestamp\":\"" + getISOTime() + "\"";
  s += "}";
  return s;
}

String getISOTime() {
  time_t now;
  time(&now);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  char buf[64];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buf);
}

void appendHistory(int seatIndex, bool occupied) {
  File f = SPIFFS.open(historyFile, FILE_APPEND);
  if(!f) {
    Serial.println("Failed to open history for append");
    return;
  }

  String line = "{\"time\":\"";
  line += getISOTime();
  line += "\",\"seat\":";
  line += (seatIndex + 1);
  line += ",\"occupied\":";
  line += occupied ? "true" : "false";
  line += "}";

  f.println(line);   // writes newline-terminated JSON object
  f.flush();         // ensure data written to SPIFFS
  f.close();
}

void broadcastAllState() {
  String json = seatStateJson();
  webSocket.broadcastTXT(json);

  // also broadcast summary
  String sum = summaryJson();
  webSocket.broadcastTXT(sum);
}

/////////////////////
// Setup
/////////////////////
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("SeatServer starting ===");

  // init SPIFFS
  if(!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed!");
  } else {
    Serial.println("SPIFFS mounted.");
  }

  // wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Connecting to WiFi '%s' ...\n", WIFI_SSID);
  int tries=0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Failed to connect to WiFi (continue offline).");
  }

  // NTP time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Syncing time with NTP...");
  time_t now = time(nullptr);
  int waitN=0;
  while (now < 8*3600*2 && waitN < 30) {
    delay(500);
    now = time(nullptr);
    waitN++;
  }
  Serial.println("Time: " + getISOTime());

  // pins
  for(int i=0;i<4;i++){
    pinMode(SEAT_PIN[i], INPUT);
    seatState[i] = digitalRead(SEAT_PIN[i]) == LOW; // assume LOW = occupied
    lastSeatState[i] = seatState[i];
  }

  // http handlers
  server.on("/", HTTP_GET, handleRoot);
  server.on("/history", HTTP_GET, handleHistory);
  server.on("/clear-history", HTTP_GET, handleClearHistory);
  server.begin();
  Serial.println("HTTP server started on port 80");

  // websocket
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocket server started on port 81");
}

/////////////////////
// Main loop
/////////////////////
void loop() {
  server.handleClient();
  webSocket.loop();

  // poll sensors
  bool changed = false;
  for(int i=0;i<4;i++){
    bool v = digitalRead(SEAT_PIN[i]) == LOW; // adjust logic if your sensor inverts
    seatState[i] = v;
    if(v != lastSeatState[i]){
      // state changed
      Serial.printf("Seat %d changed -> %s\n", i+1, v ? "OCCUPIED" : "FREE");
      appendHistory(i, v);
      lastSeatState[i] = v;
      changed = true;
    }
  }

  unsigned long now = millis();
  if(changed || now - lastBroadcast > BROADCAST_INTERVAL) {
    broadcastAllState();
    lastBroadcast = now;
  }

  delay(80);
}
