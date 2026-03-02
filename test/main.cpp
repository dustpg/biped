#include "../biped.h"

#include "app.h"

#include <cassert>
#include <timeapi.h>

static const wchar_t WINDOW_TITLE[] = L"Text Renderer";
#include <chrono>
#include <algorithm>
#include <random>
#include <vector>


int main() {
    // DPIAware
    ::SetProcessDPIAware();
    TextAppWin app;
    if (!app.Init()) return -1;

    {
        auto tick = std::chrono::high_resolution_clock::now();
        MSG msg = { 0 };
        while (msg.message != WM_QUIT) {
            if (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
            else {
                const auto now = std::chrono::high_resolution_clock::now();
                const auto dd = std::chrono::duration<double>(now - tick);
                const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(dd);
                const auto delta = double(microseconds.count()) * 1e-6;
                tick = now;
                app.DoRender(1, float(delta));
            }
        }
    }
    return 0;
}




LRESULT TextAppWin::ThisWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    const auto pThis = reinterpret_cast<TextAppWin*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg)
    {
        LPCREATESTRUCT pcs;
    case WM_CREATE:
        pcs = reinterpret_cast<LPCREATESTRUCT>(lParam);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, LONG_PTR(pcs->lpCreateParams));
        return 1;
    case WM_CLOSE:
        ::DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    case WM_KEYUP:
        switch (wParam)
        {
        case 'C':
            pThis->SetMode(DISPLAY_MODE_CACHE);
            break;
        case 'N':
            pThis->SetMode(DISPLAY_MODE_NORMAL);
            break;
        }
        return 0;
    case WM_CHAR:
        return 0;
    case WM_SIZE:
        // TODO: resize?
        break;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}


