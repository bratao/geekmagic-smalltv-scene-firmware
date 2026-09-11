#pragma once
#include <cstddef>
#include <cstdint>

namespace scene {
constexpr int Width = 240, Height = 240, BandHeight = 8;
constexpr int MaxNodes = 32, TextCapacity = 1536;
enum class Kind : uint8_t { Text, Clock, Rect, Line, Pixel, Circle, Ellipse, Triangle, RoundRect };
enum class FontId : uint8_t { Small, Meta, Heading, Title, Clock };
struct Node {
    int16_t x = 0, y = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    int16_t w = 220, h = 0, r = 0, ry = 0;
    uint16_t color = 0xffff, textOffset = 0, textLength = 0, period = 4000;
    Kind kind = Kind::Text;
    FontId font = FontId::Small;
    uint8_t low = 128, high = 240;
    bool fill = true, pulse = false;
};
struct Scene {
    Node nodes[MaxNodes];
    char text[TextCapacity] = {};
    uint16_t background = 0, textUsed = 0;
    uint8_t count = 0;
};
struct Frame { uint16_t minuteOfDay; uint32_t tickMs; };
struct Glyph {
    uint16_t offset;
    uint16_t code;
    uint8_t w, h, advance;
    int8_t x, y;
};
struct Font {
    const uint8_t* data;
    const Glyph* glyphs;
    uint16_t count;
    uint8_t baseline, height;
};
const Font& font(FontId id);
uint8_t pulseLevel(const Node& node, uint32_t tickMs);
bool intersectsBand(const Node& node, int bandY);
void renderBand(const Scene& scene, const Frame& frame, int bandY, uint16_t* pixels);
uint32_t rowHash(const uint16_t* pixels);
} // namespace scene
