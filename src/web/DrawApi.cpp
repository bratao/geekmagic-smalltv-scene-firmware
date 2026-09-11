// Drawing handlers adapted from andrewjiang/HoloClawd-Open-Firmware.
// Copyright (c) 2026 Times-Z. MIT license; see firmware/LICENSE-HoloClawd.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include "web/Api.h"
#include "display/DisplayManager.h"

static uint16_t hexToRgb565(const String& hex) {
    const char* value = hex.c_str();
    if (*value == '#') { ++value; }
    if (strlen(value) != 6) { return LCD_WHITE; }
    const uint32_t rgb = strtoul(value, nullptr, 16);
    return ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F);
}

static bool validDrawCommand(JsonObject obj, const char* type) {
    if (!type || !(String("|clear|pixel|line|rect|circle|triangle|ellipse|roundrect|text|").indexOf(String("|") + type + "|") >= 0)) {
        return false;
    }
    for (const char* key : {"x", "y", "x0", "y0", "x1", "y1", "x2", "y2"}) {
        if (!obj[key].isNull() && (!obj[key].is<int>() || obj[key].as<int>() < -480 || obj[key].as<int>() > 480)) { return false; }
    }
    for (const char* key : {"w", "h", "r", "rx", "ry"}) {
        if (!obj[key].isNull() && (!obj[key].is<int>() || obj[key].as<int>() < 0 || obj[key].as<int>() > 480)) { return false; }
    }
    if (!obj["size"].isNull() && (!obj["size"].is<int>() || obj["size"].as<int>() < 1 || obj["size"].as<int>() > 8)) { return false; }
    if (!obj["text"].isNull() && (!obj["text"].is<const char*>() || obj["text"].as<String>().length() > 512)) { return false; }
    for (const char* key : {"color", "bg"}) {
        if (obj[key].isNull()) { continue; }
        if (!obj[key].is<const char*>()) { return false; }
        const char* color = obj[key].as<const char*>();
        if (*color == '#') { ++color; }
        if (strlen(color) != 6) { return false; }
        for (size_t i = 0; i < 6; ++i) { if (!isxdigit(color[i])) { return false; } }
    }
    return true;
}

bool validateDrawing(Webserver* webserver, const char* type) {
    const String body = webserver->raw().arg("plain");
    if (body.isEmpty() && strcmp(type, "clear") == 0) { return true; }
    JsonDocument doc;
    bool valid = !deserializeJson(doc, body) && doc.is<JsonObject>();
    if (valid && strcmp(type, "batch") == 0) {
        valid = doc["commands"].is<JsonArray>() && doc["commands"].size() <= 64;
        if (valid) {
            for (JsonVariant item : doc["commands"].as<JsonArray>()) {
                if (!item.is<JsonObject>() || !validDrawCommand(item.as<JsonObject>(), item["type"] | "")) { valid = false; break; }
            }
        }
    } else if (valid) { valid = validDrawCommand(doc.as<JsonObject>(), type); }
    if (!valid) {
        webserver->raw().send(400, "application/json", "{\"status\":\"error\",\"message\":\"invalid drawing command\"}");
    }
    return valid;
}

// Default values for drawing primitives
static constexpr int16_t DEFAULT_POS = 0;
static constexpr int16_t DEFAULT_CENTER = 120;
static constexpr int16_t DEFAULT_SIZE_SMALL = 10;
static constexpr int16_t DEFAULT_SIZE_MEDIUM = 30;
static constexpr int16_t DEFAULT_SIZE_LARGE = 50;
static constexpr int16_t DEFAULT_CORNER_RADIUS = 5;
static constexpr int16_t SCREEN_SIZE = 240;
static constexpr uint8_t DEFAULT_TEXT_SIZE = 2;

// Helper to get color from JSON, returns LCD_WHITE if not present
static auto getColorFromJson(const JsonVariant& obj, const char* key = "color") -> uint16_t {
    if (obj.containsKey(key)) {
        return hexToRgb565(obj[key].as<String>());
    }
    return LCD_WHITE;
}

// Helper to send success response
static auto sendSuccessResponse(Webserver* webserver) -> void {
    JsonDocument resp;
    resp["status"] = "ok";
    String jsonOut;
    serializeJson(resp, jsonOut);
    webserver->raw().send(HTTP_CODE_OK, "application/json", jsonOut);
}

