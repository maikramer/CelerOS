#include "CaptivePortal.h"
#include "WebManager.h"

#include <string>
#include <cstring>
#include <cstdio>
#include <cstdint>

#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "WifiAP.h"

#include "../FileSystem/FileSystem.h"
#include "../Utils/StrUtils.h"
#include "../Display/Layout.h"

// Portal de setup WiFi sobre ESP-IDF puro:
//   - AP aberto "KryonOS-Setup-XXXX" (192.168.4.1) via componente WifiAP
//   - DNS wildcard (task propria, UDP :53 -> 192.168.4.1)
//   - httpd na porta 80 com /, /scan, /connect, /status e redirect nas
//     sondas de captive portal (generate_204, hotspot-detect.html, ...)
// A conexao STA acontece no loop do modal (fora dos handlers), igual ao
// porte Arduino; a pagina acompanha via GET /status.

enum PortalState { PORTAL_IDLE, PORTAL_CONNECTING, PORTAL_CONNECTED, PORTAL_FAILED };

// singleton: WifiAP::instance()
static httpd_handle_t s_portalServer = nullptr;
static volatile bool s_dnsRunning = false;

static volatile bool s_connectRequested = false;
static volatile int s_connectState = PORTAL_IDLE;
static std::string s_pendingSsid = "";
static std::string s_pendingPass = "";
static std::string s_connectBody = "";
static unsigned long s_connectStart = 0;

static const char PORTAL_HTML[] = R"html(<!DOCTYPE html>
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
// DNS wildcard (task): qualquer consulta A responde 192.168.4.1
// ---------------------------------------------------------------------------

static void dnsTask(void* /*arg*/) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock >= 0) {
        struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        struct sockaddr_in bindAddr = {};
        bindAddr.sin_family = AF_INET;
        bindAddr.sin_port = htons(53);
        bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(sock, (struct sockaddr*)&bindAddr, sizeof(bindAddr)) == 0) {
            uint8_t buf[512];
            while (s_dnsRunning) {
                struct sockaddr_in src = {};
                socklen_t srcLen = sizeof(src);
                int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&src, &srcLen);
                if (n <= 0 || n < 12) continue;

                // Resposta: mesmos ID/flags + AA setado, 1 answer
                buf[2] = 0x85;
                buf[3] = 0x80;
                buf[6] = 0; buf[7] = 1;    // ANCOUNT = 1
                buf[8] = 0; buf[9] = 0;    // NSCOUNT
                buf[10] = 0; buf[11] = 0;  // ARCOUNT

                int qd = 12;
                // pula QNAME
                while (qd < n && buf[qd] != 0) { qd += buf[qd] + 1; }
                qd += 5;  // null + QTYPE + QCLASS

                if (qd + 16 <= (int)sizeof(buf)) {
                    uint8_t* a = &buf[qd];
                    a[0] = 0xC0; a[1] = 0x0C;          // ponteiro p/ QNAME
                    a[2] = 0; a[3] = 1;                 // A
                    a[4] = 0; a[5] = 1;                 // IN
                    a[6] = 0; a[7] = 0; a[8] = 0; a[9] = 60;  // TTL
                    a[10] = 0; a[11] = 4;               // RDLENGTH
                    a[12] = 192; a[13] = 168; a[14] = 4; a[15] = 1;
                    sendto(sock, buf, qd + 16, 0, (struct sockaddr*)&src, srcLen);
                }
            }
        }
        close(sock);
    }
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string urlDecodeP(const std::string& str) {
    std::string out;
    out.reserve(str.length());
    for (size_t i = 0; i < str.length(); i++) {
        if (str[i] == '%' && i + 2 < str.length()) {
            int v = strtol(str.substr(i + 1, 2).c_str(), nullptr, 16);
            if (v > 0) { out += (char)v; i += 2; continue; }
        }
        out += (str[i] == '+') ? ' ' : str[i];
    }
    return out;
}

static std::string jsonEscapeP(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c >= 32 && c < 127) out += c;
    }
    return out;
}

