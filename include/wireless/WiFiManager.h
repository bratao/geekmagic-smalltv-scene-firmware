// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * GeekMagic Open Firmware
 * Copyright (C) 2026 Times-Z
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <ESP8266WiFi.h>
#include <Arduino.h>
#include <ArduinoJson.h>

class WiFiManager {
 public:
    WiFiManager(const char* apSsid, const char* apPass);
    void begin();
    void update();
    bool connecting() const;
    bool scheduleConnect(const char* ssid, const char* pass);
    bool startAccessPointMode();
    bool isApMode() const;
    IPAddress getIP() const;
    static bool isConnected();
    static String getConnectedSSID();
    // -1: scanning, -2: unavailable/failed/busy, 0..20: result count.
    // pollScan consumes completed SDK results once, then immediately frees them.
    static int startScan();
    static int pollScan(JsonArray& out);

 private:
    enum class Phase : uint8_t { Idle, Deferred, Attempt, Connected, DisconnectGrace, Recovery };
    void nextProfile(uint32_t now);
    void startAttempt(const char* ssid, const char* pass, uint32_t now);
    void connected(uint32_t now);
    void recover(uint32_t now);
    const char* _apSsid;
    const char* _apPass;
    String _pendingSsid, _pendingPass, _attemptSsid, _skipSsid;
    Phase _phase = Phase::Idle;
    size_t _profileIndex = 0;
    uint32_t _since = 0, _connectedSince = 0;
    bool _apMode = false;
};
#endif