// Helper to send error response
static auto sendErrorResponse(Webserver* webserver, const char* message) -> void {
    JsonDocument resp;
    resp["status"] = "error";
    resp["message"] = message;
    String jsonOut;
    serializeJson(resp, jsonOut);
    webserver->raw().send(HTTP_CODE_INTERNAL_ERROR, "application/json", jsonOut);
}

/**
 * @brief Clear the screen
 * POST /api/v1/draw/clear
 * Body: {"color": "#000000"} (optional, defaults to black)
 */
void handleDrawClear(Webserver* webserver) {
    String body = webserver->raw().arg("plain");
    JsonDocument doc;
    uint16_t color = LCD_BLACK;

    if (body.length() > 0) {
        DeserializationError err = deserializeJson(doc, body);
        if (!err && doc.containsKey("color")) {
            color = hexToRgb565(doc["color"].as<String>());
        }
    }

    DisplayManager::getGfx()->fillScreen(color);
    sendSuccessResponse(webserver);
}

/**
 * @brief Draw text on screen
 * POST /api/v1/draw/text
 * Body: {"x": 10, "y": 10, "text": "Hello", "size": 2, "color": "#ffffff", "bg": "#000000"}
 */
void handleDrawText(Webserver* webserver) {
    String body = webserver->raw().arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        sendErrorResponse(webserver, "invalid json");
        return;
    }

    int16_t posX = doc["x"] | DEFAULT_POS;
    int16_t posY = doc["y"] | DEFAULT_POS;
    String text = doc["text"] | "";
    uint8_t textSize = doc["size"] | DEFAULT_TEXT_SIZE;
    uint16_t fgColor = getColorFromJson(doc);
    uint16_t bgColor = LCD_BLACK;

    if (doc.containsKey("bg")) {
        bgColor = hexToRgb565(doc["bg"].as<String>());
    }

    bool clearBg = doc["clear"] | false;
    DisplayManager::drawTextWrapped(posX, posY, text, textSize, fgColor, bgColor, clearBg);
    sendSuccessResponse(webserver);
}

/**
 * @brief Draw rectangle
 * POST /api/v1/draw/rect
 * Body: {"x": 10, "y": 10, "w": 50, "h": 50, "color": "#ff0000", "fill": true}
 */
void handleDrawRect(Webserver* webserver) {
    String body = webserver->raw().arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        sendErrorResponse(webserver, "invalid json");
        return;
    }

    int16_t posX = doc["x"] | DEFAULT_POS;
    int16_t posY = doc["y"] | DEFAULT_POS;
    int16_t width = doc["w"] | DEFAULT_SIZE_SMALL;
    int16_t height = doc["h"] | DEFAULT_SIZE_SMALL;
    bool shouldFill = doc["fill"] | true;
    uint16_t color = getColorFromJson(doc);

    if (shouldFill) {
        DisplayManager::getGfx()->fillRect(posX, posY, width, height, color);
    } else {
        DisplayManager::getGfx()->drawRect(posX, posY, width, height, color);
    }
    sendSuccessResponse(webserver);
}

/**
 * @brief Draw circle
 * POST /api/v1/draw/circle
 * Body: {"x": 120, "y": 120, "r": 50, "color": "#00ff00", "fill": true}
 */
void handleDrawCircle(Webserver* webserver) {
    String body = webserver->raw().arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        sendErrorResponse(webserver, "invalid json");
        return;
    }

    int16_t posX = doc["x"] | DEFAULT_CENTER;
    int16_t posY = doc["y"] | DEFAULT_CENTER;
    int16_t radius = doc["r"] | DEFAULT_SIZE_LARGE;
    bool shouldFill = doc["fill"] | true;
    uint16_t color = getColorFromJson(doc);

    if (shouldFill) {
        DisplayManager::getGfx()->fillCircle(posX, posY, radius, color);
    } else {
        DisplayManager::getGfx()->drawCircle(posX, posY, radius, color);
    }
    sendSuccessResponse(webserver);
}

/**
 * @brief Draw line
 * POST /api/v1/draw/line
 * Body: {"x0": 0, "y0": 0, "x1": 240, "y1": 240, "color": "#0000ff"}
 */
