#include "TentacleEffect.h"

#include "IconTexture.h"
#include "SpineTexture.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace hyperbin {

TentacleEffect::TentacleEffect(QObject *parent)
    : Effect(parent)
    , m_iconTexture(new IconTexture)
    , m_spine(new SpineTexture)
{
}

TentacleEffect::~TentacleEffect()
{
    delete m_iconTexture;
    delete m_spine;
}

QUrl TentacleEffect::visualSource() const
{
    return QUrl(QStringLiteral("qrc:/qt/qml/hyperbin/qml/TentacleVisual.qml"));
}

QObject *TentacleEffect::iconTexture() const
{
    return m_iconTexture;
}

QObject *TentacleEffect::spineTexture() const
{
    return m_spine;
}

void TentacleEffect::setBinImage(const QImage &img)
{
    m_iconTexture->setImage(img);
    emit shapeChanged();
}

void TentacleEffect::setBinRect(const QRectF &binRect)
{
    if (m_binRect == binRect)
        return;
    m_binRect = binRect;
    m_binSize = binRect.size();
    emit shapeChanged();
}

void TentacleEffect::setSurface(const QVector<quint8> &, int, int)
{
    // Nothing here reads the silhouette. What this effect needs to know
    // about the bin's shape is where its OPENING is, and the coverage
    // grid cannot answer that — the near lip runs through the middle of
    // the silhouette, not around its edge. See setMouth.
}

void TentacleEffect::setContentLine(float)
{
    // Where the rubbish starts does not matter to something that comes
    // out from under it.
}

void TentacleEffect::setMouth(const BinMouth &mouth)
{
    m_mouth = mouth;
    emit shapeChanged();
}

void TentacleEffect::setFullness(float fullness)
{
    m_target = std::clamp(fullness, 0.0f, 1.0f);
}

void TentacleEffect::setCursor(const QPointF &pos, bool present)
{
    m_cursor = pos;
    m_cursorOn = present;
}

void TentacleEffect::step(float dt)
{
    m_time += dt;

    // Critically damped, so it eases at both ends and never overshoots —
    // the same spring OozeSim uses, and for the same reason. An
    // exponential follow was here first: it leaves at full speed and
    // creeps in, which on something the size of a Dock icon reads as a
    // bar filling rather than as something climbing out. A spring starts
    // at nothing, gathers pace and settles.
    //
    // Faster to withdraw than to emerge. Coming out is deliberate;
    // going back in is a flinch.
    //
    // Velocity first, then position — semi-implicit, which stays stable
    // at the frame intervals this runs at where the explicit form would
    // not.
    // The withdrawal's own clock. Starts when the bin is asked to empty
    // and something is still out; cleared the moment it refills, so a bin
    // that fills again mid-retreat simply carries on rather than finishing
    // a flourish nobody asked for.
    if (m_target <= 0.0f && m_level > 0.005f) {
        if (m_retreat < 0.0f)
            m_retreat = 0.0f;
        m_retreat += dt;
    } else if (m_target > 0.0f) {
        m_retreat = -1.0f;
    }
    constexpr float kEmergeEase = 2.6f;
    constexpr float kWithdrawEase = 4.4f;
    const float omega = m_target > m_level ? kEmergeEase : kWithdrawEase;
    // Frozen for as long as the arms are still making a show of leaving.
    // Letting the spring run underneath would drop the arm count out from
    // under the flourish and they would vanish one at a time mid-thrash.
    const bool holding = m_retreat >= 0.0f && m_retreat < kRetreatHold;
    if (m_retreat >= kRetreatAll) {
        // The flourish is over and the arms are under the rubbish, so the
        // level has no more work to do. Snapped rather than left to the
        // spring: the spring took another 0.8s to crawl the last of the
        // way, during which nothing was visible and isEmpty() was still
        // false, so the whole exit measured 1.9s for 1.1s of animation and
        // the teardown's own backstop fired before it finished.
        m_level = 0.0f;
        m_vel = 0.0f;
    } else if (!holding) {
        m_vel += (omega * omega * (m_target - m_level) - 2.0f * omega * m_vel) * dt;
        m_level += m_vel * dt;
    }
    // A spring approaches asymptotically and would keep the clock
    // running forever chasing the last thousandth. Close and slow is
    // arrived — and it has to actually arrive, because reaching zero is
    // what lets isEmpty() go true and the overlay be torn down.
    if (std::abs(m_target - m_level) < 0.002f && std::abs(m_vel) < 0.02f) {
        m_level = m_target;
        m_vel = 0.0f;
    }
    m_level = std::clamp(m_level, 0.0f, 1.0f);

    m_dt = dt;
    updateArms();
    emit frameChanged();

    const bool empty = isEmpty();
    if (empty != m_wasEmpty) {
        m_wasEmpty = empty;
        emit activityChanged();
    }
}

/// Same hash the rest of the app uses, so an arm keeps its identity.
static float armHash(float a, float b)
{
    const float s = std::sin(a * 127.1f + b * 311.7f) * 43758.5453f;
    return s - std::floor(s);
}