static void portalSend(httpd_req_t* req, const char* type, const std::string& body) {
    httpd_resp_set_type(req, type);
    httpd_resp_send(req, body.c_str(), body.length());
}

static esp_err_t portal_index(httpd_req_t* req) {
    portalSend(req, "text/html", PORTAL_HTML);
    return ESP_OK;
}

static esp_err_t portal_scan(httpd_req_t* req) {
    KryonScanEntry nets[16];
    int n = WebManager::scanNetworks(nets, 16);  // bloqueante ~2s; portal e single-user

    std::string json = "{\"networks\":[";
    bool first = true;
    for (int i = 0; i < n; i++) {
        if (nets[i].ssid.empty()) continue;
        std::string needle = "\"" + jsonEscapeP(nets[i].ssid) + "\"";
        if (kstr::indexOf(json, needle) != -1) continue;  // dedup
        if (!first) json += ",";
        first = false;
        json += "{\"ssid\":\"" + jsonEscapeP(nets[i].ssid) + "\",\"rssi\":" + std::to_string(nets[i].rssi) + "}";
    }
    json += "]}";
    portalSend(req, "application/json", json);
    return ESP_OK;
}

static esp_err_t portal_connect(httpd_req_t* req) {
    // le corpo (urlencoded, pequeno)
    std::string body(req->content_len, '\0');
    size_t got = 0;
    while (got < body.size()) {
        int r = httpd_req_recv(req, &body[got], body.size() - got);
        if (r <= 0) break;
        got += (size_t)r;
    }

    int ssidIdx = kstr::indexOf(body, "ssid=");
    int passIdx = kstr::indexOf(body, "password=");
    if (ssidIdx < 0) {
        portalSend(req, "application/json", "{\"success\":false}");
        return ESP_OK;
    }

    std::string ssid = body.substr(ssidIdx + 5);
    int amp = kstr::indexOf(ssid, '&');
    ssid = (amp >= 0) ? ssid.substr(0, amp) : ssid;
    ssid = urlDecodeP(ssid);
    ssid = kstr::trim(ssid);

    std::string pass = "";
    if (passIdx >= 0) {
        pass = body.substr(passIdx + 9);
        amp = kstr::indexOf(pass, '&');
        pass = (amp >= 0) ? pass.substr(0, amp) : pass;
        pass = urlDecodeP(pass);
    }

    if (ssid.empty() || ssid.length() > 32 || pass.length() > 64) {
        portalSend(req, "application/json", "{\"success\":false}");
        return ESP_OK;
    }

    s_pendingSsid = ssid;
    s_pendingPass = pass;
    s_connectState = PORTAL_IDLE;
    s_connectRequested = true;  // o loop do modal conecta
    portalSend(req, "application/json", "{\"success\":true}");
    return ESP_OK;
}

static esp_err_t portal_status(httpd_req_t* req) {
    std::string json = "{\"state\":\"";
    switch (s_connectState) {
        case PORTAL_CONNECTING: json += "connecting"; break;
        case PORTAL_CONNECTED:  json += "connected"; break;
        case PORTAL_FAILED:     json += "failed"; break;
        default:                json += "idle"; break;
    }
    std::string ip = (s_connectState == PORTAL_CONNECTED) ? WebManager::getIPAddress() : "";
    json += std::string("\",\"ip\":\"") + jsonEscapeP(ip) + "\"}";
    portalSend(req, "application/json", json);
    return ESP_OK;
}

// Sondas de captive portal (Android/Apple/Windows): redirect para a raiz
static esp_err_t portal_redirect(httpd_req_t* req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
}

// Linha de status na tela do display (apaga a regiao antes de escrever)
static void portalStatus(KryonDisplay* tft, const std::string& l1, const std::string& l2 = "") {
    tft->fillRect(UI::sx(6), UI::sy(210), UI::sx(228), UI::sy(56), TFT_BLACK);
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->setTextDatum(MC_DATUM);
    tft->drawString(l1.c_str(), UI::sx(120), UI::sy(222), UI::font(2));
    if (l2.length()) tft->drawString(l2.c_str(), UI::sx(120), UI::sy(244), UI::font(2));
}