void handleDrawLine(Webserver* webserver) {
    String body = webserver->raw().arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        sendErrorResponse(webserver, "invalid json");
        return;
    }

    int16_t startX = doc["x0"] | DEFAULT_POS;
    int16_t startY = doc["y0"] | DEFAULT_POS;
    int16_t endX = doc["x1"] | SCREEN_SIZE;
    int16_t endY = doc["y1"] | SCREEN_SIZE;
    uint16_t color = getColorFromJson(doc);

    DisplayManager::getGfx()->drawLine(startX, startY, endX, endY, color);
    sendSuccessResponse(webserver);
}

/**
 * @brief Draw pixel
 * POST /api/v1/draw/pixel
 * Body: {"x": 120, "y": 120, "color": "#ffffff"}
 */
void handleDrawPixel(Webserver* webserver) {
    String body = webserver->raw().arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        sendErrorResponse(webserver, "invalid json");
        return;
    }

    int16_t posX = doc["x"] | DEFAULT_POS;
    int16_t posY = doc["y"] | DEFAULT_POS;
    uint16_t color = getColorFromJson(doc);

    DisplayManager::getGfx()->drawPixel(posX, posY, color);
    sendSuccessResponse(webserver);
}

/**
 * @brief Draw triangle
 * POST /api/v1/draw/triangle
 * Body: {"x0": 100, "y0": 50, "x1": 50, "y1": 150, "x2": 150, "y2": 150, "color": "#ff0000", "fill": true}
 */
void handleDrawTriangle(Webserver* webserver) {
    String body = webserver->raw().arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        sendErrorResponse(webserver, "invalid json");
        return;
    }

    int16_t vertX0 = doc["x0"] | DEFAULT_POS;
    int16_t vertY0 = doc["y0"] | DEFAULT_POS;
    int16_t vertX1 = doc["x1"] | DEFAULT_POS;
    int16_t vertY1 = doc["y1"] | DEFAULT_POS;
    int16_t vertX2 = doc["x2"] | DEFAULT_POS;
    int16_t vertY2 = doc["y2"] | DEFAULT_POS;
    bool shouldFill = doc["fill"] | true;
    uint16_t color = getColorFromJson(doc);

    if (shouldFill) {
        DisplayManager::getGfx()->fillTriangle(vertX0, vertY0, vertX1, vertY1, vertX2, vertY2, color);
    } else {
        DisplayManager::getGfx()->drawTriangle(vertX0, vertY0, vertX1, vertY1, vertX2, vertY2, color);
    }
    sendSuccessResponse(webserver);
}

/**
 * @brief Draw ellipse
 * POST /api/v1/draw/ellipse
 * Body: {"x": 120, "y": 120, "rx": 50, "ry": 30, "color": "#00ff00", "fill": true}
 */
void handleDrawEllipse(Webserver* webserver) {
    String body = webserver->raw().arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        sendErrorResponse(webserver, "invalid json");
        return;
    }

    int16_t posX = doc["x"] | DEFAULT_CENTER;
    int16_t posY = doc["y"] | DEFAULT_CENTER;
    int16_t radiusX = doc["rx"] | DEFAULT_SIZE_LARGE;
    int16_t radiusY = doc["ry"] | DEFAULT_SIZE_MEDIUM;
    bool shouldFill = doc["fill"] | true;
    uint16_t color = getColorFromJson(doc);

    if (shouldFill) {
        DisplayManager::getGfx()->fillEllipse(posX, posY, radiusX, radiusY, color);
    } else {
        DisplayManager::getGfx()->drawEllipse(posX, posY, radiusX, radiusY, color);
    }
    sendSuccessResponse(webserver);
}

/**
 * @brief Draw rounded rectangle
 * POST /api/v1/draw/roundrect
 * Body: {"x": 10, "y": 10, "w": 100, "h": 50, "r": 10, "color": "#0000ff", "fill": true}
 */
void handleDrawRoundRect(Webserver* webserver) {
    String body = webserver->raw().arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        sendErrorResponse(webserver, "invalid json");
        return;
    }

    int16_t posX = doc["x"] | DEFAULT_POS;
    int16_t posY = doc["y"] | DEFAULT_POS;
    int16_t width = doc["w"] | DEFAULT_SIZE_LARGE;
    int16_t height = doc["h"] | DEFAULT_SIZE_MEDIUM;
    int16_t radius = doc["r"] | DEFAULT_CORNER_RADIUS;
    bool shouldFill = doc["fill"] | true;
    uint16_t color = getColorFromJson(doc);

    if (shouldFill) {
        DisplayManager::getGfx()->fillRoundRect(posX, posY, width, height, radius, color);
    } else {
        DisplayManager::getGfx()->drawRoundRect(posX, posY, width, height, radius, color);
    }
    sendSuccessResponse(webserver);
}

