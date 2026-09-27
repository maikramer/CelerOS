//
// Created by maikeu on 08/06/2020.
//

#ifndef BT_CONNECTIONMANAGER_H
#define BT_CONNECTIONMANAGER_H


#include <list>
#include "BluetoothConnection.h"
#include "SafeContainers.h"

class BtConnectionManager {

public:
    static auto GetFreeConnection() -> BluetoothConnection *;

    static void Connect(uint16_t conn_id);

    static void SendNotifications();

    static auto GetConnectionById(uint16_t id) -> BluetoothConnection *;

    static void Disconnect(uint16_t id);

    static void NotifyAll(bool isImportant);

    static void Init(int noOfConnections);

    static Event<BtConnectionManager *, BluetoothConnection *> OnConnect;
private:
    static SafeContainers::SafeList<BluetoothConnection *> _connectionPool;//NOLINT
};

#endif //BT_CONNECTIONMANAGER_H
