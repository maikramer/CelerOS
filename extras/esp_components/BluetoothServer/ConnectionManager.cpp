//
// Created by maikeu on 08/06/2020.
//

#include <esp_log.h>

#include "ConnectionManager.h"
#include "BluetoothServer.h"

#ifdef USER_MANAGEMENT_ENABLED

#include <ConnectedUser.h>

#endif

//#define DEBUG_INFO

SafeContainers::SafeList<BluetoothConnection *> BtConnectionManager::_connectionPool;//NOLINT
Event<BtConnectionManager *, BluetoothConnection *> BtConnectionManager::OnConnect;

void BtConnectionManager::Init(int noOfConnections) {
    for (auto i = 0; i < noOfConnections; i++) {
        auto *connection = new BluetoothConnection();
        connection->Init();
        _connectionPool.pushBack(connection);
    }
}

void BtConnectionManager::Connect(uint16_t conn_id) {
    auto *connection = GetFreeConnection();
    if (connection != nullptr) {
        connection->Connect(conn_id);
        OnConnect.trigger(nullptr, connection);
    } else {
        ESP_LOGE(__FUNCTION__, "Sem conexões livres");
        BluetoothServer::instance().BleServer->disconnect(conn_id);
    }
}

void BtConnectionManager::Disconnect(uint16_t id) {
    auto *conn = GetConnectionById(id);
    if (conn == nullptr) return;
#ifdef USER_MANAGEMENT_ENABLED

#ifdef DEBUG_INFO
    ESP_LOGI(__FUNCTION__, "Usuario com id %d desconectado", id);
#endif

#endif
    conn->Disconnect();
}

auto BtConnectionManager::GetConnectionById(uint16_t id) -> BluetoothConnection * {
    BluetoothConnection* result = nullptr;
    _connectionPool.forEach([&result, id](BluetoothConnection* const& connection) {
        if (result == nullptr && connection->GetId() == id) {
#ifdef DEBUG_INFO
            ESP_LOGI("GetConnectionById", "Connection Id: %d", id);
#endif
            result = connection;
        }
    });

    if (result == nullptr) {
        ESP_LOGE(__FUNCTION__, "Conexão com Id %d nao encontrado", id);
    }
    return result;
}

auto BtConnectionManager::GetFreeConnection() -> BluetoothConnection * {
    static auto *semaphore = xSemaphoreCreateMutex();//NOLINT
    if (xSemaphoreTake(semaphore, 1000) == pdFAIL) {
        ESP_LOGE(__FUNCTION__, "Estouro ao adquirir semaforo");
        return nullptr;
    }
    BluetoothConnection *ret = nullptr;

    _connectionPool.forEach([&ret](BluetoothConnection* const& conn) {
        if (ret == nullptr && conn->IsFree()) {
            ret = conn;
        }
    });

    if (ret == nullptr) {
        ESP_LOGE(__FUNCTION__, "Sem Conexões livres");
    }

    xSemaphoreGive(semaphore);
    return ret;
}

void BtConnectionManager::SendNotifications() {
    if (_connectionPool.empty()) return;
    
    _connectionPool.forEach([](BluetoothConnection* const& connection) {
        if (connection == nullptr || connection->IsFree())
            return;

#ifdef USER_MANAGEMENT_ENABLED
        auto *user = connection->GetUser(true, true);
        if (user == nullptr) return;

        auto isLogged = user->IsLogged;
        if (!isLogged) return;

        auto state = user->GetNotificationNeeds();
#else

#ifdef DEBUG_INFO
        ESP_LOGI("SendNotifications", "Enviando para a conexao %u", connection->GetId());
#endif  //DEBUG_INFO
        auto state = connection->GetNotificationNeeds();
#endif //USER_MANAGEMENT_ENABLED
        if (state == NotificationNeeds::NoSend) return;
        connection->SendNotifyData(state != NotificationNeeds::SendImportant);
    });
}

void BtConnectionManager::NotifyAll(bool isImportant) {
    if (_connectionPool.empty()) return;
    
    _connectionPool.forEachMut([isImportant](BluetoothConnection*& connection) {
        if (connection != nullptr) {
            connection->SetNotificationNeeds(
                    isImportant ? NotificationNeeds::SendImportant : NotificationNeeds::SendNormal);
        }
    });
}
