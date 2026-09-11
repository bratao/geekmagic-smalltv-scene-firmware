// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from Times-Z GeekMagic Open Firmware. Configuration and credential persistence.
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Logger.h>
#include <cstring>
#include "config/ConfigManager.h"

ConfigManager::ConfigManager(const char* filename) : filename(filename), secure() {}

bool ConfigManager::validWiFi(const char* name, const char* pass) {
    if (!name || !pass) return false;
    const size_t n = strlen(name), p = strlen(pass);
    if (!n || n > 32 || (p && (p < 8 || p > 64))) return false;
    for (size_t i=0;i<n;++i) if (uint8_t(name[i])<32 || uint8_t(name[i])==127) return false;
    for (size_t i=0;i<p;++i) {
        const auto c=uint8_t(pass[i]);
        if (c<32 || c==127) return false;
        if (p==64 && !((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'))) return false;
    }
    return true;
}

bool ConfigManager::setNetworks(const WifiNetwork* values, size_t count) {
    if (count>MAX_WIFI_NETWORKS || (count && !values)) return false;
    for (size_t i=0;i<count;++i) {
        if (!validWiFi(values[i].ssid.c_str(),values[i].password.c_str())) return false;
        for (size_t j=0;j<i;++j) if (values[i].ssid==values[j].ssid) return false;
    }
    std::array<WifiNetwork,MAX_WIFI_NETWORKS> candidate;
    for (size_t i=0;i<count;++i) candidate[i]=values[i];
    networks=std::move(candidate); networkCount=count;
    ssid=count?networks[0].ssid:"";
    password=count?networks[0].password:"";
    return true;
}

bool ConfigManager::load() {
    // Load persistent credentials even if the optional display configuration is missing.
    JsonDocument doc;
    if (LittleFS.begin()) {
        File file=LittleFS.open(filename.c_str(),"r");
        if (file && file.size() && file.size()<=4096) {
            if (deserializeJson(doc,file)) { doc.clear(); Logger::warn("Invalid display configuration", "ConfigManager"); }
        }
    }
    lcd_rotation=doc["lcd_rotation"] | lcd_rotation;
    ntp_server=doc["ntp_server"] | "";
    api_token=secure.get("api_token", "").c_str();
    bool migrate=false;
    if (api_token.empty() && doc["api_token"].is<const char*>()) {
        api_token=doc["api_token"].as<const char*>(); migrate=!api_token.empty();
    }
    const JsonVariantConst stored=secure.getValue("wifi_networks");
    if (!stored.isNull()) {
        if (!stored.is<JsonArrayConst>() || stored.size()>MAX_WIFI_NETWORKS) return false;
        std::array<WifiNetwork,MAX_WIFI_NETWORKS> values;
        size_t count=0;
        for (JsonVariantConst item:stored.as<JsonArrayConst>()) {
            if (!item["ssid"].is<const char*>() || !item["password"].is<const char*>()) return false;
            const JsonString n=item["ssid"].as<JsonString>(), p=item["password"].as<JsonString>();
            if (n.size()!=strlen(n.c_str()) || p.size()!=strlen(p.c_str())) return false;
            values[count++]={n.c_str(),p.c_str()};
        }
        if (!setNetworks(values.data(),count)) return false;
    } else {
        String name=secure.get("wifi_ssid", ""), pass=secure.get("wifi_password", "");
        if (name.isEmpty()) { name=doc["wifi_ssid"] | ""; pass=doc["wifi_password"] | ""; }
        if (!name.isEmpty()) {
            const WifiNetwork value{name.c_str(),pass.c_str()};
            if (!setNetworks(&value,1)) return false;
            migrate=true;
        }
    }
    // Migrate only after all fields are loaded, preserving the existing token and network.
    if (migrate && !save()) { Logger::error("Credential migration could not be saved", "ConfigManager"); return false; }
    return true;
}

const char* ConfigManager::getSSID() const { return ssid.c_str(); }
const char* ConfigManager::getPassword() const { return password.c_str(); }
const char* ConfigManager::getApiToken() const { return api_token.c_str(); }
uint8_t ConfigManager::getLCDRotation() const { return lcd_rotation; }
void ConfigManager::setLCDRotation(uint8_t value) { lcd_rotation=value; }
void ConfigManager::setApiToken(const char* value) { if (value) api_token=value; }
void ConfigManager::setWiFi(const char* name,const char* pass) {
    if (!name || !pass) return;
    auto values=networks;
    size_t count=networkCount,index=count;
    for (size_t i=0;i<count;++i) if (values[i].ssid==name) { index=i;break; }
    if (index>=MAX_WIFI_NETWORKS) return;
    values[index]={name,pass};
    if (index==count) ++count;
    setNetworks(values.data(),count);
}

bool ConfigManager::save() {
    if (!LittleFS.begin()) return false;
    JsonDocument metadata;
    metadata["lcd_rotation"]=lcd_rotation;
    if (!ntp_server.empty()) metadata["ntp_server"]=ntp_server.c_str();
    const String temporary=String(filename.c_str())+".tmp";
    File file=LittleFS.open(temporary,"w");
    if (!file) return false;
    const size_t expected=measureJson(metadata);
    const size_t written=serializeJson(metadata,file);
    file.close();
    if (metadata.overflowed() || written!=expected) { LittleFS.remove(temporary); return false; }
    JsonDocument changes;
    // Keep the legacy first-network keys for firmware rollback compatibility.
    changes["wifi_ssid"]=getSSID();
    changes["wifi_password"]=getPassword();
    changes["api_token"]=getApiToken();
    JsonArray list=changes["wifi_networks"].to<JsonArray>();
    for (size_t i=0;i<networkCount;++i) {
        JsonObject item=list.add<JsonObject>();
        item["ssid"]=networks[i].ssid.c_str(); item["password"]=networks[i].password.c_str();
    }
    if (changes.overflowed() || !secure.update(changes.as<JsonObjectConst>())) {
        LittleFS.remove(temporary); return false;
    }
    // Credentials are already durable. Failure here affects only display metadata;
    // keep the old file and report the warning without falsely rolling back credentials in RAM.
    if (!LittleFS.rename(temporary,filename.c_str())) {
        Logger::warn("Credentials saved; display metadata rename failed", "ConfigManager");
        LittleFS.remove(temporary);
    }
    return true;
}