// Helper to safely get int16_t from JSON with default
static auto getInt16(const JsonObject& obj, const char* key, int16_t defaultVal) -> int16_t {
    return obj.containsKey(key) ? static_cast<int16_t>(obj[key].as<int>()) : defaultVal;
}

// Helper to safely get bool from JSON with default
static auto getBool(const JsonObject& obj, const char* key, bool defaultVal) -> bool {
    return obj.containsKey(key) ? (obj[key].as<int>() != 0) : defaultVal;
}

// Helper functions for batch processing to reduce cognitive complexity
static auto processBatchRect(const JsonObject& cmd, uint16_t color) -> void {
    int16_t posX = getInt16(cmd, "x", DEFAULT_POS);
    int16_t posY = getInt16(cmd, "y", DEFAULT_POS);
    int16_t width = getInt16(cmd, "w", DEFAULT_SIZE_SMALL);
    int16_t height = getInt16(cmd, "h", DEFAULT_SIZE_SMALL);
    bool shouldFill = getBool(cmd, "fill", true);
    if (shouldFill) {
        DisplayManager::getGfx()->fillRect(posX, posY, width, height, color);
    } else {
        DisplayManager::getGfx()->drawRect(posX, posY, width, height, color);
    }
}

static auto processBatchCircle(const JsonObject& cmd, uint16_t color) -> void {
    int16_t posX = getInt16(cmd, "x", DEFAULT_CENTER);
    int16_t posY = getInt16(cmd, "y", DEFAULT_CENTER);
    int16_t radius = getInt16(cmd, "r", DEFAULT_SIZE_LARGE);
    bool shouldFill = getBool(cmd, "fill", true);
    if (shouldFill) {
        DisplayManager::getGfx()->fillCircle(posX, posY, radius, color);
    } else {
        DisplayManager::getGfx()->drawCircle(posX, posY, radius, color);
    }
}

static auto processBatchLine(const JsonObject& cmd, uint16_t color) -> void {
    int16_t startX = getInt16(cmd, "x0", DEFAULT_POS);
    int16_t startY = getInt16(cmd, "y0", DEFAULT_POS);
    int16_t endX = getInt16(cmd, "x1", SCREEN_SIZE);
    int16_t endY = getInt16(cmd, "y1", SCREEN_SIZE);
    DisplayManager::getGfx()->drawLine(startX, startY, endX, endY, color);
}

static auto processBatchPixel(const JsonObject& cmd, uint16_t color) -> void {
    int16_t posX = getInt16(cmd, "x", DEFAULT_POS);
    int16_t posY = getInt16(cmd, "y", DEFAULT_POS);
    DisplayManager::getGfx()->drawPixel(posX, posY, color);
}

static auto processBatchText(const JsonObject& cmd, uint16_t color) -> void {
    int16_t posX = getInt16(cmd, "x", DEFAULT_POS);
    int16_t posY = getInt16(cmd, "y", DEFAULT_POS);
    String text = cmd.containsKey("text") ? cmd["text"].as<String>() : "";
    uint8_t textSize = cmd.containsKey("size") ? static_cast<uint8_t>(cmd["size"].as<int>()) : DEFAULT_TEXT_SIZE;
    uint16_t bgColor = LCD_BLACK;
    if (cmd.containsKey("bg")) {
        bgColor = hexToRgb565(cmd["bg"].as<String>());
    }
    bool clearBg = getBool(cmd, "clear", false);
    DisplayManager::drawTextWrapped(posX, posY, text, textSize, color, bgColor, clearBg);
}

