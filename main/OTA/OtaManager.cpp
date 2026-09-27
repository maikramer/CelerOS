#include "OtaManager.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include "../FileSystem/FileSystem.h"
#include "../Utils/StrUtils.h"

OtaUpdateInfo OtaManager::info;
std::string OtaManager::lastError;

#ifdef KRYONOS_BOARD_SMARTDISPLAY_4IN
static const char* KRYONOS_UPDATE_CHANNEL = "smartdisplay_4848S040";
#else
static const char* KRYONOS_UPDATE_CHANNEL = "esp32";
#endif

// Fonte canonica dos updates deste fork. Trocar aqui (ou usar
// /local/ota_url.txt no dispositivo) para apontar outro servidor.
static const char* KRYONOS_UPDATE_BASE =
    "https://raw.githubusercontent.com/maikramer/KryonOS/refs/heads/main/updates";

static bool isVersionGreater(const std::string& newVer, const std::string& oldVer) {
    int newParts[3] = {0,0,0}, oldParts[3] = {0,0,0};
    auto parseV = [](const std::string& v, int* p) {
        int pt = 0, st = 0;
        while(pt<3 && st<(int)v.length()){
            int d = kstr::indexOf(v, '.', st);
            if(d==-1) { p[pt] = (int)kstr::toInt(v.substr(st)); break; }
            p[pt] = (int)kstr::toInt(v.substr(st, d - st));
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

std::string OtaManager::getUpdateJsonUrl() {
    // Override para testes com servidor local (tools/ota_server.py)
    if (FileSystem::exists("/local/ota_url.txt")) {
        std::string url_Override = kstr::trim(FileSystem::readTextFile("/local/ota_url.txt"));
        if (url_Override.length() > 0) {
            if (!kstr::endsWith(url_Override, ".json")) {
                if (!kstr::endsWith(url_Override, "/")) url_Override += "/";
                url_Override += "update.json";
            }
            return url_Override;
        }
    }
    return std::string(KRYONOS_UPDATE_BASE) + "/" + KRYONOS_UPDATE_CHANNEL + "/update.json";
}

bool OtaManager::checkForUpdates() {
    info = OtaUpdateInfo();
    std::string url = getUpdateJsonUrl();

    HTTPClient http;
    http.setTimeout(20000);

    // Clientes na pilha: precisam sobreviver enquanto "http" estiver em uso
    WiFiClientSecure secureClient;

    bool begun;
    if (kstr::startsWith(url, "https:")) {
        secureClient.setInsecure();
        begun = http.begin(secureClient, url.c_str());
    } else {
        begun = http.begin(url.c_str());
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

    String raw = http.getString();
    std::string payload(raw.c_str(), raw.length());
    http.end();

    info.version = FileSystem::parseJsonValue(payload, "version");
    info.changelog = FileSystem::parseJsonValue(payload, "changelog");
    info.guide = FileSystem::parseJsonValue(payload, "guide");
    info.firmwareUrl = FileSystem::parseJsonValue(payload, "firmware_url");

    info.changelog = kstr::replaceAll(info.changelog, "\\n", "\n");
    info.guide = kstr::replaceAll(info.guide, "\\n", "\n");

    bool major = FileSystem::parseJsonValue(payload, "major_update") == "true";
    bool minor = FileSystem::parseJsonValue(payload, "minor_update") == "true";
    bool security = FileSystem::parseJsonValue(payload, "security_update") == "true";

    if (major) info.type = "Major System Update Available!";
    else if (minor) info.type = "Minor Update Available!";
    else if (security) info.type = "Security Update Available!";
    else info.type = "Update Available!";

    // firmware_url relativa resolve contra o diretorio do update.json
    // (conveniente para o servidor local de testes)
    if (info.firmwareUrl.length() > 0 && !kstr::startsWith(info.firmwareUrl, "http")) {
        int slash = kstr::lastIndexOf(url, '/');
        if (slash >= 0) info.firmwareUrl = url.substr(0, slash + 1) + info.firmwareUrl;
    }
    info.hasFirmware = info.firmwareUrl.length() > 0;

    if (isVersionGreater(info.version, KRYONOS_VERSION)) {
        info.available = true;
    }
    return info.available;
}

bool OtaManager::performUpdate(const std::string& firmwareUrl, void (*onProgress)(int percent)) {
    lastError = "";

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(30000);

    // Clientes na pilha: precisam sobreviver enquanto "http" estiver em uso
    WiFiClientSecure secureClient;
    WiFiClient plainClient;

    bool begun;
    if (kstr::startsWith(firmwareUrl, "https:")) {
        // Mesmo padrao do updater original: TLS sem validacao de certificado
        secureClient.setInsecure();
        begun = http.begin(secureClient, firmwareUrl.c_str());
    } else {
        begun = http.begin(plainClient, firmwareUrl.c_str());
    }
    if (!begun) {
        lastError = "HTTP init failed";
        return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        lastError = kstr::fmt("HTTP error %d", code);
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
        lastError = std::string("Not enough space: ") + Update.errorString();
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
                lastError = std::string("Flash write failed: ") + Update.errorString();
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
        lastError = kstr::fmt("Download incomplete (%d/%d)", (int)written, totalLen);
        Update.abort();
        return false;
    }

    if (!Update.end(true)) {
        lastError = std::string("Image verify failed: ") + Update.errorString();
        return false;
    }

    return true;
}
