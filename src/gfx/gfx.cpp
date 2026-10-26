// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later

#include "gfx.h"

#include <dwrite_1.h>

#include <cmath>

namespace sr::gfx
{
namespace
{
constexpr float Pi = 3.14159265358979f;

template <class T> void SafeRelease(T*& p)
{
    if (p) p->Release();
    p = nullptr;
}

Palette MakeDark()
{
    Palette p;
    p.dark = true;
    p.base = Color(0x0E1116); p.surface1 = Color(0x151A21); p.surface2 = Color(0x1B212A); p.surface3 = Color(0x232A35);
    p.stroke = Color(0xFFFFFF, 0.08f);
    p.text = Color(0xE8ECF1); p.textDim = Color(0x9AA4B2); p.textFaint = Color(0x6B7584);
    p.accent = Color(0x35D6C0); p.accentInk = Color(0x04201C);
    p.safe = Color(0x4CC38A); p.review = Color(0xF2B84B); p.caution = Color(0xFF6F61); p.protectedC = Color(0xA391FF);
    p.freeSpace = Color(0xFFFFFF, 0.12f);
    p.kinds = { Color(0xF27A8C), Color(0xF59E6C), Color(0xE8C25A), Color(0x9BD46A), Color(0x4FD3A0),
                Color(0x3FCFD9), Color(0x5AA9F5), Color(0x8C8CF7), Color(0xB784F0), Color(0x8E99A8) };
    return p;
}

Palette MakeLight()
{
    Palette p;
    p.dark = false;
    p.base = Color(0xF4F6F8); p.surface1 = Color(0xFFFFFF); p.surface2 = Color(0xF0F2F5); p.surface3 = Color(0xE6E9EE);
    p.stroke = Color(0x000000, 0.10f);
    p.text = Color(0x11151B); p.textDim = Color(0x556070); p.textFaint = Color(0x8A94A3);
    p.accent = Color(0x0B8F80); p.accentInk = Color(0xFFFFFF);
    p.safe = Color(0x1E8A57); p.review = Color(0xA86A00); p.caution = Color(0xC8372D); p.protectedC = Color(0x6B54E0);
    p.freeSpace = Color(0x000000, 0.12f);
    p.kinds = { Color(0xC2405A), Color(0xC0612A), Color(0x9C7A0A), Color(0x4E8A1F), Color(0x14895F),
                Color(0x0F8590), Color(0x2167B8), Color(0x4B4BC4), Color(0x7A43B8), Color(0x5E6878) };
    return p;
}

struct TextSpec
{
    const wchar_t* family;
    float size;
    DWRITE_FONT_WEIGHT weight;
};

TextSpec Spec(Text t)
{
    switch (t)
    {
    case Text::Hero: return { L"Segoe UI Variable Display", 44, DWRITE_FONT_WEIGHT_SEMI_BOLD };
    case Text::Title: return { L"Segoe UI Variable Display", 28, DWRITE_FONT_WEIGHT_SEMI_BOLD };
    case Text::Section: return { L"Segoe UI Variable Display", 20, DWRITE_FONT_WEIGHT_SEMI_BOLD };
    case Text::Number: return { L"Segoe UI Variable Display", 22, DWRITE_FONT_WEIGHT_SEMI_BOLD };
    case Text::Body: return { L"Segoe UI Variable Text", 14, DWRITE_FONT_WEIGHT_REGULAR };
    case Text::BodyStrong: return { L"Segoe UI Variable Text", 14, DWRITE_FONT_WEIGHT_SEMI_BOLD };
    case Text::Caption: return { L"Segoe UI Variable Text", 12, DWRITE_FONT_WEIGHT_REGULAR };
    case Text::CaptionStrong: return { L"Segoe UI Variable Text", 12, DWRITE_FONT_WEIGHT_SEMI_BOLD };
    case Text::Overline: return { L"Segoe UI Variable Small", 11, DWRITE_FONT_WEIGHT_SEMI_BOLD };
    case Text::Icon: return { L"Segoe Fluent Icons", 16, DWRITE_FONT_WEIGHT_REGULAR };
    case Text::IconLarge: return { L"Segoe Fluent Icons", 20, DWRITE_FONT_WEIGHT_REGULAR };
    default: return { L"Segoe UI", 14, DWRITE_FONT_WEIGHT_REGULAR };
    }
}

bool FontExists(const wchar_t* family)
{
    IDWriteFontCollection* fonts = nullptr;
    if (FAILED(Write()->GetSystemFontCollection(&fonts))) return false;
    UINT32 index = 0;
    BOOL exists = FALSE;
    fonts->FindFamilyName(family, &index, &exists);
    fonts->Release();
    return exists != FALSE;
}

// Path geometry for an annular sector.
ID2D1PathGeometry* SectorGeometry(float cx, float cy, float r0, float r1, float a0, float a1)
{
    ID2D1PathGeometry* geo = nullptr;
    if (FAILED(Factory()->CreatePathGeometry(&geo))) return nullptr;
    ID2D1GeometrySink* sink = nullptr;
    geo->Open(&sink);
    auto pt = [&](float r, float a) { return D2D1::Point2F(cx + r * std::sin(a), cy - r * std::cos(a)); };
    const bool large = (a1 - a0) > Pi;
    sink->BeginFigure(pt(r1, a0), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddArc(D2D1::ArcSegment(pt(r1, a1), D2D1::SizeF(r1, r1), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE,
        large ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
    if (r0 > 0.5f)
    {
        sink->AddLine(pt(r0, a1));
        sink->AddArc(D2D1::ArcSegment(pt(r0, a0), D2D1::SizeF(r0, r0), 0, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
            large ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
    }
    else sink->AddLine(D2D1::Point2F(cx, cy));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    sink->Release();
    return geo;
}
}

const Palette& Dark()
{
    static const Palette p = MakeDark();
    return p;
}

const Palette& Light()
{
    static const Palette p = MakeLight();
    return p;
}

bool SystemPrefersDark()
{
    DWORD value = 1, size = sizeof value;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) return false;
    return value == 0;
}

ID2D1Factory* Factory()
{
    static ID2D1Factory* f = [] {
        ID2D1Factory* p = nullptr;
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr, reinterpret_cast<void**>(&p));
        return p;
    }();
    return f;
}

IDWriteFactory* Write()
{
    static IDWriteFactory* f = [] {
        IDWriteFactory* p = nullptr;
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&p));
        return p;
    }();
    return f;
}

bool AnimationsEnabled()
{
    BOOL on = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0);
    return on != FALSE;
}

float Ease(const DWORD startTick, const DWORD ms, const bool emphasized)
{
    if (!AnimationsEnabled() || ms == 0) return 1.0f;
    const float t = std::clamp(static_cast<float>(GetTickCount() - startTick) / static_cast<float>(ms), 0.0f, 1.0f);
    // Close approximations of the DESIGN.md bezier curves.
    const float inv = 1.0f - t;
    return emphasized ? 1.0f - inv * inv * inv * inv * inv : 1.0f - inv * inv * inv;
}

FileKind KindOf(const wchar_t* name, const size_t len)
{
    size_t dot = len;
    while (dot > 0 && name[dot - 1] != L'.' && name[dot - 1] != L'\\') --dot;
    if (dot == 0 || name[dot - 1] != L'.' || len - dot > 8) return FileKind::Other;
    wchar_t ext[9] = {};
    for (size_t i = 0; i < len - dot; ++i) ext[i] = static_cast<wchar_t>(towlower(name[dot + i]));
    struct Map { const wchar_t* e; FileKind k; };
    static const Map map[] = {
        { L"mp4", FileKind::Video }, { L"mkv", FileKind::Video }, { L"mov", FileKind::Video }, { L"avi", FileKind::Video },
        { L"wmv", FileKind::Video }, { L"webm", FileKind::Video }, { L"m4v", FileKind::Video }, { L"flv", FileKind::Video },
        { L"jpg", FileKind::Images }, { L"jpeg", FileKind::Images }, { L"png", FileKind::Images }, { L"gif", FileKind::Images },
        { L"bmp", FileKind::Images }, { L"webp", FileKind::Images }, { L"heic", FileKind::Images }, { L"raw", FileKind::Images },
        { L"psd", FileKind::Images }, { L"svg", FileKind::Images }, { L"ico", FileKind::Images }, { L"tif", FileKind::Images },
        { L"mp3", FileKind::Audio }, { L"wav", FileKind::Audio }, { L"flac", FileKind::Audio }, { L"m4a", FileKind::Audio },
        { L"aac", FileKind::Audio }, { L"ogg", FileKind::Audio }, { L"wma", FileKind::Audio },
        { L"pdf", FileKind::Documents }, { L"docx", FileKind::Documents }, { L"doc", FileKind::Documents },
        { L"xlsx", FileKind::Documents }, { L"pptx", FileKind::Documents }, { L"txt", FileKind::Documents },
        { L"md", FileKind::Documents }, { L"csv", FileKind::Documents }, { L"epub", FileKind::Documents },
        { L"zip", FileKind::Archives }, { L"7z", FileKind::Archives }, { L"rar", FileKind::Archives }, { L"gz", FileKind::Archives },
        { L"tar", FileKind::Archives }, { L"msi", FileKind::Archives }, { L"msix", FileKind::Archives }, { L"cab", FileKind::Archives },
        { L"appx", FileKind::Archives }, { L"nupkg", FileKind::Archives }, { L"jar", FileKind::Archives },
        { L"js", FileKind::Code }, { L"ts", FileKind::Code }, { L"py", FileKind::Code }, { L"cpp", FileKind::Code },
        { L"h", FileKind::Code }, { L"cs", FileKind::Code }, { L"java", FileKind::Code }, { L"json", FileKind::Code },
        { L"class", FileKind::Code }, { L"pyc", FileKind::Code }, { L"o", FileKind::Code }, { L"obj", FileKind::Code },
        { L"pdb", FileKind::Code }, { L"lib", FileKind::Code }, { L"map", FileKind::Code }, { L"go", FileKind::Code },
        { L"rs", FileKind::Code }, { L"node", FileKind::Code }, { L"wasm", FileKind::Code },
        { L"exe", FileKind::Programs }, { L"dll", FileKind::Programs }, { L"sys", FileKind::Programs }, { L"so", FileKind::Programs },
        { L"mui", FileKind::Programs }, { L"ocx", FileKind::Programs },
        { L"iso", FileKind::DiskImages }, { L"img", FileKind::DiskImages }, { L"vhd", FileKind::DiskImages },
        { L"vhdx", FileKind::DiskImages }, { L"vmdk", FileKind::DiskImages }, { L"qcow2", FileKind::DiskImages },
        { L"vdi", FileKind::DiskImages }, { L"wim", FileKind::DiskImages }, { L"esd", FileKind::DiskImages },
        { L"etl", FileKind::System }, { L"log", FileKind::System }, { L"dat", FileKind::System }, { L"db", FileKind::System },
        { L"evtx", FileKind::System }, { L"cat", FileKind::System }, { L"manifest", FileKind::System }, { L"tmp", FileKind::System },
    };
    for (const Map& m : map)
        if (wcscmp(m.e, ext) == 0) return m.k;
    return FileKind::Other;
}

const wchar_t* KindLabel(FileKind k)
{
    static const wchar_t* labels[] = { L"Video", L"Images", L"Audio", L"Documents", L"Archives & installers",
        L"Code & dev", L"Programs", L"Disk images & VMs", L"System files", L"Other" };
    return labels[static_cast<size_t>(k)];
}

// --- Canvas ----------------------------------------------------------------------

bool Canvas::Begin(HWND hwnd, const Palette& palette)
{
    m_palette = &palette;
    if (!m_target || m_hwnd != hwnd)
    {
        Discard();
        m_hwnd = hwnd;
        RECT rc;
        GetClientRect(hwnd, &rc);
        const UINT dpi = GetDpiForWindow(hwnd);
        m_dpi = static_cast<float>(dpi ? dpi : 96);
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), m_dpi, m_dpi);
        if (FAILED(Factory()->CreateHwndRenderTarget(props,
            D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)), &m_target)))
            return false;
        m_target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        m_target->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &m_brush);
    }
    const UINT dpi = GetDpiForWindow(hwnd);
    if (dpi && static_cast<float>(dpi) != m_dpi)
    {
        m_dpi = static_cast<float>(dpi);
        m_target->SetDpi(m_dpi, m_dpi);
        for (auto& f : m_formats) f.second->Release();
        m_formats.clear();
    }
    m_size = m_target->GetSize();
    m_target->BeginDraw();
    m_target->SetTransform(D2D1::Matrix3x2F::Identity());
    return true;
}

