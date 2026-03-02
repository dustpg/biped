#pragma once
#include <cstdint>
#include <Windows.h>
#include "font.h"
#include "textlist.h"
#include "../biped.h"

struct TRectL { long l, t, r, b; };
struct TPointL { long x, y; };
struct TRectU { unsigned long l, t, r, b; };

struct TEXT_2D_RECT {
    TRectL des;
    TRectL src;
    uint32_t color;
};

constexpr uint32_t SUB_PAGE_COUNT = 4;
constexpr uint32_t WINDOW_WIDTH = 1280;
constexpr uint32_t WINDOW_HEIGHT = 720;
constexpr uint32_t SIDE_SIZE = 256;
constexpr uint32_t SUB_PAGE_SIZE = 128;


struct LockRectU {
    TRectU rect;
    uint32_t width;
    uint32_t height;
    uint32_t page;
    uint32_t pitch;
};


struct TextGraphcs {
    virtual ~TextGraphcs() noexcept = default;
    virtual bool Init(HWND hwnd) noexcept = 0;
    virtual void BeginRender(uint32_t clearcolor) noexcept = 0;
    virtual void EndRender(uint32_t sync) noexcept = 0;
    virtual void RefreshTexture(const LockRectU& rect, const void* data) noexcept = 0;
    virtual bool AddRect(const TEXT_2D_RECT&) noexcept = 0;
    virtual void DrawAdded() noexcept = 0;
};



struct BAContext {
    alignas(uint32_t) FontAPIText key;
    alignas(uint32_t) FontAPIInfo info;
};

enum {
    DISPLAY_MODE_NORMAL =0,
    DISPLAY_MODE_CACHE
};

class TextAppWin {
private:
    HWND                                        m_hwnd = nullptr;
    TextGraphcs*                                m_pGraphcs = nullptr;
    uint32_t                                    m_uFrameCounterAll = 0;
    uint32_t                                    m_uFrameCounterFPS = 0;
    uint32_t                            const   m_cFrameCountStep = 20;
    uint32_t                                    m_uFrameTickLast = 0;
    DWORD                                       m_dwDisplayMode = DISPLAY_MODE_NORMAL;
    DWORD                                       m_dwModeCounter = 0;
    DWORD                                       m_dwCacheCounter = 0;
    float                                       m_fCacheEaseX = 0;
    float                                       m_fCacheStart = 0;
    float                                       m_fCacheEnd = 0;
    float                                       m_fCacheValue = 0;
    float                                       m_fFps = 1.f;
    float                                       m_fRandPro = 0.f;
    float                               const   m_fRandEnd = 0.233f;
    FontAPI                                     m_font;
    biped_cache_ctx_t                           m_cache;
    uint32_t                                    m_uRanStrLen = 0;
    char16_t                                    m_szRandBuf[24];
    uint32_t                                    m_dbgCounter = 0;
private:
    static LRESULT CALLBACK ThisWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
    bool InitWindow() noexcept;
    bool InitFont() noexcept;
    bool InitGraphcs() noexcept;
private:
    void DrawSimpleText(const SimpleText a[], uint32_t count, TPointL offset) noexcept;
    void DrawTextU16(const SimpleText& format, const char16_t* utf16, TPointL offset) noexcept;
    void DrawFrameInfo(float delta) noexcept;
    void DrawCacheInfo(float delta) noexcept;
    void DrawNormal() noexcept;
    void DrawRandomChar(float delta, TPointL) noexcept;
private:
    void SetMode(DWORD) noexcept;
public:
    TextAppWin() noexcept;
    ~TextAppWin() noexcept;
    bool Init() noexcept;
    void DoRender(uint32_t sync, float delta) noexcept;
};