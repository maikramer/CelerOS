#include "OtaManager.h"
#include <string>
#include "WifiOta.h"
#include "HttpClient.h"
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

    HttpClient http;
    http.setTimeout(20000);
    HttpResponse resp = http.get(url);
    if (!resp.isOk()) {
        info.fetchFailed = true;
        return false;
    }
    std::string payload = resp.body;

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

// Flash via componente WifiOta (esp_https_ota + eventos). O Event<int>
// onProgress alimenta o callback C usado pela barra do SettingsUI.
static WifiOta s_wifiOta;
static void (*s_progressCb)(int) = nullptr;
static bool s_otaBound = false;

bool OtaManager::performUpdate(const std::string& firmwareUrl, void (*onProgress)(int percent)) {
    lastError = "";
    s_progressCb = onProgress;

    if (!s_otaBound) {
        s_otaBound = true;
        s_wifiOta.onProgress.addHandler([](int pct) {
            if (s_progressCb != nullptr) s_progressCb(pct);
        });
    }

    // Bloqueante; falha deixa o slot atual intacto (checksum no finish)
    ErrorCode err = s_wifiOta.startUpdate(firmwareUrl);
    if (err != CommonErrorCodes::None) {
        lastError = "OTA failed: " + err.description();
        return false;
    }
    return true;
}