bool Canvas::End()
{
    const HRESULT hr = m_target->EndDraw();
    if (hr == static_cast<HRESULT>(D2DERR_RECREATE_TARGET))
    {
        Discard();
        return false;
    }
    return SUCCEEDED(hr);
}

void Canvas::Resize(const UINT width, const UINT height)
{
    if (m_target) m_target->Resize(D2D1::SizeU(width, height));
}

void Canvas::Discard()
{
    SafeRelease(m_brush);
    SafeRelease(m_target);
    for (auto& f : m_formats) f.second->Release();
    m_formats.clear();
}

ID2D1SolidColorBrush* Canvas::Brush(const Color& c)
{
    m_brush->SetColor(c.D2D());
    return m_brush;
}

IDWriteTextFormat* Canvas::Format(gfx::Text style)
{
    const auto it = m_formats.find(static_cast<int>(style));
    if (it != m_formats.end()) return it->second;
    TextSpec s = Spec(style);
    static const bool variable = FontExists(L"Segoe UI Variable Text");
    static const bool fluent = FontExists(L"Segoe Fluent Icons");
    if (!variable && wcsncmp(s.family, L"Segoe UI Variable", 17) == 0) s.family = L"Segoe UI";
    if (!fluent && wcscmp(s.family, L"Segoe Fluent Icons") == 0) s.family = L"Segoe MDL2 Assets";
    IDWriteTextFormat* f = nullptr;
    Write()->CreateTextFormat(s.family, nullptr, s.weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, s.size,
        L"en-us", &f);
    if (f) f->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    m_formats[static_cast<int>(style)] = f;
    return f;
}

