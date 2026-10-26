// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later
//
// Treemap: squarified layout and cushion shading follow upstream WinDirStat (Controls/TreeMapLayout.cpp
// ArrangeSquarified, Controls/TreeMap.cpp AddRidge/DrawCushion), which implement van Wijk & van de
// Wetering's "Cushion Treemaps" (SequoiaView). Rewritten for our ScanTree and pixel buffer.

#include "viz.h"

#include <chrono>
#include <cmath>

namespace sr::viz
{
namespace
{
constexpr float Pi = 3.14159265358979f;
constexpr size_t Kinds = static_cast<size_t>(gfx::FileKind::Count);
}

KindModel BuildKinds(const ScanTree& tree)
{
    KindModel m;
    const u32 n = tree.Count();
    m.kind.assign(n, static_cast<unsigned char>(gfx::FileKind::Other));
    // Per-folder byte sums by kind, only for folders (slot table keeps memory to folders).
    std::vector<u32> slot(n, NoNode);
    std::vector<std::array<u64, Kinds>> sums;
    for (u32 i = 0; i < n; ++i)
        if (tree.At(i).flags & NF_DIR)
        {
            slot[i] = static_cast<u32>(sums.size());
            sums.push_back({});
        }
    // Post-order (children before parents) propagates sums upward in one pass.
    for (const u32 i : tree.PostOrder())
    {
        const Node& node = tree.At(i);
        if (node.flags & NF_DIR)
        {
            const auto& s = sums[slot[i]];
            const auto best = std::max_element(s.begin(), s.end());
            m.kind[i] = static_cast<unsigned char>(*best ? best - s.begin() : Kinds - 1);
            if (node.parent != NoNode)
            {
                auto& p = sums[slot[node.parent]];
                for (size_t k = 0; k < Kinds; ++k) p[k] += s[k];
            }
            continue;
        }
        gfx::FileKind kind = gfx::FileKind::Other;
        if (!(node.flags & NF_AGGREGATE))
        {
            const std::wstring name = tree.Name(i);
            kind = gfx::KindOf(name.c_str(), name.size());
        }
        m.kind[i] = static_cast<unsigned char>(kind);
        m.totals[static_cast<size_t>(kind)] += node.size;
        if (node.parent != NoNode) sums[slot[node.parent]][static_cast<size_t>(kind)] += node.size;
    }
    return m;
}

// --- Sunburst -------------------------------------------------------------------

void Sunburst::Build(const ScanTree& tree, const u32 root, const u64 freeBytes, const int maxDepth)
{
    m_segments.clear();
    m_maxDepth = maxDepth;
    const u64 total = tree.At(root).size + freeBytes;
    if (!total) return;
    const double perByte = 2.0 * Pi / static_cast<double>(total);
    constexpr float MinAngle = 0.006f; // ~0.35 degrees: smaller children are grouped as "other"

    struct Item { u32 node; int depth; double a0; };
    std::vector<Item> stack{ { root, 0, 0.0 } };
    while (!stack.empty())
    {
        const Item it = stack.back();
        stack.pop_back();
        if (it.depth >= maxDepth) continue;
        double a = it.a0;
        u64 otherBytes = 0;
        double otherStart = -1;
        for (u32 c : tree.Children(it.node))
        {
            const Node& n = tree.At(c);
            if (!n.size) continue;
            const double span = static_cast<double>(n.size) * perByte;
            if (span < MinAngle)
            {
                if (otherStart < 0) otherStart = a;
                otherBytes += n.size;
                a += span;
                continue;
            }
            Segment s;
            s.node = c;
            s.depth = it.depth + 1;
            s.a0 = static_cast<float>(a);
            s.a1 = static_cast<float>(a + span);
            s.bytes = n.size;
            m_segments.push_back(s);
            if (n.flags & NF_DIR) stack.push_back({ c, it.depth + 1, a });
            a += span;
        }
        if (otherBytes && otherStart >= 0)
        {
            Segment s;
            s.depth = it.depth + 1;
            s.other = true;
            s.a0 = static_cast<float>(otherStart);
            s.a1 = static_cast<float>(a);
            s.bytes = otherBytes;
            m_segments.push_back(s);
        }
        if (it.node == root && freeBytes)
        {
            Segment s;
            s.depth = 1;
            s.freeSpace = true;
            s.a0 = static_cast<float>(a);
            s.a1 = 2 * Pi;
            s.bytes = freeBytes;
            m_segments.push_back(s);
        }
    }
}

void Sunburst::Draw(gfx::Canvas& c, const KindModel& kinds, const float cx, const float cy, const float innerR,
    const float outerR, const float progress, const int hover) const
{
    const gfx::Palette& p = c.P();
    const float ring = (outerR - innerR) / static_cast<float>(m_maxDepth);
    const gfx::Color gap = p.base;
    for (size_t i = 0; i < m_segments.size(); ++i)
    {
        const Segment& s = m_segments[i];
        // Unfold: inner rings appear first, each ring sweeps open.
        const float ringT = std::clamp(progress * (m_maxDepth + 1) - static_cast<float>(s.depth - 1), 0.0f, 1.0f);
        if (ringT <= 0) continue;
        const float a0 = s.a0 * ringT, a1 = s.a1 * ringT;
        const float r0 = innerR + ring * static_cast<float>(s.depth - 1) + 1.0f;
        const float r1 = r0 + ring - 2.0f;
        gfx::Color fill = s.freeSpace ? p.freeSpace : s.other ? p.textFaint.WithAlpha(0.35f) :
            p.Kind(kinds.Of(s.node));
        if (!s.freeSpace && !s.other) fill = fill.Mix(p.base, 0.10f * static_cast<float>(s.depth - 1));
        if (static_cast<int>(i) == hover) fill = fill.Mix(p.text, 0.25f);
        c.Arc(cx, cy, r0, r1, a0, a1, fill, &gap);
    }
}

int Sunburst::Hit(const float x, const float y, const float cx, const float cy, const float innerR, const float outerR) const
{
    const float dx = x - cx, dy = y - cy;
    const float r = std::sqrt(dx * dx + dy * dy);
    if (r < innerR || r > outerR) return -1;
    float a = std::atan2(dx, -dy);
    if (a < 0) a += 2 * Pi;
    const int depth = 1 + static_cast<int>((r - innerR) / ((outerR - innerR) / static_cast<float>(m_maxDepth)));
    for (size_t i = 0; i < m_segments.size(); ++i)
    {
        const Segment& s = m_segments[i];
        if (s.depth == depth && a >= s.a0 && a < s.a1) return static_cast<int>(i);
    }
    return -1;
}

// --- Treemap --------------------------------------------------------------------

void Treemap::Render(const ScanTree& tree, const KindModel& kinds, const gfx::Palette& palette, const u32 root,
    const int width, const int height, const TreemapOptions& options)
{
    const auto t0 = std::chrono::steady_clock::now();
    m_opt = options;
    m_width = std::max(1, width);
    m_height = std::max(1, height);
    m_pixels.assign(static_cast<size_t>(m_width) * m_height, 0);
    m_tiles.clear();
    // Light source as in upstream TreeMap.cpp: (lx, ly, 10) normalised.
    const float len = std::sqrt(options.lightX * options.lightX + options.lightY * options.lightY + 100.0f);
    m_lx = options.lightX / len;
    m_ly = options.lightY / len;
    m_lz = 10.0f / len;
    const float surface[4] = { 0, 0, 0, 0 };
    Node(tree, kinds, palette, root, 0, 0, m_width, m_height, surface, options.height, 0);
    m_millis = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void Treemap::Node(const ScanTree& tree, const KindModel& kinds, const gfx::Palette& palette, const u32 node, const int x0,
    const int y0, const int x1, const int y1, const float* parentSurface, const float h, const int depth)
{
    if (x1 <= x0 || y1 <= y0) return;
    // AddRidge (upstream TreeMap.cpp): each level adds a parabolic cushion over its rectangle.
    float s[4] = { parentSurface[0], parentSurface[1], parentSurface[2], parentSurface[3] };
    if (h > 0)
    {
        const float h4 = 4.0f * h;
        const float wf = h4 / static_cast<float>(x1 - x0);
        s[2] += wf * static_cast<float>(x1 + x0);
        s[0] -= wf;
        const float hf = h4 / static_cast<float>(y1 - y0);
        s[3] += hf * static_cast<float>(y1 + y0);
        s[1] -= hf;
    }

    const sr::Node& n = tree.At(node);
    const bool leaf = !(n.flags & NF_DIR) || n.firstChild == NoNode || depth >= m_opt.maxDepth ||
        (x1 - x0) * (y1 - y0) <= 4;
    if (leaf)
    {
        Cushion(x0, y0, x1, y1, s, palette.Kind(kinds.Of(node)));
        m_tiles.push_back({ x0, y0, x1, y1, node });
        return;
    }

    // Squarified layout (upstream ArrangeSquarified): rows along the shorter side, growing a row
    // while the worst aspect ratio improves.
    std::vector<u32> kids = tree.Children(node);
    while (!kids.empty() && tree.At(kids.back()).size == 0) kids.pop_back();
    if (kids.empty())
    {
        Cushion(x0, y0, x1, y1, s, palette.Kind(kinds.Of(node)));
        return;
    }
    double remaining = static_cast<double>(n.size);
    double rx0 = x0, ry0 = y0;
    const double rx1 = x1, ry1 = y1;
    size_t i = 0;
    while (i < kids.size())
    {
        const double w = rx1 - rx0, hgt = ry1 - ry0;
        if (w < 1 || hgt < 1 || remaining <= 0) break;
        const bool horizontal = w >= hgt;          // lay the row along the shorter side
        const double side = horizontal ? hgt : w;
        const double area = w * hgt;
        const double scale = area / remaining;     // pixels per byte
        double rowSum = 0;
        double worst = 1e300;
        size_t j = i;
        double minSize = 0, maxSize = 0;
        while (j < kids.size())
        {
            const double sz = static_cast<double>(tree.At(kids[j]).size) * scale;
            const double sum = rowSum + sz;
            const double mn = j == i ? sz : std::min(minSize, sz);
            const double mx = j == i ? sz : std::max(maxSize, sz);
            const double thick = sum / side;
            const double ratio = std::max((mx / thick) / thick, thick / (mn / thick));
            if (j > i && ratio > worst) break;
            worst = ratio;
            rowSum = sum;
            minSize = mn;
            maxSize = mx;
            ++j;
        }
        const double thick = rowSum / side;
        double pos = horizontal ? ry0 : rx0;
        for (size_t k = i; k < j; ++k)
        {
            const double len = static_cast<double>(tree.At(kids[k]).size) * scale / thick;
            const double end = k + 1 == j ? (horizontal ? ry1 : rx1) : pos + len;
            if (horizontal)
                this->Node(tree, kinds, palette, kids[k], static_cast<int>(std::lround(rx0)), static_cast<int>(std::lround(pos)),
                    static_cast<int>(std::lround(rx0 + thick)), static_cast<int>(std::lround(end)), s, h * m_opt.scaleFactor, depth + 1);
            else
                this->Node(tree, kinds, palette, kids[k], static_cast<int>(std::lround(pos)), static_cast<int>(std::lround(ry0)),
                    static_cast<int>(std::lround(end)), static_cast<int>(std::lround(ry0 + thick)), s, h * m_opt.scaleFactor, depth + 1);
            pos = end;
            remaining -= static_cast<double>(tree.At(kids[k]).size);
        }
        if (horizontal) rx0 += thick;
        else ry0 += thick;
        i = j;
    }
}

void Treemap::Cushion(const int x0, const int y0, const int x1, const int y1, const float* s, const gfx::Color& color)
{
    // Per-pixel shading (upstream DrawCushion): normal from the cushion surface, Lambert light + ambient.
    const float ia = m_opt.ambient, is = 1.0f - ia;
    const int cx0 = std::max(0, x0), cy0 = std::max(0, y0), cx1 = std::min(m_width, x1), cy1 = std::min(m_height, y1);
    for (int iy = cy0; iy < cy1; ++iy)
    {
        const float ny = -(2.0f * s[1] * (static_cast<float>(iy) + 0.5f) + s[3]);
        UINT32* row = m_pixels.data() + static_cast<size_t>(iy) * m_width;
        for (int ix = cx0; ix < cx1; ++ix)
        {
            const float nx = -(2.0f * s[0] * (static_cast<float>(ix) + 0.5f) + s[2]);
            float cosa = (nx * m_lx + ny * m_ly + m_lz) / std::sqrt(nx * nx + ny * ny + 1.0f);
            if (cosa > 1.0f) cosa = 1.0f;
            float pixel = is * cosa;
            if (pixel < 0) pixel = 0;
            pixel += ia;
            // Brighten slightly (upstream brightness 0.88 on palette colors); clamp each channel.
            const float k = pixel * 1.15f;
            const auto ch = [k](float v) { return static_cast<UINT32>(std::clamp(v * k, 0.0f, 1.0f) * 255.0f + 0.5f); };
            row[ix] = 0xFF000000u | (ch(color.r) << 16) | (ch(color.g) << 8) | ch(color.b);
        }
    }
}

u32 Treemap::Hit(const int x, const int y) const
{
    for (auto it = m_tiles.rbegin(); it != m_tiles.rend(); ++it)
        if (x >= it->x0 && x < it->x1 && y >= it->y0 && y < it->y1) return it->node;
    return NoNode;
}

// --- Flame graph ----------------------------------------------------------------

void FlameGraph::Draw(gfx::Canvas& c, const ScanTree& tree, const KindModel& kinds, const u32 root, const D2D1_RECT_F& area,
    const float rowHeight, const int maxDepth) const
{
    const gfx::Palette& p = c.P();
    const double total = static_cast<double>(std::max<u64>(1, tree.At(root).size));
    const double width = area.right - area.left;
    struct Item { u32 node; int depth; double x; };
    std::vector<Item> stack{ { root, 0, area.left } };
    while (!stack.empty())
    {
        const Item it = stack.back();
        stack.pop_back();
        const Node& n = tree.At(it.node);
        const double w = static_cast<double>(n.size) / total * width;
        const float y = area.top + static_cast<float>(it.depth) * rowHeight;
        if (w < 1.0 || y + rowHeight > area.bottom) continue;
        const D2D1_RECT_F r = { static_cast<float>(it.x), y, static_cast<float>(it.x + w) - 1.0f, y + rowHeight - 2.0f };
        c.FillRound(r, 3.0f, p.Kind(kinds.Of(it.node)).Mix(p.base, it.depth == 0 ? 0.0f : 0.15f));
        if (w > 48)
            c.Text(tree.Name(it.node) + L"  " + FormatBytes(n.size), { r.left + 6, r.top, r.right - 4, r.bottom },
                gfx::Text::Caption, p.dark ? gfx::Color(0x0E1116) : gfx::Color(0xFFFFFF), gfx::Align::Left, true);
        if (it.depth + 1 >= maxDepth || !(n.flags & NF_DIR)) continue;
        double x = it.x;
        for (u32 ch : tree.Children(it.node))
        {
            const double cw = static_cast<double>(tree.At(ch).size) / total * width;
            if (cw >= 1.0) stack.push_back({ ch, it.depth + 1, x });
            x += cw;
        }
    }
}
}