/// Ease in and out, 0..1.
static float smooth(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

QVector3D TentacleEffect::cursorScene() const
{
    return QVector3D(float(m_cursor.x() - m_binRect.center().x()),
                     float(m_binRect.center().y() - m_cursor.y()),
                     0.0f);
}

void TentacleEffect::updatePull(float dt)
{
    // Faded out with distance because the host polls the pointer every
    // frame and always reports it present — the overlay is click-through
    // and gets no enter or leave events — so "present" says nothing at all
    // about whether the pointer is anywhere near the bin. Distance is the
    // only real signal there is.
    const float binH = float(m_binSize.height());
    const QVector3D cursor = cursorScene();
    const float near0 = binH * 0.9f, far0 = binH * 2.4f;
    for (int i = 0; i < kMaxTentacles; ++i) {
        const float away = (cursor - armBase(i)).length();
        const float raw = m_cursorOn
            ? 1.0f - smooth((away - near0) / std::max(far0 - near0, 1.0f))
            : 0.0f;
        // About a sixth of a second to notice, and the same to forget.
        m_pull[i] += (raw - m_pull[i]) * std::min(1.0f, dt * 6.0f);
    }
}

QVector3D TentacleEffect::armEmerge(int i) const
{
    const Seat &seat = kSeats[i];
    return QVector3D(std::sin(seat.strike) * 0.22f, 1.0f,
                     std::cos(seat.strike) * 0.10f).normalized();
}
QVector3D TentacleEffect::armBase(int i) const
{
    const Seat &seat = kSeats[i];
    const float binH = float(m_binSize.height());
    const float sa = armHash(float(i), 5.0f);
    // Slid along the arm's OWN axis, not sideways. Two rates that do not
    // divide into each other, so an arm never returns to the same
    // extension twice; the slower one is the breath and the quicker one is
    // what makes it read as slithering rather than rising.
    const float slide = 0.60f * std::sin(m_time * 0.52f + sa * 6.28318531f)
                      + 0.40f * std::sin(m_time * 1.27f + sa * 3.11f);
    const QVector3D root(mouthX() + seat.lateral * mouthRadius(),
                         rootY() - seat.sink * binH,
                         seat.depth * mouthReachZ());
    return root + armEmerge(i) * (slide * kRootSlide * binH);
}
float TentacleEffect::moveProgress(int i) const
{
    return std::clamp(m_state[i].t / std::max(m_state[i].duration, 0.01f), 0.0f, 1.0f);
}
void TentacleEffect::advanceMove(int i, float dt)
{
    ArmState &st = m_state[i];
    st.t += dt;
    if (st.t < st.duration)
        return;
    // Pick the next one. Weighted by hand rather than uniformly: Idle is
    // the resting state and has to be common or the bin looks frantic, and
    // an arm that has just done something showy should settle before doing
    // another. Never the same move twice running, which is the cheapest
    // way to stop a coincidence of timing reading as a pattern.
    const Move was = st.move;
    st.t = 0.0f;
    st.seed = armHash(float(i), m_time * 0.37f + 11.0f);
    // Chosen here, once, and then held for the whole move. See ArmState::aim.
    st.aim = kSeats[i].strike
           + kSeats[i].swing * (armHash(float(i) + 0.25f, m_time * 0.91f + 7.0f)
                                * 2.0f - 1.0f);
    // HYPERBIN_FORCE_MOVE=<n> pins every arm to one move, in the order the
    // enum declares them. Without it a move cannot be verified at all: the
    // vocabulary is picked by a hash, so catching a particular one in a
    // grab-and-exit run is luck, and "it looked right when I happened to
    // see one" is precisely how the fruit rollup shipped invisible for two
    // rounds — it was being computed and then erased every frame, and every
    // screenshot that would have shown it was of some other move.
    //
    // ALTERNATED WITH IDLE, which is not a detail. Run back to back a move
    // never starts from rest — it starts from wherever the last instance of
    // itself left the arm — so the opening of the move cannot be observed
    // at all. That is not hypothetical: it read the slap's wind-up as
    // moving the arm OUTWARD, the exact opposite of what it does, because
    // what the frames actually showed was the previous slap still
    // recovering.
    if (Q_UNLIKELY(qEnvironmentVariableIsSet("HYPERBIN_FORCE_MOVE"))) {
        const Move want = Move(qEnvironmentVariableIntValue("HYPERBIN_FORCE_MOVE"));
        st.move = was == want ? Move::Idle : want;
        st.duration = st.move == Move::Idle ? 2.0f : 2.6f;
        return;
    }
    const float r = armHash(float(i) + 0.5f, m_time * 0.61f + 3.0f);
    // Gated on the pointer being NEAR, not on it existing.
    //
    // This read `m_cursorOn`, which is the host's "present" flag and is
    // unconditionally true — so better than half of every arm's move picks
    // were a lunge at the pointer whatever the pointer was doing, including
    // sitting on the far side of the screen. Reach's own ramp is not
    // distance-faded either, so those lunges went to full extension. That
    // is a large part of what read as the poses fighting each other.
    const float pull = m_pull[i];
    if (pull > 0.25f && r < 0.55f) {
        st.move = Move::Reach;
        st.duration = 1.6f + 1.4f * st.seed;
    } else if (was != Move::Idle && r < 0.45f) {
        st.move = Move::Idle;
        st.duration = 1.8f + 2.2f * st.seed;
    } else if (r < 0.62f) {
        st.move = Move::Slap;
        // Long enough to have parts. See the envelope in armTarget: a
        // wind-up, a strike, a stretch of crawling along the bin and a slow
        // recovery do not fit in the 1.15s this used to run in — at that
        // length the whole thing was a twitch.
        st.duration = 2.6f + 0.9f * st.seed;
    } else if (r < 0.78f) {
        st.move = Move::Coil;
        st.duration = 2.4f + 1.2f * st.seed;
    } else if (r < 0.79f) {
        st.move = Move::Wrap;
        st.duration = 3.0f + 1.5f * st.seed;
    } else if (r < 0.93f) {
        st.move = Move::Roll;
        st.duration = 3.2f + 1.4f * st.seed;
    } else {
        st.move = Move::Idle;
        st.duration = 1.8f + 2.2f * st.seed;
    }
    if (st.move == was && st.move != Move::Idle) {
        st.move = Move::Idle;
        st.duration = 1.6f;
    }
}
QVector3D TentacleEffect::armTarget(int i, float &flex, float &maxBend) const
{
    const Seat &seat = kSeats[i];
    const ArmState &st = m_state[i];
    const float sa = armHash(float(i), 1.0f);
    const float sb = armHash(float(i), 2.0f);
    const QVector3D base = armBase(i);
    const float L = armLength() * seat.size;
    const float binH = float(m_binSize.height());
    const float u = moveProgress(i);
    flex = 1.0f;
    maxBend = TentacleChain::kMaxBend;
    // Which way it lashes. Wandering, so a move is not always along the
    // same line; the back arm's wander is wide enough to take it over
    // either side of the bin -- see kSeats.
    const float dir = seat.strike
                    + seat.swing * std::sin(m_time * 0.37f + sb * 6.28f);
    // FLATTENED IN Z, and deliberately. The camera is orthographic and
    // barely tilted, so movement toward it is nearly invisible on screen
    // while still being movement -- an arm leaning out along its own radius
    // spent most of its travel coming at the viewer, where it read as
    // barely moving and yet swung far enough forward to be legitimately in
    // FRONT of the bin, which broke the occlusion for a gesture nobody
    // could see. Biasing the reach into the screen plane costs nothing
    // that shows and keeps the arms where the mask expects them.
    const QVector3D out(std::sin(dir), 0.0f, std::cos(dir) * 0.30f);
    // Near FULL STRETCH, not comfortably inside it. The first attempt put
    // this at two thirds of the arm's reach on the reasoning that slack is
    // what makes a chain hang in a curve. It is not: FABRIK has no notion
    // of an arm being a physical object, so slack is simply folded away,
    // and three arms came out as crumpled self-intersecting stubs. The
    // curve comes from the bend limit and the wave; the target's job is to
    // keep the arm extended enough to have a shape at all.
    const QVector3D idle = base
        + QVector3D(0.0f, 1.0f, 0.0f) * (L * (0.80f + 0.07f * std::sin(m_time * 0.43f + sa * 6.28f)))
        // Sideways reach scaled to the BIN, not to the arm. Scaled to the
        // arm it was 290 units on a bin only 160 deep, so an idle wave
        // carried the tip clear past the bin's own footprint.
        // Modest, because clamping the TARGET is not enough on its own:
        // the chain bows, so a tip pulled back to the rim still leaves the
        // arm's middle swinging well outside it. Measured with the target
        // clamped to 1.18 opening-radii, arms still overhung the artwork
        // by up to 219px on a 444px bin. The reach has to be smaller at
        // source, not corrected afterwards.
        + out * (mouthRadius() * seat.lean
                 * (0.36f + 0.16f * std::sin(m_time * 0.31f + sb * 6.28f)));
    // Where the pointer is, in scene units, and how much to care.
    //
    // CONTINUOUS, and that is the fix rather than a tuning: reaching used
    // to be a move, so an arm only noticed the pointer when its previous
    // move happened to end AND a dice roll came up, which is an occasional
    // lunge and reads as not tracking at all. Every arm now leans a little
    // toward a nearby pointer whatever else it is doing, and the Reach
    // move is the stronger version on top.
    //
    // How much it cares is measured once a frame and low-passed — see
    // updatePull, which also records why "present" is not the question.
    const QVector3D cursor = cursorScene();
    const float pull = m_pull[i];
    // Clamped to the arm's own reach, or an arm asked for something across
    // the screen simply points at it and stops looking like an arm.
    QVector3D toward = cursor;
    const QVector3D rel = cursor - base;
    if (rel.length() > L * 0.95f)
        toward = base + rel.normalized() * (L * 0.95f);
    // The move poses the arm from its plain drift. The cursor is applied
    // AFTERWARDS — see the blend at the bottom — because it has to be able
    // to override a move rather than be overridden by one.
    //
    // It used to be folded in here, into a `leaned` pose that every move
    // then blended away FROM: a slap ramping to full took the arm from the
    // pointer back to the bin, so hovering while anything was mid-move
    // looked like the two were fighting. They were, and the move won.
    const QVector3D leaned = idle;
    QVector3D posed = leaned;
    switch (st.move) {
    case Move::Idle:
        posed = leaned; break;
    case Move::Slap: {
        // FOUR PARTS: wind up, strike, crawl, let go. The earlier version
        // had two — out over a sixth of the move and back over a third —
        // which is a twitch, and the note it replaced complained about the
        // opposite problem, an arm that looked STUCK to the bin. Both are
        // the same mistake read from either end: contact with no travel in
        // it is either too long or too short and there is no length that
        // makes it good. Contact that MOVES has somewhere to spend the
        // time.
        //
        //   0.00 wind   the arm draws back off the bin before it hits
        //   0.14 strike fast, and the only fast part
        //   0.24 crawl  pinned to the wall, dragging down and around it
        //   0.66 lift   slowly off, no rebound
        constexpr float kWind = 0.14f, kStrike = 0.24f, kCrawl = 0.56f;
        // How far along the wall the arm has dragged, 0..1. Wanted below
        // as well as here, so it is computed whatever phase we are in.
        const float crawl =
            std::clamp((u - kStrike) / (kCrawl - kStrike), 0.0f, 1.0f);
        float hit;
        if (u < kWind) {
            // ANTICIPATION. Negative, so the arm goes PAST its resting pose
            // in the direction away from the wall — the pose is a blend
            // from rest toward the wall, so a negative coefficient is a
            // reach backwards along the same line and costs nothing to
            // express. Nothing in animation reads as intent without it:
            // a strike that begins at the moment of the strike looks like
            // the arm was pushed, not like it decided.
            hit = -0.26f * smooth(u / kWind);
        } else if (u < kStrike) {
            const float r = (u - kWind) / (kStrike - kWind);
            hit = -0.26f + 1.26f * smooth(r);
        } else if (u < kCrawl) {
            hit = 1.0f;
        } else {
            // Off slowly, and with no recoil. The rebound that used to be
            // here is the right ending for a hit-and-away, and the wrong
            // one for a hit that has been resting on the bin for the best
            // part of a second: something that has settled onto a surface
            // peels off it.
            //
            // SQUARED, not smoothstep, and the difference is the whole
            // ending. Smoothstep is slow-fast-slow, so it puts its speed in
            // the middle of the release — and the chain's own lag then
            // carries that peak later still, which measured as the arm
            // hanging on the bin for most of the recovery and then hurrying
            // up in the last tenth. Squared is fast-then-slow: the arm
            // unsticks promptly and drifts the rest of the way, and the lag
            // now has slack behind it to be absorbed into instead of a
            // deadline to miss.
            const float r = (u - kCrawl) / (1.0f - kCrawl);
            hit = (1.0f - r) * (1.0f - r);
        }
        // Rigid through the strike, then loosened again while it crawls.
        // This is what the slither is actually made of — a wave running
        // down an arm that is pinned at the far end has to travel as a
        // ripple ALONG the surface, because the ends cannot move. Held
        // rigid the whole time the arm was a bar leaning on the bin.
        flex = 1.0f - 0.75f * std::max(hit, 0.0f)
             + 0.60f * smooth(std::min(crawl * 2.2f, 1.0f))
                     * (1.0f - smooth(std::max((u - kCrawl) / 0.2f, 0.0f)));
        // WELL BELOW the contact point, not on it. Aimed AT the wall the
        // arm arrives pointing at it and pokes; aimed at a place further
        // down, the last joints have to run along the wall to get there
        // and the arm lands on its flat instead of its tip. That is the
        // whole difference between prodding the bin and hitting it, and it
        // is a property of where the target is, not of the solver.
        //
        // ON the wall at the depth it actually lands, not at a fixed
        // radius. The bin narrows going down, so a constant lateral target
        // put the tip outside a wall that was no longer there, and the
        // mask -- which does know about the taper -- correctly drew it in
        // front of the bin. Measured, 9,132 leaked pixels on the deepest
        // slap. Both sides now read the same profile.
        //
        // And it TRAVELS. The contact point starts high on the wall where
        // the strike lands and works its way down and across while the arm
        // holds on. Because binHalfWidthAt is re-read at the new height,
        // the tip stays on the taper as it descends instead of drifting
        // off a wall that is narrowing under it.
        //
        // Aimed with st.aim, FROZEN when the move began, rather than with
        // the wandering `out` the resting pose uses — see ArmState::aim for
        // the teleport that costs.
        const QVector3D aim(std::sin(st.aim), 0.0f, std::cos(st.aim) * 0.30f);
        const float wallY = rootY() - binH * (0.44f + 0.26f * crawl);
        const float sway = std::sin(crawl * 2.9f + sb * 6.28f);
        const QVector3D wall(
            std::copysign(binHalfWidthAt(wallY) * (0.98f - 0.06f * crawl),
                          aim.x()),
            wallY,
            (aim.z() + 0.42f * sway) * mouthReachZ() * 0.55f);
        posed = leaned + (wall - leaned) * hit;
        break;
    }
    case Move::Coil: {
        // Curled right up, tip brought back near its own root. Needs the
        // bend limit lifted: sixteen joints at the default cannot close a
        // circle, so without this the arm merely hooks.
        const float c = smooth(std::min(u * 2.0f, 1.0f))
                      * (1.0f - smooth(std::max((u - 0.72f) / 0.28f, 0.0f)));
        maxBend = TentacleChain::kMaxBend
                + (TentacleChain::kCoilBend - TentacleChain::kMaxBend) * c;
        flex = 1.0f - 0.55f * c;
        const QVector3D knot = base
            + QVector3D(0.0f, 1.0f, 0.0f) * (L * 0.16f)
            + out * (mouthRadius() * 0.35f);
        posed = leaned + (knot - leaned) * c;
        break;
    }
    case Move::Wrap: {
        // The tip travels ROUND the bin rather than to a point on it, so
        // the arm lies along the rim instead of across it. A target that
        // moves is the only way to get a path out of a solver that only
        // knows about destinations.
        const float turn = (sa * 6.28318531f) + u * 3.4f;
        const float ring = smooth(std::min(u * 3.0f, 1.0f))
                         * (1.0f - smooth(std::max((u - 0.78f) / 0.22f, 0.0f)));
        flex = 1.0f - 0.45f * ring;
        const float ringY = rootY() - binH * (0.10f + 0.16f * ring);
        const QVector3D around(std::sin(turn) * binHalfWidthAt(ringY) * 0.98f,
                               ringY,
                               std::cos(turn) * mouthReachZ() * 0.85f);
        posed = leaned + (around - leaned) * ring;
        break;
    }
    case Move::Roll: {
        // The shape comes from curlUp, applied after the solve. What the
        // target has to do is AGREE with it.
        //
        // It used to be the plain resting pose, which asks for an arm at
        // 0.80 of full stretch — so every frame the solver straightened the
        // arm out to reach up there and curlUp then rewrote it back into a
        // spiral, and the two took turns. That fight is most of what the
        // roll's jitter was; the axis flip (see TentacleChain::curlUp) was
        // the rest. A rolled-up arm's tip is near its own base, so the
        // target is drawn in as the roll tightens and the solver arrives at
        // roughly the pose curlUp is about to impose.
        //
        // The bend limit HAS to come up with it, and this is why the roll
        // was invisible: curlUp lays a spiral turning up to 0.75 radians a
        // joint, and the settle pass that follows re-applies the limit --
        // which was still the default 0.384 -- and quietly straightened
        // the whole thing back out every frame. The pose was being
        // computed and then erased.
        flex = 0.45f;
        // Tight enough to actually be a roll. At 0.85 the arm managed
        // about 215 degrees -- a hook, not a spiral. A curl only reads as
        // rolled up once it passes a full turn, and the limit on how tight
        // that can get is geometric: the last N segments closing a circle
        // need 2pi/N per joint, so five joints is 72 degrees each and
        // anything past that has segments doubling back through their own
        // neighbours.
        maxBend = 1.32f;
        {
            const float c = rollAmount(i);
            const QVector3D gathered = base
                + QVector3D(0.0f, 1.0f, 0.0f) * (L * 0.44f)
                + out * (mouthRadius() * 0.22f);
            posed = leaned + (gathered - leaned) * c;
        }
        break;
    }
    case Move::Reach: {
        // The full lunge, on top of the lean every arm already has. The
        // pointer arrives in the host item's pixels with +y DOWN and the
        // scene is bin-local with +y UP, which is the same conversion the
        // mouth measurements get.
        //
        // Faded by `pull` like everything else that chases the pointer. It
        // was not, and the lunge therefore went to full extension at a
        // target clamped to the arm's reach in the pointer's direction —
        // which for a pointer on the far side of the screen is simply the
        // arm pointing off at nothing. An arm mid-Reach when the pointer
        // walks away now relaxes instead of holding the point.
        const float ring = smooth(std::min(u * 2.5f, 1.0f))
                         * (1.0f - smooth(std::max((u - 0.80f) / 0.20f, 0.0f)));
        posed = leaned + (toward - leaned) * (ring * pull);
        break;
    }
    }
    // The pointer has the last word.
    //
    // Applied over whatever the move decided, at nearly full authority, so
    // a hovering pointer visibly commands the arms instead of negotiating
    // with them. Not 1.0: leaving a little of the pose in stops the arms
    // converging into one bundle at the cursor, and lets a strike still
    // read as a strike while it happens to be aimed nearby.
    constexpr float kCursorAuthority = 0.88f;
    return posed + (toward - posed) * (pull * kCursorAuthority);
}
QVector3D TentacleEffect::retreatTarget(int i, float &flex, float &sink) const
{
    const float L = armLength() * kSeats[i].size;
    const float binH = float(m_binSize.height());
    const QVector3D base = armBase(i);
    const float t = m_retreat;
    // Up. Straight up, at full stretch, whatever it was doing -- the arm
    // gathers itself before it goes.
    const float rise = smooth(std::min(t / kRetreatRise, 1.0f));
    // ...and thrashes there. The wave is what sells it, so this drives the
    // wave hard rather than moving the target about: a target that shakes
    // just drags the whole arm, where a fast wave whips its length.
    const float thrash = t < kRetreatRise ? 0.0f
                       : smooth(std::min((t - kRetreatRise) / 0.15f, 1.0f))
                         * (1.0f - smooth(std::max((t - kRetreatHold) / 0.25f, 0.0f)));
    flex = 1.0f + 5.5f * thrash;
    // Then down, past the root, out of sight. Eased in so it drops rather
    // than starting at speed.
    sink = t <= kRetreatHold ? 0.0f
         : smooth(std::clamp((t - kRetreatHold) / kRetreatSink, 0.0f, 1.0f));
    const float wobble = 0.10f * std::sin(m_time * 9.0f + float(i) * 2.1f) * thrash;
    const QVector3D up = base + QVector3D(wobble * L, L * (0.94f * rise), 0.0f);
    // Going in, the arm COILS rather than translating. Dropping the whole
    // chain by its own length does put it out of sight, but out of sight
    // below the BIN — the mask only covers the artwork's own rectangle, so
    // an arm pushed through the floor reappears underneath it. Measured,
    // 26,766 pixels of arm hanging below the bin. Pulling the tip back to
    // its own root instead gathers the arm into a knot that fits inside
    // the rubbish, where the mask already hides it.
    return up + (base - up) * sink;
}
QVector3D TentacleEffect::keepNear(const QVector3D &t) const
{
    // How far sideways an arm may reach, measured from the bin's AXIS
    // rather than from the arm's own root. The idle reach was a radius
    // added to a root that already sat 0.62 of the way out, so the outer
    // pair could finish nearly two opening-radii from the centre -- well
    // clear of the artwork, waving at nothing. Clamped, not scaled, so the
    // arms nearest the middle still get their full travel.
    const float limit = mouthRadius() * 0.82f;
    const float r = std::hypot(t.x() - mouthX(), t.z());
    if (r <= limit || r < 1e-4f)
        return t;
    const float k = limit / r;
    return QVector3D(mouthX() + (t.x() - mouthX()) * k, t.y(), t.z() * k);
}
QVector3D TentacleEffect::keepInFront(const QVector3D &t) const
{
    // Below the rim there is no "inside the bin" for an arm to reach: it
    // came out of the opening, so anything lower than the lip has to be
    // in FRONT of the body. Without this, a reach downward aimed straight
    // down from the mouth put the tip at the bin's own depth, the mask
    // correctly decided the front wall was in the way, and the arm
    // vanished into the bin exactly when it was most visible.
    //
    // Faded in over the lip rather than switched at it, so an arm crossing
    // the rim eases forward instead of jumping.
    const float binH = float(m_binSize.height());
    // Faded over a THIRD of the bin, not a tenth. A slap drives its
    // target down about thirty units a frame, so a ramp this short
    // finished in two frames and the target leapt 150 units forward in z
    // while it did — measured, a 91-unit jump every slap, and the arm
    // lurched over 100 units chasing it. The ramp has to be long compared
    // to how fast a target crosses it, not compared to the bin.
    const float under = std::clamp((mouthY() - t.y()) / (binH * 0.35f), 0.0f, 1.0f);
    const float front = mouthReachZ() * 1.10f;
    return QVector3D(t.x(), t.y(), std::max(t.z(), front * under));
}
float TentacleEffect::binHalfWidthAt(float y) const
{
    const float binH = float(m_binSize.height());
    const float below = std::clamp((mouthY() - y) / std::max(binH, 1e-4f), 0.0f, 1.0f);
    return mouthRadius() * (kBodyTop + (kBodyFoot - kBodyTop) * below);
}
float TentacleEffect::rollAmount(int i) const
{
    if (m_state[i].move != Move::Roll)
        return 0.0f;
    // In over the first third, held, out over the last quarter, so it
    // rolls up, sits curled a moment, and unrolls.
    const float u = moveProgress(i);
    const float c = smooth(std::min(u / 0.34f, 1.0f))
                  * (1.0f - smooth(std::max((u - 0.76f) / 0.24f, 0.0f)));
    // UNROLLED BY THE POINTER, and this is the reconciliation rather than a
    // priority.
    //
    // Following the pointer and being rolled into a spiral are not two
    // strengths of the same thing that can be blended: one is a statement
    // about where the tip goes, the other about the curvature of every
    // joint, and applying both at once is the solver and curlUp overwriting
    // each other frame by frame. That is what the jitter was.
    //
    // So the roll yields. The pointer keeps precedence — the arm visibly
    // uncurls and comes to it, which is a better answer than either winning
    // outright, because the unrolling IS the reaction.
    return c * (1.0f - m_pull[i]);
}
float TentacleEffect::mouthReachZ() const
{
    const float s = std::sin(m_cameraTilt * 3.14159265f / 180.0f);
    return mouthDepth() / std::max(0.05f, s);
}
void TentacleEffect::setCameraTilt(float degrees)
{
    if (qFuzzyCompare(m_cameraTilt, degrees))
        return;
    m_cameraTilt = degrees;
    emit shapeChanged();
}
void TentacleEffect::updateArms()
{
    const int n = count();
    // Before anything reads it: the moves are chosen from it and the roll
    // is faded by it.
    updatePull(m_dt);
    for (int i = 0; i < n; ++i) {
        advanceMove(i, m_dt);
        const QVector3D base = armBase(i);
        const float want = armLength() * kSeats[i].size;
        // Rebuilt only when the arm's length actually changes -- which is
        // when the bin resizes, and never per frame. Resetting a chain
        // throws away the state that makes it lag, and a chain with no lag
        // is the arc again.
        if (!m_chain[i].valid() || std::abs(m_chain[i].length() - want) > 0.5f)
            m_chain[i].reset(base, QVector3D(0, 1, 0), want);
        float flex = 1.0f;
        float maxBend = TentacleChain::kMaxBend;
        QVector3D target;
        float sink = 0.0f;
        if (m_retreat >= 0.0f) {
            // Reined in on the way out too. An arm caught mid-wrap was
            // out beyond the bin when the exit began and stayed there
            // while it sank, so the last thing on screen was a tentacle
            // hanging outside the bin rather than going into it.
            target = keepNear(retreatTarget(i, flex, sink));
        } else {
            target = keepInFront(keepNear(armTarget(i, flex, maxBend)));
        }
        // Leaves the hole going UP and a little outward, whatever it is
        // reaching for. See TentacleChain::kRootHeld.
        const QVector3D emerge = armEmerge(i);
        // Drawn down into the rubbish on the way out, by a fraction of
        // the bin rather than by the arm's length -- far enough to be
        // under the heap, never far enough to come out of the bottom.
        const QVector3D sunk =
            base - emerge * (sink * float(m_binSize.height()) * 0.30f);
        target -= emerge * (sink * float(m_binSize.height()) * 0.30f);
        // Allowed to fold up tightly while it does, or a straight arm
        // simply pivots instead of gathering itself in.
        if (sink > 0.0f)
            maxBend = TentacleChain::kMaxBend
                    + (TentacleChain::kCoilBend - TentacleChain::kMaxBend) * sink;
        // HYPERBIN_DEBUG: shout when a target teleports. A move change is
        // meant to be continuous -- every move begins and ends at the
        // resting pose precisely so the arm is never asked to be somewhere
        // else in one frame.
        if (Q_UNLIKELY(qEnvironmentVariableIsSet("HYPERBIN_DEBUG"))) {
            const float jump = (target - m_lastTarget[i]).length();
            if (jump > float(m_binSize.height()) * 0.25f && m_lastTarget[i] != QVector3D())
                qInfo("tentacle %d: TARGET jumped %.0f (move %d)",
                      i, double(jump), int(m_state[i].move));
            m_lastTarget[i] = target;
        }
        m_chain[i].solve(sunk, emerge, target, m_time,
                         armHash(float(i), 3.0f) * 6.28318531f, flex, maxBend);
        m_chain[i].curlUp(sunk, emerge, 1.30f, rollAmount(i));
        // Out of the bin, then lengths fixed again -- pushing joints
        // sideways stretches the segments that reach them.
        m_chain[i].pushOutside(mouthY(), float(m_binSize.height()),
                               mouthRadius(), mouthReachZ(), kBodyFoot / kBodyTop,
                               TentacleChain::kRootHeld + 1);
        m_chain[i].settle(sunk, emerge, maxBend);
    }
    // ...and shout when the ARM itself lurches, which is the symptom
    // rather than its cause: a target that teleports is one thing, a chain
    // that thrashes while its target sits still is quite another.
    if (Q_UNLIKELY(qEnvironmentVariableIsSet("HYPERBIN_DEBUG"))) {
        for (int i = 0; i < n; ++i) {
            float worst = 0.0f;
            if (m_lastJoints[i][0] != QVector3D() || m_lastJoints[i][1] != QVector3D()) {
                for (int j = 0; j < TentacleChain::kJoints; ++j)
                    worst = std::max(worst,
                        (m_chain[i].joints()[j] - m_lastJoints[i][j]).length());
                if (worst > float(m_binSize.height()) * 0.10f)
                    qInfo("tentacle %d: JOINT lurched %.0f in one frame (move %d)",
                          i, double(worst), int(m_state[i].move));
                // HYPERBIN_TRACE: every frame, not just the alarming ones.
                // The threshold above counts events, and a count is the
                // wrong statistic for comparing two versions of a move —
                // lengthen a move and it accrues more of them while being
                // calmer per frame. This once said a re-timed slap was
                // twice as bad when its per-frame rate had in fact more
                // than halved.
                if (Q_UNLIKELY(qEnvironmentVariableIsSet("HYPERBIN_TRACE")))
                    qInfo("F %d %d %.3f %.2f", i, int(m_state[i].move),
                          double(moveProgress(i)), double(worst));
            }
            for (int j = 0; j < TentacleChain::kJoints; ++j)
                m_lastJoints[i][j] = m_chain[i].joints()[j];
        }
    }
    m_spine->setChains(m_chain, n, kMaxTentacles);
}
QVariantList TentacleEffect::seats() const
{
    QVariantList out;
    out.reserve(kMaxTentacles);
    for (const Seat &s : kSeats)
        out.append(QVariant::fromValue(QVector4D(s.lateral, s.depth, s.size, 0.0f)));
    return out;
}

float TentacleEffect::armLength() const
{
    return kArmLength * float(m_binSize.height());
}

int TentacleEffect::count() const
{
    if (isEmpty())
        return 0;
    if (m_retreat >= 0.0f && m_retreat < kRetreatAll)
        return kMaxTentacles;
    return std::clamp(int(std::lround(m_level * kMaxTentacles)), 1, kMaxTentacles);
}

QMargins TentacleEffect::margins(qreal iconSize) const
{
    // Nearly all of it above. These reach UP out of the bin and wave
    // about; anything drawn past the window's edge is clipped and reads
    // as broken, and the window is sized from this.
    //
    // The top has to cover an arm standing straight up, which is its root
    // (a third of the way up the bin) plus its whole length (kArmLength,
    // 1.15 bin heights) plus what the sway adds, less the half of the bin
    // that is already inside the rect. At 1.6 there is room for the sway
    // and for a longer arm than the current one before this has to be
    // revisited. Fill costs effectively nothing at this size — see
    // docs/battery.md — so the margin is cheap and clipping is not.
    return QMargins(int(iconSize * 0.9), int(iconSize * 1.6),
                    int(iconSize * 0.9), int(iconSize * 0.15));
}

// --- the mouth, in scene units ------------------------------------------
//
// The measurement arrives as a fraction of the icon's tile with +y DOWN
// from its top; the scene is bin-local pixels with the origin at the
// rect's centre and +y UP. Converted here, once, so no caller has to
// remember which way round either convention runs.

float TentacleEffect::mouthX() const
{
    return float(m_mouth.centre.x() - 0.5) * float(m_binSize.width());
}

float TentacleEffect::mouthY() const
{
    // Divided by cos(tilt), and that is not a fudge — it is undoing the
    // projection the measurement already went through.
    //
    // The mouth's height arrives as a fraction of the icon, i.e. a position
    // ON SCREEN. Putting that number straight into the scene forgets that
    // the camera then foreshortens y by cos(tilt), so the scene's mouth
    // landed 4.4% lower on screen than the artwork's. Nine pixels on a
    // 600px bin, which does not sound like much until you notice what it
    // is nine pixels OF: the mask compares an arm against the measured lip
    // (a screen curve) and against the bin's front face (a scene surface),
    // and those two only meet at the rim if the rim is in the same place in
    // both. It was not, so a thin band around the rim was judged below the
    // lip and behind the wall at the same time, and arms crossing it were
    // clipped.
    //
    // Everything derived from this — the roots, the heap, the shading
    // reference — moves with it, so the shapes are unchanged; they are all
    // simply nine pixels further up, where the artwork says they are.
    const float c = std::cos(m_cameraTilt * 3.14159265f / 180.0f);
    return float(0.5 - m_mouth.centre.y()) * float(m_binSize.height())
         / std::max(0.2f, c);
}

float TentacleEffect::mouthRadius() const
{
    return m_mouth.halfWidth * float(m_binSize.width());
}

float TentacleEffect::mouthDepth() const
{
    return m_mouth.depth * float(m_binSize.height());
}

float TentacleEffect::heapTopY() const
{
    return mouthY() + kHeapRise * float(m_binSize.height());
}

float TentacleEffect::heapFloorY() const
{
    return mouthY() - kHeapSink * float(m_binSize.height());
}

float TentacleEffect::rootY() const
{
    return mouthY() - kRootDepth * float(m_binSize.height());
}

} // namespace hyperbin
