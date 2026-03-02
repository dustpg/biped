#pragma once


#include <vector>
#include <cstdint>

struct FontAPIText
{
    uint32_t code;    // unicode id
    uint16_t face;    // face index
    uint16_t size;    // x16 pixel size [240 = 15.0] (f12.4)
};


struct FontAPIInfo
{
    int16_t         offsetx;
    int16_t         offsety;
    uint16_t        boundw;
    uint16_t        boundh;
};

struct FontAPITextEx
{
    uint32_t code_style;    // unicode id in low 24-bit, style in high 8-bit
    uint16_t face_control;  // face index in low 10-bit, control in high 6-bit
    uint16_t size_x16;      // x16 pixel size [240 = 15.0]
};

constexpr uint16_t ts_float_to_fixed(float v) noexcept {
    return uint16_t(v * 16.f);
}


class FontAPI;
class FontAPIFace {
    friend FontAPI;
    void* m_pContext = nullptr;
public:
    FontAPIFace(void*) noexcept;
    ~FontAPIFace() noexcept;
    FontAPIFace(const FontAPIFace&) = delete;
    FontAPIFace(FontAPIFace&& other) noexcept { std::swap(m_pContext, other.m_pContext); }
};

struct TextBitmapData {
    const uint8_t*  data;
    uint16_t        width;
    uint16_t        height;
    FontAPIInfo     info;
};


class FontAPI {
    void* m_pContext = nullptr;
public:
    static int MakeSysFont(char path[], const char* name) noexcept;
    FontAPI() noexcept;
    ~FontAPI() noexcept;
    // return index
    int AddFace(const char* name) noexcept;
    // return pitch
    int MakeText(FontAPIText text, TextBitmapData& data) noexcept;
private:
    std::vector<FontAPIFace>    m_faces;
};