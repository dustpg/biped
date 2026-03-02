#define _CRT_SECURE_NO_WARNINGS
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#include "app.h"
#include "textlist.h"
#include <d3d11.h>
#include <d2d1.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <dxgi1_2.h>
#ifndef NDEBUG
#include <d3d11sdklayers.h>
#endif
#include <new>
#include <cstring>


// point - texture model
struct TEXT_VERTEX
{
    DirectX::XMFLOAT2   position;
    DirectX::XMFLOAT3   tex;
    DirectX::XMFLOAT4   color;
};

struct TEXT_VERTEX_RECT
{
    TEXT_VERTEX         point[4];
};

// D3D
constexpr float SCREEN_NEAR_Z = (0.01f);
constexpr float SCREEN_FAR_Z = (1000.f);
constexpr UINT MASS = 1;
constexpr UINT MAX_RECT_COUNT = 256 * 64 / sizeof(TEXT_VERTEX_RECT);



template<typename A, typename B>
float half_offset_normal(A a, B b) noexcept {
    return (float(a) + 0.5f) / float(b);
}


constexpr UINT ALIGN_AS(UINT a, UINT b) noexcept {
    return (a + b - 1) / b * b;
}


template<class Interface>
inline void SafeRelease(Interface*& pInterfaceToRelease) {
    if (pInterfaceToRelease != nullptr) {
        pInterfaceToRelease->Release();
        pInterfaceToRelease = nullptr;
    }
}

void MakeColor(uint32_t argb, float rgba[]) noexcept {
    constexpr uint32_t SHIFT_A = 24;
    constexpr uint32_t SHIFT_R = 16;
    constexpr uint32_t SHIFT_G = 8;
    constexpr uint32_t SHIFT_B = 0;
    constexpr uint32_t MASK_A = 0xff << SHIFT_A;
    constexpr uint32_t MASK_R = 0xff << SHIFT_R;
    constexpr uint32_t MASK_G = 0xff << SHIFT_G;
    constexpr uint32_t MASK_B = 0xff << SHIFT_B;

    const float a = float((argb & MASK_A) >> SHIFT_A) / 255.f;
    const float r = float((argb & MASK_R) >> SHIFT_R) / 255.f;
    const float g = float((argb & MASK_G) >> SHIFT_G) / 255.f;
    const float b = float((argb & MASK_B) >> SHIFT_B) / 255.f;

    rgba[0] = r;
    rgba[1] = g;
    rgba[2] = b;
    rgba[3] = a;
}


void MakeRect(const TEXT_2D_RECT& rect, const D2D1_SIZE_U des_src[2], TEXT_VERTEX_RECT& v) noexcept {

    const float tz = 0.f;
    const float tx0 = half_offset_normal(rect.src.l, des_src[1].width);
    const float tx1 = half_offset_normal(rect.src.r, des_src[1].width);
    const float ty0 = half_offset_normal(rect.src.t, des_src[1].height);
    const float ty1 = half_offset_normal(rect.src.b, des_src[1].height);

    const float sx0 = half_offset_normal(rect.des.l, des_src[0].width);
    const float sx1 = half_offset_normal(rect.des.r, des_src[0].width);
    const float sy0 = half_offset_normal(rect.des.t, des_src[0].height);
    const float sy1 = half_offset_normal(rect.des.b, des_src[0].height);

    /*
     (-1, +1)

        ^
        |
        |
        |

     (-1, -1) ----------> (+1, -1)
    */
    const float dx0 = sx0 * +2.f - 1.f;
    const float dx1 = sx1 * +2.f - 1.f;
    const float dy0 = sy0 * -2.f + 1.f;
    const float dy1 = sy1 * -2.f + 1.f;

    const auto vex = v.point;
    vex[0].position = { dx0, dy0 };
    vex[0].tex = { tx0, ty0, tz };

    MakeColor(rect.color, &vex[0].color.x);

    vex[1].position = { dx1, dy0 };
    vex[1].tex = { tx1, ty0, tz };
    vex[1].color = vex[0].color;

    vex[2].position = { dx0, dy1 };
    vex[2].tex = { tx0, ty1, tz };
    vex[2].color = vex[0].color;

    vex[3].position = { dx1, dy1 };
    vex[3].tex = { tx1, ty1, tz };
    vex[3].color = vex[0].color;
}

