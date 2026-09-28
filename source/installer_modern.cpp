#include "stdafx.h"
#include "installer.h"
#include "fileops.h"
#include "log.h"
#include "string_funcs.h"
#include "ui_common.h"
#include <windowsx.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwrite_3.h>
#include <wrl/client.h>
#include <initguid.h>
#include <wincodec.h>

// Custom drawn installer window (MU_UI_MODERN): gradient or image background, logo, big title,
// rounded button, blurred background while installing. Direct2D/DirectWrite are loaded at runtime,
// the caller falls back to the classic UI when they are not available.
namespace mu::installer
{
    using Microsoft::WRL::ComPtr;

    namespace
    {
        constexpr GUID kClsidWicImagingFactory = { 0xcacaf262, 0x9370, 0x4615, { 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a } };
        constexpr wchar_t kWindowClass[] = L"modupdater.installer";
        constexpr UINT_PTR kTimerAnimation = 1;     // progress page
        constexpr UINT_PTR kTimerTransition = 2;    // location list and tooltip fading in
        constexpr UINT_PTR kTimerTooltip = 3;       // hover delay of the tooltip
        constexpr UINT kTooltipDelay = 450;
        constexpr float kTransitionMs = 160.0f;
        constexpr UINT WM_APP_AUTOSTART = WM_APP + 1;
        constexpr float kDefaultWidth = 720.0f;
        constexpr float kSidePadding = 40.0f;
        constexpr float kTopPadding = 36.0f;
        constexpr float kBottomPadding = 18.0f;
        constexpr float kWindowButtonWidth = 46.0f;
        constexpr float kWindowButtonHeight = 34.0f;
        constexpr float kBoxRadius = 7.0f;
        constexpr float kBoxMinHeight = 46.0f;      // location box with a path of one line
        constexpr float kBrowseWidth = 46.0f;
        constexpr float kListRowHeight = 54.0f;     // row with a path of one line
        constexpr float kListMaxHeight = 5 * kListRowHeight;
        constexpr float kListPadding = 5.0f;
        constexpr float kListGap = 6.0f;            // between the location box and the list

        D2D1_COLOR_F ToColor(COLORREF c, float alpha = 1.0f)
        {
            return D2D1::ColorF(GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f, alpha);
        }

        COLORREF Mix(COLORREF a, COLORREF b, float t)
        {
            auto channel = [t](int x, int y) { return static_cast<BYTE>(std::lround(x + (y - x) * t)); };
            return RGB(channel(GetRValue(a), GetRValue(b)), channel(GetGValue(a), GetGValue(b)), channel(GetBValue(a), GetBValue(b)));
        }

        float EaseOut(float t)
        {
            t = 1.0f - std::clamp(t, 0.0f, 1.0f);
            return 1.0f - t * t * t;
        }

        D2D1_COLOR_F WithAlpha(D2D1_COLOR_F c, float alpha)
        {
            c.a = alpha;
            return c;
        }

        float Luminance(COLORREF c)
        {
            return (0.2126f * GetRValue(c) + 0.7152f * GetGValue(c) + 0.0722f * GetBValue(c)) / 255.0f;
        }

        D2D1_RECT_F Inflate(D2D1_RECT_F r, float d)
        {
            return D2D1::RectF(r.left - d, r.top - d, r.right + d, r.bottom + d);
        }

        bool Contains(const D2D1_RECT_F& r, D2D1_POINT_2F p)
        {
            return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
        }

        D2D1_RECT_F CenteredRect(float cx, float top, float width, float height)
        {
            return D2D1::RectF(cx - width / 2, top, cx + width / 2, top + height);
        }

        // Paths are always shown in full. Long ones wrap after a folder separator, not inside a folder name:
        // "C:\Program Files (x86)\Steam\steamapps\common\" + "Grand Theft Auto IV\GTAIV".
        // keepSpaces: the path is part of a sentence that may also wrap at spaces.
        std::wstring WrappablePath(std::wstring_view path, wchar_t separator = L'\\', bool keepSpaces = false)
        {
            std::wstring text;
            text.reserve(path.size() + 16);
            for (wchar_t c : path)
            {
                text.push_back(c == L' ' && !keepSpaces ? L'\u00A0' : c);
                if (c == separator)
                    text.push_back(L'\u200B'); // zero width space: a line break opportunity
            }
            return text;
        }

        // ---- text with <a href="...">links</a>

        struct RichText
        {
            struct Link
            {
                UINT32 start;
                UINT32 length;
                std::wstring url;
            };
            std::wstring text;
            std::vector<Link> links;
        };

        RichText ParseRichText(std::wstring_view markup)
        {
            RichText rich;
            size_t i = 0;
            while (i < markup.size())
            {
                if (markup[i] == L'<' && (starts_with(markup.substr(i), L"<a ", false) || starts_with(markup.substr(i), L"<a>", false)))
                {
                    auto tagEnd = markup.find(L'>', i);
                    auto lower = toLowerWStr(std::wstring(markup));
                    auto close = (tagEnd == std::wstring_view::npos) ? std::wstring::npos : lower.find(L"</a>", tagEnd);
                    if (close != std::wstring::npos)
                    {
                        auto tag = std::wstring(markup.substr(i, tagEnd - i));
                        std::wstring url;
                        auto href = toLowerWStr(tag).find(L"href=");
                        if (href != std::wstring::npos)
                        {
                            auto value = tag.substr(href + 5);
                            if (!value.empty() && (value.front() == L'"' || value.front() == L'\''))
                            {
                                auto quote = value.front();
                                value = value.substr(1, value.find(quote, 1) - 1);
                            }
                            url = value;
                        }
                        auto text = std::wstring(markup.substr(tagEnd + 1, close - tagEnd - 1));
                        rich.links.push_back({ static_cast<UINT32>(rich.text.size()), static_cast<UINT32>(text.size()), url.empty() ? text : url });
                        rich.text += text;
                        i = close + 4;
                        continue;
                    }
                }
                rich.text.push_back(markup[i++]);
            }
            return rich;
        }

        // ---- images