bool TextAppWin::Init() noexcept
{
    if (!this->InitWindow()) return false;
    ::ShowWindow(m_hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(m_hwnd);
    return this->InitFont() && this->InitGraphcs();
}

bool TextAppWin::InitWindow() noexcept
{
    const auto hInstance = ::GetModuleHandleA(nullptr);
    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = ThisWndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = sizeof(LONG_PTR);
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = nullptr;
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = L"Dust.Biped.Window.Normal";
    wcex.hIcon = nullptr;
    ::RegisterClassExW(&wcex);


    RECT window_rect = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
    DWORD window_style = WS_OVERLAPPEDWINDOW;
    ::AdjustWindowRect(&window_rect, window_style, FALSE);
    window_rect.right -= window_rect.left;
    window_rect.bottom -= window_rect.top;
    window_rect.left = (::GetSystemMetrics(SM_CXFULLSCREEN) - window_rect.right) / 2;
    window_rect.top = (::GetSystemMetrics(SM_CYFULLSCREEN) - window_rect.bottom) / 2;


    m_hwnd = ::CreateWindowExW(
        0,
        wcex.lpszClassName, WINDOW_TITLE, window_style,
        window_rect.left, window_rect.top, window_rect.right, window_rect.bottom,
        0, 0, hInstance, this
    );
    return !!m_hwnd;
}


bool TextAppWin::InitFont() noexcept
{
    const auto nArial = m_font.AddFace("Arial.ttf");
    const auto nCourierNew = m_font.AddFace("Cour.ttf");
    const auto nMicrosoftYaHei = m_font.AddFace("Msyh.ttc");
    // for fallback
    const auto nSimSun_ExtB = m_font.AddFace("Simsunb.ttf");
    
    return nArial >= 0 && nCourierNew >= 0 && nMicrosoftYaHei >= 0;
}

TextAppWin::TextAppWin() noexcept :
    m_cache(biped_cache_create(SIDE_SIZE, sizeof(BAContext), sizeof(FontAPIText)))
{
    m_uFrameTickLast = ::timeGetTime();
    m_szRandBuf[0] = 0;
}

TextAppWin::~TextAppWin() noexcept
{
    biped_cache_dispose(m_cache);
    m_cache = nullptr;
    delete m_pGraphcs;
}


namespace impl {
    auto ease_out_cubic(float x) noexcept {
        const float x_1 = 1.f - x;
        return 1.f - x_1 * x_1 * x_1;
    };
    auto lerp(float a, float b, float x) noexcept {
        return a + (b - a) * x;
    };
    auto rand() noexcept {
        static int seed = 0;
        return ++seed;
        //return std::rand();
    }
    char16_t rulg() noexcept {
        return 'A' + (impl::rand() % 26);
    }
    char16_t rllg() noexcept {
        return 'a' + (impl::rand() % 26);
    }
    char16_t rdng() noexcept {
        return '0' + (impl::rand() % 10);
    }
    char16_t rccg() noexcept {
        return 0x4e00 + (impl::rand() % 100);
    }
    char16_t rkcg() noexcept {
        return 0xAC00 + (impl::rand() % 50);
    }
    char16_t rjkg() noexcept {
        return 0x3041 + (impl::rand() % 46);
    }
    char16_t reeg() noexcept {
        return 0x00A0 + (impl::rand() % 50);
    }
    void morton_decode(uint32_t morton, uint32_t& x, uint32_t& y) noexcept {
        x = 0;
        y = 0;
        uint32_t bit_pos = 0;
        while (morton >> bit_pos) {
            if (bit_pos & 1) {
                x |= ((morton >> bit_pos) & 1) << (bit_pos >> 1);
            } else {
                y |= ((morton >> bit_pos) & 1) << (bit_pos >> 1);
            }
            ++bit_pos;
        }
    }
}



void TextAppWin::DrawRandomChar(float delta, TPointL offset) noexcept
{
    const wchar_t ch = 0x4e00;
    const auto rand_char = []() noexcept ->char16_t {
        switch (std::rand() % 7)
        {
        case 0: return impl::rulg();
        case 1: return impl::rllg();
        case 2: return impl::rdng();
        case 3: return impl::rccg();
        case 4: return impl::rkcg();
        case 5: return impl::rjkg();
        case 6: return impl::reeg();
        }
        return ' ';
    };
    m_fRandPro += delta;
    if (m_fRandPro >= m_fRandEnd) {
        m_fRandPro -= m_fRandEnd;
        m_uRanStrLen++;
        if (m_uRanStrLen >= ARRAYSIZE(m_szRandBuf) - 1) {
            m_uRanStrLen = 0;
        }
    }
    m_szRandBuf[m_uRanStrLen] = rand_char();
    m_szRandBuf[m_uRanStrLen + 1] = 0;

    SimpleText fmt = {};
    fmt.text.size = ts_float_to_fixed(15.9f);
    fmt.color = 0xff66ccff;


    this->DrawTextU16(fmt, m_szRandBuf, offset);
}

void TextAppWin::DrawCacheInfo(float delta) noexcept
{
    this->DrawRandomChar(delta, { 64, 512 });
    TPointL offset = { SUB_PAGE_SIZE, 64 };
    constexpr float time = 0.3f;

    float time_x = 0.;

    if (m_fCacheEaseX > 0) {
        m_fCacheEaseX += delta;
        time_x = m_fCacheEaseX / time;
        if (time_x >= 1) {
            m_fCacheEaseX = 0;
            time_x = 0;
            m_fCacheValue = m_fCacheEnd;
        }
        else 
            m_fCacheValue = impl::lerp(m_fCacheStart, m_fCacheEnd, impl::ease_out_cubic(time_x));
    }

    if (m_dwCacheCounter != m_dwModeCounter) {
        m_fCacheStart = float((m_dwCacheCounter % SUB_PAGE_COUNT) * SUB_PAGE_SIZE);
        m_dwCacheCounter = m_dwModeCounter;
        m_fCacheEnd = float((m_dwCacheCounter % SUB_PAGE_COUNT) * SUB_PAGE_SIZE);
        m_fCacheEaseX += delta;
        m_fCacheValue = m_fCacheStart;
    }

    offset.x -= long(m_fCacheValue);
    offset.y -= long(m_fCacheValue);

    TEXT_2D_RECT rect;

    rect.des = {
        offset.x,
        offset.y ,
        offset.x + long(SUB_PAGE_SIZE),
        offset.y + long(SUB_PAGE_SIZE)
    };
    rect.src = { 0, 0 , long(SUB_PAGE_SIZE), long(SUB_PAGE_SIZE) };
    rect.color = 0xff000000;


    for (uint32_t i = 0; i != SUB_PAGE_COUNT; ++i) {
        uint32_t x, y;
        impl::morton_decode(i, x, y);
        
        rect.src = {
            long(x * SUB_PAGE_SIZE),
            long(y * SUB_PAGE_SIZE),
            long((x + 1) * SUB_PAGE_SIZE),
            long((y + 1) * SUB_PAGE_SIZE)
        };
        m_pGraphcs->AddRect(rect);
        rect.des.l += long(SUB_PAGE_SIZE);
        rect.des.t += long(SUB_PAGE_SIZE);
        rect.des.r += long(SUB_PAGE_SIZE);
        rect.des.b += long(SUB_PAGE_SIZE);
    }
    m_pGraphcs->DrawAdded();
}

void TextAppWin::DrawNormal() noexcept
{
    this->DrawSimpleText(TestTextA, ARRAYSIZE(TestTextA), { 128, 128 });
    {
        SimpleText format = {};
        format.text.face = 2;
        format.text.size = ts_float_to_fixed(14.1f);
        format.color = 0xff000000;
        this->DrawTextU16(format, u"'𤭢' is U+24B62", { 16, 256 });
    }
    {
        SimpleText format = {};
        format.text.face = 2;
        format.text.size = ts_float_to_fixed(16.1f);
        format.color = 0xff000000;
        char16_t buffer[256];
        for (char16_t i = 0; i != 128; ++i)
            buffer[i] = 127 - i;
        this->DrawTextU16(format, buffer, { 16, 300 });
    }
}

void TextAppWin::DoRender(uint32_t sync, float delta) noexcept
{
    m_dbgCounter = 0;
    m_pGraphcs->BeginRender(0xffffffff);
    this->DrawFrameInfo(delta);
    switch (m_dwDisplayMode)
    {
    case DISPLAY_MODE_NORMAL:
        this->DrawNormal();
        break;
    case DISPLAY_MODE_CACHE:
        this->DrawCacheInfo(delta);
        break;
    }

    biped_cache_force_unlock(m_cache);
    m_pGraphcs->DrawAdded();

    m_pGraphcs->EndRender(sync);
    //m_cache.debug_check();
}



void TextAppWin::DrawSimpleText(const SimpleText a[], uint32_t count, TPointL offset) noexcept
{
    TextBitmapData data;
    biped_block_info_t info;
    BAContext context;
    for (uint32_t i = 0; i != count; ++i) {
        auto& obj = a[i];
        ++m_dbgCounter;
        info.value = nullptr;
        biped_result_t result = biped_cache_lock_key(m_cache, obj.key(), &info);
        if (!biped_is_success(result)) {
            if (const auto pitch = m_font.MakeText(obj.text, data)) {
#ifndef NDEBUG
                const int ch = int(obj.text.code);
                if (ch >= 32 && ch < 127 && ch != 7) {
                    std::printf("ft2: load font: %4x(%c)@size=%3d,family=%3d\n", ch, char(ch), int(obj.text.size), int(obj.text.face));
                } 
                else {
                    std::printf("ft2: load font: %4x(?)@size=%3d,family=%3d\n", ch, int(obj.text.size), int(obj.text.face));
                }
#endif
                context.key = obj.text;
                context.info = data.info;
                for (int j = 0; j < 2; ++j) {
                    biped_size2d_t size = { data.width, data.height };
                    result = biped_cache_lock_key_value(m_cache, size, reinterpret_cast<const uint32_t*>(&context), &info);
                    // FORCE BREAK
                    if (!biped_is_success(result))
                        return;
                    if (result >= biped_result_valid_but_high_pressure) {
                        biped_cache_force_unlock(m_cache);
                        m_pGraphcs->DrawAdded();
                    }
                    {
                        // LOAD
                        LockRectU locked;
                        locked.rect.l = info.position.x;
                        locked.rect.t = info.position.y;
                        locked.rect.r = info.position.x + data.width;
                        locked.rect.b = info.position.y + data.height;
                        locked.width = info.aligned_size.w;
                        locked.height = info.aligned_size.h;
                        locked.page = 0;
                        locked.pitch = pitch;
                        if (data.width && data.height) {
                            static LockRectU s_rect = {};
                            if (locked.width > 60) {
                                s_rect = locked;
                                s_rect.rect.r = s_rect.rect.l + locked.width;
                                s_rect.rect.b = s_rect.rect.t + locked.height;
                            }
                            else if (s_rect.page == locked.page) {
                                if (locked.rect.l >= s_rect.rect.l && locked.rect.t >= s_rect.rect.t) {
                                    if (locked.rect.r <= s_rect.rect.r && locked.rect.b <= s_rect.rect.b) {
                                        int bk = 9;
                                    }
                                }
                            }
                            m_pGraphcs->RefreshTexture(locked, data.data);
                        }
                        break;
                    }
                }
            }
        }

        const auto ctx = reinterpret_cast<FontAPIInfo*>(info.value);
        if (ctx) {

            if (info.real_size.w && info.real_size.h) {
                TEXT_2D_RECT rect = {};
                const long x = offset.x + ctx->offsetx;
                const long y = offset.y - ctx->offsety;
                rect.des.l = x;
                rect.des.t = y;
                rect.des.r = x + info.real_size.w;
                rect.des.b = y + info.real_size.h;
                rect.src.l = info.position.x;
                rect.src.t = info.position.y;
                rect.src.r = info.position.x + info.real_size.w;
                rect.src.b = info.position.y + info.real_size.h;
                rect.color = obj.color;
                if (m_pGraphcs->AddRect(rect))
                    biped_cache_force_unlock(m_cache);
            }
            offset.x += ctx->boundw;
            //offset.y +=  ctx->info.boundh;
        }
    }
#if 1
    //m_cache.flush();
    //m_pGraphcs->DrawAdded();
#endif
}


#include <string>
#include <vector>

namespace impl {
    // is_surrogate
    static inline bool is_surrogate(uint16_t ch) noexcept { return ((ch) & 0xF800) == 0xD800; }
    // is_2nd_surrogate
    static inline bool is_2nd_surrogate(uint16_t ch) noexcept { return ((ch) & 0xFC00) == 0xDC00; }
    // is_1st_surrogate
    static inline bool is_1st_surrogate(uint16_t ch) noexcept { return ((ch) & 0xFC00) == 0xD800; }

    // char16 x2 -> char32
    inline char32_t char16x2to32(char16_t lead, char16_t trail) {
        assert(is_1st_surrogate(lead) && "illegal utf-16 char");
        assert(is_2nd_surrogate(trail) && "illegal utf-16 char");
        return char32_t((uint16_t(lead) - 0xD800) << 10 | (uint16_t(trail) - 0xDC00)) + (0x10000);
    };
}

void TextAppWin::DrawTextU16(const SimpleText& format, const char16_t* utf16, TPointL offset) noexcept
{
    auto tmp = format;
    const auto length = std::char_traits<char16_t>::length(utf16);
    std::vector<SimpleText> buffer;
    try {
        buffer.reserve(length);
        while (const auto ch = *utf16) {
            ++utf16;
            char32_t ucs4 = ch;
            if (impl::is_surrogate(ch)) {
                const auto trail = *utf16;
                ++utf16;
                ucs4 = impl::char16x2to32(ch, trail);
            }
            tmp.text.code = ucs4;
            buffer.push_back(tmp);
        }
    }
    catch (...) {
        return;
    }
    this->DrawSimpleText(buffer.data(), uint32_t(buffer.size()), offset);
}


void TextAppWin::DrawFrameInfo(float delta) noexcept
{
    ++m_uFrameCounterAll;
    ++m_uFrameCounterFPS;


    if (m_uFrameCounterFPS == m_cFrameCountStep) {
        m_uFrameCounterFPS = 0;
        const auto tick = ::timeGetTime();
        const auto ddelta = double(uint32_t(tick - m_uFrameTickLast));
        m_uFrameTickLast = tick;
        const auto fps = 1000.0 / ddelta * double(m_cFrameCountStep);
        m_fFps = float(fps);
    }



    constexpr UINT BUFFER_COUNT = 250;
    wchar_t buffer[BUFFER_COUNT];

    const auto c = std::swprintf(
        buffer, BUFFER_COUNT,
        L"%6.2f fps(c) %6.2f fps(m) @%8ld | Cache(c) Normal(n)",
        1.f / delta, m_fFps, long(m_uFrameCounterAll)
    );
    assert(c > 0 && "bad std::swprintf call");

    static_assert(sizeof(wchar_t) == sizeof(char16_t), "windows only?");


    SimpleText format = {};
    format.text.face = 1;
    format.text.size = ts_float_to_fixed(14.1f);
    format.color = 0xff000000;
    this->DrawTextU16(format, reinterpret_cast<const char16_t*>(buffer), {16, 16});
}

#pragma comment(lib, "Winmm.lib")

void TextAppWin::SetMode(DWORD mode) noexcept
{
    if (m_dwDisplayMode == mode)
        m_dwModeCounter++;
    else
        m_dwModeCounter = 0;
    m_dwDisplayMode = mode;
}

#define BIPED_C_IMPLEMENTATION
#include "../biped.h"
