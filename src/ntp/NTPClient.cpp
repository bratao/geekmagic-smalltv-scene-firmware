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

#include "ntp/NTPClient.h"
#include "config/ConfigManager.h"
#include "wireless/WiFiManager.h"
#include <coredecls.h>
#include <lwip/apps/sntp.h>
#include <Logger.h>
#include <ctime>

extern ConfigManager configManager;
namespace {
constexpr uint32_t TimeoutMs=10000;
constexpr time_t ReasonableEpoch=1600000000;
constexpr const char* DefaultServer="pool.ntp.org";
volatile uint32_t syncGeneration=0;
}

NTPClient::NTPClient() = default;

void NTPClient::begin(uint32_t intervalSeconds,uint8_t maxRetries) {
    // Bounds keep all elapsed-millis intervals well below the wrap period.
    _syncIntervalSeconds=intervalSeconds<60?60:(intervalSeconds>604800?604800:intervalSeconds);
    _maxRetries=maxRetries<1?1:(maxRetries>3?3:maxRetries);
    _phase=Phase::Idle;_since=millis();_waitMs=0;_attempt=0;
    _lastStatus="not started";
    settimeofday_cb([](bool fromSntp) {
        // SDK invokes this from its scheduled callback, not the display/web handler.
        if (fromSntp) ++syncGeneration;
    });
    Logger::info("Nonblocking NTP client initialized", "NTPClient");
}

void NTPClient::performSync() {
    sntp_stop();
    const char* configured=configManager.getNtpServer();
    _server=(configured && configured[0])?configured:DefaultServer;
    _syncGeneration=syncGeneration;
    ++_attempt;_phase=Phase::Active;_since=millis();_lastOk=false;
    _lastStatus=String("sync requested (attempt ")+_attempt+")";
    // UTC system time; renderer applies its own display timezone exactly once.
    if (_server!=DefaultServer) configTime(0,0,_server.c_str(),DefaultServer);
    else configTime(0,0,DefaultServer);
}

void NTPClient::finish(bool success) {
    sntp_stop();
    _phase=Phase::Idle;_since=millis();_waitMs=_syncIntervalSeconds*1000;
    _lastOk=success;
    if (success) {
        _lastSync=time(nullptr);
        _lastStatus="sync completed";
        Logger::info("NTP synchronization completed", "NTPClient");
    } else {
        _lastStatus="sync failed after retries";
        Logger::warn("NTP synchronization failed after retries", "NTPClient");
    }
}

void NTPClient::loop() {
    const uint32_t now=millis();
    if (!WiFiManager::isConnected()) {
        if (_phase!=Phase::Idle) {
            sntp_stop();_phase=Phase::Idle;_lastOk=false;_waitMs=0;
        }
        // Resume promptly after profile failover rather than waiting the old six-hour interval.
        _waitMs=0;
        _lastStatus="network unavailable";
        return;
    }
    if (_phase==Phase::Idle) {
        if (uint32_t(now-_since)>=_waitMs) { _attempt=0;performSync(); }
        return;
    }
    if (_phase==Phase::Backoff) {
        if (uint32_t(now-_since)>=_waitMs) performSync();
        return;
    }
    // A merely valid pre-existing wall clock cannot complete this attempt.
    if (syncGeneration!=_syncGeneration && time(nullptr)>ReasonableEpoch) { finish(true);return; }
    if (uint32_t(now-_since)<TimeoutMs) return;
    sntp_stop();
    if (_attempt>=_maxRetries) { finish(false);return; }
    _phase=Phase::Backoff;_since=now;_waitMs=500u*_attempt;
    _lastStatus="retry pending";
}

bool NTPClient::syncNow() {
    if (!WiFiManager::isConnected()) { _lastStatus="network unavailable";return false; }
    if (_phase==Phase::Active || _phase==Phase::Backoff) return true;
    _attempt=0;performSync();return true;
}

bool NTPClient::lastSyncOk() const { return _lastOk; }
time_t NTPClient::lastSyncTime() const { return _lastSync; }
String NTPClient::lastStatus() const { return _lastStatus; }
