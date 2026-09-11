// Included by Api.cpp: shares its Bearer authentication and CORS helpers.
namespace {
bool wifiScanPending=false;
void wifiError(Webserver* webserver,int code,const char* message) {
    JsonDocument doc;doc["status"]="error";doc["message"]=message;
    String body;serializeJson(doc,body);setCorsHeaders(webserver);
    webserver->raw().send(code,"application/json",body);
}
bool wifiString(JsonVariantConst value) {
    if (!value.is<const char*>()) return false;
    const JsonString text=value.as<JsonString>();
    return text.c_str() && text.size()==strlen(text.c_str());
}
bool wifiDocument(Webserver* webserver,JsonDocument& doc) {
    if (webserver->raw().arg("plain").length()>2048) {wifiError(webserver,413,"WiFi request too large");return false;}
    if (deserializeJson(doc,webserver->raw().arg("plain")) || !doc.is<JsonObject>()) {
        wifiError(webserver,400,"Invalid JSON object");return false;
    }
    return true;
}
int storedIndex(const char* ssid) {
    for (size_t i=0;i<configManager.getNetworkCount();++i)
        if (configManager.getNetwork(i).ssid==ssid) return int(i);
    return -1;
}
bool saveNetworks(const WifiNetwork* values,size_t count) {
    const auto previous=configManager.networks;
    const size_t previousCount=configManager.getNetworkCount();
    if (!configManager.setNetworks(values,count)) return false;
    if (configManager.save()) return true;
    configManager.setNetworks(previous.data(),previousCount);
    return false;
}
}

void handleWifiNetworksGet(Webserver* webserver) {
    if (!requireBearerToken(webserver)) return;
    JsonDocument doc;doc["max_networks"]=ConfigManager::MAX_WIFI_NETWORKS;
    JsonArray list=doc["networks"].to<JsonArray>();
    for (size_t i=0;i<configManager.getNetworkCount();++i) {
        JsonObject item=list.add<JsonObject>();
        const auto& network=configManager.getNetwork(i);
        item["ssid"]=network.ssid.c_str();item["has_password"]=!network.password.empty();
    }
    String body;serializeJson(doc,body);setCorsHeaders(webserver);
    webserver->raw().send(200,"application/json",body);
}

void handleWifiNetworksPut(Webserver* webserver) {
    if (!requireBearerToken(webserver)) return;
    if (wifiManager && wifiManager->connecting()) {wifiError(webserver,409,"Connection in progress; retry shortly");return;}
    JsonDocument doc;if (!wifiDocument(webserver,doc)) return;
    if (!doc["networks"].is<JsonArrayConst>() || doc["networks"].size()>ConfigManager::MAX_WIFI_NETWORKS) {
        wifiError(webserver,400,"Expected a list of up to three networks");return;
    }
    std::array<WifiNetwork,ConfigManager::MAX_WIFI_NETWORKS> values;
    size_t count=0;
    for (JsonVariantConst item:doc["networks"].as<JsonArrayConst>()) {
        if (!item.is<JsonObjectConst>() || !wifiString(item["ssid"])) {wifiError(webserver,400,"Invalid SSID");return;}
        const char* ssid=item["ssid"];
        const int existing=storedIndex(ssid);
        const bool hasPassword=item.as<JsonObjectConst>().containsKey("password");
        if ((hasPassword && !wifiString(item["password"])) || (!hasPassword && existing<0)) {
            wifiError(webserver,400,"New networks require a password field (empty for open networks)");return;
        }
        const char* pass=hasPassword?item["password"].as<const char*>():configManager.getNetwork(existing).password.c_str();
        if (!ConfigManager::validWiFi(ssid,pass)) {wifiError(webserver,400,"Invalid WiFi credentials or lengths");return;}
        for (size_t i=0;i<count;++i) if (values[i].ssid==ssid) {wifiError(webserver,400,"Duplicate SSID");return;}
        values[count++]={ssid,pass};
    }
    if (!saveNetworks(values.data(),count)) {wifiError(webserver,507,"Could not persist networks; previous settings retained");return;}
    setCorsHeaders(webserver);webserver->raw().send(200,"application/json","{\"status\":\"saved\"}");
}

void handleWifiScan(Webserver* webserver) {
    if (!requireBearerToken(webserver)) return;
    if (!wifiManager || wifiManager->connecting()) {wifiError(webserver,409,"Connection in progress; retry shortly");return;}
    if (!wifiScanPending) {
        if (WiFiManager::startScan()==-2) {wifiError(webserver,503,"Could not start WiFi scan");return;}
        wifiScanPending=true;
    }
    JsonDocument doc;JsonArray list=doc.to<JsonArray>();
    const int result=WiFiManager::pollScan(list);
    setCorsHeaders(webserver);
    if (result==-1) {webserver->raw().send(202,"application/json","{\"status\":\"scanning\"}");return;}
    wifiScanPending=false;
    if (result<0 || doc.overflowed()) {wifiError(webserver,503,"WiFi scan failed; retry");return;}
    String body;serializeJson(doc,body);webserver->raw().send(200,"application/json",body);
}

void handleWifiConnect(Webserver* webserver) {
    if (!requireBearerToken(webserver)) return;
    if (!wifiManager || wifiManager->connecting() || WiFi.scanComplete()==WIFI_SCAN_RUNNING) {
        wifiError(webserver,409,"WiFi is busy; retry shortly");return;
    }
    JsonDocument doc;if (!wifiDocument(webserver,doc)) return;
    if (!wifiString(doc["ssid"])) {wifiError(webserver,400,"SSID required");return;}
    const char* name=doc["ssid"];
    int index=storedIndex(name);
    const bool provided=doc.as<JsonObjectConst>().containsKey("password");
    if ((provided && !wifiString(doc["password"])) || (!provided && index<0)) {
        wifiError(webserver,400,"Password required for a new network");return;
    }
    const char* pass=provided?doc["password"].as<const char*>():configManager.getNetwork(index).password.c_str();
    if (!ConfigManager::validWiFi(name,pass)) {wifiError(webserver,400,"Invalid WiFi credentials or lengths");return;}
    auto values=configManager.networks;
    size_t count=configManager.getNetworkCount();
    if (index<0) {
        if (count==ConfigManager::MAX_WIFI_NETWORKS) {wifiError(webserver,409,"Three networks already saved; edit the list first");return;}
        index=int(count++);
    }
    values[index]={name,pass};
    if (!saveNetworks(values.data(),count)) {wifiError(webserver,507,"Could not persist network");return;}
    // Use owned candidate strings after replacing ConfigManager's allocations.
    if (!wifiManager->scheduleConnect(values[index].ssid.c_str(),values[index].password.c_str())) {
        wifiError(webserver,409,"Network saved; WiFi busy, retry connection shortly");return;
    }
    setCorsHeaders(webserver);
    webserver->raw().send(202,"application/json","{\"status\":\"connecting\",\"message\":\"Network saved. Connection starts shortly; the device IP may change.\"}");
}

void handleWifiStatus(Webserver* webserver) {
    if (!requireBearerToken(webserver)) return;
    JsonDocument doc;
    doc["connected"]=wifiManager && WiFiManager::isConnected();
    doc["connecting"]=wifiManager && wifiManager->connecting();
    doc["ap_mode"]=wifiManager && wifiManager->isApMode();
    doc["ssid"]=WiFiManager::getConnectedSSID();
    doc["ip"]=wifiManager?wifiManager->getIP().toString():String();
    doc["saved_networks"]=configManager.getNetworkCount();
    String body;serializeJson(doc,body);setCorsHeaders(webserver);
    webserver->raw().send(200,"application/json",body);
}