        ComPtr<IWICBitmap> DecodeImage(IWICImagingFactory* wic, const std::vector<uint8_t>& data)
        {
            if (data.empty())
                return nullptr;

            ComPtr<IWICStream> stream;
            ComPtr<IWICBitmapDecoder> decoder;
            if (FAILED(wic->CreateStream(&stream)) ||
                FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(data.data()), static_cast<DWORD>(data.size()))) ||
                FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)))
            {
                Log(L"Cannot decode the installer image ({} bytes)", data.size());
                return nullptr;
            }

            // icons contain several sizes, use the largest one
            UINT frames = 0, best = 0, bestWidth = 0;
            decoder->GetFrameCount(&frames);
            for (UINT i = 0; i < frames; i++)
            {
                ComPtr<IWICBitmapFrameDecode> frame;
                UINT w = 0, h = 0;
                if (SUCCEEDED(decoder->GetFrame(i, &frame)) && SUCCEEDED(frame->GetSize(&w, &h)) && w > bestWidth)
                {
                    bestWidth = w;
                    best = i;
                }
            }

            ComPtr<IWICBitmapFrameDecode> frame;
            ComPtr<IWICFormatConverter> converter;
            ComPtr<IWICBitmap> bitmap;
            if (FAILED(decoder->GetFrame(best, &frame)) ||
                FAILED(wic->CreateFormatConverter(&converter)) ||
                FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeMedianCut)) ||
                FAILED(wic->CreateBitmapFromSource(converter.Get(), WICBitmapCacheOnLoad, &bitmap)))
            {
                return nullptr;
            }
            return bitmap;
        }

        ComPtr<IWICBitmap> IconToBitmap(IWICImagingFactory* wic, HICON icon)
        {
            if (!icon)
                return nullptr;

            // icons loaded from resources can be copied at a larger size
            HICON large = static_cast<HICON>(CopyImage(icon, IMAGE_ICON, 256, 256, LR_COPYFROMRESOURCE));
            ComPtr<IWICBitmap> source;
            HRESULT hr = wic->CreateBitmapFromHICON(large ? large : icon, &source);
            if (large)
                DestroyIcon(large);
            if (FAILED(hr))
                return nullptr;

            ComPtr<IWICFormatConverter> converter;
            ComPtr<IWICBitmap> bitmap;
            if (FAILED(wic->CreateFormatConverter(&converter)) ||
                FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeMedianCut)) ||
                FAILED(wic->CreateBitmapFromSource(converter.Get(), WICBitmapCacheOnLoad, &bitmap)))
            {
                return nullptr;
            }
            return bitmap;
        }

        // High quality resampling for the exact pixel size the image is displayed at
        ComPtr<ID2D1Bitmap> CreateScaledBitmap(IWICImagingFactory* wic, ID2D1RenderTarget* rt, IWICBitmapSource* source, UINT width, UINT height)
        {
            if (!source || width == 0 || height == 0)
                return nullptr;

            ComPtr<IWICBitmapScaler> scaler;
            ComPtr<ID2D1Bitmap> bitmap;
            if (FAILED(wic->CreateBitmapScaler(&scaler)) ||
                FAILED(scaler->Initialize(source, width, height, WICBitmapInterpolationModeFant)) ||
                FAILED(rt->CreateBitmapFromWicBitmap(scaler.Get(), nullptr, &bitmap)))
            {
                return nullptr;
            }
            return bitmap;
        }

        // Separable box blur on premultiplied BGRA pixels, a few passes look like a gaussian blur
        void BlurBitmap(IWICBitmap* bitmap, int radius, int passes)
        {
            UINT width = 0, height = 0;
            bitmap->GetSize(&width, &height);
            WICRect rect = { 0, 0, static_cast<INT>(width), static_cast<INT>(height) };
            ComPtr<IWICBitmapLock> lock;
            if (FAILED(bitmap->Lock(&rect, WICBitmapLockRead | WICBitmapLockWrite, &lock)))
                return;

            UINT stride = 0, size = 0;
            BYTE* data = nullptr;
            lock->GetStride(&stride);
            lock->GetDataPointer(&size, &data);
            if (!data || width == 0 || height == 0)
                return;

            std::vector<uint8_t> a(static_cast<size_t>(width) * height * 4), b(a.size());
            for (UINT y = 0; y < height; y++)
                memcpy(&a[static_cast<size_t>(y) * width * 4], data + static_cast<size_t>(y) * stride, width * 4);

            const int w = static_cast<int>(width), h = static_cast<int>(height);
            const int window = radius * 2 + 1;
            auto blurLine = [&](const uint8_t* src, uint8_t* dst, int count, int step)
            {
                for (int c = 0; c < 4; c++)
                {
                    int sum = 0;
                    for (int i = -radius; i <= radius; i++)
                        sum += src[std::clamp(i, 0, count - 1) * step + c];
                    for (int i = 0; i < count; i++)
                    {
                        dst[i * step + c] = static_cast<uint8_t>(sum / window);
                        sum += src[std::clamp(i + radius + 1, 0, count - 1) * step + c];
                        sum -= src[std::clamp(i - radius, 0, count - 1) * step + c];
                    }
                }
            };

            for (int pass = 0; pass < passes; pass++)
            {
                for (int y = 0; y < h; y++)
                    blurLine(&a[static_cast<size_t>(y) * w * 4], &b[static_cast<size_t>(y) * w * 4], w, 4);
                for (int x = 0; x < w; x++)
                    blurLine(&b[static_cast<size_t>(x) * 4], &a[static_cast<size_t>(x) * 4], h, w * 4);
            }

            for (UINT y = 0; y < height; y++)
                memcpy(data + static_cast<size_t>(y) * stride, &a[static_cast<size_t>(y) * width * 4], width * 4);
        }

        bool SavePng(IWICImagingFactory* wic, IWICBitmap* bitmap, const std::filesystem::path& file)
        {
            UINT width = 0, height = 0;
            bitmap->GetSize(&width, &height);
            ComPtr<IWICStream> stream;
            ComPtr<IWICBitmapEncoder> encoder;
            ComPtr<IWICBitmapFrameEncode> frame;
            WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
            return SUCCEEDED(wic->CreateStream(&stream)) &&
                   SUCCEEDED(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE)) &&
                   SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
                   SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
                   SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
                   SUCCEEDED(frame->Initialize(nullptr)) &&
                   SUCCEEDED(frame->SetSize(width, height)) &&
                   SUCCEEDED(frame->SetPixelFormat(&format)) &&
                   SUCCEEDED(frame->WriteSource(bitmap, nullptr)) &&
                   SUCCEEDED(frame->Commit()) &&
                   SUCCEEDED(encoder->Commit());
        }

        // ---- graphics objects that do not depend on a render target

        struct Graphics
        {
            ComPtr<ID2D1Factory> d2d;
            ComPtr<IDWriteFactory> dwrite;
            ComPtr<IWICImagingFactory> wic;
            ComPtr<IDWriteFontCollection> privateFonts;
            ComPtr<ID2D1StrokeStyle> roundStroke;
            ComPtr<ID2D1PathGeometry> chevron;      // 9 x 4.5 DIP, pointing down
            ComPtr<ID2D1PathGeometry> openFolder;   // 20 x 16 DIP outline

            bool Init(const Config& config)
            {
                using D2D1CreateFactoryFn = HRESULT(WINAPI*)(D2D1_FACTORY_TYPE, REFIID, const D2D1_FACTORY_OPTIONS*, void**);
                using DWriteCreateFactoryFn = HRESULT(WINAPI*)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);

                HMODULE d2dDll = ui::LoadSystemLibrary(L"d2d1.dll");
                HMODULE dwriteDll = ui::LoadSystemLibrary(L"dwrite.dll");
                auto createD2D = d2dDll ? reinterpret_cast<D2D1CreateFactoryFn>(GetProcAddress(d2dDll, "D2D1CreateFactory")) : nullptr;
                auto createDWrite = dwriteDll ? reinterpret_cast<DWriteCreateFactoryFn>(GetProcAddress(dwriteDll, "DWriteCreateFactory")) : nullptr;
                if (!createD2D || !createDWrite)
                    return false;

                D2D1_FACTORY_OPTIONS options = {};
                if (FAILED(createD2D(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), &options, reinterpret_cast<void**>(d2d.GetAddressOf()))) ||
                    FAILED(createDWrite(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()))) ||
                    FAILED(CoCreateInstance(kClsidWicImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
                {
                    return false;
                }

                d2d->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND), nullptr, 0, &roundStroke);
                CreateGlyphs();

                if (!config.fontData.empty())
                    LoadPrivateFont(config.fontData);
                return true;
            }

            // Outlines are stroked in one piece: two separate lines would overlap at the corners with translucent colors
            void CreateGlyphs()
            {
                using D2D1::Point2F;
                auto curve = [](ID2D1GeometrySink* sink, D2D1_POINT_2F control, D2D1_POINT_2F end)
                {
                    sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(control, end));
                };

                ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(d2d->CreatePathGeometry(&chevron)) && SUCCEEDED(chevron->Open(&sink)))
                {
                    sink->BeginFigure(Point2F(0.0f, 0.0f), D2D1_FIGURE_BEGIN_HOLLOW);
                    sink->AddLine(Point2F(4.5f, 4.5f));
                    sink->AddLine(Point2F(9.0f, 0.0f));
                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    sink->Close();
                }

                sink.Reset();
                if (SUCCEEDED(d2d->CreatePathGeometry(&openFolder)) && SUCCEEDED(openFolder->Open(&sink)))
                {
                    // back with the tab
                    sink->BeginFigure(Point2F(1.5f, 14.5f), D2D1_FIGURE_BEGIN_HOLLOW);
                    sink->AddLine(Point2F(1.5f, 3.0f));
                    curve(sink.Get(), Point2F(1.5f, 1.5f), Point2F(3.0f, 1.5f));
                    sink->AddLine(Point2F(6.6f, 1.5f));
                    sink->AddLine(Point2F(8.4f, 3.5f));
                    sink->AddLine(Point2F(14.5f, 3.5f));
                    curve(sink.Get(), Point2F(16.0f, 3.5f), Point2F(16.0f, 5.0f));
                    sink->AddLine(Point2F(16.0f, 6.5f));
                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    // front, tilted open
                    sink->BeginFigure(Point2F(1.5f, 14.5f), D2D1_FIGURE_BEGIN_HOLLOW);
                    sink->AddLine(Point2F(4.3f, 7.6f));
                    curve(sink.Get(), Point2F(4.7f, 6.5f), Point2F(5.9f, 6.5f));
                    sink->AddLine(Point2F(18.0f, 6.5f));
                    curve(sink.Get(), Point2F(19.5f, 6.5f), Point2F(19.0f, 7.9f));
                    sink->AddLine(Point2F(16.6f, 13.6f));
                    curve(sink.Get(), Point2F(16.2f, 14.5f), Point2F(15.2f, 14.5f));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();
                }
            }

            // Fonts from memory need IDWriteFactory5 (Windows 10 1703+)
            void LoadPrivateFont(const std::vector<uint8_t>& data)
            {
                ComPtr<IDWriteFactory5> factory5;
                ComPtr<IDWriteInMemoryFontFileLoader> loader;
                ComPtr<IDWriteFontFile> fontFile;
                ComPtr<IDWriteFontSetBuilder1> builder;
                ComPtr<IDWriteFontSet> fontSet;
                ComPtr<IDWriteFontCollection1> collection;
                if (SUCCEEDED(dwrite.As(&factory5)) &&
                    SUCCEEDED(factory5->CreateInMemoryFontFileLoader(&loader)) &&
                    SUCCEEDED(factory5->RegisterFontFileLoader(loader.Get())) &&
                    SUCCEEDED(loader->CreateInMemoryFontFileReference(factory5.Get(), data.data(), static_cast<UINT32>(data.size()), nullptr, &fontFile)) &&
                    SUCCEEDED(factory5->CreateFontSetBuilder(&builder)) &&
                    SUCCEEDED(builder->AddFontFile(fontFile.Get())) &&
                    SUCCEEDED(builder->CreateFontSet(&fontSet)) &&
                    SUCCEEDED(factory5->CreateFontCollectionFromFontSet(fontSet.Get(), &collection)))
                {
                    privateFonts = collection;
                }
                else
                {
                    Log(L"The installer font could not be loaded, using the system font instead");
                }
            }

            bool HasSystemFont(const wchar_t* family)
            {
                ComPtr<IDWriteFontCollection> fonts;
                UINT32 index = 0;
                BOOL exists = FALSE;
                return SUCCEEDED(dwrite->GetSystemFontCollection(&fonts)) && SUCCEEDED(fonts->FindFamilyName(family, &index, &exists)) && exists;
            }
        };

        struct Theme
        {
            D2D1_COLOR_F color[MU_COLOR_COUNT];
            D2D1_COLOR_F accent;        // open location box and the selected location, the button color
            bool dark = false;          // dark background with light text
            float panelAlpha = 0.38f;
            float borderAlpha = 0.18f;
            float trackAlpha = 0.45f;
            float popupAlpha = 0.84f;   // over the blurred page
            float popupBorderAlpha = 0.10f;
        };

        // resolvedTheme: MU_THEME_LIGHT or MU_THEME_DARK
        Theme MakeTheme(const Config& config, int resolvedTheme)
        {
            const bool darkTheme = resolvedTheme == MU_THEME_DARK;
            auto& overrides = config.Overrides(resolvedTheme);
            auto custom = [&](int element) { return overrides.colors[element] ? overrides.colors[element] : config.colors[element]; };

            // light: the look of the reference web installer, orange gradient and blue button
            COLORREF top = custom(MU_COLOR_GRADIENT_TOP).value_or(darkTheme ? RGB(0x2A, 0x2F, 0x3A) : RGB(0xEE, 0xAB, 0x37));
            COLORREF bottom = custom(MU_COLOR_GRADIENT_BOTTOM).value_or(darkTheme ? RGB(0x12, 0x14, 0x19) : RGB(0xFF, 0xE0, 0x89));

            // the other defaults follow the actual background: a gradient set for both themes keeps its readable text
            Theme theme;
            theme.dark = (Luminance(top) + Luminance(bottom)) / 2.0f < 0.45f;

            const COLORREF light[MU_COLOR_COUNT] = {
                top, bottom,
                RGB(0x14, 0x12, 0x0E), RGB(0x3D, 0x35, 0x28),
                RGB(0x0B, 0x4F, 0xC4), RGB(0x30, 0x88, 0xE0),
                RGB(0x00, 0x56, 0xF5), RGB(0x30, 0x76, 0xF5), RGB(0x26, 0x5C, 0xC0), RGB(0xFF, 0xFF, 0xFF),
                RGB(0x00, 0x56, 0xF5), RGB(0xFF, 0xFF, 0xFF),
                RGB(0xFF, 0xFF, 0xFF), RGB(0x00, 0x00, 0x00),
                RGB(0xB7, 0x1C, 0x1C), RGB(0x2E, 0x7D, 0x32),
                RGB(0xFF, 0xFF, 0xFF),
            };
            const COLORREF dark[MU_COLOR_COUNT] = {
                top, bottom,
                RGB(0xF5, 0xF6, 0xF8), RGB(0xB4, 0xB9, 0xC4),
                RGB(0x6C, 0xC4, 0xFF), RGB(0xA6, 0xDC, 0xFF),
                RGB(0x00, 0x56, 0xF5), RGB(0x30, 0x76, 0xF5), RGB(0x26, 0x5C, 0xC0), RGB(0xFF, 0xFF, 0xFF),
                RGB(0x30, 0x9C, 0xFF), RGB(0xFF, 0xFF, 0xFF),
                RGB(0xFF, 0xFF, 0xFF), RGB(0xFF, 0xFF, 0xFF),
                RGB(0xFF, 0x6B, 0x6B), RGB(0x4C, 0xD9, 0x64),
                Mix(top, RGB(0xFF, 0xFF, 0xFF), 0.08f),
            };

            COLORREF resolved[MU_COLOR_COUNT];
            for (int i = 0; i < MU_COLOR_COUNT; i++)
            {
                resolved[i] = custom(i).value_or(theme.dark ? dark[i] : light[i]);
                theme.color[i] = ToColor(resolved[i]);
            }
            // small marks in the button color need more contrast on dark backgrounds
            theme.accent = ToColor(theme.dark ? Mix(resolved[MU_COLOR_BUTTON], RGB(0xFF, 0xFF, 0xFF), 0.3f) : resolved[MU_COLOR_BUTTON]);

            if (theme.dark)
            {
                theme.panelAlpha = 0.08f;
                theme.borderAlpha = 0.16f;
                theme.trackAlpha = 0.14f;
                theme.popupAlpha = 0.88f;
                theme.popupBorderAlpha = 0.14f;
            }
            return theme;
        }

        // Soft shadow below popups: a blurred rounded rectangle at half resolution
        struct Shadow
        {
            ComPtr<ID2D1Bitmap> bitmap;
            float width = 0, height = 0;    // shape it was made for
            float pad = 0;                  // blur margin around the shape
            D2D1_SIZE_F size = {};          // bitmap size in DIP
        };

        bool CreateShadow(Graphics& graphics, ID2D1RenderTarget* target, Shadow& shadow, float width, float height, float radius, float blur, float dpi)
        {
            const float scale = dpi / 96.0f / 2.0f;
            const float pad = blur * 2.5f;
            UINT w = std::max(1u, static_cast<UINT>(std::ceil((width + pad * 2) * scale)));
            UINT h = std::max(1u, static_cast<UINT>(std::ceil((height + pad * 2) * scale)));

            ComPtr<IWICBitmap> bitmap;
            ComPtr<ID2D1RenderTarget> rt;
            ComPtr<ID2D1SolidColorBrush> brush;
            auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f * scale, 96.0f * scale);
            if (FAILED(graphics.wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap)) ||
                FAILED(graphics.d2d->CreateWicBitmapRenderTarget(bitmap.Get(), props, &rt)) ||
                FAILED(rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush)))
            {
                return false;
            }

            rt->BeginDraw();
            rt->Clear(D2D1::ColorF(0, 0, 0, 0));
            rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(pad, pad, pad + width, pad + height), radius, radius), brush.Get());
            if (FAILED(rt->EndDraw()))
                return false;
            BlurBitmap(bitmap.Get(), std::max(1, static_cast<int>(std::lround(blur * scale))), 3);

            shadow = {};
            if (FAILED(target->CreateBitmapFromWicBitmap(bitmap.Get(), nullptr, &shadow.bitmap)))
                return false;
            shadow.width = width;
            shadow.height = height;
            shadow.pad = pad;
            shadow.size = D2D1::SizeF(w / scale, h / scale);
            return true;
        }

        // ---- per render target resources

        struct Canvas
        {
            ComPtr<ID2D1RenderTarget> rt;
            ComPtr<ID2D1SolidColorBrush> brush;
            ComPtr<ID2D1SolidColorBrush> linkBrush;
            ComPtr<ID2D1SolidColorBrush> linkHoverBrush;
            ComPtr<ID2D1Bitmap> logo;
            D2D1_SIZE_U logoSize = {};
            ComPtr<ID2D1Bitmap> background;
            D2D1_SIZE_U backgroundSize = {};
            ComPtr<ID2D1Bitmap> backdrop;                   // the background picture blurred, shown behind the texts
            D2D1_SIZE_U backdropSize = {};
            ComPtr<ID2D1BitmapBrush> backdropBrush;
            std::map<UINT, ComPtr<ID2D1Bitmap>> shields;   // by pixel size
            ComPtr<ID2D1Bitmap> blurred;
            ComPtr<ID2D1BitmapBrush> acrylic;               // the blurred page, behind the location list
            Shadow listShadow, tooltipShadow;
            ComPtr<ID2D1Layer> layer;
            float dpi = 96.0f;

            bool Init(ID2D1RenderTarget* target, float targetDpi)
            {
                rt = target;
                dpi = targetDpi;
                rt->SetDpi(dpi, dpi);
                rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
                return SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush)) &&
                       SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &linkBrush)) &&
                       SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &linkHoverBrush));
            }

            void DropBitmaps()
            {
                logo.Reset();
                background.Reset();
                backdrop.Reset();
                backdropBrush.Reset();
                backdropSize = {};
                shields.clear();
                DropBlur();
                listShadow = {};
                tooltipShadow = {};
                logoSize = backgroundSize = {};
            }

            void DropBlur()
            {
                blurred.Reset();
                acrylic.Reset();
            }

            ID2D1SolidColorBrush* Brush(const D2D1_COLOR_F& color)
            {
                brush->SetColor(color);
                return brush.Get();
            }

            // Everything drawn until PopLayer is blended with 'opacity'. Returns false when no layer was pushed.
            bool PushOpacity(float opacity)
            {
                if (opacity >= 0.999f || (!layer && FAILED(rt->CreateLayer(nullptr, &layer))))
                    return false;
                rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), opacity), layer.Get());
                return true;
            }

            UINT Pixels(float dip) const
            {
                return static_cast<UINT>(std::lround(dip * dpi / 96.0f));
            }
        };

        enum class Page
        {
            Main,
            Progress,
            Result,
        };

        enum class Action
        {
            None,
            Minimize,
            Close,
            Location,
            Browse,
            IniToggle,
            Install,
            Cancel,
            Retry,
            Launch,
            Done,
            Link,
        };

        struct Hit
        {
            Action action = Action::None;
            int index = 0;
            std::wstring url;

            bool operator==(const Hit& other) const { return action == other.action && index == other.index && url == other.url; }
        };

        struct TextBlock
        {
            ComPtr<IDWriteTextLayout> layout;
            D2D1_POINT_2F origin = {};
            float width = 0;
            float height = 0;
            RichText rich;
            int id = 0;
        };

        struct Zone
        {
            D2D1_RECT_F rect;
            Action action;
            bool focusable;
        };

        class InstallerWindow
        {
        public:
            InstallerWindow(Config& config, const CommandLine& commandLine) : config(config), commandLine(commandLine)
            {
                keepSettings = config.iniMode != IniMode::Replace;
                resolvedTheme = ResolveTheme(config);
                theme = MakeTheme(config, resolvedTheme);
            }

            ~InstallerWindow()
            {
                job.reset();
                if (taskbar)
                    taskbar->Release();
                if (windowIcon && ownsWindowIcon)
                    DestroyIcon(windowIcon);
            }

            int Run();
            bool RenderPreview(const std::wstring& page, UINT previewDpi, const std::filesystem::path& png);

        private:
            // setup
            bool Init();
            void LoadImages();
            void LoadLogo();
            void CreateTextFormats();
            D2D1_SIZE_F ComputeWindowSize();
            bool EnsureTarget();
            void DiscardTarget();
            void OnSystemThemeChanged();

            // state
            void SelectLocation(int index);
            IniMode CurrentIniMode() const;
            void StartInstall(const std::filesystem::path& path);
            void MakeSnapshot(UINT width, UINT height, float snapshotDpi);
            void RefreshSnapshot();
            void Finish(int code);
            int ResultCode() const;
            Progress CurrentProgress() const;
            std::wstring CurrentStatus() const;

            // layout and drawing
            void Layout();
            void LayoutMain(float W, float H);
            void LayoutProgress(float W, float H);
            void LayoutResult(float W, float H);
            void LayoutList();
            float MeasureMainHeight(float W);
            float LocationBoxWidth(float W) const;
            TextBlock LocationText(int index, float boxWidth);
            static float LocationBoxHeight(const TextBlock& path) { return std::max(kBoxMinHeight, std::ceil(path.height + 28.0f)); }
            void FitWindowToContent();
            TextBlock MakeText(const std::wstring& text, IDWriteTextFormat* format, float width, DWRITE_TEXT_ALIGNMENT alignment, bool rich, int id);
            void Draw(Canvas& canvas, bool snapshotPass = false);
            void DrawBackground(Canvas& canvas);
            ComPtr<ID2D1Bitmap> CreateBackgroundBitmap(Canvas& canvas, UINT width, UINT height, float blur);
            void DrawTextBackdrops(Canvas& canvas, UINT width, UINT height);
            void DrawBlurredBackground(Canvas& canvas);
            ID2D1Bitmap* BlurredBitmap(Canvas& canvas);
            ID2D1BitmapBrush* AcrylicBrush(Canvas& canvas);
            void DrawMain(Canvas& canvas, bool snapshot);
            void DrawLocationBox(Canvas& canvas, bool snapshot);
            void DrawBrowseButton(Canvas& canvas, bool snapshot);
            void DrawList(Canvas& canvas);
            void DrawListRow(Canvas& canvas, const D2D1_RECT_F& row, int index);
            void DrawTooltip(Canvas& canvas);
            void DrawProgress(Canvas& canvas);
            void DrawResult(Canvas& canvas);
            void DrawWindowButtons(Canvas& canvas);
            void PaintText(Canvas& canvas, TextBlock& block, const D2D1_COLOR_F& color);
            void DrawTextLine(Canvas& canvas, const std::wstring& text, IDWriteTextFormat* format, float left, float cy, float width, const D2D1_COLOR_F& color);
            float DrawPill(Canvas& canvas, float right, float cy, float height, const std::wstring& text, const D2D1_COLOR_F& fill, const D2D1_COLOR_F& textColor);
            void DrawPanelBox(Canvas& canvas, const D2D1_RECT_F& box, bool hovered, bool down, bool active);
            void DrawFolderGlyph(Canvas& canvas, float x, float cy, const D2D1_COLOR_F& color);
            void DrawGlyph(Canvas& canvas, ID2D1Geometry* glyph, const D2D1_MATRIX_3X2_F& transform, const D2D1_COLOR_F& color, float width);
            void DrawShield(Canvas& canvas, float x, float y, float size, float opacity);
            void DrawShadow(Canvas& canvas, Shadow& shadow, const D2D1_RECT_F& rect, float radius, float blur, float offset, float opacity);
            void DrawButton(Canvas& canvas, const D2D1_RECT_F& rect, const std::wstring& text, Action action, bool primary, bool enabled, bool shield = false);
            void DrawFocus(Canvas& canvas, const D2D1_RECT_F& rect, float radius);
            void DrawLogo(Canvas& canvas, const D2D1_RECT_F& rect);

            // input
            static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
            LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
            Hit HitTest(D2D1_POINT_2F point);
            D2D1_POINT_2F ToDip(POINT pt) const;
            void Activate(const Hit& hit);
            void MoveFocus(int direction);
            void SetHover(const Hit& hit);
            void RefreshHover();
            void OnLocation();
            void OnBrowse();
            void OnInstall();
            void RequestClose();
            bool Confirm(const std::wstring& text);
            void Invalidate();
            void OnTimer();
            void UpdateTaskbar();
            void OnDpiChanged(UINT newDpi, const RECT* suggested);

            // list of install locations below the location box
            void OpenList(bool keyboard);
            void CloseList();
            D2D1_RECT_F ListViewport() const;
            int ListRowAt(D2D1_POINT_2F point);
            void ScrollToRow(int row);
            void OnListKey(WPARAM key);
            void OnListMouse(UINT message, D2D1_POINT_2F point);
            void UpdateTooltip();
            void StartTransition();
            bool IsTransitionRunning() const;

            bool IsHover(Action action) const { return hover.action == action; }
            bool IsPressed(Action action) const { return pressed.action == action && hover.action == action; }
            bool IsFocused(Action action) const;

            Config& config;
            CommandLine commandLine;
            Graphics graphics;
            int resolvedTheme = MU_THEME_LIGHT;
            Theme theme;

            HWND hwnd = nullptr;
            UINT dpi = 96;
            D2D1_SIZE_F windowSize = {};
            ComPtr<ID2D1HwndRenderTarget> hwndTarget;
            Canvas screen;
            HICON windowIcon = nullptr;
            bool ownsWindowIcon = false;
            ITaskbarList3* taskbar = nullptr;
            UINT taskbarCreatedMessage = 0;

            // fonts
            std::wstring displayFamily, textFamily;
            ComPtr<IDWriteTextFormat> fmtHeading, fmtTitle, fmtBody, fmtLabel, fmtPath, fmtSmall, fmtButton, fmtFooter, fmtItem, fmtItemPath;

            // images
            ComPtr<IWICBitmap> logoImage, backgroundImage, snapshot;

            // install location
            std::vector<Location> locations;
            int selected = -1;
            PathCheck check;
            bool keepSettings = true;
            bool elevationDenied = false;

            // installation
            Page page = Page::Main;
            std::unique_ptr<Job> job;
            std::filesystem::path target;
            Outcome outcome;
            bool closeAfterCancel = false;
            float shownFraction = 0.0f;
            float marqueePhase = 0.0f;
            ULONGLONG lastTick = 0;
            std::optional<Progress> previewProgress;
            int result = MU_INSTALL_CANCELLED;

            // layout
            bool layoutDirty = true;
            std::vector<Zone> zones;
            std::vector<TextBlock*> linkBlocks;
            struct
            {
                D2D1_RECT_F logo = {};
                TextBlock heading, content, label, note, footer, checkbox, path;
                D2D1_RECT_F location = {}, browse = {}, checkboxBox = {}, checkboxRow = {}, button = {};
            } main;
            struct
            {
                bool open = false;
                bool above = false;         // opens above the location box when there is no room below
                int hot = -1;               // row under the mouse or selected with the keyboard
                int pressed = -1;
                float scroll = 0.0f;
                ULONGLONG openTick = 0;
                D2D1_RECT_F panel = {};
                std::vector<PathCheck> checks;
                std::vector<std::wstring> titles;
                std::vector<TextBlock> paths;       // full paths, wrapped
                std::vector<float> rowTops;         // top of each row and the end of the last one
            } list;
            struct
            {
                bool armed = false;         // the mouse rested on the button long enough
                bool visible = false;
                ULONGLONG shownTick = 0;
            } tooltip;
            bool transitionTimer = false;
            POINT lastMouse = { -1, -1 };
            struct
            {
                TextBlock title, status, line, detail;
                D2D1_RECT_F bar = {}, cancel = {};
            } progress;
            struct
            {
                D2D1_RECT_F icon = {};
                TextBlock title, message, links;
                D2D1_RECT_F primary = {}, secondary = {};
                Action primaryAction = Action::Done, secondaryAction = Action::None;
                std::wstring primaryText, secondaryText;
            } resultPage;

            // interaction
            Hit hover, pressed;
            int focus = -1;
            bool keyboardCues = false;
            bool trackingMouse = false;
        };

        bool InstallerWindow::Init()
        {
            if (!graphics.Init(config))
                return false;

            // Segoe UI Variable on Windows 11, Segoe UI before
            if (!config.fontFamily.empty())
            {
                displayFamily = textFamily = config.fontFamily;
            }
            else if (graphics.HasSystemFont(L"Segoe UI Variable Display"))
            {
                displayFamily = L"Segoe UI Variable Display";
                textFamily = L"Segoe UI Variable Text";
            }
            else
            {
                displayFamily = textFamily = L"Segoe UI";
            }

            CreateTextFormats();
            LoadImages();

            locations = DetectLocations(config);
            if (!commandLine.installDir.empty())
            {
                auto dir = std::filesystem::path(commandLine.installDir).lexically_normal();
                auto it = std::find_if(locations.begin(), locations.end(), [&](const Location& l) { return iequals(l.path.wstring(), dir.wstring()); });
                if (it == locations.end())
                {
                    locations.insert(locations.begin(), { dir, L"" });
                    it = locations.begin();
                }
                SelectLocation(static_cast<int>(it - locations.begin()));
            }
            else if (!locations.empty())
            {
                SelectLocation(0);
            }
            return true;
        }

        void InstallerWindow::CreateTextFormats()
        {
            auto make = [&](const std::wstring& family, float size, DWRITE_FONT_WEIGHT weight)
            {
                ComPtr<IDWriteTextFormat> format;
                IDWriteFontCollection* collection = nullptr;
                if (graphics.privateFonts)
                {
                    UINT32 index = 0;
                    BOOL exists = FALSE;
                    if (SUCCEEDED(graphics.privateFonts->FindFamilyName(family.c_str(), &index, &exists)) && exists)
                        collection = graphics.privateFonts.Get();
                }
                graphics.dwrite->CreateTextFormat(family.c_str(), collection, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"", &format);
                return format;
            };

            fmtHeading = make(displayFamily, 28.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            fmtTitle = make(displayFamily, 21.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            fmtBody = make(textFamily, 13.5f, DWRITE_FONT_WEIGHT_NORMAL);
            fmtLabel = make(textFamily, 12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            fmtPath = make(textFamily, 13.0f, DWRITE_FONT_WEIGHT_NORMAL);
            fmtSmall = make(textFamily, 12.0f, DWRITE_FONT_WEIGHT_NORMAL);
            fmtButton = make(displayFamily, 16.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            fmtFooter = make(textFamily, 11.5f, DWRITE_FONT_WEIGHT_NORMAL);
            fmtItem = make(textFamily, 13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            fmtItemPath = make(textFamily, 12.0f, DWRITE_FONT_WEIGHT_NORMAL);

            // names of locations stay on one line, the full path is shown below them
            ComPtr<IDWriteInlineObject> ellipsis;
            if (fmtItem && SUCCEEDED(graphics.dwrite->CreateEllipsisTrimmingSign(fmtItem.Get(), &ellipsis)))
            {
                DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
                fmtItem->SetTrimming(&trimming, ellipsis.Get());
                fmtItem->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            }
        }

        // logo and background of the current theme
        void InstallerWindow::LoadImages()
        {
            LoadLogo();
            auto& themed = config.Overrides(resolvedTheme).background;
            backgroundImage = DecodeImage(graphics.wic.Get(), themed.empty() ? config.background : themed);
        }

        void InstallerWindow::LoadLogo()
        {
            auto& themed = config.Overrides(resolvedTheme).logo;
            logoImage = DecodeImage(graphics.wic.Get(), themed.empty() ? config.logo : themed);
            if (!logoImage && themed.empty() && config.logo.empty() && config.icon)
                logoImage = IconToBitmap(graphics.wic.Get(), config.icon);
        }

        void InstallerWindow::OnSystemThemeChanged()
        {
            int next = ResolveTheme(config);
            if (config.theme != MU_THEME_AUTO || next == resolvedTheme)
                return;

            Log(L"Windows switched to the {} mode", next == MU_THEME_DARK ? L"dark" : L"light");
            resolvedTheme = next;
            theme = MakeTheme(config, resolvedTheme);
            LoadImages();
            screen.DropBitmaps();
            ui::SetWindowDarkMode(hwnd, theme.dark);
            if (snapshot || list.open)
                RefreshSnapshot(); // blurred background of the progress page and the list
            layoutDirty = true;
            Invalidate();
        }

        void InstallerWindow::SelectLocation(int index)
        {
            selected = index;
            elevationDenied = false;
            if (selected >= 0 && selected < static_cast<int>(locations.size()))
                check = CheckPath(config, locations[selected].path);
            else
                check = {};
            layoutDirty = true;
            FitWindowToContent();
            Invalidate();
        }

        IniMode InstallerWindow::CurrentIniMode() const
        {
            if (!config.iniSelectable)
                return config.iniMode;
            if (!keepSettings)
                return IniMode::Replace;
            return config.iniMode == IniMode::Replace ? IniMode::Merge : config.iniMode;
        }

        TextBlock InstallerWindow::MakeText(const std::wstring& text, IDWriteTextFormat* format, float width, DWRITE_TEXT_ALIGNMENT alignment, bool rich, int id)
        {
            TextBlock block;
            block.id = id;
            if (text.empty() || !format)
                return block;

            block.rich = rich ? ParseRichText(text) : RichText{ text, {} };
            if (FAILED(graphics.dwrite->CreateTextLayout(block.rich.text.c_str(), static_cast<UINT32>(block.rich.text.size()), format, std::max(1.0f, width), 10000.0f, &block.layout)))
                return block;

            block.layout->SetTextAlignment(alignment);
            for (auto& link : block.rich.links)
                block.layout->SetUnderline(TRUE, { link.start, link.length });

            DWRITE_TEXT_METRICS metrics = {};
            block.layout->GetMetrics(&metrics);
            block.width = width;
            block.height = metrics.height;
            return block;
        }

        float InstallerWindow::MeasureMainHeight(float W)
        {
            LayoutMain(W, 0.0f);
            return main.footer.origin.y + main.footer.height + kBottomPadding;
        }

        void InstallerWindow::LayoutMain(float W, float H)
        {
            const float contentWidth = std::min(W - kSidePadding * 2, 620.0f);
            const float cx = W / 2;

            // sizes
            float logoWidth = 0, logoHeight = 0;
            if (logoImage)
            {
                UINT iw = 0, ih = 0;
                logoImage->GetSize(&iw, &ih);
                if (iw && ih)
                {
                    float maxWidth = std::min(contentWidth * 0.8f, 460.0f);
                    float maxHeight = config.content.empty() ? 150.0f : 118.0f;
                    float scale = std::min(maxWidth / iw, maxHeight / ih);
                    scale = std::min(scale, 3.0f); // don't blow up tiny images too much
                    logoWidth = iw * scale;
                    logoHeight = ih * scale;
                }
            }

            main.heading = MakeText(Text(config, MU_STR_HEADING), fmtHeading.Get(), contentWidth, DWRITE_TEXT_ALIGNMENT_CENTER, false, 0);
            main.content = MakeText(config.content, fmtBody.Get(), contentWidth, DWRITE_TEXT_ALIGNMENT_CENTER, true, 1);
            main.label = MakeText(config.mainInstruction, fmtLabel.Get(), contentWidth, DWRITE_TEXT_ALIGNMENT_CENTER, false, 2);
            main.footer = MakeText(config.footer, fmtFooter.Get(), W - kSidePadding * 2, DWRITE_TEXT_ALIGNMENT_CENTER, true, 3);

            std::wstring note;
            if (selected >= 0)
            {
                if (!check.hasGameExe)
                    note = L"\u26A0 " + Text(config, MU_STR_EXE_MISSING);
                else if (elevationDenied)
                    note = L"Administrator permissions were not granted.";
                else if (!check.writable)
                    note = Text(config, MU_STR_ADMIN_REQUIRED);
            }
            main.note = MakeText(note, fmtSmall.Get(), contentWidth, DWRITE_TEXT_ALIGNMENT_CENTER, false, 4);
            main.checkbox = MakeText(config.iniSelectable ? Text(config, MU_STR_KEEP_SETTINGS) : L"", fmtBody.Get(), contentWidth, DWRITE_TEXT_ALIGNMENT_LEADING, false, 5);

            // the location box grows with the lines of the path
            const float boxWidth = std::min(contentWidth, 560.0f);
            main.path = LocationText(selected, LocationBoxWidth(W));
            const float boxHeight = LocationBoxHeight(main.path);
            const float noteHeight = 22.0f;
            const float buttonHeight = 50.0f;

            float height = 0;
            if (logoHeight > 0)
                height += logoHeight + 18.0f;
            if (main.heading.layout)
                height += main.heading.height + 10.0f;
            if (main.content.layout)
                height += main.content.height + 22.0f;
            if (main.label.layout)
                height += main.label.height + 8.0f;
            height += boxHeight + noteHeight + 8.0f;
            if (main.checkbox.layout)
                height += 26.0f + 12.0f;
            height += buttonHeight;

            const float footerSpace = (main.footer.layout ? main.footer.height + 22.0f : 0.0f) + kBottomPadding;
            float y = kTopPadding;
            if (H > 0)
                y += std::max(0.0f, (H - kTopPadding - footerSpace - 16.0f - height) / 2.0f);

            if (logoHeight > 0)
            {
                main.logo = CenteredRect(cx, y, logoWidth, logoHeight);
                y += logoHeight + 18.0f;
            }
            else
            {
                main.logo = {};
            }

            auto place = [&](TextBlock& block, float gap)
            {
                if (!block.layout)
                    return;
                block.origin = D2D1::Point2F(cx - block.width / 2, y);
                y += block.height + gap;
            };
            place(main.heading, 10.0f);
            place(main.content, 22.0f);
            place(main.label, 8.0f);

            // location box and the browse button next to it
            main.location = CenteredRect(cx, y, boxWidth, boxHeight);
            main.browse = D2D1::RectF(main.location.right - kBrowseWidth, y, main.location.right, y + boxHeight);
            main.location.right = main.browse.left - 8.0f;
            y += boxHeight;
            if (main.note.layout)
                main.note.origin = D2D1::Point2F(cx - main.note.width / 2, y + 4.0f);
            y += noteHeight + 8.0f;

            if (main.checkbox.layout)
            {
                DWRITE_TEXT_METRICS metrics = {};
                main.checkbox.layout->GetMetrics(&metrics);
                float rowWidth = 18.0f + 10.0f + metrics.widthIncludingTrailingWhitespace;
                main.checkboxRow = CenteredRect(cx, y, rowWidth, 26.0f);
                main.checkboxBox = D2D1::RectF(main.checkboxRow.left, y + 4.0f, main.checkboxRow.left + 18.0f, y + 22.0f);
                main.checkbox.origin = D2D1::Point2F(main.checkboxBox.right + 10.0f - metrics.left, y + 13.0f - main.checkbox.height / 2);
                y += 26.0f + 12.0f;
            }
            else
            {
                main.checkboxRow = main.checkboxBox = {};
            }

            main.button = CenteredRect(cx, y, 230.0f, buttonHeight);
            y += buttonHeight;

            if (main.footer.layout)
            {
                float footerTop = y + 22.0f;
                if (H > 0)
                    footerTop = std::max(footerTop, H - kBottomPadding - main.footer.height);
                main.footer.origin = D2D1::Point2F(cx - main.footer.width / 2, footerTop);
            }
            else
            {
                main.footer.origin = D2D1::Point2F(0, y + 4.0f);
                main.footer.height = 0;
            }

            zones.push_back({ main.location, Action::Location, true });
            zones.push_back({ main.browse, Action::Browse, true });
            if (main.checkbox.layout)
                zones.push_back({ main.checkboxRow, Action::IniToggle, true });
            zones.push_back({ main.button, Action::Install, true });
            linkBlocks = { &main.content, &main.footer };
        }

        void InstallerWindow::LayoutProgress(float W, float H)
        {
            const float contentWidth = std::min(W - kSidePadding * 2, 520.0f);
            const float cx = W / 2;
            auto p = CurrentProgress();

            progress.title = MakeText(Text(config, MU_STR_INSTALLING), fmtTitle.Get(), contentWidth, DWRITE_TEXT_ALIGNMENT_CENTER, false, 20);
            progress.status = MakeText(CurrentStatus(), fmtBody.Get(), contentWidth, DWRITE_TEXT_ALIGNMENT_CENTER, false, 21);
            progress.line = MakeText(ProgressLine(p), fmtSmall.Get(), contentWidth, DWRITE_TEXT_ALIGNMENT_CENTER, false, 22);
            progress.detail = MakeText(p.phase == Phase::Extracting ? WrappablePath(p.file, L'/') : L"", fmtSmall.Get(), contentWidth, DWRITE_TEXT_ALIGNMENT_CENTER, false, 23);

            // room for a file path of two lines, the layout doesn't jump with the file names
            const float lineHeight = 18.0f;
            float total = progress.title.height + 12.0f + std::max(progress.status.height, 20.0f) + 16.0f + 8.0f + 12.0f + lineHeight * 3 + 20.0f + 40.0f;
            float y = std::max(kTopPadding, (H - total) / 2.0f);

            progress.title.origin = D2D1::Point2F(cx - contentWidth / 2, y);
            y += progress.title.height + 12.0f;
            progress.status.origin = D2D1::Point2F(cx - contentWidth / 2, y);
            y += std::max(progress.status.height, 20.0f) + 16.0f;
            progress.bar = CenteredRect(cx, y, contentWidth, 8.0f);
            y += 8.0f + 12.0f;
            progress.line.origin = D2D1::Point2F(cx - contentWidth / 2, y);
            progress.detail.origin = D2D1::Point2F(cx - contentWidth / 2, y + lineHeight);
            y += lineHeight * 3 + 20.0f;
            progress.cancel = CenteredRect(cx, y, 150.0f, 40.0f);

            zones.push_back({ progress.cancel, Action::Cancel, true });
            linkBlocks.clear();
        }

        void InstallerWindow::LayoutResult(float W, float H)
        {
            const float contentWidth = std::min(W - kSidePadding * 2, 560.0f);
            const float cx = W / 2;

            int titleId = outcome.result == Result::Success ? MU_STR_COMPLETE : outcome.result == Result::Failed ? MU_STR_FAILED : MU_STR_CANCELLED;
            resultPage.title = MakeText(Text(config, titleId), fmtTitle.Get(), contentWidth, DWRITE_TEXT_ALIGNMENT_CENTER, false, 30);
            resultPage.message = MakeText(WrappablePath(DescribeOutcome(config, outcome, target), L'\\', true), fmtBody.Get(), contentWidth, DWRITE_TEXT_ALIGNMENT_CENTER, false, 31);

            std::wstring links;
            if (outcome.result == Result::Success)
                links = L"<a href=\"action:open\">" + Text(config, MU_STR_OPEN_FOLDER) + L"</a>";
            if (!LogGetFile().empty() && outcome.result != Result::Success)
                links += (links.empty() ? L"" : L"      ") + std::wstring(L"<a href=\"action:log\">") + Text(config, MU_STR_VIEW_LOG) + L"</a>";
            resultPage.links = MakeText(links, fmtSmall.Get(), contentWidth, DWRITE_TEXT_ALIGNMENT_CENTER, true, 32);

            // buttons
            resultPage.secondaryAction = Action::None;
            if (outcome.result == Result::Success)
            {
                auto launch = LaunchText(config, target);
                if (!launch.empty())
                {
                    resultPage.primaryAction = Action::Launch;
                    resultPage.primaryText = launch;
                    resultPage.secondaryAction = Action::Done;
                    resultPage.secondaryText = Text(config, MU_STR_CLOSE);
                }
                else
                {
                    resultPage.primaryAction = Action::Done;
                    resultPage.primaryText = Text(config, MU_STR_CLOSE);
                }
            }
            else
            {
                resultPage.primaryAction = Action::Retry;
                resultPage.primaryText = Text(config, MU_STR_RETRY);
                resultPage.secondaryAction = Action::Done;
                resultPage.secondaryText = Text(config, MU_STR_CLOSE);
            }

            auto measure = [&](const std::wstring& text)
            {
                auto block = MakeText(text, fmtButton.Get(), 1000.0f, DWRITE_TEXT_ALIGNMENT_LEADING, false, 0);
                DWRITE_TEXT_METRICS metrics = {};
                if (block.layout)
                    block.layout->GetMetrics(&metrics);
                return metrics.widthIncludingTrailingWhitespace;
            };
            float primaryWidth = std::max(170.0f, measure(resultPage.primaryText) + 56.0f);
            float secondaryWidth = resultPage.secondaryAction != Action::None ? std::max(130.0f, measure(resultPage.secondaryText) + 48.0f) : 0.0f;
            float rowWidth = primaryWidth + (secondaryWidth > 0 ? 12.0f + secondaryWidth : 0.0f);

            const float iconSize = 64.0f;
            float total = iconSize + 18.0f + resultPage.title.height + 10.0f + resultPage.message.height + 26.0f + 46.0f + (resultPage.links.layout ? 18.0f + resultPage.links.height : 0.0f);
            float y = std::max(kTopPadding, (H - total) / 2.0f);

            resultPage.icon = CenteredRect(cx, y, iconSize, iconSize);
            y += iconSize + 18.0f;
            resultPage.title.origin = D2D1::Point2F(cx - contentWidth / 2, y);
            y += resultPage.title.height + 10.0f;
            if (resultPage.message.layout)
            {
                resultPage.message.origin = D2D1::Point2F(cx - contentWidth / 2, y);
                y += resultPage.message.height;
            }
            y += 26.0f;

            float x = cx - rowWidth / 2;
            resultPage.primary = D2D1::RectF(x, y, x + primaryWidth, y + 46.0f);
            resultPage.secondary = secondaryWidth > 0 ? D2D1::RectF(x + primaryWidth + 12.0f, y, x + rowWidth, y + 46.0f) : D2D1_RECT_F{};
            y += 46.0f + 18.0f;
            if (resultPage.links.layout)
                resultPage.links.origin = D2D1::Point2F(cx - contentWidth / 2, y);

            zones.push_back({ resultPage.primary, resultPage.primaryAction, true });
            if (resultPage.secondaryAction != Action::None)
                zones.push_back({ resultPage.secondary, resultPage.secondaryAction, true });
            linkBlocks = { &resultPage.links };
        }

        void InstallerWindow::Layout()
        {
            if (!layoutDirty)
                return;
            layoutDirty = false;
            zones.clear();
            linkBlocks.clear();

            float W = windowSize.width, H = windowSize.height;
            switch (page)
            {
            case Page::Main: LayoutMain(W, H); break;
            case Page::Progress: LayoutProgress(W, H); break;
            case Page::Result: LayoutResult(W, H); break;
            }
            if (page == Page::Main && list.open)
                LayoutList();

            // window buttons come first in hit testing
            zones.insert(zones.begin(), { D2D1::RectF(W - kWindowButtonWidth, 0, W, kWindowButtonHeight), Action::Close, false });
            zones.insert(zones.begin(), { D2D1::RectF(W - kWindowButtonWidth * 2, 0, W - kWindowButtonWidth, kWindowButtonHeight), Action::Minimize, false });

            int focusable = static_cast<int>(std::count_if(zones.begin(), zones.end(), [](const Zone& z) { return z.focusable; }));
            if (focus >= focusable)
                focus = -1;
        }

        // location box without the browse button
        float InstallerWindow::LocationBoxWidth(float W) const
        {
            return std::min(std::min(W - kSidePadding * 2, 620.0f), 560.0f) - kBrowseWidth - 8.0f;
        }

        // Full path of a location (-1: none chosen yet) at the width it has between the folder glyph and the source
        TextBlock InstallerWindow::LocationText(int index, float boxWidth)
        {
            float width = boxWidth - 46.0f - (locations.empty() ? 16.0f : 36.0f);
            if (index >= 0 && !locations[index].source.empty())
            {
                auto source = MakeText(locations[index].source, fmtSmall.Get(), 400.0f, DWRITE_TEXT_ALIGNMENT_LEADING, false, 0);
                DWRITE_TEXT_METRICS metrics = {};
                if (source.layout)
                    source.layout->GetMetrics(&metrics);
                width -= metrics.widthIncludingTrailingWhitespace + 16.0f + 10.0f;
            }
            auto text = index >= 0 ? WrappablePath(locations[index].path.wstring()) : Text(config, MU_STR_CHOOSE_FOLDER);
            return MakeText(text, fmtPath.Get(), std::max(60.0f, width), DWRITE_TEXT_ALIGNMENT_LEADING, false, 6);
        }

        // A folder chosen later can have a longer path than the window was made for
        void InstallerWindow::FitWindowToContent()
        {
            if (!hwnd || page != Page::Main)
                return;
            float needed = std::round(MeasureMainHeight(windowSize.width));
            zones.clear();
            linkBlocks.clear();
            layoutDirty = true;
            if (needed <= windowSize.height)
                return;

            HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO info = { sizeof(info) };
            GetMonitorInfoW(monitor, &info);
            float maxHeight = std::floor((info.rcWork.bottom - info.rcWork.top) * 96.0f / dpi * 0.94f);
            if (maxHeight <= windowSize.height)
                return;
            windowSize.height = std::min(needed, maxHeight);

            RECT rc = {};
            GetWindowRect(hwnd, &rc);
            int height = static_cast<int>(std::lround(windowSize.height * dpi / 96.0f));
            int top = std::max(info.rcWork.top, std::min(rc.top, info.rcWork.bottom - height));
            SetWindowPos(hwnd, nullptr, rc.left, top, rc.right - rc.left, height, SWP_NOZORDER | SWP_NOACTIVATE);
            Log(L"The installer window grew to {} for the path of the install folder", height);
        }

        void InstallerWindow::LayoutList()
        {
            // below the location box, above it when there is more room there
            const auto box = main.location;
            const float content = list.rowTops.empty() ? 0.0f : list.rowTops.back();
            const float firstRow = list.rowTops.size() > 1 ? list.rowTops[1] : kListRowHeight;
            const float wanted = std::min(content, std::max(kListMaxHeight, firstRow)) + kListPadding * 2;
            const float below = windowSize.height - 12.0f - (box.bottom + kListGap);
            const float above = box.top - kListGap - (kWindowButtonHeight + 6.0f);
            list.above = wanted > below && above > below;

            float height = std::min(wanted, std::max(list.above ? above : below, firstRow + kListPadding * 2));
            float top = list.above ? box.top - kListGap - height : box.bottom + kListGap;
            list.panel = D2D1::RectF(box.left, top, box.right, top + height);
            list.scroll = std::clamp(list.scroll, 0.0f, std::max(0.0f, content - (height - kListPadding * 2)));
        }

        D2D1_SIZE_F InstallerWindow::ComputeWindowSize()
        {
            float width = config.width > 0 ? static_cast<float>(config.width) : kDefaultWidth;
            float height = static_cast<float>(config.height);
            if (height <= 0)
            {
                // room for the longest path of the suggested folders
                float boxWidth = LocationBoxWidth(width);
                float current = LocationBoxHeight(LocationText(selected, boxWidth));
                float tallest = current;
                for (int i = 0; i < static_cast<int>(locations.size()); i++)
                    tallest = std::max(tallest, LocationBoxHeight(LocationText(i, boxWidth)));
                height = std::max(430.0f, MeasureMainHeight(width) + tallest - current);
            }
            zones.clear();
            linkBlocks.clear();
            return D2D1::SizeF(std::round(width), std::round(height));
        }

        Progress InstallerWindow::CurrentProgress() const
        {
            if (job)
                return job->GetProgress();
            return previewProgress.value_or(Progress{});
        }

        std::wstring InstallerWindow::CurrentStatus() const
        {
            if (job)
                return job->StatusText();
            auto p = CurrentProgress();
            if (p.phase == Phase::Downloading)
                return Text(config, MU_STR_DOWNLOADING, target, p.file);
            if (p.phase == Phase::Extracting)
                return Text(config, MU_STR_EXTRACTING, target, p.file);
            return Text(config, MU_STR_PREPARING);
        }

        // ---- drawing

        void InstallerWindow::DrawBackground(Canvas& canvas)
        {
            auto rt = canvas.rt.Get();
            auto size = rt->GetSize();
            auto full = D2D1::RectF(0, 0, size.width, size.height);

            float overlay = 1.0f;
            if (backgroundImage)
            {
                UINT pixelWidth = canvas.Pixels(size.width), pixelHeight = canvas.Pixels(size.height);
                if (!canvas.background || canvas.backgroundSize.width != pixelWidth || canvas.backgroundSize.height != pixelHeight)
                {
                    canvas.background = CreateBackgroundBitmap(canvas, pixelWidth, pixelHeight, static_cast<float>(config.backgroundBlur));
                    canvas.backgroundSize = D2D1::SizeU(pixelWidth, pixelHeight);
                }
                if (canvas.background)
                {
                    rt->DrawBitmap(canvas.background.Get(), full, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                    if (page == Page::Main && config.textBackdropBlur > 0)
                        DrawTextBackdrops(canvas, pixelWidth, pixelHeight);
                    overlay = std::clamp(config.Overrides(resolvedTheme).backgroundOverlay.value_or(config.backgroundOverlay), 0, 100) / 100.0f;
                }
            }

            if (overlay > 0)
            {
                D2D1_GRADIENT_STOP stops[] = {
                    { 0.0f, WithAlpha(theme.color[MU_COLOR_GRADIENT_TOP], overlay) },
                    { 1.0f, WithAlpha(theme.color[MU_COLOR_GRADIENT_BOTTOM], overlay) },
                };
                ComPtr<ID2D1GradientStopCollection> collection;
                ComPtr<ID2D1LinearGradientBrush> gradient;
                if (SUCCEEDED(rt->CreateGradientStopCollection(stops, 2, &collection)) &&
                    SUCCEEDED(rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, size.height)), collection.Get(), &gradient)))
                {
                    rt->FillRectangle(full, gradient.Get());
                }
            }
        }

        // The background picture covering a window of width x height pixels: the middle of the picture with the
        // aspect ratio of the window, blurred when asked. A strong blur is made at a lower resolution,
        // stretching the result adds to the blur.
        ComPtr<ID2D1Bitmap> InstallerWindow::CreateBackgroundBitmap(Canvas& canvas, UINT width, UINT height, float blurDip)
        {
            UINT iw = 0, ih = 0;
            backgroundImage->GetSize(&iw, &ih);
            if (!iw || !ih || !width || !height)
                return nullptr;

            const double aspect = static_cast<double>(width) / height;
            WICRect crop = { 0, 0, static_cast<INT>(iw), static_cast<INT>(ih) };
            if (static_cast<double>(iw) / ih > aspect)
            {
                crop.Width = std::max(1, static_cast<INT>(std::lround(ih * aspect)));
                crop.X = (static_cast<INT>(iw) - crop.Width) / 2;
            }
            else
            {
                crop.Height = std::max(1, static_cast<INT>(std::lround(iw / aspect)));
                crop.Y = (static_cast<INT>(ih) - crop.Height) / 2;
            }

            const float blur = blurDip * canvas.dpi / 96.0f; // pixels
            const UINT factor = blur >= 8.0f ? std::min(4u, static_cast<UINT>(blur / 4.0f)) : 1u;
            ComPtr<IWICBitmapClipper> clipper;
            ComPtr<IWICBitmapScaler> scaler;
            if (FAILED(graphics.wic->CreateBitmapClipper(&clipper)) ||
                FAILED(clipper->Initialize(backgroundImage.Get(), &crop)) ||
                FAILED(graphics.wic->CreateBitmapScaler(&scaler)) ||
                FAILED(scaler->Initialize(clipper.Get(), std::max(1u, width / factor), std::max(1u, height / factor), WICBitmapInterpolationModeFant)))
            {
                return nullptr;
            }

            ComPtr<ID2D1Bitmap> bitmap;
            if (blur > 0)
            {
                // three box blur passes of radius r are close to a gaussian blur with sigma r
                ComPtr<IWICBitmap> pixels;
                if (FAILED(graphics.wic->CreateBitmapFromSource(scaler.Get(), WICBitmapCacheOnLoad, &pixels)))
                    return nullptr;
                BlurBitmap(pixels.Get(), std::max(1, static_cast<int>(std::lround(blur / factor))), 3);
                canvas.rt->CreateBitmapFromWicBitmap(pixels.Get(), nullptr, &bitmap);
            }
            else
            {
                canvas.rt->CreateBitmapFromWicBitmap(scaler.Get(), nullptr, &bitmap);
            }
            return bitmap;
        }

        // The background picture blurred behind the lines of text of the main page, a rectangle
        // around each line like the background of subtitles, and behind the translucent install
        // location box. The rest of the picture stays sharp.
        void InstallerWindow::DrawTextBackdrops(Canvas& canvas, UINT width, UINT height)
        {
            auto rt = canvas.rt.Get();
            if (!canvas.backdrop || canvas.backdropSize.width != width || canvas.backdropSize.height != height)
            {
                canvas.backdrop = CreateBackgroundBitmap(canvas, width, height, static_cast<float>(config.textBackdropBlur));
                canvas.backdropSize = D2D1::SizeU(width, height);
                canvas.backdropBrush.Reset();
            }
            if (!canvas.backdrop || (!canvas.backdropBrush && FAILED(rt->CreateBitmapBrush(canvas.backdrop.Get(), &canvas.backdropBrush))))
                return;
            auto bitmapSize = canvas.backdrop->GetSize();
            auto size = rt->GetSize();
            canvas.backdropBrush->SetTransform(D2D1::Matrix3x2F::Scale(size.width / bitmapSize.width, size.height / bitmapSize.height));

            constexpr float padX = 6.0f, padY = 1.5f, radius = 3.0f;
            for (TextBlock* block : { &main.heading, &main.content, &main.label, &main.note, &main.checkbox, &main.footer })
            {
                if (!block->layout)
                    continue;
                // one hit test rectangle per line, empty lines have no width
                auto length = static_cast<UINT32>(block->rich.text.size());
                UINT32 count = 0;
                block->layout->HitTestTextRange(0, length, block->origin.x, block->origin.y, nullptr, 0, &count);
                std::vector<DWRITE_HIT_TEST_METRICS> lines(count);
                if (!count || FAILED(block->layout->HitTestTextRange(0, length, block->origin.x, block->origin.y, lines.data(), count, &count)))
                    continue;
                for (auto& line : lines)
                {
                    if (line.width < 1.0f)
                        continue;
                    auto rect = D2D1::RectF(line.left - padX, line.top - padY, line.left + line.width + padX, line.top + line.height + padY);
                    rt->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), canvas.backdropBrush.Get());
                }
            }

            for (auto& box : { main.location, main.browse })
            {
                if (box.right > box.left)
                    rt->FillRoundedRectangle(D2D1::RoundedRect(box, kBoxRadius, kBoxRadius), canvas.backdropBrush.Get());
            }
        }

        ID2D1Bitmap* InstallerWindow::BlurredBitmap(Canvas& canvas)
        {
            if (!canvas.blurred && snapshot)
                canvas.rt->CreateBitmapFromWicBitmap(snapshot.Get(), nullptr, &canvas.blurred);
            return canvas.blurred.Get();
        }

        // Brush that paints the blurred main page at its place in the window
        ID2D1BitmapBrush* InstallerWindow::AcrylicBrush(Canvas& canvas)
        {
            auto blurred = BlurredBitmap(canvas);
            if (!blurred || (!canvas.acrylic && FAILED(canvas.rt->CreateBitmapBrush(blurred, &canvas.acrylic))))
                return nullptr;
            auto bitmapSize = blurred->GetSize();
            auto size = canvas.rt->GetSize();
            canvas.acrylic->SetTransform(D2D1::Matrix3x2F::Scale(size.width / bitmapSize.width, size.height / bitmapSize.height));
            return canvas.acrylic.Get();
        }

        void InstallerWindow::DrawBlurredBackground(Canvas& canvas)
        {
            auto rt = canvas.rt.Get();
            auto size = rt->GetSize();
            auto full = D2D1::RectF(0, 0, size.width, size.height);
            auto blurred = BlurredBitmap(canvas);
            if (!blurred)
            {
                DrawBackground(canvas);
                return;
            }
            rt->DrawBitmap(blurred, full, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

            // frosted glass: bring the theme back over the blurred page so the text on top stays readable
            D2D1_GRADIENT_STOP stops[] = {
                { 0.0f, WithAlpha(theme.color[MU_COLOR_GRADIENT_TOP], backgroundImage ? 0.35f : 0.5f) },
                { 1.0f, WithAlpha(theme.color[MU_COLOR_GRADIENT_BOTTOM], backgroundImage ? 0.35f : 0.5f) },
            };
            ComPtr<ID2D1GradientStopCollection> collection;
            ComPtr<ID2D1LinearGradientBrush> gradient;
            if (SUCCEEDED(rt->CreateGradientStopCollection(stops, 2, &collection)) &&
                SUCCEEDED(rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, size.height)), collection.Get(), &gradient)))
            {
                rt->FillRectangle(full, gradient.Get());
            }
            rt->FillRectangle(full, canvas.Brush(theme.dark ? D2D1::ColorF(0, 0, 0, 0.18f) : D2D1::ColorF(1, 1, 1, 0.12f)));
        }

        void InstallerWindow::PaintText(Canvas& canvas, TextBlock& block, const D2D1_COLOR_F& color)
        {
            if (!block.layout)
                return;

            canvas.linkBrush->SetColor(theme.color[MU_COLOR_LINK]);
            canvas.linkHoverBrush->SetColor(theme.color[MU_COLOR_LINK_HOVER]);
            for (size_t i = 0; i < block.rich.links.size(); i++)
            {
                auto& link = block.rich.links[i];
                bool hovered = hover.action == Action::Link && hover.index == block.id * 1000 + static_cast<int>(i);
                block.layout->SetDrawingEffect(hovered ? canvas.linkHoverBrush.Get() : canvas.linkBrush.Get(), { link.start, link.length });
            }
            canvas.rt->DrawTextLayout(block.origin, block.layout.Get(), canvas.Brush(color));
        }

        // Single line of text centered on 'cy', trimmed with an ellipsis if the format has trimming
        void InstallerWindow::DrawTextLine(Canvas& canvas, const std::wstring& text, IDWriteTextFormat* format, float left, float cy, float width, const D2D1_COLOR_F& color)
        {
            ComPtr<IDWriteTextLayout> layout;
            if (text.empty() || !format || FAILED(graphics.dwrite->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, std::max(10.0f, width), 100.0f, &layout)))
                return;
            DWRITE_TEXT_METRICS metrics = {};
            layout->GetMetrics(&metrics);
            canvas.rt->DrawTextLayout(D2D1::Point2F(left, cy - metrics.height / 2), layout.Get(), canvas.Brush(color));
        }

        // Rounded label ending at 'right', returns its left edge
        float InstallerWindow::DrawPill(Canvas& canvas, float right, float cy, float height, const std::wstring& text, const D2D1_COLOR_F& fill, const D2D1_COLOR_F& textColor)
        {
            auto label = MakeText(text, fmtSmall.Get(), 400.0f, DWRITE_TEXT_ALIGNMENT_LEADING, false, 0);
            if (!label.layout)
                return right;
            DWRITE_TEXT_METRICS metrics = {};
            label.layout->GetMetrics(&metrics);
            float r = height / 2;
            auto pill = D2D1::RectF(right - metrics.widthIncludingTrailingWhitespace - 16.0f, cy - r, right, cy + r);
            canvas.rt->FillRoundedRectangle(D2D1::RoundedRect(pill, r, r), canvas.Brush(fill));
            canvas.rt->DrawTextLayout(D2D1::Point2F(pill.left + 8.0f, cy - metrics.height / 2), label.layout.Get(), canvas.Brush(textColor));
            return pill.left;
        }

        // Background of the location box and the browse button
        void InstallerWindow::DrawPanelBox(Canvas& canvas, const D2D1_RECT_F& box, bool hovered, bool down, bool active)
        {
            auto rt = canvas.rt.Get();
            float alpha = theme.panelAlpha * (down ? 1.9f : (hovered || active) ? 1.5f : 1.0f);
            rt->FillRoundedRectangle(D2D1::RoundedRect(box, kBoxRadius, kBoxRadius), canvas.Brush(WithAlpha(theme.color[MU_COLOR_PANEL], std::min(alpha, 1.0f))));
            if (active)
                rt->DrawRoundedRectangle(D2D1::RoundedRect(Inflate(box, -0.75f), kBoxRadius, kBoxRadius), canvas.Brush(theme.accent), 1.5f);
            else
                rt->DrawRoundedRectangle(D2D1::RoundedRect(Inflate(box, -0.5f), kBoxRadius, kBoxRadius), canvas.Brush(WithAlpha(theme.color[MU_COLOR_PANEL_BORDER], theme.borderAlpha * (hovered ? 1.6f : 1.0f))), 1.0f);
        }

        // 18 x 14 DIP folder, 'x' is the left edge
        void InstallerWindow::DrawFolderGlyph(Canvas& canvas, float x, float cy, const D2D1_COLOR_F& color)
        {
            float y = cy - 7.0f;
            auto brush = canvas.Brush(color);
            canvas.rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + 7.5f, y + 4.0f), 1.2f, 1.2f), brush);
            canvas.rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y + 2.5f, x + 18.0f, y + 14.0f), 1.8f, 1.8f), brush);
        }

        void InstallerWindow::DrawGlyph(Canvas& canvas, ID2D1Geometry* glyph, const D2D1_MATRIX_3X2_F& transform, const D2D1_COLOR_F& color, float width)
        {
            if (!glyph)
                return;
            D2D1_MATRIX_3X2_F saved;
            canvas.rt->GetTransform(&saved);
            canvas.rt->SetTransform(transform * saved);
            canvas.rt->DrawGeometry(glyph, canvas.Brush(color), width, graphics.roundStroke.Get());
            canvas.rt->SetTransform(saved);
        }

        void InstallerWindow::DrawShield(Canvas& canvas, float x, float y, float size, float opacity)
        {
            UINT px = canvas.Pixels(size);
            auto& bitmap = canvas.shields[px];
            if (!bitmap)
            {
                HICON icon = nullptr;
                if (SUCCEEDED(LoadIconWithScaleDown(nullptr, IDI_SHIELD, px, px, &icon)) && icon)
                {
                    if (auto source = IconToBitmap(graphics.wic.Get(), icon))
                        bitmap = CreateScaledBitmap(graphics.wic.Get(), canvas.rt.Get(), source.Get(), px, px);
                    DestroyIcon(icon);
                }
                if (!bitmap)
                    return;
            }
            canvas.rt->DrawBitmap(bitmap.Get(), D2D1::RectF(x, y, x + size, y + size), opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }

        void InstallerWindow::DrawShadow(Canvas& canvas, Shadow& shadow, const D2D1_RECT_F& rect, float radius, float blur, float offset, float opacity)
        {
            float width = rect.right - rect.left, height = rect.bottom - rect.top;
            if ((!shadow.bitmap || std::abs(shadow.width - width) > 0.5f || std::abs(shadow.height - height) > 0.5f) &&
                !CreateShadow(graphics, canvas.rt.Get(), shadow, width, height, radius, blur, canvas.dpi))
            {
                return;
            }
            float left = rect.left - shadow.pad, top = rect.top - shadow.pad + offset;
            canvas.rt->DrawBitmap(shadow.bitmap.Get(), D2D1::RectF(left, top, left + shadow.size.width, top + shadow.size.height), opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }

        void InstallerWindow::DrawFocus(Canvas& canvas, const D2D1_RECT_F& rect, float radius)
        {
            canvas.rt->DrawRoundedRectangle(D2D1::RoundedRect(Inflate(rect, 3.0f), radius + 3.0f, radius + 3.0f), canvas.Brush(WithAlpha(theme.color[MU_COLOR_TEXT], 0.85f)), 2.0f);
        }

        void InstallerWindow::DrawLogo(Canvas& canvas, const D2D1_RECT_F& rect)
        {
            if (!logoImage || rect.right <= rect.left)
                return;
            UINT width = canvas.Pixels(rect.right - rect.left), height = canvas.Pixels(rect.bottom - rect.top);
            if (!canvas.logo || canvas.logoSize.width != width || canvas.logoSize.height != height)
            {
                canvas.logo = CreateScaledBitmap(graphics.wic.Get(), canvas.rt.Get(), logoImage.Get(), width, height);
                canvas.logoSize = D2D1::SizeU(width, height);
            }
            if (canvas.logo)
                canvas.rt->DrawBitmap(canvas.logo.Get(), rect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }

        void InstallerWindow::DrawButton(Canvas& canvas, const D2D1_RECT_F& rect, const std::wstring& text, Action action, bool primary, bool enabled, bool shield)
        {
            auto rt = canvas.rt.Get();
            bool hovered = enabled && IsHover(action);
            bool down = enabled && IsPressed(action);
            const float radius = 8.0f;
            D2D1_RECT_F r = rect;
            if (down)
            {
                r.top += 1.0f;
                r.bottom += 1.0f;
            }

            D2D1_COLOR_F textColor;
            if (primary)
            {
                auto color = down ? theme.color[MU_COLOR_BUTTON_PRESSED] : hovered ? theme.color[MU_COLOR_BUTTON_HOVER] : theme.color[MU_COLOR_BUTTON];
                if (!enabled)
                    color.a = 0.45f;
                if (enabled && !down)
                {
                    auto shadow = r;
                    shadow.top += 2.0f;
                    shadow.bottom += 2.0f;
                    rt->FillRoundedRectangle(D2D1::RoundedRect(shadow, radius, radius), canvas.Brush(D2D1::ColorF(0, 0, 0, 0.18f)));
                }
                rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), canvas.Brush(color));
                textColor = theme.color[MU_COLOR_BUTTON_TEXT];
            }
            else
            {
                float alpha = theme.panelAlpha * (down ? 2.2f : hovered ? 1.7f : 1.2f);
                rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), canvas.Brush(WithAlpha(theme.color[MU_COLOR_PANEL], std::min(alpha, 1.0f))));
                rt->DrawRoundedRectangle(D2D1::RoundedRect(Inflate(r, -0.5f), radius, radius), canvas.Brush(WithAlpha(theme.color[MU_COLOR_PANEL_BORDER], theme.borderAlpha * 1.5f)), 1.0f);
                textColor = theme.color[MU_COLOR_TEXT];
            }
            if (!enabled)
                textColor.a = 0.6f;

            auto label = MakeText(text, fmtButton.Get(), 1000.0f, DWRITE_TEXT_ALIGNMENT_LEADING, false, 0);
            if (label.layout)
            {
                DWRITE_TEXT_METRICS metrics = {};
                label.layout->GetMetrics(&metrics);
                float shieldSize = shield ? 18.0f : 0.0f;
                float gap = shield ? 8.0f : 0.0f;
                float contentWidth = shieldSize + gap + metrics.widthIncludingTrailingWhitespace;
                float x = (r.left + r.right - contentWidth) / 2;
                float y = (r.top + r.bottom - metrics.height) / 2;

                if (shield)
                {
                    DrawShield(canvas, x, (r.top + r.bottom - shieldSize) / 2, shieldSize, enabled ? 1.0f : 0.5f);
                    x += shieldSize + gap;
                }

                if (primary && enabled)
                {
                    // text shadow like the reference installer
                    auto shadow = theme.color[MU_COLOR_BUTTON_PRESSED];
                    shadow.r *= 0.55f;
                    shadow.g *= 0.55f;
                    shadow.b *= 0.55f;
                    shadow.a = 0.55f;
                    rt->DrawTextLayout(D2D1::Point2F(x + 1.0f, y + 1.0f), label.layout.Get(), canvas.Brush(shadow));
                }
                rt->DrawTextLayout(D2D1::Point2F(x, y), label.layout.Get(), canvas.Brush(textColor));
            }

            if (IsFocused(action))
                DrawFocus(canvas, rect, radius);
        }

        void InstallerWindow::DrawWindowButtons(Canvas& canvas)
        {
            auto rt = canvas.rt.Get();
            float W = windowSize.width;
            auto minimize = D2D1::RectF(W - kWindowButtonWidth * 2, 0, W - kWindowButtonWidth, kWindowButtonHeight);
            auto close = D2D1::RectF(W - kWindowButtonWidth, 0, W, kWindowButtonHeight);
            auto glyph = theme.color[MU_COLOR_TEXT];

            if (IsHover(Action::Minimize))
                rt->FillRectangle(minimize, canvas.Brush(WithAlpha(glyph, IsPressed(Action::Minimize) ? 0.2f : 0.12f)));
            if (IsHover(Action::Close))
                rt->FillRectangle(close, canvas.Brush(IsPressed(Action::Close) ? D2D1::ColorF(0.95f, 0.44f, 0.48f) : D2D1::ColorF(0.91f, 0.07f, 0.14f)));

            float cy = kWindowButtonHeight / 2;
            float mx = (minimize.left + minimize.right) / 2;
            rt->DrawLine(D2D1::Point2F(mx - 5.5f, cy), D2D1::Point2F(mx + 5.5f, cy), canvas.Brush(glyph), 1.2f, graphics.roundStroke.Get());

            float cxClose = (close.left + close.right) / 2;
            auto closeGlyph = IsHover(Action::Close) ? D2D1::ColorF(1, 1, 1) : glyph;
            rt->DrawLine(D2D1::Point2F(cxClose - 5.5f, cy - 5.5f), D2D1::Point2F(cxClose + 5.5f, cy + 5.5f), canvas.Brush(closeGlyph), 1.2f, graphics.roundStroke.Get());
            rt->DrawLine(D2D1::Point2F(cxClose + 5.5f, cy - 5.5f), D2D1::Point2F(cxClose - 5.5f, cy + 5.5f), canvas.Brush(closeGlyph), 1.2f, graphics.roundStroke.Get());
        }

        void InstallerWindow::DrawMain(Canvas& canvas, bool snapshotPass)
        {
            auto rt = canvas.rt.Get();
            DrawBackground(canvas);
            DrawLogo(canvas, main.logo);

            PaintText(canvas, main.heading, theme.color[MU_COLOR_TEXT]);
            PaintText(canvas, main.content, theme.color[MU_COLOR_TEXT_SECONDARY]);
            PaintText(canvas, main.label, theme.color[MU_COLOR_TEXT_SECONDARY]);

            DrawLocationBox(canvas, snapshotPass);
            DrawBrowseButton(canvas, snapshotPass);
            PaintText(canvas, main.note, (!check.hasGameExe || elevationDenied) ? theme.color[MU_COLOR_ERROR] : theme.color[MU_COLOR_TEXT_SECONDARY]);

            // "keep my settings"
            if (main.checkbox.layout)
            {
                auto box = main.checkboxBox;
                if (keepSettings)
                {
                    rt->FillRoundedRectangle(D2D1::RoundedRect(box, 4.0f, 4.0f), canvas.Brush(theme.color[MU_COLOR_BUTTON]));
                    auto white = canvas.Brush(theme.color[MU_COLOR_BUTTON_TEXT]);
                    rt->DrawLine(D2D1::Point2F(box.left + 4.2f, box.top + 9.3f), D2D1::Point2F(box.left + 7.6f, box.top + 12.6f), white, 2.0f, graphics.roundStroke.Get());
                    rt->DrawLine(D2D1::Point2F(box.left + 7.6f, box.top + 12.6f), D2D1::Point2F(box.left + 13.8f, box.top + 5.6f), white, 2.0f, graphics.roundStroke.Get());
                }
                else
                {
                    rt->FillRoundedRectangle(D2D1::RoundedRect(box, 4.0f, 4.0f), canvas.Brush(WithAlpha(theme.color[MU_COLOR_PANEL], theme.panelAlpha * 1.5f)));
                    rt->DrawRoundedRectangle(D2D1::RoundedRect(Inflate(box, -0.75f), 3.5f, 3.5f), canvas.Brush(WithAlpha(theme.color[MU_COLOR_TEXT], !snapshotPass && IsHover(Action::IniToggle) ? 0.8f : 0.55f)), 1.5f);
                }
                PaintText(canvas, main.checkbox, theme.color[MU_COLOR_TEXT]);
                if (!snapshotPass && IsFocused(Action::IniToggle))
                    DrawFocus(canvas, main.checkboxRow, 4.0f);
            }

            DrawButton(canvas, main.button, Text(config, MU_STR_INSTALL), Action::Install, true, selected >= 0, selected >= 0 && !check.writable);
            PaintText(canvas, main.footer, theme.color[MU_COLOR_TEXT_SECONDARY]);
        }

        void InstallerWindow::DrawLocationBox(Canvas& canvas, bool snapshotPass)
        {
            auto box = main.location;
            bool open = !snapshotPass && list.open;
            bool hovered = !snapshotPass && !open && IsHover(Action::Location);
            bool down = !snapshotPass && IsPressed(Action::Location);
            DrawPanelBox(canvas, box, hovered, down, open);

            float cy = (box.top + box.bottom) / 2;
            float left = box.left + 16.0f;
            DrawFolderGlyph(canvas, left, cy, WithAlpha(theme.color[MU_COLOR_TEXT], 0.75f));
            left += 18.0f + 12.0f;

            // chevron and source on the right, without suggestions the box opens the folder dialog
            float right = box.right - 16.0f;
            if (!locations.empty())
            {
                auto chevron = open ? D2D1::Matrix3x2F::Scale(1.0f, -1.0f) * D2D1::Matrix3x2F::Translation(right - 9.0f, cy + 2.25f)
                                    : D2D1::Matrix3x2F::Translation(right - 9.0f, cy - 2.25f);
                DrawGlyph(canvas, graphics.chevron.Get(), chevron, WithAlpha(theme.color[MU_COLOR_TEXT], 0.7f), 1.5f);
                right -= 20.0f;
            }
            if (selected >= 0 && !locations[selected].source.empty())
                DrawPill(canvas, right, cy, 22.0f, locations[selected].source, WithAlpha(theme.color[MU_COLOR_TEXT], 0.08f), theme.color[MU_COLOR_TEXT_SECONDARY]);

            // the whole path, measured by the layout
            if (main.path.layout)
                canvas.rt->DrawTextLayout(D2D1::Point2F(left, cy - main.path.height / 2), main.path.layout.Get(),
                    canvas.Brush(selected >= 0 ? theme.color[MU_COLOR_TEXT] : theme.color[MU_COLOR_TEXT_SECONDARY]));

            if (!snapshotPass && !open && IsFocused(Action::Location))
                DrawFocus(canvas, box, kBoxRadius);
        }

        void InstallerWindow::DrawBrowseButton(Canvas& canvas, bool snapshotPass)
        {
            auto r = main.browse;
            bool hovered = !snapshotPass && !list.open && IsHover(Action::Browse);
            bool down = !snapshotPass && IsPressed(Action::Browse);
            DrawPanelBox(canvas, r, hovered, down, false);

            float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2 + (down ? 0.5f : 0.0f);
            DrawGlyph(canvas, graphics.openFolder.Get(), D2D1::Matrix3x2F::Translation(cx - 10.0f, cy - 8.0f), WithAlpha(theme.color[MU_COLOR_TEXT], hovered ? 0.95f : 0.8f), 1.4f);

            if (!snapshotPass && IsFocused(Action::Browse))
                DrawFocus(canvas, r, kBoxRadius);
        }

        // Translucent panel over the blurred page, drawn on top of everything else
        void InstallerWindow::DrawList(Canvas& canvas)
        {
            if (!list.open)
                return;

            auto rt = canvas.rt.Get();
            float t = EaseOut((GetTickCount64() - list.openTick) / kTransitionMs);
            float offset = (1.0f - t) * (list.above ? 6.0f : -6.0f);
            auto panel = list.panel;
            panel.top += offset;
            panel.bottom += offset;
            const float radius = 9.0f;
            auto shape = D2D1::RoundedRect(panel, radius, radius);

            bool layer = canvas.PushOpacity(t);
            DrawShadow(canvas, canvas.listShadow, panel, radius, 12.0f, 8.0f, theme.dark ? 0.55f : 0.24f);
            if (auto acrylic = AcrylicBrush(canvas))
                rt->FillRoundedRectangle(shape, acrylic);
            rt->FillRoundedRectangle(shape, canvas.Brush(WithAlpha(theme.color[MU_COLOR_POPUP], theme.popupAlpha)));
            rt->DrawRoundedRectangle(D2D1::RoundedRect(Inflate(panel, -0.5f), radius, radius), canvas.Brush(WithAlpha(theme.color[MU_COLOR_PANEL_BORDER], theme.popupBorderAlpha)), 1.0f);

            auto view = ListViewport();
            view.top += offset;
            view.bottom += offset;
            rt->PushAxisAlignedClip(view, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            for (int i = 0; i + 1 < static_cast<int>(list.rowTops.size()); i++)
            {
                float top = view.top + list.rowTops[i] - list.scroll;
                float bottom = view.top + list.rowTops[i + 1] - list.scroll;
                if (bottom > view.top && top < view.bottom)
                    DrawListRow(canvas, D2D1::RectF(view.left, top, view.right, bottom), i);
            }
            rt->PopAxisAlignedClip();

            // scroll position
            float content = list.rowTops.empty() ? 0.0f : list.rowTops.back(), height = view.bottom - view.top;
            if (content > height + 0.5f)
            {
                float thumb = std::max(24.0f, height * height / content);
                float y = view.top + (height - thumb) * list.scroll / (content - height);
                auto bar = D2D1::RectF(panel.right - 4.5f, y + 2.0f, panel.right - 2.0f, y + thumb - 2.0f);
                rt->FillRoundedRectangle(D2D1::RoundedRect(bar, 1.25f, 1.25f), canvas.Brush(WithAlpha(theme.color[MU_COLOR_TEXT], 0.3f)));
            }
            if (layer)
                rt->PopLayer();
        }

        void InstallerWindow::DrawListRow(Canvas& canvas, const D2D1_RECT_F& row, int index)
        {
            auto rt = canvas.rt.Get();
            auto& location = locations[index];
            PathCheck rowCheck = index < static_cast<int>(list.checks.size()) ? list.checks[index] : PathCheck{ .writable = true };
            bool isSelected = index == selected;
            bool isHot = index == list.hot;
            auto text = theme.color[MU_COLOR_TEXT];

            float fill = (isHot && index == list.pressed) ? 0.12f : isHot ? 0.075f : isSelected ? 0.045f : 0.0f;
            if (fill > 0)
                rt->FillRoundedRectangle(D2D1::RoundedRect(row, 6.0f, 6.0f), canvas.Brush(WithAlpha(text, theme.dark ? fill * 1.3f : fill)));

            float cy = (row.top + row.bottom) / 2;
            if (isSelected)
                rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(row.left + 1.0f, cy - 9.0f, row.left + 4.0f, cy + 9.0f), 1.5f, 1.5f), canvas.Brush(theme.accent));
            DrawFolderGlyph(canvas, row.left + 15.0f, cy, isSelected ? theme.accent : WithAlpha(text, 0.7f));

            // folder name with the source and warnings, the full path below
            float left = row.left + 15.0f + 18.0f + 14.0f;
            float right = row.right - 10.0f;
            float line1 = row.top + 18.0f;
            if (!location.source.empty())
                right = DrawPill(canvas, right, line1, 20.0f, location.source, WithAlpha(text, 0.08f), theme.color[MU_COLOR_TEXT_SECONDARY]) - 6.0f;
            if (!rowCheck.hasGameExe)
                right = DrawPill(canvas, right, line1, 20.0f, Text(config, MU_STR_EXE_NOT_FOUND), WithAlpha(theme.color[MU_COLOR_ERROR], 0.13f), theme.color[MU_COLOR_ERROR]) - 6.0f;
            if (!rowCheck.writable)
            {
                // installing there needs administrator rights
                DrawShield(canvas, right - 16.0f, line1 - 8.0f, 16.0f, 1.0f);
                right -= 16.0f + 6.0f;
            }

            auto name = index < static_cast<int>(list.titles.size()) ? list.titles[index] : location.path.wstring();
            DrawTextLine(canvas, name, fmtItem.Get(), left, line1, right - left - 4.0f, text);
            if (index < static_cast<int>(list.paths.size()) && list.paths[index].layout)
                rt->DrawTextLayout(D2D1::Point2F(left, row.top + 29.0f), list.paths[index].layout.Get(), canvas.Brush(theme.color[MU_COLOR_TEXT_SECONDARY]));
        }

        void InstallerWindow::DrawTooltip(Canvas& canvas)
        {
            if (!tooltip.visible)
                return;

            auto label = MakeText(Text(config, MU_STR_BROWSE), fmtSmall.Get(), 360.0f, DWRITE_TEXT_ALIGNMENT_LEADING, false, 0);
            if (!label.layout)
                return;
            DWRITE_TEXT_METRICS metrics = {};
            label.layout->GetMetrics(&metrics);

            // centered below the button, inside the window
            auto anchor = main.browse;
            float width = metrics.widthIncludingTrailingWhitespace + 20.0f, height = metrics.height + 12.0f;
            float left = std::clamp((anchor.left + anchor.right - width) / 2, 8.0f, std::max(8.0f, windowSize.width - 8.0f - width));
            float top = anchor.bottom + 8.0f;
            if (top + height > windowSize.height - 8.0f)
                top = anchor.top - 8.0f - height;
            float t = EaseOut((GetTickCount64() - tooltip.shownTick) / kTransitionMs);
            top -= (1.0f - t) * 4.0f;
            auto rect = D2D1::RectF(left, top, left + width, top + height);

            auto rt = canvas.rt.Get();
            bool layer = canvas.PushOpacity(t);
            DrawShadow(canvas, canvas.tooltipShadow, rect, 6.0f, 5.0f, 3.0f, theme.dark ? 0.5f : 0.2f);
            rt->FillRoundedRectangle(D2D1::RoundedRect(rect, 6.0f, 6.0f), canvas.Brush(WithAlpha(theme.color[MU_COLOR_POPUP], 0.97f)));
            rt->DrawRoundedRectangle(D2D1::RoundedRect(Inflate(rect, -0.5f), 6.0f, 6.0f), canvas.Brush(WithAlpha(theme.color[MU_COLOR_PANEL_BORDER], theme.popupBorderAlpha)), 1.0f);
            rt->DrawTextLayout(D2D1::Point2F(rect.left + 10.0f - metrics.left, rect.top + 6.0f), label.layout.Get(), canvas.Brush(theme.color[MU_COLOR_TEXT]));
            if (layer)
                rt->PopLayer();
        }

        void InstallerWindow::DrawProgress(Canvas& canvas)
        {
            auto rt = canvas.rt.Get();
            DrawBlurredBackground(canvas);
            PaintText(canvas, progress.title, theme.color[MU_COLOR_TEXT]);
            PaintText(canvas, progress.status, theme.color[MU_COLOR_TEXT]);

            auto bar = progress.bar;
            float radius = (bar.bottom - bar.top) / 2;
            rt->FillRoundedRectangle(D2D1::RoundedRect(bar, radius, radius), canvas.Brush(WithAlpha(theme.color[MU_COLOR_PROGRESS_BACKGROUND], theme.trackAlpha)));

            auto p = CurrentProgress();
            bool indeterminate = p.total == 0 || p.phase == Phase::Preparing;
            float width = bar.right - bar.left;
            if (indeterminate)
            {
                // a segment sliding over the track
                float segment = width * 0.28f;
                float x = bar.left - segment + (width + segment) * marqueePhase;
                auto fill = D2D1::RectF(std::max(bar.left, x), bar.top, std::min(bar.right, x + segment), bar.bottom);
                if (fill.right > fill.left)
                    rt->FillRoundedRectangle(D2D1::RoundedRect(fill, radius, radius), canvas.Brush(theme.color[MU_COLOR_PROGRESS]));
            }
            else
            {
                float fraction = std::clamp(shownFraction, 0.0f, 1.0f);
                if (fraction > 0)
                {
                    auto fill = D2D1::RectF(bar.left, bar.top, bar.left + std::max(width * fraction, radius * 2), bar.bottom);
                    rt->FillRoundedRectangle(D2D1::RoundedRect(fill, radius, radius), canvas.Brush(theme.color[MU_COLOR_PROGRESS]));
                }
            }

            PaintText(canvas, progress.line, theme.color[MU_COLOR_TEXT_SECONDARY]);
            PaintText(canvas, progress.detail, theme.color[MU_COLOR_TEXT_SECONDARY]);
            bool cancelling = job && job->IsCancelling();
            DrawButton(canvas, progress.cancel, Text(config, cancelling ? MU_STR_CANCELLING : MU_STR_CANCEL), Action::Cancel, false, !cancelling);
        }

        void InstallerWindow::DrawResult(Canvas& canvas)
        {
            auto rt = canvas.rt.Get();
            DrawBlurredBackground(canvas);

            auto icon = resultPage.icon;
            float cx = (icon.left + icon.right) / 2, cy = (icon.top + icon.bottom) / 2, r = (icon.right - icon.left) / 2;
            D2D1_COLOR_F circle = outcome.result == Result::Success ? theme.color[MU_COLOR_SUCCESS] : outcome.result == Result::Failed ? theme.color[MU_COLOR_ERROR] : WithAlpha(theme.color[MU_COLOR_TEXT_SECONDARY], 0.9f);
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy + 2.0f), r, r), canvas.Brush(D2D1::ColorF(0, 0, 0, 0.12f)));
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), canvas.Brush(circle));
            auto white = canvas.Brush(D2D1::ColorF(1, 1, 1));
            if (outcome.result == Result::Success)
            {
                rt->DrawLine(D2D1::Point2F(cx - 14.0f, cy + 1.0f), D2D1::Point2F(cx - 4.0f, cy + 11.0f), white, 5.0f, graphics.roundStroke.Get());
                rt->DrawLine(D2D1::Point2F(cx - 4.0f, cy + 11.0f), D2D1::Point2F(cx + 15.0f, cy - 10.0f), white, 5.0f, graphics.roundStroke.Get());
            }
            else if (outcome.result == Result::Failed)
            {
                rt->DrawLine(D2D1::Point2F(cx - 11.0f, cy - 11.0f), D2D1::Point2F(cx + 11.0f, cy + 11.0f), white, 5.0f, graphics.roundStroke.Get());
                rt->DrawLine(D2D1::Point2F(cx + 11.0f, cy - 11.0f), D2D1::Point2F(cx - 11.0f, cy + 11.0f), white, 5.0f, graphics.roundStroke.Get());
            }
            else
            {
                rt->DrawLine(D2D1::Point2F(cx - 13.0f, cy), D2D1::Point2F(cx + 13.0f, cy), white, 5.0f, graphics.roundStroke.Get());
            }

            PaintText(canvas, resultPage.title, theme.color[MU_COLOR_TEXT]);
            PaintText(canvas, resultPage.message, theme.color[MU_COLOR_TEXT_SECONDARY]);
            DrawButton(canvas, resultPage.primary, resultPage.primaryText, resultPage.primaryAction, true, true);
            if (resultPage.secondaryAction != Action::None)
                DrawButton(canvas, resultPage.secondary, resultPage.secondaryText, resultPage.secondaryAction, false, true);
            PaintText(canvas, resultPage.links, theme.color[MU_COLOR_TEXT_SECONDARY]);
        }

        void InstallerWindow::Draw(Canvas& canvas, bool snapshotPass)
        {
            Layout();
            canvas.rt->SetTransform(D2D1::IdentityMatrix());
            if (snapshotPass)
            {
                DrawMain(canvas, true);
                return;
            }

            switch (page)
            {
            case Page::Main: DrawMain(canvas, false); break;
            case Page::Progress: DrawProgress(canvas); break;
            case Page::Result: DrawResult(canvas); break;
            }
            DrawWindowButtons(canvas);
            if (page == Page::Main)
            {
                DrawList(canvas);
                DrawTooltip(canvas);
            }
        }

        // ---- render targets

        bool InstallerWindow::EnsureTarget()
        {
            if (hwndTarget)
                return true;

            RECT rc;
            GetClientRect(hwnd, &rc);
            auto props = D2D1::RenderTargetProperties();
            auto hwndProps = D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top));
            if (FAILED(graphics.d2d->CreateHwndRenderTarget(props, hwndProps, &hwndTarget)))
                return false;

            screen = {};
            return screen.Init(hwndTarget.Get(), static_cast<float>(dpi));
        }

        void InstallerWindow::DiscardTarget()
        {
            screen = {};
            hwndTarget.Reset();
        }

        void InstallerWindow::MakeSnapshot(UINT width, UINT height, float snapshotDpi)
        {
            // the main page is rendered at a quarter of the resolution and blurred, stretching it back adds more blur
            constexpr UINT factor = 4;
            UINT w = std::max(1u, width / factor), h = std::max(1u, height / factor);

            ComPtr<IWICBitmap> bitmap;
            ComPtr<ID2D1RenderTarget> rt;
            auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            if (FAILED(graphics.wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap)) ||
                FAILED(graphics.d2d->CreateWicBitmapRenderTarget(bitmap.Get(), props, &rt)))
            {
                return;
            }

            Canvas canvas;
            if (!canvas.Init(rt.Get(), snapshotDpi / factor))
                return;

            // render the page as it looks without mouse or keyboard interaction
            Page savedPage = page;
            Hit savedHover = hover, savedPressed = pressed;
            int savedFocus = focus;
            page = Page::Main;
            hover = pressed = {};
            focus = -1;
            layoutDirty = true;
            rt->BeginDraw();
            Draw(canvas, true);
            rt->EndDraw();
            page = savedPage;
            hover = savedHover;
            pressed = savedPressed;
            focus = savedFocus;
            layoutDirty = true;

            // three box blur passes of radius r are close to a gaussian blur with sigma r (~26 DIP here)
            int radius = std::max(3, static_cast<int>(std::lround(26.0f * snapshotDpi / 96.0f / factor)));
            BlurBitmap(bitmap.Get(), radius, 3);
            snapshot = bitmap;
            screen.DropBlur();
        }

        void InstallerWindow::RefreshSnapshot()
        {
            MakeSnapshot(static_cast<UINT>(std::lround(windowSize.width * dpi / 96.0f)), static_cast<UINT>(std::lround(windowSize.height * dpi / 96.0f)), static_cast<float>(dpi));
        }

        void InstallerWindow::Invalidate()
        {
            if (hwnd)
                InvalidateRect(hwnd, nullptr, FALSE);
        }

        // ---- state changes

        void InstallerWindow::StartInstall(const std::filesystem::path& path)
        {
            target = path;
            outcome = {};
            CloseList();

            // the page as it looks now becomes the blurred background
            RefreshSnapshot();

            job = std::make_unique<Job>(config, target, CurrentIniMode());
            job->Start();

            page = Page::Progress;
            shownFraction = 0.0f;
            marqueePhase = 0.0f;
            lastTick = GetTickCount64();
            focus = -1;
            hover = pressed = {};
            layoutDirty = true;
            SetTimer(hwnd, kTimerAnimation, 16, nullptr);
            UpdateTooltip();
            UpdateTaskbar();
            Invalidate();
        }

        void InstallerWindow::OnTimer()
        {
            if (page != Page::Progress || !job)
            {
                KillTimer(hwnd, kTimerAnimation);
                return;
            }

            auto now = GetTickCount64();
            float dt = std::min(0.1f, (now - lastTick) / 1000.0f);
            lastTick = now;

            auto p = job->GetProgress();
            float targetFraction = p.total ? static_cast<float>(static_cast<double>(p.done) / p.total) : 0.0f;
            if (p.phase == Phase::Downloading && shownFraction > targetFraction + 0.25f)
                shownFraction = targetFraction; // extraction finished and a new phase began
            shownFraction += (targetFraction - shownFraction) * std::min(1.0f, dt * 10.0f);
            marqueePhase = std::fmod(marqueePhase + dt * 0.7f, 1.0f);

            layoutDirty = true;
            UpdateTaskbar();

            if (job->IsFinished())
            {
                KillTimer(hwnd, kTimerAnimation);
                outcome = job->GetOutcome();
                if (closeAfterCancel)
                {
                    Finish(outcome.result == Result::Success ? MU_INSTALL_SUCCESS : MU_INSTALL_CANCELLED);
                    return;
                }
                page = Page::Result;
                focus = -1;
                hover = pressed = {};
                layoutDirty = true;
                UpdateTaskbar();

                if (GetForegroundWindow() != hwnd)
                {
                    FLASHWINFO flash = { sizeof(flash), hwnd, FLASHW_TRAY | FLASHW_TIMERNOFG, 0, 0 };
                    FlashWindowEx(&flash);
                }
            }
            Invalidate();
        }

        void InstallerWindow::UpdateTaskbar()
        {
            if (!taskbar || !hwnd)
                return;
            if (page == Page::Progress && job)
            {
                auto p = job->GetProgress();
                if (p.total == 0 || p.phase == Phase::Preparing)
                {
                    taskbar->SetProgressState(hwnd, TBPF_INDETERMINATE);
                }
                else
                {
                    taskbar->SetProgressState(hwnd, job->IsCancelling() ? TBPF_PAUSED : TBPF_NORMAL);
                    taskbar->SetProgressValue(hwnd, std::min(p.done, p.total), p.total);
                }
            }
            else if (page == Page::Result && outcome.result == Result::Failed)
            {
                taskbar->SetProgressState(hwnd, TBPF_ERROR);
                taskbar->SetProgressValue(hwnd, 100, 100);
            }
            else
            {
                taskbar->SetProgressState(hwnd, TBPF_NOPROGRESS);
            }
        }

        int InstallerWindow::ResultCode() const
        {
            switch (outcome.result)
            {
            case Result::Success: return MU_INSTALL_SUCCESS;
            case Result::Cancelled: return MU_INSTALL_CANCELLED;
            default: return MU_INSTALL_FAILED;
            }
        }

        void InstallerWindow::Finish(int code)
        {
            result = code;
            if (hwnd)
                DestroyWindow(hwnd);
        }

        bool InstallerWindow::Confirm(const std::wstring& text)
        {
            auto title = WindowTitle(config);
            TASKDIALOGCONFIG tdc = { sizeof(tdc) };
            tdc.hwndParent = hwnd;
            tdc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
            tdc.dwCommonButtons = TDCBF_YES_BUTTON | TDCBF_NO_BUTTON;
            tdc.nDefaultButton = IDNO;
            tdc.pszWindowTitle = title.c_str();
            tdc.pszMainIcon = TD_WARNING_ICON;
            tdc.pszMainInstruction = text.c_str();
            int clicked = IDNO;
            return SUCCEEDED(TaskDialogIndirect(&tdc, &clicked, nullptr, nullptr)) && clicked == IDYES;
        }

        void InstallerWindow::RequestClose()
        {
            CloseList();
            if (page == Page::Progress && job && !job->IsFinished())
            {
                if (!job->IsCancelling() && Confirm(Text(config, MU_STR_CONFIRM_CANCEL)))
                {
                    job->Cancel();
                    closeAfterCancel = true;
                    layoutDirty = true;
                    Invalidate();
                }
                return;
            }
            Finish(page == Page::Result ? ResultCode() : MU_INSTALL_CANCELLED);
        }

        void InstallerWindow::OnLocation()
        {
            if (locations.empty())
                OnBrowse();
            else
                OpenList(keyboardCues);
        }

        void InstallerWindow::OnBrowse()
        {
            CloseList();
            SetHover({});
            pressed = {};
            auto folder = BrowseForFolder(hwnd, config, selected >= 0 ? locations[selected].path : std::filesystem::path());
            if (folder.empty())
                return;
            auto it = std::find_if(locations.begin(), locations.end(), [&](const Location& l) { return iequals(l.path.wstring(), folder.wstring()); });
            if (it == locations.end())
            {
                locations.push_back({ folder, L"" });
                it = locations.end() - 1;
            }
            SelectLocation(static_cast<int>(it - locations.begin()));
        }

        // ---- list of install locations

        void InstallerWindow::OpenList(bool keyboard)
        {
            list.checks.clear();
            for (auto& location : locations)
                list.checks.push_back(CheckPath(config, location.path));
            list.titles = ShortLocationNames(locations);

            // rows are as tall as their paths
            Layout();
            float textWidth = main.location.right - main.location.left - kListPadding * 2 - 47.0f - 10.0f;
            list.paths.clear();
            list.rowTops = { 0.0f };
            for (auto& location : locations)
            {
                list.paths.push_back(MakeText(WrappablePath(location.path.wstring()), fmtItemPath.Get(), textWidth, DWRITE_TEXT_ALIGNMENT_LEADING, false, 0));
                list.rowTops.push_back(list.rowTops.back() + std::max(kListRowHeight, std::ceil(29.0f + list.paths.back().height + 9.0f)));
            }

            RefreshSnapshot(); // shines through the list
            list.open = true;
            list.hot = keyboard ? selected : -1;
            list.pressed = -1;
            list.scroll = 0.0f;
            list.openTick = GetTickCount64();
            hover = pressed = {};
            layoutDirty = true;
            if (selected >= 0)
                ScrollToRow(selected);
            UpdateTooltip();
            StartTransition();
            Invalidate();
        }

        void InstallerWindow::CloseList()
        {
            if (!list.open)
                return;
            list.open = false;
            list.hot = list.pressed = -1;
            if (hwnd && GetCapture() == hwnd)
                ReleaseCapture();
            layoutDirty = true;
            RefreshHover();
            Invalidate();
        }

        D2D1_RECT_F InstallerWindow::ListViewport() const
        {
            auto p = list.panel;
            return D2D1::RectF(p.left + kListPadding, p.top + kListPadding, p.right - kListPadding, p.bottom - kListPadding);
        }

        int InstallerWindow::ListRowAt(D2D1_POINT_2F point)
        {
            Layout();
            auto view = ListViewport();
            if (!list.open || !Contains(view, point))
                return -1;
            float y = point.y - view.top + list.scroll;
            for (int i = 0; i + 1 < static_cast<int>(list.rowTops.size()); i++)
            {
                if (y >= list.rowTops[i] && y < list.rowTops[i + 1])
                    return i;
            }
            return -1;
        }

        void InstallerWindow::ScrollToRow(int row)
        {
            if (row < 0 || row + 1 >= static_cast<int>(list.rowTops.size()))
                return;
            Layout();
            auto view = ListViewport();
            float height = view.bottom - view.top;
            float top = list.rowTops[row], bottom = list.rowTops[row + 1];
            if (top < list.scroll)
                list.scroll = top;
            else if (bottom > list.scroll + height)
                list.scroll = std::min(top, bottom - height);
            layoutDirty = true;
        }

        void InstallerWindow::OnListKey(WPARAM key)
        {
            const int count = static_cast<int>(locations.size());
            const int visible = std::max(1, static_cast<int>((ListViewport().bottom - ListViewport().top) / kListRowHeight));
            int hot = list.hot;
            switch (key)
            {
            case VK_UP: hot = hot < 0 ? count - 1 : std::max(0, hot - 1); break;
            case VK_DOWN: hot = hot < 0 ? 0 : std::min(count - 1, hot + 1); break;
            case VK_PRIOR: hot = std::max(0, (hot < 0 ? 0 : hot) - visible); break;
            case VK_NEXT: hot = std::min(count - 1, (hot < 0 ? 0 : hot) + visible); break;
            case VK_HOME: hot = 0; break;
            case VK_END: hot = count - 1; break;
            case VK_RETURN:
            case VK_SPACE:
            {
                int row = list.hot;
                CloseList();
                if (row >= 0)
                    SelectLocation(row);
                return;
            }
            case VK_ESCAPE:
                CloseList();
                return;
            case VK_TAB:
                CloseList();
                MoveFocus(GetKeyState(VK_SHIFT) < 0 ? -1 : 1);
                return;
            default:
                return;
            }

            if (hot >= 0 && hot != list.hot)
            {
                list.hot = hot;
                ScrollToRow(hot);
                Invalidate();
            }
        }

        void InstallerWindow::OnListMouse(UINT message, D2D1_POINT_2F point)
        {
            int row = ListRowAt(point);
            switch (message)
            {
            case WM_MOUSEMOVE:
                if (row != list.hot)
                {
                    list.hot = row;
                    Invalidate();
                }
                break;
            case WM_LBUTTONDOWN:
                if (Contains(list.panel, point))
                {
                    list.pressed = row;
                    SetCapture(hwnd);
                    Invalidate();
                }
                else
                {
                    CloseList(); // a click outside only closes the list
                }
                break;
            case WM_LBUTTONUP:
            {
                int clicked = list.pressed;
                list.pressed = -1;
                if (GetCapture() == hwnd)
                    ReleaseCapture();
                if (clicked >= 0 && clicked == row)
                {
                    CloseList();
                    SelectLocation(row);
                }
                Invalidate();
                break;
            }
            }
        }

        // ---- hover, tooltip and transitions

        void InstallerWindow::SetHover(const Hit& hit)
        {
            if (hit == hover)
                return;
            bool wasBrowse = hover.action == Action::Browse;
            hover = hit;
            if (hover.action == Action::Browse && !wasBrowse)
            {
                tooltip.armed = false;
                if (hwnd)
                    SetTimer(hwnd, kTimerTooltip, kTooltipDelay, nullptr);
            }
            else if (hover.action != Action::Browse)
            {
                tooltip.armed = false;
                if (hwnd)
                    KillTimer(hwnd, kTimerTooltip);
            }
            UpdateTooltip();
            Invalidate();
        }

        // after the layout changed under the mouse
        void InstallerWindow::RefreshHover()
        {
            if (!hwnd)
                return;
            POINT pt = {};
            RECT rc = {};
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            GetClientRect(hwnd, &rc);
            SetHover(!list.open && PtInRect(&rc, pt) ? HitTest(ToDip(pt)) : Hit{});
        }

        void InstallerWindow::UpdateTooltip()
        {
            bool wanted = page == Page::Main && !list.open &&
                          ((hover.action == Action::Browse && tooltip.armed) || IsFocused(Action::Browse));
            if (wanted == tooltip.visible)
                return;
            tooltip.visible = wanted;
            tooltip.shownTick = GetTickCount64();
            if (wanted)
                StartTransition();
            Invalidate();
        }

        bool InstallerWindow::IsTransitionRunning() const
        {
            auto now = GetTickCount64();
            return (list.open && now - list.openTick < kTransitionMs) || (tooltip.visible && now - tooltip.shownTick < kTransitionMs);
        }

        void InstallerWindow::StartTransition()
        {
            if (hwnd && !transitionTimer)
                transitionTimer = SetTimer(hwnd, kTimerTransition, 15, nullptr) != 0;
        }

        void InstallerWindow::OnInstall()
        {
            if (selected < 0)
            {
                OnLocation();
                return;
            }

            auto path = locations[selected].path;
            check = CheckPath(config, path);
            if (!check.hasGameExe && !ConfirmMissingExecutable(hwnd, config, path, check.exeName))
                return;

            if (!check.writable)
            {
                if (RestartElevated(path, CurrentIniMode()))
                {
                    Finish(MU_INSTALL_RESTARTED);
                    return;
                }
                elevationDenied = true;
                layoutDirty = true;
                Invalidate();
                return;
            }

            if (!ConfirmGameClosed(hwnd, config, path))
                return;

            StartInstall(path);
        }

        void InstallerWindow::Activate(const Hit& hit)
        {
            switch (hit.action)
            {
            case Action::Minimize:
                ShowWindow(hwnd, SW_MINIMIZE);
                break;
            case Action::Close:
                RequestClose();
                break;
            case Action::Location:
                OnLocation();
                break;
            case Action::Browse:
                OnBrowse();
                break;
            case Action::IniToggle:
                keepSettings = !keepSettings;
                Invalidate();
                break;
            case Action::Install:
                OnInstall();
                break;
            case Action::Cancel:
                if (job && !job->IsFinished() && !job->IsCancelling() && Confirm(Text(config, MU_STR_CONFIRM_CANCEL)))
                {
                    job->Cancel();
                    layoutDirty = true;
                    Invalidate();
                }
                break;
            case Action::Retry:
                StartInstall(target);
                break;
            case Action::Launch:
                LaunchGame(config, target);
                Finish(MU_INSTALL_SUCCESS);
                break;
            case Action::Done:
                Finish(ResultCode());
                break;
            case Action::Link:
                if (hit.url == L"action:open")
                    ui::OpenInExplorer(target.wstring());
                else if (hit.url == L"action:log")
                    ShellExecuteW(hwnd, L"open", LogGetFile().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                else
                    ui::OpenUrl(hwnd, hit.url);
                break;
            default:
                break;
            }
        }

        bool InstallerWindow::IsFocused(Action action) const
        {
            if (!keyboardCues || focus < 0)
                return false;
            int index = 0;
            for (auto& zone : zones)
            {
                if (!zone.focusable)
                    continue;
                if (index++ == focus)
                    return zone.action == action;
            }
            return false;
        }

        void InstallerWindow::MoveFocus(int direction)
        {
            Layout();
            int count = static_cast<int>(std::count_if(zones.begin(), zones.end(), [](const Zone& z) { return z.focusable; }));
            if (count == 0)
                return;
            focus = (focus < 0) ? (direction > 0 ? 0 : count - 1) : (focus + direction + count) % count;
            keyboardCues = true;
            UpdateTooltip();
            Invalidate();
        }

        D2D1_POINT_2F InstallerWindow::ToDip(POINT pt) const
        {
            return D2D1::Point2F(pt.x * 96.0f / dpi, pt.y * 96.0f / dpi);
        }

        Hit InstallerWindow::HitTest(D2D1_POINT_2F point)
        {
            Layout();
            for (auto& zone : zones)
            {
                if (Contains(zone.rect, point))
                {
                    if (zone.action == Action::Install && selected < 0)
                        return { Action::Install };
                    if (zone.action == Action::Cancel && job && job->IsCancelling())
                        return {};
                    return { zone.action };
                }
            }

            for (auto block : linkBlocks)
            {
                if (!block->layout || block->rich.links.empty())
                    continue;
                BOOL trailing = FALSE, inside = FALSE;
                DWRITE_HIT_TEST_METRICS metrics = {};
                if (FAILED(block->layout->HitTestPoint(point.x - block->origin.x, point.y - block->origin.y, &trailing, &inside, &metrics)) || !inside)
                    continue;
                for (size_t i = 0; i < block->rich.links.size(); i++)
                {
                    auto& link = block->rich.links[i];
                    if (metrics.textPosition >= link.start && metrics.textPosition < link.start + link.length)
                        return { Action::Link, block->id * 1000 + static_cast<int>(i), link.url };
                }
            }
            return {};
        }

        void InstallerWindow::OnDpiChanged(UINT newDpi, const RECT* suggested)
        {
            CloseList();
            dpi = newDpi;
            int width = static_cast<int>(std::lround(windowSize.width * dpi / 96.0f));
            int height = static_cast<int>(std::lround(windowSize.height * dpi / 96.0f));
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);

            if (hwndTarget)
            {
                hwndTarget->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
                hwndTarget->Resize(D2D1::SizeU(width, height));
                screen.dpi = static_cast<float>(dpi);
                screen.DropBitmaps();
            }
            if (snapshot)
                MakeSnapshot(width, height, static_cast<float>(dpi));
            layoutDirty = true;
            Invalidate();
        }

        LRESULT CALLBACK InstallerWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
        {
            InstallerWindow* self = nullptr;
            if (message == WM_NCCREATE)
            {
                self = static_cast<InstallerWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
                self->hwnd = hwnd;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            }
            else
            {
                self = reinterpret_cast<InstallerWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            }

            if (self)
                return self->HandleMessage(message, wParam, lParam);
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        LRESULT InstallerWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
        {
            if (message == taskbarCreatedMessage && taskbarCreatedMessage)
            {
                if (!taskbar && SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&taskbar))) && FAILED(taskbar->HrInit()))
                {
                    taskbar->Release();
                    taskbar = nullptr;
                }
                UpdateTaskbar();
                return 0;
            }

            switch (message)
            {
            case WM_PAINT:
            {
                PAINTSTRUCT ps;
                BeginPaint(hwnd, &ps);
                if (EnsureTarget())
                {
                    hwndTarget->BeginDraw();
                    Draw(screen);
                    if (hwndTarget->EndDraw() == D2DERR_RECREATE_TARGET)
                        DiscardTarget();
                }
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;
            case WM_SIZE:
                if (hwndTarget)
                    hwndTarget->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
                return 0;
            case WM_DPICHANGED:
                OnDpiChanged(HIWORD(wParam), reinterpret_cast<RECT*>(lParam));
                return 0;
            case WM_NCHITTEST:
            {
                LRESULT hit = DefWindowProcW(hwnd, message, wParam, lParam);
                if (hit == HTCLIENT && !list.open)
                {
                    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                    ScreenToClient(hwnd, &pt);
                    if (HitTest(ToDip(pt)).action == Action::None)
                        return HTCAPTION; // drag the window by its background
                }
                return hit;
            }
            case WM_NCLBUTTONDBLCLK:
                return 0; // no maximizing
            case WM_MOUSEMOVE:
            {
                if (!trackingMouse)
                {
                    TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                    trackingMouse = TrackMouseEvent(&tme) != FALSE;
                }
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                bool moved = pt.x != lastMouse.x || pt.y != lastMouse.y;
                lastMouse = pt;
                if (list.open)
                {
                    // keep the row chosen with the keyboard until the mouse really moves
                    if (moved)
                        OnListMouse(message, ToDip(pt));
                    return 0;
                }
                SetHover(HitTest(ToDip(pt)));
                return 0;
            }
            case WM_MOUSELEAVE:
                trackingMouse = false;
                lastMouse = { -1, -1 };
                SetHover({});
                return 0;
            case WM_LBUTTONDOWN:
            {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                keyboardCues = false;
                tooltip.armed = false;
                KillTimer(hwnd, kTimerTooltip);
                if (list.open)
                {
                    OnListMouse(message, ToDip(pt));
                    return 0;
                }
                pressed = HitTest(ToDip(pt));
                if (pressed.action != Action::None)
                    SetCapture(hwnd);
                UpdateTooltip();
                Invalidate();
                return 0;
            }
            case WM_LBUTTONUP:
            {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                if (list.open)
                {
                    OnListMouse(message, ToDip(pt));
                    return 0;
                }
                if (GetCapture() == hwnd)
                    ReleaseCapture();
                auto hit = HitTest(ToDip(pt));
                auto clicked = pressed;
                pressed = {};
                Invalidate();
                if (clicked.action != Action::None && hit == clicked)
                    Activate(clicked);
                return 0;
            }
            case WM_MOUSEWHEEL:
                if (list.open)
                {
                    list.scroll -= GET_WHEEL_DELTA_WPARAM(wParam) * kListRowHeight / WHEEL_DELTA;
                    layoutDirty = true;
                    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                    ScreenToClient(hwnd, &pt);
                    list.hot = ListRowAt(ToDip(pt));
                    Invalidate();
                }
                return 0;
            case WM_SETCURSOR:
                if (LOWORD(lParam) == HTCLIENT)
                {
                    bool clickable = !list.open && hover.action != Action::None && hover.action != Action::Minimize && hover.action != Action::Close;
                    SetCursor(LoadCursor(nullptr, clickable ? IDC_HAND : IDC_ARROW));
                    return TRUE;
                }
                break;
            case WM_ACTIVATE:
                if (LOWORD(wParam) == WA_INACTIVE)
                {
                    CloseList();
                    tooltip.armed = false;
                    KillTimer(hwnd, kTimerTooltip);
                    UpdateTooltip();
                }
                break;
            case WM_SETTINGCHANGE:
                if (lParam && CompareStringOrdinal(reinterpret_cast<LPCWSTR>(lParam), -1, L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL)
                    OnSystemThemeChanged();
                break;
            case WM_KEYDOWN:
                if (list.open)
                {
                    OnListKey(wParam);
                    return 0;
                }
                switch (wParam)
                {
                case VK_TAB:
                    MoveFocus(GetKeyState(VK_SHIFT) < 0 ? -1 : 1);
                    return 0;
                case VK_RIGHT:
                case VK_DOWN:
                    MoveFocus(1);
                    return 0;
                case VK_LEFT:
                case VK_UP:
                    MoveFocus(-1);
                    return 0;
                case VK_ESCAPE:
                    RequestClose();
                    return 0;
                case VK_RETURN:
                case VK_SPACE:
                {
                    Layout();
                    Hit hit;
                    int index = 0;
                    for (auto& zone : zones)
                    {
                        if (zone.focusable && index++ == focus)
                            hit.action = zone.action;
                    }
                    if (hit.action == Action::None && wParam == VK_RETURN)
                        hit.action = page == Page::Main ? Action::Install : page == Page::Result ? resultPage.primaryAction : Action::None;
                    if (hit.action != Action::None)
                        Activate(hit);
                    return 0;
                }
                }
                break;
            case WM_TIMER:
                switch (wParam)
                {
                case kTimerAnimation:
                    OnTimer();
                    break;
                case kTimerTransition:
                    Invalidate();
                    if (!IsTransitionRunning())
                    {
                        KillTimer(hwnd, kTimerTransition);
                        transitionTimer = false;
                    }
                    break;
                case kTimerTooltip:
                    KillTimer(hwnd, kTimerTooltip);
                    tooltip.armed = hover.action == Action::Browse;
                    UpdateTooltip();
                    break;
                }
                return 0;
            case WM_APP_AUTOSTART:
                if (selected >= 0)
                {
                    auto path = locations[selected].path;
                    if (!TestWriteAccess(path, true))
                    {
                        outcome = {};
                        outcome.result = Result::Failed;
                        outcome.error = L"No permission to write to " + path.wstring();
                        target = path;
                        RefreshSnapshot();
                        page = Page::Result;
                        layoutDirty = true;
                        Invalidate();
                    }
                    else
                    {
                        StartInstall(path);
                    }
                }
                return 0;
            case WM_CLOSE:
                RequestClose();
                return 0;
            case WM_DESTROY:
                KillTimer(hwnd, kTimerAnimation);
                KillTimer(hwnd, kTimerTransition);
                KillTimer(hwnd, kTimerTooltip);
                PostQuitMessage(0);
                return 0;
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        int InstallerWindow::Run()
        {
            ui::ScopedDpiAwareness dpiAwareness;
            if (!Init())
                return -1;

            HINSTANCE instance = GetModuleHandleW(nullptr);
            WNDCLASSEXW wc = { sizeof(wc) };
            wc.style = CS_DROPSHADOW;
            wc.lpfnWndProc = WindowProc;
            wc.hInstance = instance;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.lpszClassName = kWindowClass;
            if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                return -1;

            // open on the monitor with the mouse cursor
            POINT cursor = {};
            GetCursorPos(&cursor);
            HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO info = { sizeof(info) };
            GetMonitorInfoW(monitor, &info);
            dpi = ui::GetMonitorDpi(monitor);

            windowSize = ComputeWindowSize();
            float maxHeight = (info.rcWork.bottom - info.rcWork.top) * 96.0f / dpi * 0.94f;
            windowSize.height = std::min(windowSize.height, std::round(maxHeight));
            int width = static_cast<int>(std::lround(windowSize.width * dpi / 96.0f));
            int height = static_cast<int>(std::lround(windowSize.height * dpi / 96.0f));
            int x = info.rcWork.left + (info.rcWork.right - info.rcWork.left - width) / 2;
            int y = info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - height) / 2;

            taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarButtonCreated");
            auto title = WindowTitle(config);
            hwnd = CreateWindowExW(WS_EX_APPWINDOW, kWindowClass, title.c_str(), WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU,
                x, y, width, height, nullptr, nullptr, instance, this);
            if (!hwnd)
                return -1;

            // the taskbar message is blocked for elevated processes otherwise
            using ChangeFilterFn = BOOL(WINAPI*)(HWND, UINT, DWORD, void*);
            if (auto changeFilter = reinterpret_cast<ChangeFilterFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "ChangeWindowMessageFilterEx")))
                changeFilter(hwnd, taskbarCreatedMessage, 1 /* MSGFLT_ALLOW */, nullptr);

            UINT actualDpi = ui::GetWindowDpi(hwnd);
            if (actualDpi != dpi)
            {
                RECT rc = { x, y, x + width, y + height };
                OnDpiChanged(actualDpi, &rc);
            }

            // rounded corners on Windows 11
            using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
            if (HMODULE dwm = ui::LoadSystemLibrary(L"dwmapi.dll"))
            {
                if (auto setAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(dwm, "DwmSetWindowAttribute")))
                {
                    int preference = 2; // DWMWCP_ROUND
                    setAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &preference, sizeof(preference));
                }
            }
            ui::SetWindowDarkMode(hwnd, theme.dark);
            Log(L"Installer theme: {}{}", resolvedTheme == MU_THEME_DARK ? L"dark" : L"light", config.theme == MU_THEME_AUTO ? L" (Windows app mode)" : L"");

            windowIcon = config.icon;
            if (!windowIcon)
            {
                auto exe = GetModuleFilePath(nullptr);
                HICON large = nullptr;
                if (ExtractIconExW(exe.c_str(), 0, &large, nullptr, 1) && large)
                {
                    windowIcon = large;
                    ownsWindowIcon = true;
                }
            }
            if (windowIcon)
            {
                SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(windowIcon));
                SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(windowIcon));
            }

            ShowWindow(hwnd, SW_SHOWNORMAL);
            SetForegroundWindow(hwnd);
            UpdateWindow(hwnd);

            if (commandLine.autoStart && selected >= 0)
                PostMessageW(hwnd, WM_APP_AUTOSTART, 0, 0);

            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0)
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            hwnd = nullptr;
            job.reset();
            return result;
        }

        bool InstallerWindow::RenderPreview(const std::wstring& pageName, UINT previewDpi, const std::filesystem::path& png)
        {
            if (!Init())
                return false;

            // made-up folders that show every state of the list: no administrator rights needed,
            // administrator rights needed, game executable missing
            std::vector<PathCheck> checks;
            if (locations.empty())
            {
                locations = {
                    { L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Test Game", L"Steam" },
                    { L"C:\\Program Files\\Rockstar Games\\Test Game", L"Rockstar Games Launcher" },
                    { L"D:\\Games\\Test Game (backup)", L"" },
                };
                checks = { { .writable = true }, { .writable = false }, { .writable = true, .hasGameExe = false } };
                SelectLocation(0);
                check = checks[0];
            }

            dpi = previewDpi;
            windowSize = ComputeWindowSize();
            UINT width = static_cast<UINT>(std::lround(windowSize.width * dpi / 96.0f));
            UINT height = static_cast<UINT>(std::lround(windowSize.height * dpi / 96.0f));
            target = locations[selected].path;

            if (pageName == L"locations")
            {
                OpenList(false);
                if (!checks.empty())
                    list.checks = checks;
                list.hot = std::min(1, static_cast<int>(locations.size()) - 1);
                list.openTick = 0; // fully faded in
            }
            else if (pageName == L"tooltip")
            {
                hover.action = Action::Browse;
                tooltip.armed = true;
                UpdateTooltip();
                tooltip.shownTick = 0;
            }
            else if (pageName != L"main")
            {
                MakeSnapshot(width, height, static_cast<float>(dpi));
                if (pageName == L"progress")
                {
                    page = Page::Progress;
                    Progress p;
                    p.phase = Phase::Downloading;
                    p.total = 206711636;
                    p.done = p.total * 42 / 100;
                    p.bytesPerSecond = 12.4 * 1000 * 1000;
                    p.file = L"TestMod.zip";
                    previewProgress = p;
                    shownFraction = 0.42f;
                }
                else
                {
                    page = Page::Result;
                    if (pageName == L"success")
                    {
                        outcome.result = Result::Success;
                    }
                    else if (pageName == L"failure")
                    {
                        outcome.result = Result::Failed;
                        outcome.error = L"Cannot download TestMod.zip.";
                        outcome.details = { L"HTTP 404 Not Found" };
                        LogSetFile(LogGetFile().empty() ? std::filesystem::temp_directory_path() / L"modupdater-preview.log" : LogGetFile());
                    }
                    else
                    {
                        outcome.result = Result::Cancelled;
                    }
                }
            }
            layoutDirty = true;

            ComPtr<IWICBitmap> bitmap;
            ComPtr<ID2D1RenderTarget> rt;
            auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            if (FAILED(graphics.wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap)) ||
                FAILED(graphics.d2d->CreateWicBitmapRenderTarget(bitmap.Get(), props, &rt)))
            {
                return false;
            }

            Canvas canvas;
            if (!canvas.Init(rt.Get(), static_cast<float>(dpi)))
                return false;

            rt->BeginDraw();
            rt->Clear(D2D1::ColorF(0, 0, 0, 1));
            Draw(canvas);
            if (FAILED(rt->EndDraw()))
                return false;

            return SavePng(graphics.wic.Get(), bitmap.Get(), png);
        }
    }

    int RunModern(Config& config, const CommandLine& commandLine)
    {
        InstallerWindow window(config, commandLine);
        return window.Run();
    }

    bool RenderModernPreview(Config& config, const std::wstring& page, UINT dpi, const std::filesystem::path& png)
    {
        HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        bool rendered = false;
        {
            InstallerWindow window(config, CommandLine{});
            rendered = window.RenderPreview(page, dpi, png);
        }
        if (SUCCEEDED(co))
            CoUninitialize();
        return rendered;
    }
}
