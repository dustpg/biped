#define _CRT_SECURE_NO_WARNINGS
#include "font.h"
#include <cstdio>
#include <cassert>
#include <algorithm>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <Shlobj.h>

FontAPI::FontAPI() noexcept
{
    static_assert(sizeof(m_pContext) == sizeof(FT_Library), "bad context");
    auto& ctx = reinterpret_cast<FT_Library&>(m_pContext);
    const auto code = ::FT_Init_FreeType(&ctx);
    assert(code == 0 && "error handle");
}


FontAPI::~FontAPI() noexcept
{
    m_faces.clear();
    static_assert(sizeof(m_pContext) == sizeof(FT_Library), "bad context");
    auto& ctx = reinterpret_cast<FT_Library&>(m_pContext);
    ::FT_Done_FreeType(ctx);
}


FontAPIFace::~FontAPIFace() noexcept
{
    static_assert(sizeof(m_pContext) == sizeof(FT_Face), "bad context");
    auto& ctx = reinterpret_cast<FT_Face&>(m_pContext);
    ::FT_Done_Face(ctx);
    m_pContext = nullptr;
}


FontAPIFace::FontAPIFace(void* ctx) noexcept : m_pContext(ctx)
{
}


int FontAPI::AddFace(const char* name) noexcept
{
    char buffer[MAX_PATH * 2];
    if (const auto code = this->MakeSysFont(buffer, name))
        return code;

    FT_Face face;
    auto& ctx = reinterpret_cast<FT_Library&>(m_pContext);
    const auto code = ::FT_New_Face(ctx, buffer, 0, &face);
    if (code) return -2;

    try {
        const int index = m_faces.size();
        m_faces.emplace_back(face);
        return index;
    }
    catch (...) {
        return -1;
    }
}


int FontAPI::MakeSysFont(char path[], const char* name) noexcept
{
    const auto len = std::strlen(name);
    if (len >= MAX_PATH - 10) return -4;

    const auto hr = ::SHGetFolderPathA(nullptr, CSIDL_FONTS, nullptr, SHGFP_TYPE_DEFAULT, path);
    if (FAILED(hr)) return -3;

    const auto ptr = path + std::strlen(path);
    ptr[0] = '/';
    std::memcpy(ptr + 1, name, len + 1);

    return 0;
}

int FontAPI::MakeText(FontAPIText text, TextBitmapData& data) noexcept
{
    const auto id = text.face;
    if (id >= m_faces.size()) return 0;
    auto face = reinterpret_cast<FT_Face>(m_faces[id].m_pContext);
    auto index = ::FT_Get_Char_Index(face, text.code);

    // eazy fallback
    const auto eazy_fallback = [this](FT_Face base, uint32_t code) noexcept {
        std::pair<FT_Face, FT_UInt> rv = { base , 0 };
        for (auto& ele : m_faces) {
            const auto face = reinterpret_cast<FT_Face>(ele.m_pContext);
            const auto id = ::FT_Get_Char_Index(face, code);
            if (id) return std::pair<FT_Face, FT_UInt>{ face, id };
        }
        return rv;
    };

    if (!index) {
        const auto rv = eazy_fallback(face, text.code);
        face = rv.first;
        index = rv.second;
    }


    FT_Size_RequestRec  req;
    req.type = FT_SIZE_REQUEST_TYPE_NOMINAL;

    // x16 in FontAPIText, x64 in FT2
    const FT_UInt size = text.size << (6 - 4);
    req.width = size;
    req.height = size;
    req.horiResolution = 0;
    req.vertResolution = 0;
    auto code =  ::FT_Request_Size(face, &req);
    if (code) {
        return 0;
    }

    code = ::FT_Load_Glyph(face, index, FT_LOAD_DEFAULT);
    if (code) {
        return 0;
    }

    code = ::FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
    if (code) {
        return 0;
    }

    const auto slot = face->glyph;
    const auto& bmp = slot->bitmap;
    data.width = bmp.width;
    data.height = bmp.rows;
    data.data = bmp.buffer;
    data.info.offsetx = slot->bitmap_left;
    data.info.offsety = slot->bitmap_top;

    //constexpr FT_Pos roundValue = 0;
    //constexpr FT_Pos roundValue = 1 << 5;
    constexpr FT_Pos roundValue = 1 << 6 - 1;

    data.info.boundw = uint16_t((slot->advance.x + roundValue) >> 6);
    data.info.boundh = uint16_t((slot->advance.y + roundValue) >> 6);
#ifndef NDEBUG
    if (this == nullptr) {
        if (const auto file = std::fopen("dump.data", "wb")) {
            std::fwrite(data.data, sizeof(uint8_t), bmp.pitch * bmp.rows, file);
            std::fclose(file);
        }
    }
    if (this == nullptr) {
        const auto yy = bmp.rows;
        const auto xx = bmp.width;
        const auto pp = bmp.pitch;
        auto itr = bmp.buffer;
        
        std::printf("U+%08X:\n", text.code);
        
        for (unsigned int y = 0; y != yy; ++y) {
            for (unsigned int x = 0; x != xx; ++x) {
                const auto code = itr[x] / 26;
                const auto code2 = code ? code + '0' : ' ';
                if (code2 >= 32 && code2 < 127) {
                    std::putchar(code2);
                } else {
                    std::putchar('?');
                }
            }
            std::putchar('\n');
            itr += pp;
        }
    }
#endif
    return std::max(bmp.pitch, 1);
}