//
// Created by maikeu on 18/08/2019.
//

#include <cstdio>
#include <esp_log.h>
#include <sstream>
#include <Utility.h>
#include <priorities.h>
#include "Commander.h"
#include "vector"

typedef struct Command_s {//NOLINT
    std::function<void(const std::vector<std::string> &, BaseConnection *)> Function{};
    std::vector<std::string> Data;
    BaseConnection *Connection{};
} Command_t;

static QueueHandle_t xCommandQueue = xQueueCreate(10, sizeof(void *));//NOLINT

static void CommandExecutorTask(void *arg __unused) {
    for (;;) {
        Command_t *command = nullptr;
        if (xQueueReceive(xCommandQueue, &command, portMAX_DELAY) == pdPASS) {
            command->Function(command->Data, command->Connection);
            delete command;
        }
    }
}

void Commander::Init() {
    Utility::CreateAndProfile("CommandExecutorTask", CommandExecutorTask, 8192, HIGH_PRIORITY, 0, nullptr);
}

void Commander::AddCommand(const DeviceCommand& command) {
    for (const auto& comm : _commands) {
        if (comm.Code == command.Code) {
            ESP_LOGE(__FUNCTION__, "Codigo %u, para o comando %s, ja utilizado pelo comando %s", comm.Code,
                     command.InternalName.c_str(), comm.InternalName.c_str());
            return;
        }
    }
    _commands.push_back(command);
}

std::list<DeviceCommand> Commander::_commands;//NOLINT

void Commander::CheckForCommand(const std::string &rxValue, BaseConnection *connection) {
    //Extrai comando
    uint8_t commandCode = rxValue.c_str()[0];
    auto rxData = std::string(rxValue.c_str() + 1);
    auto data = Utility::split(rxData, ':');
    ESP_LOGI(__FUNCTION__, "Command: %u", commandCode);
    std::stringstream printData;
    for (const auto &d : data) {
        printData << d << " | ";
    }
    if (printData.str().size() > 1) {
        ESP_LOGI(__FUNCTION__, "Data : %s", printData.str().c_str());
    }

    for (const auto& command : _commands) {
        if (command.Code == commandCode) {
            ESP_LOGI(__FUNCTION__, "Comando encontrado: %s", command.InternalName.c_str());
            if (data.size() != command.DataSize) {
                ESP_LOGW(__FUNCTION__,
                         "Número de argumentos recebidos %d diferente do esperado %ld", data.size(),
                         command.DataSize);
                return;
            }
            auto *commandToSend = new Command_t;
            commandToSend->Function = command.Function;
            commandToSend->Data = data;
            commandToSend->Connection = connection;
            xQueueSendToBack(xCommandQueue, &commandToSend, portMAX_DELAY);
        }
    }
}