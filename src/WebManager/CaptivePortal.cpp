#include "CaptivePortal.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include "../Display/Layout.h"
#include "../File System/FileSystem.h"

// Estado compartilhado entre a task do AsyncTCP (handlers) e o loop do
// modal. As credenciais pendentes sao escritas antes de connectRequested,
// lidas uma vez pelo loop — pragmático no mesmo padrao dos eventos do
// satisfaction-hub.
enum PortalState { PORTAL_IDLE, PORTAL_CONNECTING, PORTAL_CONNECTED, PORTAL_FAILED };

static DNSServer dnsServer;
static AsyncWebServer portalServer(80);

static volatile bool connectRequested = false;
static volatile int connectState = PORTAL_IDLE;
static String pendingSsid = "";
static String pendingPass = "";
static String connectBody = "";
static unsigned long connectStart = 0;

static const char PORTAL_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>KryonOS WiFi Setup</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;
background:#0d1117;color:#f1f5f9;min-height:100vh;padding:20px}
.container{max-width:400px;margin:0 auto;background:#161d27;border:1px solid #2a3441;
border-radius:16px;overflow:hidden}
.header{background:#161d27;padding:24px;text-align:center;border-bottom:1px solid #2a3441}
.header h1{font-size:22px;color:#22d3ee}
.header p{opacity:.7;font-size:13px;margin-top:6px}
.content{padding:20px}
.network-list{max-height:220px;overflow-y:auto;border:1px solid #2a3441;border-radius:8px;
margin-bottom:14px}
.network{padding:12px 14px;cursor:pointer;border-bottom:1px solid #232b36;display:flex;
justify-content:space-between;align-items:center}
.network:hover{background:#1c2530}
.network.selected{background:#12333b}
.network-name{font-weight:500}
.network-signal{font-size:12px;color:#8b98a9}
.form-group{margin-bottom:14px}
label{display:block;margin-bottom:6px;font-weight:500;font-size:14px;color:#8b98a9}
input[type=text],input[type=password]{width:100%;padding:12px 14px;border:2px solid #2a3441;
border-radius:8px;font-size:16px;background:#0d1117;color:#f1f5f9}
input:focus{outline:none;border-color:#22d3ee}
.btn{width:100%;padding:14px;background:#0e7490;color:#fff;border:none;border-radius:8px;
font-size:16px;font-weight:600;cursor:pointer}
.btn:hover{background:#22d3ee;color:#0d1117}
.btn-scan{background:#1c2530;color:#f1f5f9;margin-bottom:14px}
.btn-scan:hover{background:#2a3441;color:#fff}
.status{margin-top:14px;padding:12px;border-radius:8px;text-align:center;font-size:14px;display:none}
.status.error{background:#3b1d20;color:#ef4444;display:block}
.status.success{background:#12331f;color:#22c55e;display:block}
.status.info{background:#12333b;color:#22d3ee;display:block}
</style>
</head>
<body>
<div class="container">
<div class="header"><h1>KryonOS WiFi Setup</h1>
<p>Choose a network and connect</p></div>
<div class="content">
<button class="btn btn-scan" onclick="scan()">Scan Networks</button>
<div class="network-list" id="list">
<div style="padding:18px;text-align:center;color:#8b98a9">Tap "Scan Networks" to list networks</div>
</div>
<form onsubmit="return connect(event)">
<div class="form-group"><label>Network name (SSID)</label>
<input type="text" id="ssid" required placeholder="Select or type"></div>
<div class="form-group"><label>Password</label>
<input type="password" id="password" placeholder="Network password"></div>
<button type="submit" class="btn" id="cbtn">Connect</button>
</form>
<div id="status" class="status"></div>
</div></div>
<script>
let pollTimer=null;
function esc(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;')}
function sig(r){return r>-60?'||||':r>-72?'|||':r>-82?'||':'|'}
function scan(){
document.getElementById('list').innerHTML='<div style="padding:18px;text-align:center;color:#8b98a9">Scanning...</div>';
fetch('/scan').then(r=>r.json()).then(d=>{
const l=document.getElementById('list');
if(!d.networks.length){l.innerHTML='<div style="padding:18px;text-align:center;color:#8b98a9">No networks found</div>';return}
l.innerHTML=d.networks.map(n=>
'<div class="network" onclick="pick(this,\''+n.ssid.replace(/'/g,"\\'")+'\')">'+
'<span class="network-name">'+esc(n.ssid)+'</span>'+
'<span class="network-signal">'+sig(n.rssi)+' '+n.rssi+' dBm</span></div>').join('');
}).catch(()=>{document.getElementById('list').innerHTML=
'<div style="padding:18px;text-align:center;color:#ef4444">Scan failed</div>'});
}
function pick(el,ssid){
document.querySelectorAll('.network').forEach(n=>n.classList.remove('selected'));
el.classList.add('selected');
document.getElementById('ssid').value=ssid;
}
function st(msg,cls){const s=document.getElementById('status');s.textContent=msg;s.className='status '+cls}
function connect(e){
e.preventDefault();
const ssid=document.getElementById('ssid').value;
const password=document.getElementById('password').value;
if(!ssid){st('Select or type a network name','error');return false}
st('Connecting...','info');
document.getElementById('cbtn').disabled=true;
fetch('/connect',{method:'POST',
headers:{'Content-Type':'application/x-www-form-urlencoded'},
body:'ssid='+encodeURIComponent(ssid)+'&password='+encodeURIComponent(password)
}).catch(()=>{});
if(pollTimer)clearInterval(pollTimer);
pollTimer=setInterval(()=>{
fetch('/status').then(r=>r.json()).then(d=>{
if(d.state=='connected'){clearInterval(pollTimer);pollTimer=null;
st('Connected! IP: '+d.ip,'success');document.getElementById('cbtn').disabled=false}
else if(d.state=='failed'){clearInterval(pollTimer);pollTimer=null;
st('Connection failed — check the password and try again','error');
document.getElementById('cbtn').disabled=false}
}).catch(()=>{});
},1000);
return false;
}
scan();
</script>
</body>
</html>)html";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static String urlDecode(const String& str) {
    String out;
    out.reserve(str.length());
    for (size_t i = 0; i < str.length(); i++) {
        if (str[i] == '%' && i + 2 < str.length()) {
            int v = strtol(str.substring(i + 1, i + 3).c_str(), nullptr, 16);
            if (v > 0) { out += (char)v; i += 2; continue; }
        }
        out += (str[i] == '+') ? ' ' : str[i];
    }
    return out;
}

static String jsonEscape(const String& s) {
    String out;
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c >= 32 && c < 127) out += c;
    }
    return out;
}

static void registerRoutes() {
    portalServer.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/html", PORTAL_HTML);
    });

    portalServer.on("/scan", HTTP_GET, [](AsyncWebServerRequest* req) {
        int n = WiFi.scanNetworks();  // sincrono (~2s); ok para o portal single-user
        String json = "{\"networks\":[";
        bool first = true;
        for (int i = 0; i < n; i++) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue;
            String needle = "\"" + jsonEscape(ssid) + "\"";
            if (json.indexOf(needle) != -1) continue;  // dedup (scan ja vem por RSSI)
            if (!first) json += ",";
            first = false;
            json += "{\"ssid\":\"" + jsonEscape(ssid) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
        }
        json += "]}";
        WiFi.scanDelete();
        req->send(200, "application/json", json);
    });

    portalServer.on("/connect", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            int ssidIdx = connectBody.indexOf("ssid=");
            int passIdx = connectBody.indexOf("password=");
            if (ssidIdx < 0) {
                connectBody = "";
                req->send(400, "application/json", "{\"success\":false}");
                return;
            }
            String raw = connectBody;
            connectBody = "";

            String ssid = raw.substring(ssidIdx + 5);
            int amp = ssid.indexOf('&');
            ssid = (amp >= 0) ? ssid.substring(0, amp) : ssid;
            ssid = urlDecode(ssid);
            ssid.trim();

            String pass = "";
            if (passIdx >= 0) {
                pass = raw.substring(passIdx + 9);
                amp = pass.indexOf('&');
                pass = (amp >= 0) ? pass.substring(0, amp) : pass;
                pass = urlDecode(pass);
            }

            if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 64) {
                req->send(400, "application/json", "{\"success\":false}");
                return;
            }
            pendingSsid = ssid;
            pendingPass = pass;
            connectState = PORTAL_IDLE;
            connectRequested = true;  // o loop do modal faz o WiFi.begin
            req->send(200, "application/json", "{\"success\":true}");
        },
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0) connectBody = "";
            if (connectBody.length() + len < 600) connectBody.concat((const char*)data, len);
        });

    portalServer.on("/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        String json = "{\"state\":\"";
        switch (connectState) {
            case PORTAL_CONNECTING: json += "connecting"; break;
            case PORTAL_CONNECTED:  json += "connected"; break;
            case PORTAL_FAILED:     json += "failed"; break;
            default:                json += "idle"; break;
        }
        json += "\",\"ip\":\"" + (connectState == PORTAL_CONNECTED ? WiFi.localIP().toString() : String("")) + "\"}";
        req->send(200, "application/json", json);
    });

    // Sondas de deteccao de captive portal (Android/Apple/Windows) e
    // qualquer outra rota: redireciona para a raiz
    portalServer.onNotFound([](AsyncWebServerRequest* req) {
        req->redirect("http://192.168.4.1/");
    });
}

// Linha de status na tela do display (apaga a regiao antes de escrever)
static void portalStatus(KryonDisplay* tft, const String& l1, const String& l2 = "") {
    tft->fillRect(UI::sx(6), UI::sy(210), UI::sx(228), UI::sy(56), TFT_BLACK);
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->setTextDatum(MC_DATUM);
    tft->drawString(l1, UI::sx(120), UI::sy(222), UI::font(2));
    if (l2.length()) tft->drawString(l2, UI::sx(120), UI::sy(244), UI::font(2));
}

static void portalCleanup(bool connected) {
    dnsServer.stop();
    portalServer.end();
    WiFi.scanDelete();
    connectBody = "";
    if (connected) WiFi.mode(WIFI_STA);   // derruba o AP, mantem a conexao
    else           WiFi.mode(WIFI_OFF);
}

// ---------------------------------------------------------------------------
// Modal bloqueante
// ---------------------------------------------------------------------------

bool CaptivePortal::runBlocking(KryonDisplay* tft) {
    // Sufixo do nome do AP: 4 ultimos hex do MAC
    uint8_t mac[6] = {0};
    WiFi.macAddress(mac);
    String apSsid = "KryonOS-Setup-" + String(mac[4], HEX) + String(mac[5], HEX);
    apSsid.toUpperCase();

    connectRequested = false;
    connectState = PORTAL_IDLE;

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(apSsid.c_str());  // rede aberta; IP padrao 192.168.4.1
    dnsServer.start(53, "*", WiFi.softAPIP());
    registerRoutes();
    portalServer.begin();

    // Tela de espera
    tft->fillScreen(TFT_BLACK);
    tft->drawRoundRect(UI::sx(3), UI::sy(3), UI::sx(234), UI::sy(314), UI::sx(5), TFT_WHITE);
    tft->fillRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_BLACK);
    tft->drawRoundRect(UI::sx(6), UI::sy(6), UI::sx(228), UI::sy(30), UI::sx(5), TFT_GREEN);
    tft->setTextColor(TFT_GREEN, TFT_BLACK);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("WiFi Setup Portal", UI::sx(120), UI::sy(21), UI::font(2));

    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->drawString("1. Connect to the AP:", UI::sx(120), UI::sy(70), UI::font(2));
    tft->setTextColor(TFT_CYAN, TFT_BLACK);
    tft->drawString(apSsid, UI::sx(120), UI::sy(92), UI::font(2));
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->drawString("2. Open a browser:", UI::sx(120), UI::sy(122), UI::font(2));
    tft->setTextColor(TFT_CYAN, TFT_BLACK);
    tft->drawString("http://192.168.4.1", UI::sx(120), UI::sy(144), UI::font(2));
    tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft->drawString("Waiting for setup...", UI::sx(120), UI::sy(180), UI::font(2));

    // Botao SKIP
    tft->fillRoundRect(UI::sx(70), UI::sy(280), UI::sx(100), UI::sy(30), UI::sx(5), TFT_DARKGREY);
    tft->setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft->drawString("SKIP", UI::sx(120), UI::sy(295), UI::font(2));

    while (true) {
        dnsServer.processNextRequest();

        if (connectRequested) {
            connectRequested = false;
            connectState = PORTAL_CONNECTING;
            portalStatus(tft, "Connecting to:", pendingSsid);
            WiFi.begin(pendingSsid.c_str(),
                       pendingPass.length() ? pendingPass.c_str() : nullptr);
            connectStart = millis();
        }

        if (connectState == PORTAL_CONNECTING) {
            if (WiFi.status() == WL_CONNECTED) {
                String creds = pendingSsid + "\n" + pendingPass;
                if (FileSystem::exists("/sd/")) {
                    FileSystem::writeTextFile("/sd/wifi.txt", creds.c_str());
                } else {
                    FileSystem::writeTextFile("/local/wifi.txt", creds.c_str());
                }
                connectState = PORTAL_CONNECTED;
                portalStatus(tft, "Connected!", WiFi.localIP().toString());
                delay(1500);
                portalCleanup(true);
                return true;
            }
            if (millis() - connectStart > 15000) {
                WiFi.disconnect();  // aborta so o lado STA; o AP segue de pe
                connectState = PORTAL_FAILED;
                portalStatus(tft, "Connection failed!", "Try again from the page");
            }
        }

        uint16_t tx = 0, ty = 0;
        if (tft->getTouch(&tx, &ty)) {
            if (ty >= UI::sy(270) && tx >= UI::sx(60) && tx <= UI::sx(180)) {
                while (tft->getTouch(&tx, &ty)) { delay(10); }
                portalCleanup(false);
                return false;
            }
            while (tft->getTouch(&tx, &ty)) { delay(10); }
        }

        delay(10);
    }
}