static auto processBatchTriangle(const JsonObject& cmd, uint16_t color) -> void {
    int16_t vertX0 = getInt16(cmd, "x0", DEFAULT_POS);
    int16_t vertY0 = getInt16(cmd, "y0", DEFAULT_POS);
    int16_t vertX1 = getInt16(cmd, "x1", DEFAULT_POS);
    int16_t vertY1 = getInt16(cmd, "y1", DEFAULT_POS);
    int16_t vertX2 = getInt16(cmd, "x2", DEFAULT_POS);
    int16_t vertY2 = getInt16(cmd, "y2", DEFAULT_POS);
    bool shouldFill = getBool(cmd, "fill", true);
    if (shouldFill) {
        DisplayManager::getGfx()->fillTriangle(vertX0, vertY0, vertX1, vertY1, vertX2, vertY2, color);
    } else {
        DisplayManager::getGfx()->drawTriangle(vertX0, vertY0, vertX1, vertY1, vertX2, vertY2, color);
    }
}

static auto processBatchEllipse(const JsonObject& cmd, uint16_t color) -> void {
    int16_t posX = getInt16(cmd, "x", DEFAULT_CENTER);
    int16_t posY = getInt16(cmd, "y", DEFAULT_CENTER);
    int16_t radiusX = getInt16(cmd, "rx", DEFAULT_SIZE_LARGE);
    int16_t radiusY = getInt16(cmd, "ry", DEFAULT_SIZE_MEDIUM);
    bool shouldFill = getBool(cmd, "fill", true);
    if (shouldFill) {
        DisplayManager::getGfx()->fillEllipse(posX, posY, radiusX, radiusY, color);
    } else {
        DisplayManager::getGfx()->drawEllipse(posX, posY, radiusX, radiusY, color);
    }
}

static auto processBatchRoundRect(const JsonObject& cmd, uint16_t color) -> void {
    int16_t posX = getInt16(cmd, "x", DEFAULT_POS);
    int16_t posY = getInt16(cmd, "y", DEFAULT_POS);
    int16_t width = getInt16(cmd, "w", DEFAULT_SIZE_LARGE);
    int16_t height = getInt16(cmd, "h", DEFAULT_SIZE_MEDIUM);
    int16_t radius = getInt16(cmd, "r", DEFAULT_CORNER_RADIUS);
    bool shouldFill = getBool(cmd, "fill", true);
    if (shouldFill) {
        DisplayManager::getGfx()->fillRoundRect(posX, posY, width, height, radius, color);
    } else {
        DisplayManager::getGfx()->drawRoundRect(posX, posY, width, height, radius, color);
    }
}

/**
 * @brief Draw multiple primitives in one request (batch)
 * POST /api/v1/draw/batch
 * Body: {"commands": [
 *   {"type": "clear", "color": "#000000"},
 *   {"type": "rect", "x": 10, "y": 10, "w": 50, "h": 50, "color": "#ff0000", "fill": true},
 *   {"type": "text", "x": 70, "y": 100, "text": "Hello", "size": 2, "color": "#ffffff"},
 *   {"type": "circle", "x": 180, "y": 60, "r": 30, "color": "#00ff00", "fill": true},
 *   {"type": "line", "x0": 0, "y0": 0, "x1": 240, "y1": 240, "color": "#0000ff"}
 * ]}
 */
void handleDrawBatch(Webserver* webserver) {
    String body = webserver->raw().arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        sendErrorResponse(webserver, "invalid json");
        return;
    }

    JsonArray commands = doc["commands"].as<JsonArray>();
    int processed = 0;

    for (JsonObject cmd : commands) {
        String cmdType = cmd.containsKey("type") ? cmd["type"].as<String>() : "";
        uint16_t color = getColorFromJson(cmd);

        if (cmdType == "clear") {
            DisplayManager::getGfx()->fillScreen(color);
        } else if (cmdType == "rect") {
            processBatchRect(cmd, color);
        } else if (cmdType == "circle") {
            processBatchCircle(cmd, color);
        } else if (cmdType == "line") {
            processBatchLine(cmd, color);
        } else if (cmdType == "pixel") {
            processBatchPixel(cmd, color);
        } else if (cmdType == "text") {
            processBatchText(cmd, color);
        } else if (cmdType == "triangle") {
            processBatchTriangle(cmd, color);
        } else if (cmdType == "ellipse") {
            processBatchEllipse(cmd, color);
        } else if (cmdType == "roundrect") {
            processBatchRoundRect(cmd, color);
        }

        processed++;
        yield();  // Allow other tasks to run between commands
    }

    JsonDocument resp;
    resp["status"] = "ok";
    resp["processed"] = processed;

    String jsonOut;
    serializeJson(resp, jsonOut);
    webserver->raw().send(HTTP_CODE_OK, "application/json", jsonOut);
}