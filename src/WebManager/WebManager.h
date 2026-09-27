#pragma once

#include <Arduino.h>
#include <WiFi.h>

class WebManager {
public:
    // Try reading wifi.txt and connecting to WiFi. Returns true if connected.
    // Also starts the Async Web Server if connected.
    static bool init();

    // Liga o WiFi em tempo de execucao (mesmo efeito do init() no boot)
    static bool enable();

    // Encerra o servidor e desliga o WiFi em tempo de execucao (sem reboot)
    static void disable();

    // Encerra apenas o servidor web, mantendo o WiFi ligado — usado antes de
    // abrir o captive portal, que precisa da porta 80
    static void stopWebServer();

    // Deve ser chamado no loop(): consumir o reboot diferido do upload web
    static void tick();

    // Check if WiFi is active
    static bool isActive();

    // Get the current IP address as string
    static String getIPAddress();

private:
    static void onWifiEvent(arduino_event_id_t event);
};