void Canvas::Clear(const Color& c) { m_target->Clear(c.D2D()); }

void Canvas::Fill(const D2D1_RECT_F& r, const Color& c) { m_target->FillRectangle(r, Brush(c)); }

void Canvas::FillRound(const D2D1_RECT_F& r, const float radius, const Color& c)
{
    m_target->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), Brush(c));
}

void Canvas::StrokeRound(const D2D1_RECT_F& r, const float radius, const Color& c, const float width)
{
    const float h = width / 2;
    m_target->DrawRoundedRectangle(D2D1::RoundedRect({ r.left + h, r.top + h, r.right - h, r.bottom - h }, radius, radius),
        Brush(c), width);
}

void Canvas::Line(const float x1, const float y1, const float x2, const float y2, const Color& c, const float width)
{
    m_target->DrawLine({ x1, y1 }, { x2, y2 }, Brush(c), width);
}

void Canvas::Circle(const float cx, const float cy, const float radius, const Color& c)
{
    m_target->FillEllipse(D2D1::Ellipse({ cx, cy }, radius, radius), Brush(c));
}

void Canvas::Shadow(const D2D1_RECT_F& r, const float radius, const float blur, const float offsetY, const float alpha)
{
    const int steps = 6;
    for (int i = steps; i >= 1; --i)
    {
        const float grow = blur * static_cast<float>(i) / steps;
        const float a = alpha * (1.0f - static_cast<float>(i - 1) / steps) / steps * 1.6f;
        FillRound({ r.left - grow, r.top - grow + offsetY, r.right + grow, r.bottom + grow + offsetY }, radius + grow,
            Color(0x000000, a));
    }
}

