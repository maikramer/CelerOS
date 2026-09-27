#include "WebManager.h"
#include <string>
#include <WiFi.h>
#include <SD.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Update.h>
#include "../FileSystem/FileSystem.h"
#include "../Utils/StrUtils.h"
#include "filemanager_html.h"
#include "ota_upload_html.h"
#include "../Kernel/TimeManager.h"

AsyncWebServer server(80);
bool isWiFiConnected = false;
static bool serverConfigured = false;
static bool serverRunning = false;
static WiFiEventId_t wifiEventId = 0;
static unsigned long reconnectGateUntil = 0;  // auto-reconnect suspenso ate este ms
static volatile bool rebootPending = false;   // setado pelo /update, consumido por tick()

// Auto-reconnect com backoff de 10s: se o roteador cair, o KryonOS tenta
// voltar sozinho (antes era preciso reboot). So age em modo STA puro —
// durante o captive portal (AP_STA) e na janela pos-disable() fica mudo.
void WebManager::onWifiEvent(arduino_event_id_t event) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        isWiFiConnected = false;
        if (WiFi.getMode() == WIFI_MODE_STA && millis() > reconnectGateUntil) {
            static unsigned long lastTry = 0;
            unsigned long now = millis();
            if (now - lastTry >= 10000) {
                lastTry = now;
                Serial.println("WiFi lost, reconnecting...");
                WiFi.reconnect();
            }
        }
    } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        isWiFiConnected = true;
    }
}

// Helper to get FS based on path
fs::FS* getFSFromPath(std::string& path) {
    if (kstr::startsWith(path, "/sd")) {
        path = path.substr(3);
        if (path == "") path = "/";
        return &SD;
    } else if (kstr::startsWith(path, "/littlefs")) {
        path = path.substr(9);
        if (path == "") path = "/";
        return &LittleFS;
    }
    return nullptr;
}