void clear_mem(void* b, void* e) noexcept {
    std::memset(b, 0, (char*)(e) -(char*)(b));
}


class TextGraphcsD3D11 : public TextGraphcs {
    IDXGIFactory2*              m_pDxgiFactory;
    ID3D11Device*               m_pD3d11Device;
    ID3D11DeviceContext*        m_pD3d11DeviceContext;
    ID3D11Texture2D*            m_pD3d11RtvBuffer;
    ID3D11Texture2D*            m_pD3d11DsvBuffer;
    ID3D11RenderTargetView*     m_pD3d11Rtv;
    ID3D11DepthStencilView*     m_pD3d11Dsv ;
    IDXGIDevice1*               m_pDxgiDevice;
    IDXGISwapChain1*            m_pSwapChain;

    ID3D11Buffer*               m_p2DVertexBuffer;
    ID3D11Buffer*               m_p2DIndexBuffer;
    ID3D11BlendState*           m_pBasicBlend;
    ID3D11VertexShader*         m_pBasic2DVS;
    ID3D11PixelShader*          m_pBasic2DPS;
    ID3D11InputLayout*          m_pBasic2DIL;
    ID3D11SamplerState*         m_pLinearSampler;

    ID3D11Texture2D*            m_pTextCache;
    ID3D11ShaderResourceView*   m_pTextView;

    uint32_t                    m_cAddedCounter;
    TEXT_VERTEX_RECT            m_szBuffer[MAX_RECT_COUNT];
#ifndef NDEBUG
    HRESULT InitD3D11Debug() noexcept;
#endif
public:
    TextGraphcsD3D11() noexcept { clear_mem(&m_pDxgiFactory, std::end(m_szBuffer)); }
    HRESULT InitD3D_A(HWND hwnd) noexcept;
    HRESULT InitD3D_B() noexcept;
    void ResetViewport() noexcept;
    void Render(ID3D11DeviceContext& ctx) noexcept;
    void Clear() noexcept;
public:
    ~TextGraphcsD3D11() noexcept override { this->Clear(); }
    bool Init(HWND hwnd) noexcept override;
    void BeginRender(uint32_t clearcolor) noexcept override;
    void EndRender(uint32_t sync) noexcept override;
    void RefreshTexture(const LockRectU& rect, const void* data) noexcept override;
    bool AddRect(const TEXT_2D_RECT&) noexcept override;
    void DrawAdded() noexcept override;
};


bool TextGraphcsD3D11::AddRect(const TEXT_2D_RECT& rect) noexcept {
    const bool full = m_cAddedCounter >= MAX_RECT_COUNT;
    if (full)
        this->DrawAdded();
    assert(m_cAddedCounter < MAX_RECT_COUNT);
    D2D1_SIZE_U s2[2] = { { WINDOW_WIDTH, WINDOW_HEIGHT}, { SIDE_SIZE, SIDE_SIZE} };
    ::MakeRect(rect, s2, m_szBuffer[m_cAddedCounter]);
    ++m_cAddedCounter;
    return full;
}

void TextGraphcsD3D11::DrawAdded() noexcept
{
    auto count = m_cAddedCounter;
    if (!m_cAddedCounter) return;
    m_cAddedCounter = 0;
    D3D11_BOX box = {};
    box.bottom = box.back = 1;
    box.right = sizeof(TEXT_VERTEX_RECT) * count;
    m_pD3d11DeviceContext->UpdateSubresource(m_p2DVertexBuffer, 0, &box, m_szBuffer, 1, 1);
    m_pD3d11DeviceContext->DrawIndexed(6 * count, 0, 0);
}


void TextGraphcsD3D11::RefreshTexture(const LockRectU& rect, const void* data) noexcept {
    D3D11_BOX box;
#if 0
    const auto buf = malloc(rect.width * rect.height);
    std::memset(buf,-1, rect.width * rect.height);
    box.left = rect.rect.l;
    box.top = rect.rect.t;
    box.front = 0;
    box.right = rect.rect.l + rect.width;
    box.bottom = rect.rect.t + rect.height;
    box.back = 1;
    m_pD3d11DeviceContext->UpdateSubresource(m_pTextCache, rect.page, &box, buf, rect.width, rect.width * rect.height);
    std::free(buf);
#endif
    box.left = rect.rect.l;
    box.top = rect.rect.t;
    box.front = 0;
    box.right = rect.rect.r;
    box.bottom = rect.rect.b;
    box.back = 1;
    m_pD3d11DeviceContext->UpdateSubresource(m_pTextCache, rect.page, &box, data, rect.pitch, rect.pitch);
}


