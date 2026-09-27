//
// Created by maikeu on 18/07/2020.
//

#ifndef COMMANDCODE_H
#define COMMANDCODE_H

enum class CommandCode : uint8_t {
    SetAdminInfoCode = 100,
    LoginCode = 101,
    LogoffCode = 102,
    SignUpCode = 103,
    GetUsersWaitingCode = 104,
    ApproveUserCode = 105,
    ClearUsers = 106,
    ScanForWifi = 107

};

#endif //COMMANDCODE_H
