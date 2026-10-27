// SpaceRescue - SPDX-License-Identifier: GPL-3.0-or-later
//
// Visualisations over a ScanTree: sunburst, cushion treemap (layout and shading ported from
// upstream WinDirStat's TreeMapLayout.cpp / TreeMap.cpp, SequoiaView method) and flame graph.

#pragma once

#include "../core/scanner.h"
#include "../gfx/gfx.h"

namespace sr::viz
{
// File kind per node: files by extension, folders by the kind holding most of their bytes.
struct KindModel
{
    std::vector<unsigned char> kind;                          // per node, gfx::FileKind
    std::array<u64, static_cast<size_t>(gfx::FileKind::Count)> totals{}; // bytes per kind in the whole tree
    gfx::FileKind Of(u32 node) const { return static_cast<gfx::FileKind>(node < kind.size() ? kind[node] : 9); }
};
KindModel BuildKinds(const ScanTree& tree);

// --- Sunburst -----------------------------------------------------------------
struct Segment
{
    u32 node = NoNode;      // NoNode = free space or grouped "other"
    int depth = 1;          // ring 1 = children of the root
    float a0 = 0, a1 = 0;   // radians, 0 = 12 o'clock, clockwise
    bool freeSpace = false;
    bool other = false;
    u64 bytes = 0;
};

class Sunburst
{
public:
    // freeBytes > 0 adds a neutral free-space slice on ring 1 (drive roots).
    void Build(const ScanTree& tree, u32 root, u64 freeBytes, int maxDepth = 4);
    // progress 0..1 unfolds the rings (motion "emphasized").
    void Draw(gfx::Canvas& c, const KindModel& kinds, float cx, float cy, float innerR, float outerR, float progress,
        int hover = -1) const;
    // Segment index under (x, y), or -1.
    int Hit(float x, float y, float cx, float cy, float innerR, float outerR) const;
    const std::vector<Segment>& Segments() const { return m_segments; }
    int MaxDepth() const { return m_maxDepth; }

private:
    std::vector<Segment> m_segments;
    int m_maxDepth = 4;
};

// --- Treemap ------------------------------------------------------------------
struct TreemapOptions
{
    float height = 0.38f;      // upstream defaults (TreeMap.h)
    float scaleFactor = 0.91f;
    float ambient = 0.13f;
    float lightX = -1.0f, lightY = -1.0f;
    int maxDepth = 64;
};

struct TileRect
{
    int x0, y0, x1, y1;
    u32 node;
};

class Treemap
{
public:
    // Lays out and shades `root` into a width x height BGRA buffer.
    void Render(const ScanTree& tree, const KindModel& kinds, const gfx::Palette& palette, u32 root, int width, int height,
        const TreemapOptions& options = {});
    const std::vector<UINT32>& Pixels() const { return m_pixels; }
    int Width() const { return m_width; }
    int Height() const { return m_height; }
    // Leaf node under a pixel, or NoNode.
    u32 Hit(int x, int y) const;
    // Pixel rectangle a node (file or folder) occupies in the last render; false if it was too small to lay out.
    bool RectOf(u32 node, TileRect* out) const;
    double LayoutMillis() const { return m_millis; }

private:
    void Node(const ScanTree& tree, const KindModel& kinds, const gfx::Palette& palette, u32 node, int x0, int y0, int x1,
        int y1, const float* surface, float h, int depth);
    void Cushion(int x0, int y0, int x1, int y1, const float* surface, const gfx::Color& color);

    std::vector<UINT32> m_pixels;
    std::vector<TileRect> m_tiles;   // leaves (hit testing)
    std::vector<TileRect> m_all;     // every laid-out node, folders included (selection outline)
    int m_width = 0, m_height = 0;
    TreemapOptions m_opt;
    float m_lx = 0, m_ly = 0, m_lz = 0;
    double m_millis = 0;
};

// --- Flame graph --------------------------------------------------------------
class FlameGraph
{
public:
    // `hits` (optional) receives each drawn bar for hit testing.
    void Draw(gfx::Canvas& c, const ScanTree& tree, const KindModel& kinds, u32 root, const D2D1_RECT_F& area,
        float rowHeight = 22.0f, int maxDepth = 12, std::vector<std::pair<D2D1_RECT_F, u32>>* hits = nullptr,
        u32 selected = NoNode) const;
};
}