void TextGraphcsD3D11::Clear() noexcept {

    ::SafeRelease(m_pDxgiFactory);
    ::SafeRelease(m_pD3d11Device);
    ::SafeRelease(m_pD3d11DeviceContext);
    ::SafeRelease(m_pD3d11RtvBuffer);
    ::SafeRelease(m_pD3d11DsvBuffer);
    ::SafeRelease(m_pD3d11Rtv);
    ::SafeRelease(m_pD3d11Dsv);
    ::SafeRelease(m_pDxgiDevice);
    ::SafeRelease(m_pSwapChain);

    ::SafeRelease(m_p2DVertexBuffer);
    ::SafeRelease(m_p2DIndexBuffer);
    ::SafeRelease(m_pBasicBlend);
    ::SafeRelease(m_pBasic2DVS);
    ::SafeRelease(m_pBasic2DPS);
    ::SafeRelease(m_pBasic2DIL);
    ::SafeRelease(m_pLinearSampler);

    ::SafeRelease(m_pTextCache);
    ::SafeRelease(m_pTextView);
}


const char D3D11_BASIC_SHADER[] = R"shader(
struct VertexInputType {
    float4 position     : POSITION;
    float4 tex          : TEXCOORD0;
    float4 color        : COLOR0;
};

struct PixelInputType {
    float4 position     : SV_POSITION;
    float4 tex          : TEXCOORD0;
    float4 color        : COLOR0;
};

Texture2D Texture;
SamplerState SampleType;

PixelInputType Basic2DVS(VertexInputType input) {
    PixelInputType output;
    output.position = float4(input.position.xy, 0, 1);
    output.color = input.color;
    output.tex = input.tex;
    return output;
}

float4 Basic2DPS(PixelInputType input) : SV_TARGET {
    float4 color = input.color;
    color.a *= Texture.Sample(SampleType, input.tex).x;
    return color;
}


)shader";




bool TextGraphcsD3D11::Init(HWND hwnd) noexcept {
    HRESULT hr = S_OK;
    if (SUCCEEDED(hr)) hr = this->InitD3D_A(hwnd);
    if (SUCCEEDED(hr)) hr = this->InitD3D_B();
    return SUCCEEDED(hr);
}