bool WebManager::init() {
    // Registra o auto-reconnect uma unica vez, antes de qualquer early-return
    if (wifiEventId == 0) {
        wifiEventId = WiFi.onEvent(onWifiEvent);
    }

    File wifiFile = SD.open("/wifi.txt", FILE_READ);
    if (!wifiFile) {
        // Fallback to LittleFS
        wifiFile = LittleFS.open("/wifi.txt", FILE_READ);
        if (!wifiFile) {
            Serial.println("No wifi.txt found on SD card or LittleFS.");
            return false;
        }
    }

    std::string ssid = wifiFile.readStringUntil('\n').c_str();
    std::string pass = wifiFile.readStringUntil('\n').c_str();
    wifiFile.close();

    ssid = kstr::trim(ssid);
    pass = kstr::trim(pass);

    if (ssid.length() == 0) {
        Serial.println("wifi.txt is empty.");
        return false;
    }

    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid.c_str());

    if (pass.length() > 0) {
        WiFi.begin(ssid.c_str(), pass.c_str());
    } else {
        WiFi.begin(ssid.c_str());
    }
    
    // Try to connect for up to 10 seconds
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        isWiFiConnected = true;
        Serial.println("WiFi connected!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());

        // Sync NTP Time
        TimeManager::syncNTP();

        // Check if Web Server is enabled by user
        if (!FileSystem::exists("/local/web_on.txt")) {
            Serial.println("Web Server disabled by user (web_on.txt not found).");
            return true; // WiFi is connected, but server is not started
        }

        // Configure Web Server (handlers registrados apenas uma vez)
        if (serverConfigured) {
            if (!serverRunning) {
                server.begin();
                serverRunning = true;
                Serial.println("Async Web Server started on port 80");
            }
            return true;
        }
        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(200, "text/html", filemanager_html);
        });

        server.on("/api/list", HTTP_GET, [](AsyncWebServerRequest *request){
            if (!request->hasParam("dir")) {
                request->send(400, "text/plain", "Missing dir parameter");
                return;
            }
            std::string path = request->getParam("dir")->value().c_str();
            fs::FS* fs = getFSFromPath(path);
            if (!fs) {
                request->send(400, "text/plain", "Invalid storage");
                return;
            }

            File dir = fs->open(path.c_str());
            if (!dir || !dir.isDirectory()) {
                request->send(404, "text/plain", "Not a directory");
                return;
            }

            JsonDocument doc;
            JsonArray array = doc.to<JsonArray>();

            File file = dir.openNextFile();
            while (file) {
                JsonObject item = array.add<JsonObject>();
                item["name"] = std::string(file.name());  // copia (ArduinoJson nao copia const char*)
                item["type"] = file.isDirectory() ? "dir" : "file";
                item["size"] = file.size();
                file.close();
                file = dir.openNextFile();
            }
            dir.close();

            std::string response;
            serializeJson(doc, response);
            request->send(200, "application/json", response.c_str());
        });

        server.on("/api/edit", HTTP_GET, [](AsyncWebServerRequest *request){
            if (!request->hasParam("path")) {
                request->send(400, "text/plain", "Missing path");
                return;
            }
            std::string path = request->getParam("path")->value().c_str();
            fs::FS* fs = getFSFromPath(path);
            if (!fs || !fs->exists(path.c_str())) {
                request->send(404, "text/plain", "File not found");
                return;
            }
            request->send(*fs, path.c_str(), "text/plain");
        });

        server.on("/api/edit", HTTP_POST, [](AsyncWebServerRequest *request){
            if (!request->hasParam("path", true) || !request->hasParam("content", true)) {
                request->send(400, "text/plain", "Missing parameters");
                return;
            }
            std::string path = request->getParam("path", true)->value().c_str();
            std::string content = request->getParam("content", true)->value().c_str();
            fs::FS* fs = getFSFromPath(path);
            if (!fs) {
                request->send(400, "text/plain", "Invalid storage");
                return;
            }

            File f = fs->open(path.c_str(), FILE_WRITE);
            if (f) {
                f.print(content.c_str());
                f.close();
                request->send(200, "text/plain", "OK");
            } else {
                request->send(500, "text/plain", "Failed to write file");
            }
        });

        server.on("/api/download", HTTP_GET, [](AsyncWebServerRequest *request){
            if (!request->hasParam("path")) {
                request->send(400, "text/plain", "Missing path");
                return;
            }
            std::string path = request->getParam("path")->value().c_str();
            fs::FS* fs = getFSFromPath(path);
            if (!fs || !fs->exists(path.c_str())) {
                request->send(404, "text/plain", "File not found");
                return;
            }
            AsyncWebServerResponse *response = request->beginResponse(*fs, path.c_str(), "application/octet-stream", true);
            request->send(response);
        });

        server.on("/api/delete", HTTP_DELETE, [](AsyncWebServerRequest *request){
            if (!request->hasParam("path", true)) {
                request->send(400, "text/plain", "Missing path");
                return;
            }
            std::string path = request->getParam("path", true)->value().c_str();
            fs::FS* fs = getFSFromPath(path);
            if (!fs) {
                request->send(400, "text/plain", "Invalid storage");
                return;
            }

            File f = fs->open(path.c_str());
            bool isDir = false;
            if (f) {
                isDir = f.isDirectory();
                f.close();
            }

            if (isDir) {
                fs->rmdir(path.c_str());
            } else {
                fs->remove(path.c_str());
            }
            request->send(200, "text/plain", "OK");
        });

        server.on("/api/create", HTTP_POST, [](AsyncWebServerRequest *request){
            if (!request->hasParam("path", true) || !request->hasParam("type", true)) {
                request->send(400, "text/plain", "Missing parameters");
                return;
            }
            std::string path = request->getParam("path", true)->value().c_str();
            std::string type = request->getParam("type", true)->value().c_str();
            fs::FS* fs = getFSFromPath(path);
            if (!fs) {
                request->send(400, "text/plain", "Invalid storage");
                return;
            }

            if (type == "folder") {
                fs->mkdir(path.c_str());
            } else {
                File f = fs->open(path.c_str(), FILE_WRITE);
                if (f) f.close();
            }
            request->send(200, "text/plain", "OK");
        });

        server.on("/api/rename", HTTP_POST, [](AsyncWebServerRequest *request){
            if (!request->hasParam("oldPath", true) || !request->hasParam("newPath", true)) {
                request->send(400, "text/plain", "Missing parameters");
                return;
            }
            std::string oldPath = request->getParam("oldPath", true)->value().c_str();
            std::string newPath = request->getParam("newPath", true)->value().c_str();

            std::string oldFsPath = oldPath;
            std::string newFsPath = newPath;
            fs::FS* fs1 = getFSFromPath(oldFsPath);
            fs::FS* fs2 = getFSFromPath(newFsPath);

            if (fs1 != fs2 || !fs1) {
                request->send(400, "text/plain", "Cannot rename across different storages or invalid");
                return;
            }

            if (fs1->rename(oldFsPath.c_str(), newFsPath.c_str())) {
                request->send(200, "text/plain", "OK");
            } else {
                request->send(500, "text/plain", "Rename failed");
            }
        });

        // Handle file uploads
        server.on("/api/upload", HTTP_POST, [](AsyncWebServerRequest *request){
            request->send(200, "text/plain", "Upload Complete");
        }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            std::string path = filename.c_str();

            // Intercept app uploads and respect default installation location
            if (kstr::startsWith(path, "/local/apps/") || kstr::startsWith(path, "/sd/apps/")) {
                bool defaultSD = FileSystem::exists("/local/config_install_sd.txt");
                int appsIndex = kstr::indexOf(path, "/apps/");
                std::string relativePath = path.substr(appsIndex + 6);

                if (defaultSD && FileSystem::exists("/sd/")) {
                    path = "/sd/apps/" + relativePath;
                } else {
                    path = "/local/apps/" + relativePath;
                }
            }

            // filemanager.html appends the destination path to the filename in FormData
            // So filename here is the absolute path.
            fs::FS* fs = getFSFromPath(path);
            if (!fs) return;

            if (!index) {
                // Ensure parent directories exist
                int pos = 0;
                while ((pos = kstr::indexOf(path, '/', pos + 1)) > 0) {
                    std::string dirPath = path.substr(0, pos);
                    if (!fs->exists(dirPath.c_str())) {
                        fs->mkdir(dirPath.c_str());
                    }
                }

                // Open file for writing
                request->_tempFile = fs->open(path.c_str(), FILE_WRITE);
            }
            if (request->_tempFile) {
                if (len) {
                    request->_tempFile.write(data, len);
                }
                if (final) {
                    request->_tempFile.close();
                }
            }
        });

        // Flash de firmware pelo navegador (estilo ElegantOTA). O reboot e
        // diferido para o tick() do loop principal — nao se pode reiniciar
        // dentro do handler do AsyncWebServer.
        server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(200, "text/html", ota_upload_html);
        });

        server.on("/update", HTTP_POST,
            [](AsyncWebServerRequest *request){
                bool ok = !Update.hasError();
                if (ok) {
                    request->send(200, "text/plain", "OK - rebooting");
                    rebootPending = true;
                } else {
                    request->send(500, "text/plain", Update.errorString());
                }
            },
            [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
                if (index == 0) {
                    Serial.println("OTA web upload: " + filename);
                    Update.begin(UPDATE_SIZE_UNKNOWN);
                }
                if (len) {
                    if (Update.write(data, len) != len) {
                        Serial.printf("OTA write failed: %s\n", Update.errorString());
                    }
                }
                if (final) {
                    if (Update.end(true)) {
                        Serial.println("OTA web upload flashed OK");
                    } else {
                        Serial.printf("OTA end failed: %s\n", Update.errorString());
                    }
                }
            });

        // Required CORS for API usage if needed
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

        serverConfigured = true;
        server.begin();
        serverRunning = true;
        Serial.println("Async Web Server started on port 80");
        
        return true;
    } else {
        Serial.println("WiFi connection failed.");
        return false;
    }
}

bool WebManager::enable() {
    return WebManager::init();
}

void WebManager::disable() {
    // Silencia o auto-reconnect por uns segundos para nao religar o WiFi
    // logo apos um desligamento intencional
    reconnectGateUntil = millis() + 8000;
    if (serverRunning) {
        server.end();
        serverRunning = false;
        Serial.println("Async Web Server stopped");
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    isWiFiConnected = false;
    Serial.println("WiFi disabled at runtime");
}

void WebManager::stopWebServer() {
    if (serverRunning) {
        server.end();
        serverRunning = false;
        Serial.println("Async Web Server paused");
    }
}

void WebManager::tick() {
    if (rebootPending) {
        rebootPending = false;
        Serial.println("Rebooting after web OTA...");
        delay(500);  // da tempo para a resposta HTTP chegar ao navegador
        ESP.restart();
    }
}

bool WebManager::isActive() {
    return isWiFiConnected;
}

std::string WebManager::getIPAddress() {
    if (isWiFiConnected) {
        return WiFi.localIP().toString().c_str();
    }
    return "";
}
