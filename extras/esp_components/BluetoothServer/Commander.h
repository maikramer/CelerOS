//
// Created by maikeu on 18/08/2019.
//

#ifndef COMMANDER_H
#define COMMANDER_H

#include <string>
#include <utility>
#include <list>
#include <vector>
#include <nameof.hpp>
#include "BluetoothConnection.h"

class BluetoothConnection;

class DeviceCommand {
public:
    DeviceCommand(const uint32_t dataSize, std::string internalName, const uint8_t code,
                  std::function<void(const std::vector<std::string> &,
                                     BluetoothConnection *)> functionPtr) : DataSize(
            dataSize), InternalName(std::move(internalName)), Code(code), Function(
            std::move(functionPtr)) {}

    const uint32_t DataSize;
    std::string InternalName;
    const uint8_t Code;
    std::function<void(const std::vector<std::string> &, BluetoothConnection *)> Function;
};

class Commander {
public:
    Commander() = delete;

    static void CheckForCommand(const std::string &rxValue, BluetoothConnection *connection);

    static void AddCommand(const DeviceCommand &command);

    static void Init();

private:
    static std::list<DeviceCommand> _commands;

};


#endif //COMMANDER_H