void TextGraphcsD3D11::BeginRender(uint32_t clearcolor) noexcept {
    // RTV
    float color[4];
    ::MakeColor(clearcolor, color);

    m_pD3d11DeviceContext->ClearRenderTargetView(m_pD3d11Rtv, color);
    m_pD3d11DeviceContext->OMSetRenderTargets(1, &m_pD3d11Rtv, m_pD3d11Dsv);

    ID3D11DeviceContext& ctx = *m_pD3d11DeviceContext;

    float factor[4] = {};
    ctx.OMSetBlendState(m_pBasicBlend, factor, 0xffffffff);
    const UINT stride = sizeof(TEXT_VERTEX);
    const UINT offset = 0;

    ctx.IASetVertexBuffers(0, 1, &m_p2DVertexBuffer, &stride, &offset);
    ctx.IASetIndexBuffer(m_p2DIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    ctx.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx.IASetInputLayout(m_pBasic2DIL);
    ctx.PSSetShaderResources(0, 1, &m_pTextView);
    //ctx.VSSetConstantBuffers(0, 1, &m_p2DMatrixBuffer);
    ctx.VSSetShader(m_pBasic2DVS, nullptr, 0);
    ctx.PSSetSamplers(0, 1, &m_pLinearSampler);
    ctx.PSSetShader(m_pBasic2DPS, nullptr, 0);
}

void TextGraphcsD3D11::EndRender(uint32_t sync) noexcept {
    m_pSwapChain->Present(sync, 0);
}



HRESULT TextGraphcsD3D11::InitD3D_A(HWND hwnd) noexcept {

    UINT creationFlags = 0;

#ifndef NDEBUG
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_DRIVER_TYPE dtype = D3D_DRIVER_TYPE_HARDWARE;
    //dtype = D3D_DRIVER_TYPE_WARP;

    const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1
    };
    constexpr uint32_t fl_size = sizeof(featureLevels) / sizeof(featureLevels[0]);
    D3D_FEATURE_LEVEL d3dFeatureLevel;
    HRESULT hr = S_OK;
    hr = ::D3D11CreateDevice(
        nullptr,
        dtype,
        nullptr,
        creationFlags,
        featureLevels,
        fl_size,
        D3D11_SDK_VERSION,
        &m_pD3d11Device,
        &d3dFeatureLevel,
        &m_pD3d11DeviceContext
    );
    assert(SUCCEEDED(hr) && L"D3D11CreateDevice faild");
#ifndef NDEBUG
    if (SUCCEEDED(hr)) {
        hr = this->InitD3D11Debug();
    }
#endif
    assert(m_pD3d11DeviceContext);
    IDXGIAdapter* pDxgiAdapter = nullptr;
    // Query IDXGIDevice1
    if (SUCCEEDED(hr)) {
        hr = m_pD3d11Device->QueryInterface(
            IID_IDXGIDevice1,
            reinterpret_cast<void**>(&m_pDxgiDevice)
        );
        assert(SUCCEEDED(hr) && L"m_pD3d11Device->QueryInterface(IID_IDXGIDevice1) faild");
    }
    // Get adapter
    if (SUCCEEDED(hr)) {
        hr = m_pDxgiDevice->GetAdapter(&pDxgiAdapter);
        assert(SUCCEEDED(hr) && L"pDxgiDev->GetAdapter faild");
    }
    // Get information
    if (SUCCEEDED(hr)) {
        //hr = pDxgiAdapter->GetDesc(&cp.dxgiAdapterDesc);
        assert(SUCCEEDED(hr) && L"pDxgiAdapter->GetDesc faild");
    }
    // get dxgi factory
    if (SUCCEEDED(hr)) {
        hr = pDxgiAdapter->GetParent(
            IID_IDXGIFactory2,
            reinterpret_cast<void**>(&m_pDxgiFactory)
        );
        assert(SUCCEEDED(hr) && L"pDxgiAdapter->GetParent(IID_IDXGIFactory2) faild");
    }
    // Cleanup
    ::SafeRelease(pDxgiAdapter);


    assert(hwnd && "bad window");
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc;
    // get window size
    RECT clientRect; ::GetClientRect(hwnd, &clientRect);
    const UINT width = clientRect.right - clientRect.left;
    const UINT height = clientRect.bottom - clientRect.top;
    assert(width && height && "bad size");
    //m_sizeRender = { width, height };
    swapChainDesc.Width = width;
    swapChainDesc.Height = height;
    // BOOKMARK: FORMAT SUPPORT
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.Scaling = DXGI_SCALING_NONE;
    // TODO: waitable object
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    swapChainDesc.Flags = 0;

    if (SUCCEEDED(hr)) {
        // normal app
        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        hr = m_pDxgiFactory->CreateSwapChainForHwnd(
            m_pD3d11Device,
            hwnd,
            &swapChainDesc,
            nullptr,
            nullptr,
            &m_pSwapChain
        );
    }

    IDXGISurface* surface = nullptr;

    // get backbuffer dxgisurface
    if (SUCCEEDED(hr)) {
        hr = m_pSwapChain->GetBuffer(
            0, IID_IDXGISurface,
            reinterpret_cast<void**>(&surface)
        );
        assert(SUCCEEDED(hr) && L"swapchan->GetBuffer faild");
    }
    // get backbuffer texture2d
    if (SUCCEEDED(hr)) {
        hr = surface->QueryInterface(
            IID_ID3D11Texture2D,
            reinterpret_cast<void**>(&m_pD3d11RtvBuffer)
        );
        assert(SUCCEEDED(hr) && L"swapchan->QueryInterface IID_ID3D11Texture2D faild");
    }
    // text cache
    if (SUCCEEDED(hr)) {
        D3D11_TEXTURE2D_DESC cache = {};
        cache.Width = SIDE_SIZE;
        cache.Height = SIDE_SIZE;
        cache.MipLevels = 1;
        cache.ArraySize = 1;
        cache.Format = DXGI_FORMAT_R8_UNORM;
        cache.SampleDesc = { 1, 0 };
        cache.Usage = D3D11_USAGE_DEFAULT;
        cache.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        hr = m_pD3d11Device->CreateTexture2D(&cache, nullptr, &m_pTextCache);
        assert(SUCCEEDED(hr) && L"CreateTexture2D faild");
    }
    // text cache view
    if (SUCCEEDED(hr)) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2DArray.MostDetailedMip = 0;
        srv.Texture2DArray.MipLevels = 1;
        srv.Texture2DArray.ArraySize = 1;
        srv.Texture2DArray.FirstArraySlice = 0;
        m_pD3d11Device->CreateShaderResourceView(m_pTextCache, &srv, &m_pTextView);
    }
    // create RTV
    if (SUCCEEDED(hr)) {
        assert(m_pD3d11Rtv == nullptr);
        hr = m_pD3d11Device->CreateRenderTargetView(m_pD3d11RtvBuffer, nullptr, &m_pD3d11Rtv);
        assert(SUCCEEDED(hr) && L"m_pD3d11Device->CreateRenderTargetView faild");
    }
    // TODO: create DSV
    if (SUCCEEDED(hr)) {

    }
    // do on succeeded
    if (SUCCEEDED(hr)) {
        // as rendertarget
        m_pD3d11DeviceContext->OMSetRenderTargets(1, &m_pD3d11Rtv, m_pD3d11Dsv);
        // reset viewport
        this->ResetViewport();
    }
    ::SafeRelease(surface);

    return hr;
}


