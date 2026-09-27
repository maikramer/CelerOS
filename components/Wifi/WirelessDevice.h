//
// Created by maikeu on 27/06/22.
//

#include <utility>

#include "nlohmann/json.hpp"
#include "JsonModels.h"

#ifndef WIRELESSDEVICE_H
#define WIRELESSDEVICE_H


class WirelessDevice : public JsonModels::BaseJsonData {
public:
    std::string Ssid;
    int Rssi{};
    uint Channel{};

    WirelessDevice(std::string ssid, int rssi, uint channel) : Ssid(std::move(ssid)), Rssi(rssi),
                                                                      Channel(channel) {}

    WirelessDevice() = default;


    [[nodiscard]] std::string toJson() const override {
        return toPureJson().dump();
    }

    [[nodiscard]] std::string toString() const {
        std::stringstream stream;
        stream << "SSID: " << Ssid << "RSSI: " << Rssi
               << "Canal: "
               << Channel;
        return stream.str();
    }

    [[nodiscard]] nlohmann::json toPureJson() const {
        nlohmann::json j;
        j["Ssid"] = Ssid;
        j["Rssi"] = Rssi;
        j["Channel"] = Channel;
        return j;
    }

    bool fromJson(const nlohmann::json &j) override {
        if (j.is_null()) return false;
        try {
            Ssid = j["Ssid"];
            Rssi = j["Rssi"];
            Channel = j["Channel"];
        } catch (nlohmann::json::exception &e) {
            const char* error_msg = e.what();
            ESP_LOGE(__FUNCTION__, "WirelessDevice:Exception: %s", error_msg);
            return false;
        } catch (...) {
            return false;
        }

        return true;
    }
};

class ScanForWifiListJsonData : public JsonModels::BaseListJsonData<std::string, WirelessDevice> {
public:
    std::string Ssid;
    WirelessDevice WirelessDeviceJson{};

    ScanForWifiListJsonData(std::string ssid, WirelessDevice wirelessDeviceJson)
            : Ssid(std::move(ssid)), WirelessDeviceJson(std::move(wirelessDeviceJson)) {}

    ScanForWifiListJsonData() = default;

    [[nodiscard]] std::string toJson() const override {
        auto j = getPartialListJson();
        j["Ssid"] = Ssid;
        j["WirelessDeviceJson"] = WirelessDeviceJson.toPureJson();

        return j.dump();
    }

    void fromPair(std::string ssid, WirelessDevice wirelessDevice) override {
        Ssid = ssid;
        WirelessDeviceJson = wirelessDevice;
    }
};


void from_json(const nlohmann::json &j, WirelessDevice &device);


#endif //WIRELESSDEVICE_H
