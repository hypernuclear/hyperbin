#include "OozeEffect.h"

#include "IconTexture.h"
#include <algorithm>
#include <cmath>

namespace hyperbin {

OozeEffect::OozeEffect(QObject *parent)
    : Effect(parent)
    , m_iconTexture(new IconTexture)
{
}

OozeEffect::~OozeEffect()
{
    delete m_iconTexture;
}
QUrl OozeEffect::visualSource() const
{
    return QUrl(QStringLiteral("qrc:/qt/qml/hyperbin/qml/OozeVisual.qml"));
}

QObject *OozeEffect::iconTexture() const
{
    return m_iconTexture;
}

void OozeEffect::setBinImage(const QImage &img)
{
    m_iconTexture->setImage(img);
    emit shapeChanged();
}

void OozeEffect::setBinRect(const QRectF &binRect)
{
    m_sim.setBinRect(binRect);
    if (m_binRect != binRect) {
        m_binRect = binRect;
        m_binSize = binRect.size();
        m_shape.measure(m_coverage, m_covW, m_covH, float(m_binSize.width()),
                        float(m_binSize.height()));
        emit shapeChanged();
    }
}

void OozeEffect::setFullness(float fullness) { m_sim.setFullness(fullness); }

void OozeEffect::setCursor(const QPointF &pos, bool present)
{
    // The EYES react; the sludge does not. That distinction is why this
    // still never reports itself dismissed: being dismissed means the
    // effect has finished responding to the pointer and can be put away,
    // and an eye that follows you is doing the opposite — it is at its
    // most alive exactly when the pointer is there.
    m_cursor = pos;
    m_cursorOn = present;
}

void OozeEffect::updateGaze(float dt)
{
    const float binH = float(m_binSize.height());
    if (binH <= 0.0f)
        return;

    // Item pixels with +y DOWN, to bin-local scene units with +y UP.
    const QPointF centre = m_binRect.center();
    const float sx = float(m_cursor.x() - centre.x());
    const float sy = float(centre.y() - m_cursor.y());

    // THE GOO'S OWN FOOTPRINT, and barely more.
    //
    // This was a radius from the bin's centre, out to three and a half bin
    // heights — most of the screen around the icon. The eyes were
    // therefore tracking almost all the time, and an eye that is always
    // following you never appears to notice you: the interesting moment is
    // the one where it starts. Worse, at that range the pointer is far
    // enough away that every eye turns by only a few degrees, so the
    // tracking is both constant and faint.
    //
    // Bounded by the gel instead. The puddle is the widest part of it, so
    // that sets the sides; five per cent of slack either way keeps the
    // edge off the silhouette, where a hard boundary would be most
    // visible. Vertically it runs from under the spill to the gel's own
    // surface. Inside that box the pointer is close, and close is what
    // makes the eyes swing hard.
    const float halfW = m_shape.poolRadius(m_sim.level()) * 1.05f;
    const float top = m_shape.surfaceY(m_contentLine, m_sim.level());
    const float bottom = m_shape.poolBottom();
    // How far outside the box the pointer is, in each axis, as a share of
    // the fade. Faded rather than switched: an eye that snapped to the
    // pointer the instant it crossed a line would read as a trigger, and
    // the whole illusion is a creature noticing something.
    const float fade = std::max(binH * 0.10f, 1.0f);
    const float outX = std::max(0.0f, std::abs(sx) - halfW);
    const float outY = std::max(0.0f, std::max(bottom - sy, sy - top));
    float raw = 0.0f;
    if (m_cursorOn) {
        const float t = std::clamp(std::hypot(outX, outY) / fade, 0.0f, 1.0f);
        raw = 1.0f - t * t * (3.0f - 2.0f * t);
    }
    // About a fifth of a second to notice, and the same to lose interest.
    // Not instant: a pointer crossing the screen would otherwise snap every
    // eye through ninety degrees in one frame, which reads as a glitch
    // rather than as a head turning.
    m_gazePull += (raw - m_gazePull) * std::min(1.0f, dt * 5.0f);

    // Un-project the pointer, then push it toward the camera.
    //
    // The screen y it arrives as is the scene's y foreshortened by the
    // camera's tilt, so recovering the scene position divides by cos —
    // the same correction the tentacles' mouth needed, and the same one
    // that was missing there for a while and put every arm nine pixels
    // low. Sliding along the view ray afterwards keeps it over the same
    // pixel; see kGazeDepth.
    const float tilt = m_cameraTilt * 3.14159265f / 180.0f;
    const float c = std::max(0.2f, std::cos(tilt));
    const float d = kGazeDepth * binH;
    m_gazeTarget = QVector3D(sx,
                             sy / c + d * std::sin(tilt),
                             d * std::cos(tilt));
    if (Q_UNLIKELY(qEnvironmentVariableIsSet("HYPERBIN_TRACE")))
        qInfo("gaze: cursor(%.0f,%.0f) pull %.2f target(%.0f,%.0f,%.0f) tilt %.0f",
              m_cursor.x(), m_cursor.y(), double(m_gazePull),
              double(m_gazeTarget.x()), double(m_gazeTarget.y()),
              double(m_gazeTarget.z()), double(m_cameraTilt));
}

void OozeEffect::setContentLine(float y01)
{
    if (qFuzzyCompare(m_contentLine, y01))
        return;
    m_contentLine = y01;
    emit shapeChanged();
}

void OozeEffect::setSurface(const QVector<quint8> &coverage, int w, int h)
{
    m_coverage = coverage;
    m_covW = w;
    m_covH = h;
    // No distance field here any more. It existed to trim the mesh's
    // edge against the bin's silhouette, back when the mesh was a height
    // field that overshot it. The mesh is its own shape now — a swept
    // profile — so the field was a 128x128 chamfer pass and a texture
    // upload on every icon change that nothing sampled.
    m_shape.measure(m_coverage, m_covW, m_covH, float(m_binSize.width()),
                    float(m_binSize.height()));
    emit shapeChanged();
}

void OozeEffect::setCameraTilt(float degrees)
{
    if (qFuzzyCompare(m_cameraTilt, degrees))
        return;
    m_cameraTilt = degrees;
    emit shapeChanged();
}

void OozeEffect::step(float dt)
{
    m_sim.step(dt);
    // Before the eyes: where they look is part of where they are.
    updateGaze(dt);
    updateEyes();
    emit frameChanged();

    const bool empty = isEmpty();
    if (empty != m_wasEmpty) {
        m_wasEmpty = empty;
        emit activityChanged();
    }
}

QMargins OozeEffect::margins(qreal iconSize) const
{
    const int x = OozeSim::marginX(iconSize);
    return QMargins(x, OozeSim::marginTop(iconSize),
                    x, OozeSim::marginBottom(iconSize));
}

OozeEffect::EyeSeat OozeEffect::sweepSeat(float r, float y, float drdp,
                                          float dydp, float angle)
{
    // Every surface here is one ring radius and one height, both moving
    // with a single parameter and swept round the axis. Its normal falls
    // straight out of the two tangents:
    //
    //   P(a, p) = (r sin a, y, r D cos a)
    //   dP/da x dP/dp  =  r * (D y' sin a, -r' D, y' cos a)
    //
    // The leading r only scales it, so it drops out in the normalise.
    // Note the D on the x term and NOT on the z: a squashed circle's
    // normal leans toward the flattened axis, which is why an eye on the
    // front of the body faces the camera more squarely than one on the
    // side faces sideways.
    //
    // The body walks this with p = height, so y' is 1 and r' is the
    // profile's slope. The puddle walks it with p along the shoulder,
    // where both move — and there r' is large and negative, which is what
    // turns the normal to face nearly straight up.
    // angle 0 puts the eye on +z, which is the side the camera is on.
    EyeSeat s;
    s.pos = QVector3D(r * std::sin(angle), y,
                      r * std::cos(angle) * OozeShape::kDepth);
    s.nrm = QVector3D(OozeShape::kDepth * dydp * std::sin(angle),
                      -drdp * OozeShape::kDepth,
                      dydp * std::cos(angle))
                .normalized();
    return s;
}

OozeEffect::EyeSeat OozeEffect::bodySeat(float t, float angle) const
{
    // Between the pool's crest and the gel's surface.
    const float lo = m_shape.poolCrest();
    const float hi = m_shape.surfaceY(m_contentLine, m_sim.level());
    const float y = lo + (hi - lo) * qBound(0.0f, t, 1.0f);

    const float e = std::max(0.5f, float(m_binSize.height()) * 0.02f);
    const float dR = (m_shape.radiusAt(y + e) - m_shape.radiusAt(y - e)) / (2.0f * e);
    return sweepSeat(m_shape.radiusAt(y), y, dR, 1.0f, angle);
}

OozeEffect::EyeSeat OozeEffect::poolSeat(float u, float angle) const
{
    // The puddle's top, which is the shoulder OozeGeometry sweeps: out at
    // the rim it is the widest part of the spill, and it climbs and draws
    // back in on a smoothstep until it meets the foot of the body. Walked
    // here with the same two lines the mesh uses, because an eye that sat
    // on a different curve from the one the goo is made of would float
    // above the spill or disappear under it.
    const float poolR = m_shape.poolRadius(m_sim.level());
    const float crest = m_shape.poolCrest();
    const float topY = m_shape.poolTop();
    const auto ring = [&](float p) {
        p = qBound(0.0f, p, 1.0f);
        const float y = crest + (topY - crest) * p;
        const float e = p * p * (3.0f - 2.0f * p);
        return QVector3D(poolR + (m_shape.radiusAt(y) - poolR) * e, y, 0.0f);
    };
    constexpr float kDu = 0.02f;
    const QVector3D at = ring(u);
    const QVector3D lo = ring(u - kDu), hi = ring(u + kDu);
    return sweepSeat(at.x(), at.y(),
                     (hi.x() - lo.x()) / (2.0f * kDu),
                     (hi.y() - lo.y()) / (2.0f * kDu), angle);
}

float OozeEffect::slotY(const EyeSlot &s) const
{
    // Both surfaces are measured up from the pool's crest — bodySeat and
    // poolSeat each start there — so one line answers for either, and the
    // only difference is how far up they reach.
    const float crest = m_shape.poolCrest();
    const float topOf = s.pool ? m_shape.poolTop()
                               : m_shape.surfaceY(m_contentLine, m_sim.level());
    return crest + (topOf - crest) * qBound(0.0f, s.param, 1.0f);
}

void OozeEffect::spreadEyes(EyeSlot *slot, int n) const
{
    if (n < 2)
        return;

    const float binW = float(m_binSize.width());
    // Not merely "not intersecting". Two balls exactly touching still
    // read as one lumpy thing at this size, and the goo needs somewhere
    // to close around each of them — the collar reaches well over an eye's
    // own radius, so leaving them a hair apart puts two meniscus rings on
    // top of each other and neither shows.
    const float margin = binW * 0.045f;
    const float crest = m_shape.poolCrest();

    // Measured in the surface's OWN units, not in the parameters.
    //
    // Angle and height are not comparable: a tenth of a radian is a fifth
    // of the body's width, and a tenth of the height parameter is a
    // fifteenth of its height. Pushing in parameter space moves eyes by
    // wildly different amounts depending on which way they happen to be
    // crowded, which mostly moved them sideways and never resolved a
    // vertical pile. Arc length and height are both in bin pixels and can
    // simply be compared.
    // What each eye's own numbers are, worked out ONCE per pass and not
    // once per pair. They only depend on one eye each, and there are n of
    // them against n^2/2 pairs — computed in the inner loop, the profile
    // lookups alone cost more than everything else in this effect put
    // together, and measurably so: it put half again on the whole app's
    // processor time.
    const float bodySpan = m_shape.surfaceY(m_contentLine, m_sim.level()) - crest;
    const float poolSpan = m_shape.poolTop() - crest;
    float ey[kMaxEyes], ering[kMaxEyes], espan[kMaxEyes];

    for (int pass = 0; pass < 8; ++pass) {
        for (int i = 0; i < n; ++i) {
            espan[i] = std::max(slot[i].pool ? poolSpan : bodySpan, 1.0f);
            ey[i] = crest + espan[i] * qBound(0.0f, slot[i].param, 1.0f);
            ering[i] = m_shape.radiusAt(ey[i]);
        }
        bool moved = false;
        for (int a = 0; a < n; ++a) {
            for (int b = a + 1; b < n; ++b) {
                const float ya = ey[a], yb = ey[b];
                const float rA = ering[a], rB = ering[b];
                // Arc length, discounted by how edge-on it is.
                //
                // Two eyes out on the flank are properly apart in SPACE
                // and still look stacked: the surface there is turned
                // almost side-on, so a step round it barely moves on
                // screen — about a quarter as far as the same step across
                // the front. Spreading them by true arc alone left the
                // silhouette looking like a pile of eyes that measured
                // clean. Counting arc for less where the body turns away
                // buys those pairs a wider berth, and the floor keeps it
                // from running to infinity at the exact edge.
                const float face = 0.5f * (std::cos(slot[a].angle)
                                           + std::cos(slot[b].angle));
                const float ring = 0.5f * (rA + rB)
                                 * (0.40f + 0.60f * std::abs(face));
                const float dx = ring * (slot[b].angle - slot[a].angle);
                const float dy = yb - ya;
                const float dist = std::sqrt(dx * dx + dy * dy);
                const float need = slot[a].radius + slot[b].radius + margin;
                if (dist >= need)
                    continue;

                // Straight on top of each other, which the sequences can
                // do exactly: pick an axis rather than dividing by zero.
                float ux = 1.0f, uy = 0.0f;
                if (dist > 1e-3f) {
                    ux = dx / dist;
                    uy = dy / dist;
                }
                const float push = (need - dist) * 0.5f;
                const float over = push * ux / std::max(ring, 1.0f);
                slot[a].angle -= over;
                slot[b].angle += over;
                slot[a].param -= push * uy / espan[a];
                slot[b].param += push * uy / espan[b];
                moved = true;
            }
        }

        // Back inside their bounds after every pass, not once at the end.
        // Clamping only at the end lets a pair settle in a place the
        // clamp then undoes, putting them back on top of each other with
        // no pass left to notice.
        for (int i = 0; i < n; ++i) {
            // A little past the arc they were dealt, so a crowded front
            // has somewhere to give — but not so far round that an eye
            // ends up behind the bin.
            slot[i].angle = qBound(-2.30f, slot[i].angle, 2.30f);
            if (slot[i].pool) {
                slot[i].param = qBound(0.10f, slot[i].param, 0.80f);
            } else {
                // The same ceiling the placement applies, recomputed:
                // spreading moves eyes round the body, and the ceiling
                // depends on where round it they are.
                const float back = 0.5f * (1.0f - std::cos(slot[i].angle));
                slot[i].param = qBound(0.14f, slot[i].param, 0.90f - 0.34f * back);
            }
        }
        if (!moved)
            break;
    }
}

/// The same hash the scene used when it placed these itself, so an eye
/// keeps the identity it already had: same index, same size, same spot.
static float eyeHash(float a, float b)
{
    const float s = std::sin(a * 127.1f + b * 311.7f) * 43758.5453f;
    return s - std::floor(s);
}

void OozeEffect::updateEyes()
{
    m_eyeSpheres.clear();
    m_eyeNormals.clear();

    // How many are out, and it is NOT proportional to the level.
    //
    // Straight proportion spread the difference evenly and so made
    // neither end of the range feel like anything: a quarter-full bin
    // still had a scatter of them, and a full one was only a few more.
    // Squaring holds the low end nearly clear — a bin with a little in
    // it gets one or two, which reads as something starting — and banks
    // the whole increase into the top, where the point is that it should
    // look infested.
    //
    // fill(), NOT level(). The two are different questions and using the
    // wrong one here looked exactly like the code not working: level() is
    // how DEEP the goo is and is floored at seventy per cent the moment
    // the bin is dirty, so squaring it separated a quarter-full bin from
    // a full one by a factor of two rather than of sixteen, and all four
    // test renders came out with much the same crowd.
    //
    // With a floor of one, though. Squared alone rounds to nothing below
    // about a third full, and a bin with goo in it and no eyes at all is
    // not a quieter version of this effect — it is a different one. The
    // first eye arrives with the first of the sludge; the rest are what
    // the fill buys.
    const float fill = m_sim.fill();
    const int want = int(std::lround(fill * fill * kMaxEyes));
    const int n = m_sim.isEmpty() ? 0 : qBound(1, want, kMaxEyes);
    const float t = m_sim.time();
    const float binW = float(m_binSize.width());

    // Placed first, spread second, seated third. The spreading has to see
    // all of them at once, so it cannot happen inside the loop that makes
    // them.
    EyeSlot slot[kMaxEyes] {};

    for (int i = 0; i < n; ++i) {
        const float fi = float(i);
        const float seedA = eyeHash(fi, 1.0f);
        const float seedB = eyeHash(fi, 2.0f);
        const float seedC = eyeHash(fi, 3.0f);

        // RADIUS, not diameter. The mesh is a unit sphere, so a scale of s
        // spans 2s — sized as a diameter, the first pass came out at twice
        // the intent and the eyes sat on the gel rather than in it.
        const float radius = binW * (0.036f + 0.028f * seedA);

        // WHERE an eye is does not change, and that is the point.
        //
        // These used to walk round the body — an angle with the clock in
        // it — and a ball of gel with eyes revolving through it does not
        // read as anything alive. Nothing suspended in something this
        // thick travels; it sits where it set. What is left is a wobble
        // small enough to be the gel shifting under it rather than the eye
        // going anywhere: about a degree of turn and a hair of rise.
        //
        // Where they go is a LOW-DISCREPANCY set, not a random draw.
        //
        // Random was fine while they travelled: one that started out of
        // sight came round eventually. Now that they stay put, a draw
        // that happens to put six of the nine on the back of the bin puts
        // them there for good, which is exactly what the first run of
        // this looked like.
        //
        // The two coordinates get DIFFERENT constructions, and the reason
        // is that this is nine points, not nine hundred.
        //
        // An R2 sequence went in first, on the reasoning that a
        // two-dimensional low-discrepancy set is the right tool for a
        // two-dimensional placement. It is — asymptotically. At nine
        // points its stride comes back to within a fiftieth of where it
        // started after only four steps, so while the 2D spread is fine
        // its ONE-dimensional projections clump into pairs and triples.
        // An angle that clumps is three eyes stacked in a vertical line
        // down the front of the bin, which is what it drew.
        //
        // A golden-angle stride has no such return and lays nine points
        // out very nearly evenly; the radical inverse in base two does
        // the same for the height and does not track it. Both are chosen
        // for THIS count rather than in the limit.
        // Both sequences are started so that index 0 lands somewhere
        // WORTH looking at, because the order is not arbitrary: the count
        // rises with the bin, so the first indices are the only ones a
        // barely-dirty bin ever shows. Left at their natural starts, eye
        // zero came out on the back-left flank at the very bottom of the
        // range — a bin with a little in it grew exactly one eye and it
        // was behind the goo. The half-turn puts it dead centre front,
        // and stepping the height sequence on by one puts it at mid
        // height rather than down in the puddle.
        const float ua = std::fmod(0.5f + fi * 0.3819660113f, 1.0f);
        float ub = 0.0f;
        for (int bits = i + 1, place = 1; bits; bits >>= 1, ++place)
            ub += (bits & 1) ? std::ldexp(1.0f, -place) : 0.0f;

        // Squeezed toward the camera, and LINEARLY.
        //
        // Spread over the whole circle, two thirds of them sit on a side
        // of the body the camera never sees. Bending the angle by a power
        // was the first attempt at that and it traded one fault for
        // another: a power pulls hardest near zero, so it stacked eyes on
        // the centre line while barely moving the ones round the back. A
        // plain scale keeps whatever spacing it was handed and only
        // narrows the arc — nine eyes evenly spread across the front and
        // both flanks, the outermost pair just past the silhouette where
        // they read as something further down in the goo rather than as
        // nothing at all.
        // The base placement carries NO clock. Everything time-varying —
        // the wobble, how proud it rides, the blink — is added after the
        // spread, so the spread itself is a pure function of the count
        // and the shape and can simply be remembered.
        const float bent = (2.0f * ua - 1.0f) * 3.14159265f * 0.62f;

        // The low third of them go in the PUDDLE, not on the wall.
        //
        // Which third is not a separate draw — it is the bottom of the
        // same height sequence, so the two populations cannot both grow
        // or both empty out. The spill is a nearly horizontal sheet and
        // the body is a nearly vertical one, so an eye in it is looking
        // up out of a flat pool rather than out of a wall; that falls out
        // of the seat's own normal and needs nothing said here.
        constexpr float kPoolShare = 0.22f;
        const bool inPool = ub < kPoolShare;

        // Stopping short of the surface, and how far short depends on
        // which way round the body it is.
        //
        // The camera looks DOWN. An eye on the far side of the body is
        // therefore drawn HIGHER on screen than one at the same height in
        // front of it, by about twice the body's radius times the sine of
        // the tilt — here roughly a third of the whole range. Left with
        // one ceiling for all of them, the ones at the back climbed over
        // the bin's front rim; and because the gel's lid is cut away
        // exactly there, they did not disappear behind the goo, they
        // appeared to float in the bin's open mouth.
        //
        // The front gets the higher ceiling anyway. The gel's collar has
        // run down the OUTSIDE of the bin and sits lower than the liquid
        // within it, so even there the top of the range is not the top of
        // the gel.
        const float back = 0.5f * (1.0f - std::cos(bent));
        const float top = 0.90f - 0.34f * back;
        const float height = 0.16f + (top - 0.16f) * (0.94f * ub + 0.06f * seedB);

        slot[i].angle = bent;
        slot[i].param = inPool ? 0.14f + 0.62f * (ub / kPoolShare) : height;
        slot[i].radius = radius;
        slot[i].pool = inPool;
    }

    // Nothing above guarantees they do not touch, so this does — and it
    // is the same answer every frame until the bin itself moves, so it is
    // worked out then and kept.
    const float key = m_shape.surfaceY(m_contentLine, m_sim.level())
                    + 7.0f * m_shape.poolTop() + 13.0f * binW;
    if (n != m_spreadCount || !qFuzzyCompare(key, m_spreadKey)) {
        spreadEyes(slot, n);
        std::copy(slot, slot + n, m_spread);
        m_spreadCount = n;
        m_spreadKey = key;
    } else {
        std::copy(m_spread, m_spread + n, slot);
    }

    for (int i = 0; i < n; ++i) {
        const float fi = float(i);
        const float seedA = eyeHash(fi, 1.0f);
        const float seedB = eyeHash(fi, 2.0f);
        const float seedC = eyeHash(fi, 3.0f);
        const float radius = slot[i].radius;

        // The wobble goes on HERE, after the spread. About a degree of
        // turn and a hair of rise — an order of magnitude under the gap
        // the spread leaves, so it can shift an eye without ever walking
        // one back into its neighbour.
        const float angle = slot[i].angle + 0.020f * std::sin(t * 0.21f + fi * 1.7f);
        const float param = slot[i].param
                          + (slot[i].pool ? 0.0f
                                          : 0.008f * std::sin(t * 0.27f + fi * 2.3f));
        const EyeSeat seat = slot[i].pool ? poolSeat(param, angle)
                                          : bodySeat(param, angle);

        // The whole of the motion, and it runs along the normal: an eye
        // rises out of the gel and settles back into it.
        //
        // Measured in the EYE's own radius rather than the body's, which
        // is what lets one number serve a wall and a puddle at once — and
        // the eyes vary in size by nearly two to one, so a distance fixed
        // against the bin sat the small ones deeper than the large ones
        // for no reason anyone could see.
        //
        // The useful range is narrow and not obvious. At 0 the ball's
        // centre is ON the surface and half of it stands clear, which is
        // already enough to expose the pupil; the gel's own lumps swing
        // by more than this does, so the low end of the range is where
        // they close over it. Much past 0.6 and it is a bead resting on
        // top. Each eye breathes at its own rate so they never pulse
        // together.
        const float proud = radius * (0.30f
            + 0.32f * std::sin(t * (0.28f + 0.14f * seedA) + seedC * 6.2831853f));

        // Blinking, and only how MUCH — the lids themselves are drawn on
        // the eyeball, in shaders/eye3d.frag, because the gel is 64
        // segments round and one of them is as wide as a whole eye.
        //
        // Not a symmetric fade. A real lid slams shut in about a tenth of
        // a second, rests there, and opens back at half the speed, and
        // that asymmetry is most of what makes a blink read as a blink
        // rather than as something dissolving. Each eye keeps its own
        // period and phase, so nine of them never go together.
        constexpr float kShut = 0.11f, kHold = 0.05f, kOpen = 0.21f;
        const float period = 3.2f + 4.6f * seedB;
        const float phase = std::fmod(t + seedC * period, period);
        float open = 1.0f;
        if (phase < kShut) {
            open = 1.0f - phase / kShut;
        } else if (phase < kShut + kHold) {
            open = 0.0f;
        } else if (phase < kShut + kHold + kOpen) {
            const float o = (phase - kShut - kHold) / kOpen;
            open = o * o * (3.0f - 2.0f * o);
        }

        m_eyeSpheres.append(
            QVariant::fromValue(QVector4D(seat.pos + seat.nrm * proud, radius)));
        m_eyeNormals.append(QVariant::fromValue(QVector4D(seat.nrm, open)));
    }
}

} // namespace hyperbin
