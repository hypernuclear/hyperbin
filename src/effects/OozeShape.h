// The gel's silhouette, in one place.
//
// Several things need to agree about the bin's shape: the mesh swept
// from it, where the gel's surface may reach, and the margins the
// overlay reserves. When they each measured for themselves they
// disagreed — the same class of bug as the flies' simulation disagreeing
// with their shader about where the bin was, and it wants the same fix:
// one authority, measured once.
//
// Everything here is in bin-local units: the origin is the centre of the
// bin's rect and +y is up, which is the space the Quick3D scene is
// authored in.
#pragma once

#include <QVector>

namespace hyperbin {

class OozeShape
{
public:
    /// How much the body is flattened front to back when it is swept.
    ///
    /// The bin is a little deeper than it is wide, so the sweep squashes
    /// z. It lives here rather than in OozeGeometry because anything that
    /// puts something ON the surface has to apply it too: eyeAt() did not,
    /// and an eye facing the camera came out a seventh of the body's
    /// radius clear of the gel it was supposed to be sitting in — which
    /// only showed at the front and back, where cos(angle) is largest.
    static constexpr float kDepth = 0.86f;

    /// Measure the bin's taper from its silhouette. Safe to call with an
    /// empty grid: falls back to a generic tapered barrel so the effect
    /// still draws something before the first artwork arrives.
    void measure(const QVector<quint8> &coverage, int gridW, int gridH,
                 float binW, float binH);

    /// Half-width of the gel at a height, already widened past the bin.
    float radiusAt(float y) const;
    /// Half-width of the BIN itself at a height — the raw measurement,
    /// before any widening or blending. What the gel is not allowed to
    /// shrink inside of: the difference between this and radiusAt() is
    /// the whole of the room an undulation has to move inward in.
    float binHalfWidthAt(float y) const;

    /// The bottom of the bin's ARTWORK, which is not the bottom of its
    /// rect: the rect is the Dock's square tile and the bin sits well
    /// above its lower edge.
    float floorY() const { return m_floorY; }

    /// The top of the gel, given how far up the bin it has crept. Never
    /// above the rim: liquid that reaches the rim is liquid that has gone
    /// over the side, and going over the side is the part of this effect
    /// that runs down the outside.
    float surfaceY(float contentLine, float level) const;
    /// The bin's rim: the widest point of its silhouette, which for a
    /// tapered container is the lip.
    ///
    /// NOT the content line. That is the topmost near-opaque row, which
    /// on the macOS Trash is the crumpled paper sticking OUT of the bin —
    /// a sixth of the way down the icon, well above the lip. Filling to
    /// it put the gel's surface above the rim, where it covered the lip
    /// and read as a lid resting on the bin rather than as something the
    /// bin is standing in.
    float rimY() const { return m_rimY; }
    float rimRadius() const { return m_widest; }

    /// The puddle at the foot. It reaches below the artwork — liquid
    /// that has run down has to end up somewhere — and its widest point
    /// grows as the bin fills.
    ///
    /// Round, not flat. The underside is given about as much height as
    /// the shoulder above it, so the pool comes out as a bulge with some
    /// depth to it. When the underside was a sliver and the shoulder did
    /// all the work, the whole thing read as the brim of a hat: a thin
    /// disc with a slope on top.
    float poolBottom() const { return m_floorY - m_binH * 0.095f; }
    float poolCrest() const { return m_floorY - m_binH * 0.030f; }
    float poolTop() const { return m_floorY + m_binH * 0.070f; }
    float poolRadius(float level) const
    {
        return radiusAt(m_floorY) * (1.18f + 0.34f * level);
    }

    float binWidth() const { return m_binW; }
    float binHeight() const { return m_binH; }

private:
    static constexpr int kRows = 48;

    float m_prof[kRows] {};
    float m_widest = 0.0f;
    float m_rimY = 0.0f;
    float m_floorY = 0.0f;
    float m_binW = 40.0f;
    float m_binH = 40.0f;
};

} // namespace hyperbin
