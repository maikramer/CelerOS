#include "OtaManager.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include "../File System/FileSystem.h"

OtaUpdateInfo OtaManager::info;
String OtaManager::lastError = "";

#ifdef KRYONOS_BOARD_SMARTDISPLAY_4IN
static const char* KRYONOS_UPDATE_CHANNEL = "smartdisplay_4848S040";
#else
static const char* KRYONOS_UPDATE_CHANNEL = "esp32";
#endif

// Fonte canonica dos updates deste fork. Trocar aqui (ou usar
// /local/ota_url.txt no dispositivo) para apontar outro servidor.
static const char* KRYONOS_UPDATE_BASE =
    "https://raw.githubusercontent.com/maikramer/KryonOS/refs/heads/main/updates";

static bool isVersionGreater(const String& newVer, const String& oldVer) {
    int newParts[3] = {0,0,0}, oldParts[3] = {0,0,0};
    auto parseV = [](const String& v, int* p) {
        int pt = 0, st = 0;
        while(pt<3 && st<(int)v.length()){
            int d = v.indexOf('.', st);
            if(d==-1) { p[pt] = v.substring(st).toInt(); break; }
            p[pt] = v.substring(st, d).toInt();
            st = d+1; pt++;
        }
    };
    parseV(newVer, newParts);
    parseV(oldVer, oldParts);
    if(newParts[0] > oldParts[0]) return true;
    if(newParts[0] < oldParts[0]) return false;
    if(newParts[1] > oldParts[1]) return true;
    if(newParts[1] < oldParts[1]) return false;
    if(newParts[2] > oldParts[2]) return true;
    return false;
}

String OtaManager::getUpdateJsonUrl() {
    // Override para testes com servidor local (tools/ota_server.py)
    if (FileSystem::exists("/local/ota_url.txt")) {
        String override = FileSystem::readTextFile("/local/ota_url.txt");
        override.trim();
        if (override.length() > 0) {
            if (!override.endsWith(".json")) {
                if (!override.endsWith("/")) override += "/";
                override += "update.json";
            }
            return override;
        }
    }
    return String(KRYONOS_UPDATE_BASE) + "/" + KRYONOS_UPDATE_CHANNEL + "/update.json";
}

bool OtaManager::checkForUpdates() {
    info = OtaUpdateInfo();
    String url = getUpdateJsonUrl();

    HTTPClient http;
    http.setTimeout(20000);

    // Clientes na pilha: precisam sobreviver enquanto "http" estiver em uso
    WiFiClientSecure secureClient;

    bool begun;
    if (url.startsWith("https:")) {
        secureClient.setInsecure();
        begun = http.begin(secureClient, url);
    } else {
        begun = http.begin(url);
    }
    if (!begun) {
        info.fetchFailed = true;
        return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        info.fetchFailed = true;
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    info.version = FileSystem::parseJsonValue(payload, "version");
    info.changelog = FileSystem::parseJsonValue(payload, "changelog");
    info.guide = FileSystem::parseJsonValue(payload, "guide");
    info.firmwareUrl = FileSystem::parseJsonValue(payload, "firmware_url");

    info.changelog.replace("\\n", "\n");
    info.guide.replace("\\n", "\n");

    bool major = FileSystem::parseJsonValue(payload, "major_update") == "true";
    bool minor = FileSystem::parseJsonValue(payload, "minor_update") == "true";
    bool security = FileSystem::parseJsonValue(payload, "security_update") == "true";

    if (major) info.type = "Major System Update Available!";
    else if (minor) info.type = "Minor Update Available!";
    else if (security) info.type = "Security Update Available!";
    else info.type = "Update Available!";

    // firmware_url relativa resolve contra o diretorio do update.json
    // (conveniente para o servidor local de testes)
    if (info.firmwareUrl.length() > 0 && !info.firmwareUrl.startsWith("http")) {
        int slash = url.lastIndexOf('/');
        if (slash >= 0) info.firmwareUrl = url.substring(0, slash + 1) + info.firmwareUrl;
    }
    info.hasFirmware = info.firmwareUrl.length() > 0;

    if (isVersionGreater(info.version, KRYONOS_VERSION)) {
        info.available = true;
    }
    return info.available;
}

bool OtaManager::performUpdate(const String& firmwareUrl, void (*onProgress)(int percent)) {
    lastError = "";

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(30000);

    // Clientes na pilha: precisam sobreviver enquanto "http" estiver em uso
    WiFiClientSecure secureClient;
    WiFiClient plainClient;

    bool begun;
    if (firmwareUrl.startsWith("https:")) {
        // Mesmo padrao do updater original: TLS sem validacao de certificado
        secureClient.setInsecure();
        begun = http.begin(secureClient, firmwareUrl);
    } else {
        begun = http.begin(plainClient, firmwareUrl);
    }
    if (!begun) {
        lastError = "HTTP init failed";
        return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        lastError = "HTTP error " + String(code);
        http.end();
        return false;
    }

    int totalLen = http.getSize();
    if (totalLen <= 0) {
        lastError = "Unknown firmware size";
        http.end();
        return false;
    }

    if (!Update.begin(totalLen)) {
        lastError = "Not enough space: " + String(Update.errorString());
        http.end();
        return false;
    }

    WiFiClient *stream = http.getStreamPtr();
    uint8_t buff[2048];
    size_t written = 0;
    int lastPct = -1;

    while (http.connected() && written < (size_t)totalLen) {
        size_t avail = stream->available();
        if (avail) {
            size_t readLen = stream->readBytes(buff, (avail > sizeof(buff)) ? sizeof(buff) : avail);
            if (readLen == 0) break;
            if (Update.write(buff, readLen) != readLen) {
                lastError = "Flash write failed: " + String(Update.errorString());
                Update.abort();
                http.end();
                return false;
            }
            written += readLen;
            int pct = (written * 100) / totalLen;
            if (onProgress && pct != lastPct) {
                onProgress(pct);
                lastPct = pct;
            }
        } else {
            delay(1);
        }
    }
    http.end();

    if (written != (size_t)totalLen) {
        lastError = "Download incomplete (" + String(written) + "/" + String(totalLen) + ")";
        Update.abort();
        return false;
    }

    if (!Update.end(true)) {
        lastError = "Image verify failed: " + String(Update.errorString());
        return false;
    }

    return true;
}
