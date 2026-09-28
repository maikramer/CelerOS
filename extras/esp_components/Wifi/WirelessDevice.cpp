//
// Created by maikeu on 27/06/22.
//

#include "WirelessDevice.h"

void from_json(const nlohmann::json &j, WirelessDevice &device) {
    device.fromJson(j);
}