void Canvas::Card(const D2D1_RECT_F& r, const float radius, const bool raised)
{
    if (raised) Shadow(r, radius, 16, 4, P().dark ? 0.24f : 0.08f);
    FillRound(r, radius, raised ? P().surface2 : P().surface1);
    StrokeRound(r, radius, P().stroke);
}

void Canvas::Arc(const float cx, const float cy, const float r0, const float r1, float a0, float a1, const Color& fill,
    const Color* stroke)
{
    if (a1 - a0 >= 2 * Pi - 0.0005f)
    {
        // Full ring: two halves avoid a degenerate arc.
        Arc(cx, cy, r0, r1, a0, a0 + Pi, fill, stroke);
        Arc(cx, cy, r0, r1, a0 + Pi, a1, fill, stroke);
        return;
    }
    if (a1 - a0 < 0.0005f) return;
    ID2D1PathGeometry* geo = SectorGeometry(cx, cy, r0, r1, a0, a1);
    if (!geo) return;
    m_target->FillGeometry(geo, Brush(fill));
    if (stroke) m_target->DrawGeometry(geo, Brush(*stroke), 1.0f);
    geo->Release();
}

void Canvas::ArcStroke(const float cx, const float cy, const float radius, float a0, float a1, const float width, const Color& c)
{
    if (a1 - a0 < 0.001f) return;
    a1 = std::min(a1, a0 + 2 * Pi - 0.001f);
    ID2D1PathGeometry* geo = nullptr;
    if (FAILED(Factory()->CreatePathGeometry(&geo))) return;
    ID2D1GeometrySink* sink = nullptr;
    geo->Open(&sink);
    auto pt = [&](float a) { return D2D1::Point2F(cx + radius * std::sin(a), cy - radius * std::cos(a)); };
    sink->BeginFigure(pt(a0), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(pt(a1), D2D1::SizeF(radius, radius), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE,
        (a1 - a0) > Pi ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    sink->Release();
    ID2D1StrokeStyle* style = nullptr;
    Factory()->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND), nullptr, 0, &style);
    m_target->DrawGeometry(geo, Brush(c), width, style);
    if (style) style->Release();
    geo->Release();
}

