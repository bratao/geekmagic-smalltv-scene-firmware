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

#include "wireless/WiFiManager.h"
#include "config/ConfigManager.h"
#include <Logger.h>
#include <cstring>
#include <utility>

extern ConfigManager configManager;

namespace {
constexpr uint32_t DeferredMs=500, AttemptMs=12000, DisconnectMs=5000;
constexpr uint32_t RetryMs=60000, ApGraceMs=10000;
constexpr int MaxScanResults=20;
bool scanPending=false;
uint32_t scanStarted=0;
bool connectionAttempt=false;

void expireScan() {
    // Abandoned browser polling must not hold scan memory or prevent roaming forever.
    if (scanPending && uint32_t(millis()-scanStarted)>=30000 && WiFi.scanComplete()!=WIFI_SCAN_RUNNING) {
        WiFi.scanDelete();scanPending=false;
    }
}
}

WiFiManager::WiFiManager(const char* apSsid,const char* apPass)
    : _apSsid(apSsid),_apPass(apPass) {}

void WiFiManager::begin() {
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_STA);
    _profileIndex=0;
    _skipSsid="";
    nextProfile(millis());
}

bool WiFiManager::scheduleConnect(const char* ssid,const char* pass) {
    expireScan();
    if (scanPending) return false;
    if (!ssid || !pass || strlen(ssid)==0 || strlen(ssid)>32 || strlen(pass)>64) return false;
    // Copy before acknowledging: callers may supply temporary JSON/config strings.
    String nextSsid(ssid),nextPass(pass);
    if (nextSsid.length()!=strlen(ssid) || nextPass.length()!=strlen(pass)) return false;
    _pendingSsid=std::move(nextSsid);_pendingPass=std::move(nextPass);
    _phase=Phase::Deferred;_since=millis();connectionAttempt=true;
    return true;
}

void WiFiManager::startAttempt(const char* ssid,const char* pass,uint32_t now) {
    _attemptSsid=ssid;
    // The ESP8266 SDK copies station credentials in begin; persistence remains off.
    WiFi.mode(_apMode?WIFI_AP_STA:WIFI_STA);
    WiFi.disconnect(false);
    WiFi.begin(ssid,pass);
    connectionAttempt=true;
    _phase=Phase::Attempt;_since=now;
    Logger::info("Trying configured WiFi profile", "WiFiManager");
}

void WiFiManager::nextProfile(uint32_t now) {
    const size_t count=configManager.getNetworkCount();
    while (_profileIndex<count && _profileIndex<ConfigManager::MAX_WIFI_NETWORKS) {
        const auto& profile=configManager.getNetwork(_profileIndex++);
        if (profile.ssid.empty() || (!_skipSsid.isEmpty() && _skipSsid==profile.ssid.c_str())) continue;
        startAttempt(profile.ssid.c_str(),profile.password.c_str(),now);
        return;
    }
    recover(now);
}

void WiFiManager::recover(uint32_t now) {
    WiFi.disconnect(false);
    connectionAttempt=false;
    startAccessPointMode();
    _attemptSsid="";_skipSsid="";
    _phase=Phase::Recovery;_since=now;
    Logger::info("WiFi recovery access point available", "WiFiManager");
}

void WiFiManager::connected(uint32_t now) {
    connectionAttempt=false;
    _pendingSsid="";_pendingPass="";_skipSsid="";
    _phase=Phase::Connected;_connectedSince=now;
    Logger::info("WiFi station connected", "WiFiManager");
}

void WiFiManager::update() {
    expireScan();
    const uint32_t now=millis();
    switch (_phase) {
    case Phase::Deferred:
        // No radio mutation in the request handler; let the response leave first.
        if (uint32_t(now-_since)<DeferredMs || scanPending) return;
        _skipSsid=_pendingSsid;_profileIndex=0;
        startAttempt(_pendingSsid.c_str(),_pendingPass.c_str(),now);
        _pendingSsid="";_pendingPass="";
        break;
    case Phase::Attempt:
        if (isConnected() && WiFi.SSID()==_attemptSsid) { connected(now);return; }
        if (uint32_t(now-_since)>=AttemptMs) nextProfile(now);
        break;
    case Phase::Connected:
        if (!isConnected()) { _phase=Phase::DisconnectGrace;_since=now;return; }
        if (_apMode && !scanPending && uint32_t(now-_connectedSince)>=ApGraceMs) {
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            _apMode=false;
        }
        break;
    case Phase::DisconnectGrace:
        if (isConnected()) { connected(now);return; }
        if (uint32_t(now-_since)>=DisconnectMs && !scanPending) {
            _profileIndex=0;_skipSsid="";nextProfile(now);
        }
        break;
    case Phase::Recovery:
        if (isConnected()) { connected(now);return; }
        if (uint32_t(now-_since)>=RetryMs && !scanPending) {
            _profileIndex=0;_skipSsid="";nextProfile(now);
        }
        break;
    case Phase::Idle: break;
    }
}

bool WiFiManager::startAccessPointMode() {
    WiFi.mode(WIFI_AP_STA);
    if (!_apMode) _apMode=WiFi.softAP(_apSsid,_apPass);
    return _apMode;
}

int WiFiManager::startScan() {
    expireScan();
    if (scanPending) return -1;
    if (connectionAttempt) return -2;
    WiFi.scanDelete();
    const int result=WiFi.scanNetworks(true);
    if (result==WIFI_SCAN_FAILED) return -2;
    scanPending=true;scanStarted=millis();
    return -1;
}

int WiFiManager::pollScan(JsonArray& out) {
    out.clear();
    if (!scanPending) return -2;
    const int count=WiFi.scanComplete();
    if (count==WIFI_SCAN_RUNNING) return -1;
    scanPending=false;
    if (count<0) { WiFi.scanDelete();return -2; }
    const int bounded=count<MaxScanResults?count:MaxScanResults;
    for (int i=0;i<bounded;++i) {
        JsonObject item=out.add<JsonObject>();
        item["ssid"]=WiFi.SSID(i);
        item["rssi"]=WiFi.RSSI(i);
        item["enc"]=static_cast<int>(WiFi.encryptionType(i));
    }
    WiFi.scanDelete();
    return bounded;
}

bool WiFiManager::connecting() const { return _phase==Phase::Deferred || _phase==Phase::Attempt; }

bool WiFiManager::isConnected() { return WiFi.status()==WL_CONNECTED; }
String WiFiManager::getConnectedSSID() { return isConnected()?WiFi.SSID():String(); }
bool WiFiManager::isApMode() const { return _apMode; }
IPAddress WiFiManager::getIP() const { return isConnected()?WiFi.localIP():WiFi.softAPIP(); }