static void portalCleanup(bool connected) {
    if (s_portalServer != nullptr) {
        httpd_stop(s_portalServer);
        s_portalServer = nullptr;
    }
    s_dnsRunning = false;
    WifiAP::instance().stop();
    if (connected) {
        esp_wifi_set_mode(WIFI_MODE_STA);   // derruba o AP, mantem a conexao
    } else {
        esp_wifi_stop();
    }
    s_connectBody = "";
}

// ---------------------------------------------------------------------------
// Modal bloqueante
// ---------------------------------------------------------------------------

bool CaptivePortal::runBlocking(KryonDisplay* tft) {
    // A porta 80 e do portal: pausa o servidor principal se estiver no ar
    WebManager::stopWebServer();

    // Sufixo do nome do AP: 4 ultimos hex do MAC
    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    std::string apSsid = "KryonOS-Setup-" + kstr::fmt("%X", mac[4]) + kstr::fmt("%X", mac[5]);

    s_connectRequested = false;
    s_connectState = PORTAL_IDLE;

    // Ordem importante: STA primeiro (o init do WifiConnection forca modo STA),
    // AP depois, e entao promove para AP_STA mantendo o AP de pe.
    WebManager::wifi().init();
    if (!WifiAP::instance().start(apSsid)) {
        Serial.println("Falha ao iniciar AP do portal");
        return false;
    }
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_start();

    s_dnsRunning = true;
    xTaskCreate(dnsTask, "kryon_dns", 4096, nullptr, 5, nullptr);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 12288;
    config.max_uri_handlers = 8;
    config.uri_match_fn = httpd_uri_match_wildcard;
    if (httpd_start(&s_portalServer, &config) == ESP_OK) {
        const httpd_uri_t root = { "/", HTTP_GET, portal_index, nullptr };
        httpd_register_uri_handler(s_portalServer, &root);
        const httpd_uri_t scan = { "/scan", HTTP_GET, portal_scan, nullptr };
        httpd_register_uri_handler(s_portalServer, &scan);
        const httpd_uri_t connect = { "/connect", HTTP_POST, portal_connect, nullptr };
        httpd_register_uri_handler(s_portalServer, &connect);
        const httpd_uri_t status = { "/status", HTTP_GET, portal_status, nullptr };
        httpd_register_uri_handler(s_portalServer, &status);
        const httpd_uri_t wild = { "/*", HTTP_GET, portal_redirect, nullptr };
        httpd_register_uri_handler(s_portalServer, &wild);
    }

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
    tft->drawString(apSsid.c_str(), UI::sx(120), UI::sy(92), UI::font(2));
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
        if (s_connectRequested) {
            s_connectRequested = false;
            s_connectState = PORTAL_CONNECTING;
            portalStatus(tft, "Connecting to:", s_pendingSsid);
            WebManager::wifi().setConnectionTimeout(15000);
            // Async: o AP precisa continuar atendendo /status durante a conexao
            WebManager::wifi().connect(s_pendingSsid, s_pendingPass, true);
            s_connectStart = millis();
        }

        if (s_connectState == PORTAL_CONNECTING) {
            if (WebManager::wifi().isConnected()) {
                // Credenciais agora vivem no NetworkCredentialStore (NVS)
                NetworkManager::instance().getCredentialStore().saveNetwork(
                    KnownNetwork(s_pendingSsid.c_str(), s_pendingPass.c_str()));
                s_connectState = PORTAL_CONNECTED;
                portalStatus(tft, "Connected!", WebManager::getIPAddress());
                delay(1500);
                portalCleanup(true);
                return true;
            }
            if (millis() - s_connectStart > 16000) {
                WebManager::wifi().disconnect();  // aborta o STA; o AP segue de pe
                s_connectState = PORTAL_FAILED;
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