HRESULT TextGraphcsD3D11::InitD3D_B() noexcept {
    assert(m_pD3d11Device);
    HRESULT hr = S_OK;
    // basic 2d vertices input
    const D3D11_INPUT_ELEMENT_DESC inputLayout2D[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(TEXT_VERTEX, position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(TEXT_VERTEX, tex), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(TEXT_VERTEX, color), D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    // create common 2d vertex
    if (SUCCEEDED(hr)) {
        assert(m_p2DVertexBuffer == nullptr);
        D3D11_BUFFER_DESC bufferDesc = { 0 };
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = ALIGN_AS(sizeof(m_szBuffer), 256);
        bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA sb = { m_szBuffer, 1, 1 };
        hr = m_pD3d11Device->CreateBuffer(&bufferDesc, &sb, &m_p2DVertexBuffer);;
        assert(SUCCEEDED(hr) && L"CreateBuffer m_p2DVertexBuffer failed");
    }
    // create common 2d index
    if (SUCCEEDED(hr)) {
        assert(m_p2DIndexBuffer == nullptr);
        uint16_t buffer[MAX_RECT_COUNT * 6];
        for (UINT i = 0; i != MAX_RECT_COUNT; ++i) {
            buffer[i * 6 + 0] = i * 4 + 0;
            buffer[i * 6 + 1] = i * 4 + 1;
            buffer[i * 6 + 2] = i * 4 + 2;
            buffer[i * 6 + 3] = i * 4 + 2;
            buffer[i * 6 + 4] = i * 4 + 1;
            buffer[i * 6 + 5] = i * 4 + 3;
        }
        D3D11_BUFFER_DESC bufferDesc = { 0 };
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(buffer);
        bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA sb = { buffer, 1, 1 };
        hr = m_pD3d11Device->CreateBuffer(&bufferDesc, &sb, &m_p2DIndexBuffer);;
        assert(SUCCEEDED(hr) && L"CreateBuffer m_p2DIndexBuffer failed");
    }
    // create basic blend state
    if (SUCCEEDED(hr)) {
        D3D11_BLEND_DESC desc = {};
        desc.RenderTarget[0].BlendEnable = TRUE;
        desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        hr = m_pD3d11Device->CreateBlendState(&desc, &m_pBasicBlend);
        assert(SUCCEEDED(hr) && L"CreateBlendState failed");
    }
    // create basic shader
    ID3DBlob* vs = nullptr, * ps = nullptr;
    UINT flag = 0;
#if !defined(NDEBUG)
    flag |= D3DCOMPILE_DEBUG;
#endif
    // Compile VS 
    if (SUCCEEDED(hr)) {
        hr = ::D3DCompile(
            D3D11_BASIC_SHADER, sizeof(D3D11_BASIC_SHADER),
            nullptr, nullptr, nullptr,
            "Basic2DVS", "vs_4_0",
            flag, 0, &vs, nullptr
        );
        assert(SUCCEEDED(hr) && L"D3DCompile VertexShader failed");
    }
    // Compile PS
    if (SUCCEEDED(hr)) {
        hr = ::D3DCompile(
            D3D11_BASIC_SHADER, sizeof(D3D11_BASIC_SHADER),
            nullptr, nullptr, nullptr,
            "Basic2DPS", "ps_4_0",
            flag, 0, &ps, nullptr
        );
        assert(SUCCEEDED(hr) && L"D3DCompile PixelShader failed");
    }
    // create basic 2d vs
    if (SUCCEEDED(hr)) {
        hr = m_pD3d11Device->CreateVertexShader(
            vs->GetBufferPointer(),
            vs->GetBufferSize(),
            nullptr, &m_pBasic2DVS
        );
        assert(SUCCEEDED(hr) && L"CreateVertexShader failed");
    }
    // create basic 2d ps
    if (SUCCEEDED(hr)) {
        hr = m_pD3d11Device->CreatePixelShader(
            ps->GetBufferPointer(),
            ps->GetBufferSize(),
            nullptr, &m_pBasic2DPS
        );
        assert(SUCCEEDED(hr) && L"CreatePixelShader failed");
    }
    // create basic 2d input layout
    if (SUCCEEDED(hr)) {
        hr = m_pD3d11Device->CreateInputLayout(
            inputLayout2D, sizeof(inputLayout2D) / sizeof(inputLayout2D[0]),
            vs->GetBufferPointer(),
            vs->GetBufferSize(),
            &m_pBasic2DIL
        );
        assert(SUCCEEDED(hr) && L"CreateInputLayout failed");
    }
    ::SafeRelease(vs);
    ::SafeRelease(ps);
    // create basic sampler
    if (SUCCEEDED(hr)) {
        D3D11_SAMPLER_DESC desc = { };
        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        desc.MipLODBias = 0.0f;
        desc.MaxAnisotropy = 1;
        desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        desc.MinLOD = 0;
        desc.MaxLOD = D3D11_FLOAT32_MAX;
        hr = m_pD3d11Device->CreateSamplerState(&desc, &m_pLinearSampler);
        assert(SUCCEEDED(hr) && L"CreateSamplerState m_pLinearSampler failed");
    }
    return hr;
}

#ifndef NDEBUG
HRESULT TextGraphcsD3D11::InitD3D11Debug() noexcept {
    HRESULT hr = S_OK;
    //ID3D11InfoQueue* pInfoQueue = nullptr;
    //
    //if (SUCCEEDED(hr)) {
    //    hr = m_pD3d11Device->QueryInterface(__uuidof(ID3D11InfoQueue), reinterpret_cast<void**>(&pInfoQueue));
    //}
    //
    //if (SUCCEEDED(hr) && pInfoQueue) {
    //    D3D11_MESSAGE_SEVERITY severities[] = {
    //        D3D11_MESSAGE_SEVERITY_CORRUPTION,
    //        D3D11_MESSAGE_SEVERITY_ERROR,
    //        D3D11_MESSAGE_SEVERITY_WARNING,
    //        D3D11_MESSAGE_SEVERITY_INFO,
    //        D3D11_MESSAGE_SEVERITY_MESSAGE
    //    };
    //    
    //    D3D11_INFO_QUEUE_FILTER filter = {};
    //    D3D11_INFO_QUEUE_FILTER_DESC allowList = {};
    //    allowList.NumSeverities = sizeof(severities) / sizeof(severities[0]);
    //    allowList.pSeverityList = severities;
    //    filter.AllowList = allowList;
    //    
    //    hr = pInfoQueue->AddStorageFilterEntries(&filter);
    //    
    //    pInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
    //    pInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
    //    pInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, FALSE);
    //}
    //
    //::SafeRelease(pInfoQueue);
    return hr;
}
#endif

void TextGraphcsD3D11::ResetViewport() noexcept {
    D3D11_VIEWPORT viewport;
    viewport.Width = static_cast<float>(WINDOW_WIDTH);
    viewport.Height = static_cast<float>(WINDOW_HEIGHT);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    m_pD3d11DeviceContext->RSSetViewports(1, &viewport);
}


bool TextAppWin::InitGraphcs() noexcept
{
    assert(m_pGraphcs == nullptr);
    assert(m_hwnd != nullptr);
    m_pGraphcs = new(std::nothrow) TextGraphcsD3D11;
    if (!m_pGraphcs) return false;
    return m_pGraphcs->Init(m_hwnd);
}