void Canvas::Text(const std::wstring& s, const D2D1_RECT_F& r, gfx::Text style, const Color& c, const Align align,
    const bool vcenter)
{
    IDWriteTextFormat* f = Format(style);
    if (!f || s.empty()) return;
    IDWriteTextLayout* layout = nullptr;
    if (FAILED(Write()->CreateTextLayout(s.c_str(), static_cast<UINT32>(s.size()), f, std::max(1.0f, r.right - r.left),
        std::max(1.0f, r.bottom - r.top), &layout))) return;
    layout->SetTextAlignment(align == Align::Center ? DWRITE_TEXT_ALIGNMENT_CENTER :
        align == Align::Right ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
    if (vcenter) layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    // Numbers line up: tabular figures for every numeric style.
    if (style == Text::Hero || style == Text::Number || style == Text::Caption || style == Text::CaptionStrong ||
        style == Text::Body || style == Text::BodyStrong)
    {
        IDWriteTypography* typo = nullptr;
        if (SUCCEEDED(Write()->CreateTypography(&typo)))
        {
            typo->AddFontFeature({ DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1 });
            layout->SetTypography(typo, { 0, static_cast<UINT32>(s.size()) });
            typo->Release();
        }
    }
    if (style == Text::Overline)
    {
        IDWriteTextLayout1* layout1 = nullptr;
        if (SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout1), reinterpret_cast<void**>(&layout1))))
        {
            layout1->SetCharacterSpacing(0, 0.7f, 0, { 0, static_cast<UINT32>(s.size()) });
            layout1->Release();
        }
    }
    // Single-line styles get an ellipsis instead of wrapping.
    if (r.bottom - r.top < Spec(style).size * 2.2f)
    {
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        IDWriteInlineObject* ellipsis = nullptr;
        if (SUCCEEDED(Write()->CreateEllipsisTrimmingSign(f, &ellipsis)))
        {
            DWRITE_TRIMMING trim = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
            layout->SetTrimming(&trim, ellipsis);
            ellipsis->Release();
        }
    }
    m_target->DrawTextLayout({ r.left, r.top }, layout, Brush(c), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    layout->Release();
}

D2D1_SIZE_F Canvas::Measure(const std::wstring& s, gfx::Text style, const float maxWidth)
{
    IDWriteTextFormat* f = Format(style);
    IDWriteTextLayout* layout = nullptr;
    if (!f || FAILED(Write()->CreateTextLayout(s.c_str(), static_cast<UINT32>(s.size()), f, maxWidth, 10000.0f, &layout)))
        return { 0, 0 };
    DWRITE_TEXT_METRICS m = {};
    layout->GetMetrics(&m);
    layout->Release();
    return { m.widthIncludingTrailingWhitespace, m.height };
}

void Canvas::Icon(const wchar_t glyph, const float x, const float y, const float size, const Color& c)
{
    const std::wstring s(1, glyph);
    Text(s, Rect(x, y, size, size), size > 18 ? Text::IconLarge : Text::Icon, c, Align::Center, true);
}

void Canvas::Pixels(const std::vector<UINT32>& bgra, const UINT width, const UINT height, const D2D1_RECT_F& dest)
{
    if (!width || !height || bgra.size() < static_cast<size_t>(width) * height) return;
    ID2D1Bitmap* bmp = nullptr;
    const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), m_dpi, m_dpi);
    if (FAILED(m_target->CreateBitmap(D2D1::SizeU(width, height), bgra.data(), width * 4, props, &bmp))) return;
    m_target->DrawBitmap(bmp, dest, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    bmp->Release();
}

void Canvas::PushClip(const D2D1_RECT_F& r) { m_target->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED); }

void Canvas::PopClip() { m_target->PopAxisAlignedClip(); }
}
