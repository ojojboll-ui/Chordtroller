#pragma once

#include "PadState.h"

#include <algorithm>
#include <cmath>

namespace chordtroller
{

/**
    Which way is down, as a controller sees it, from its gyro and accelerometer.

    Neither sensor can do it alone. The accelerometer knows where gravity is but
    cannot tell it from the hand's own acceleration, so a shake reads as a tilt.
    The gyro follows a shake perfectly but only knows how fast it is turning, and
    integrating even a tiny offset puts "level" somewhere else within a minute.

    Together they are a complementary filter: the gyro turns the estimate of down
    as the controller turns, and the accelerometer pulls it back towards the truth
    slowly. Three things keep the accelerometer from pulling it the wrong way:

    - **It is not trusted while reading far from one gravity.** Then it is
      measuring a hand, not the floor.
    - **It is not trusted while the gyro says the controller is turning fast.**
      A trembling wrist accelerates the controller sideways as it turns, which
      changes the reading's direction without changing its size — the first rule
      cannot see that, and a vibrato would drag the pitch flat.
    - **The gyro's own offset is learnt while the controller is still,** so the
      stretches where only the gyro is trusted do not drift. Still means the gyro
      is barely turning and the accelerometer's direction has settled; a slow
      deliberate tilt moves that direction and is not mistaken for an offset.

    Down is kept as a vector in the controller's own frame rather than as angles,
    so no orientation is special and nothing flips at ninety degrees. Angles are
    read off it relative to a home pose, by motionPitch() and motionTwist().

    Axes are SDL's: +X to the right across the face, +Y up the face, +Z out of
    the face. Gyro in radians a second, right-handed about those axes;
    accelerometer in metres a second squared.
*/
class MotionFusion
{
public:
    static constexpr float kGravity = 9.80665f;

    /** How hard the accelerometer pulls the estimate back, per second, when it
        is fully trusted. Four is a quarter of a second. */
    static constexpr float kCorrection = 4.0f;

    /** Turning at this many radians a second halves the accelerometer's say, and
        it falls with the square beyond. A three-degree wrist tremor at five hertz
        turns at nearly two, where it is down to two per cent; a slow sweep of the
        pitch at twenty degrees a second still keeps a third. */
    static constexpr float kTurningRate = 0.25f;

    void reset() noexcept
    {
        primed = false;
        bx = by = bz = 0.0f;
    }

    bool isPrimed() const noexcept { return primed; }

    /** One reading of both sensors, dt seconds after the last. */
    void update (float ax, float ay, float az, float gx, float gy, float gz, float dt) noexcept
    {
        const float norm = std::sqrt (ax * ax + ay * ay + az * az);

        if (! (norm > 1.0e-3f) || ! std::isfinite (norm)
             || ! std::isfinite (gx) || ! std::isfinite (gy) || ! std::isfinite (gz))
            return;

        const float mx = ax / norm, my = ay / norm, mz = az / norm;

        // The first reading has nothing to fuse with; take the accelerometer's
        // word for it.
        if (! primed)
        {
            vx = fx = sx = mx;
            vy = fy = sy = my;
            vz = fz = sz = mz;
            primed = true;
            return;
        }

        // A gap longer than this is a stall, not motion to integrate.
        dt = std::clamp (dt, 0.0f, 0.05f);

        const float surprise = std::abs (norm / kGravity - 1.0f);

        // Two views of the accelerometer's direction, a quick one and a slow one.
        // While they agree it has settled.
        const float quick = std::min (1.0f, dt / 0.05f);
        const float slow  = std::min (1.0f, dt / 0.5f);

        fx += (mx - fx) * quick;  fy += (my - fy) * quick;  fz += (mz - fz) * quick;
        sx += (mx - sx) * slow;   sy += (my - sy) * slow;   sz += (mz - sz) * slow;

        const float settled = (fx * sx + fy * sy + fz * sz)
                                / std::max (1.0e-9f, std::sqrt ((fx * fx + fy * fy + fz * fz)
                                                                * (sx * sx + sy * sy + sz * sz)));

        float wx = gx - bx, wy = gy - by, wz = gz - bz;
        const float rate = std::sqrt (wx * wx + wy * wy + wz * wz);

        // Still: barely turning, settled to within a degree, reading one gravity.
        // Then whatever the gyro reports is its own offset.
        if (rate < 0.06f && settled > 0.99985f && surprise < 0.05f)
        {
            const float learn = std::min (1.0f, dt / 0.5f);

            bx += (gx - bx) * learn;
            by += (gy - by) * learn;
            bz += (gz - bz) * learn;

            wx = gx - bx;  wy = gy - by;  wz = gz - bz;
        }

        // A direction fixed in the room, seen from a controller turning at w,
        // turns the other way: dv/dt = -(w x v).
        const float cx = wy * vz - wz * vy;
        const float cy = wz * vx - wx * vz;
        const float cz = wx * vy - wy * vx;

        vx -= cx * dt;
        vy -= cy * dt;
        vz -= cz * dt;

        const float trustSize   = std::clamp (1.0f - surprise * 4.0f, 0.0f, 1.0f);
        const float turning     = rate / kTurningRate;
        const float trustMotion = 1.0f / (1.0f + turning * turning);
        const float pull        = std::min (1.0f, kCorrection * dt * trustSize * trustMotion);

        vx += (mx - vx) * pull;
        vy += (my - vy) * pull;
        vz += (mz - vz) * pull;

        const float length = std::sqrt (vx * vx + vy * vy + vz * vz);

        if (length > 1.0e-6f)
        {
            vx /= length;
            vy /= length;
            vz /= length;
        }
    }

    Gravity gravity() const noexcept
    {
        Gravity g;
        g.x = vx;
        g.y = vy;
        g.z = vz;
        g.valid = primed;
        return g;
    }

private:
    float vx = 0.0f, vy = 0.0f, vz = 1.0f;     // the estimate of down
    float fx = 0.0f, fy = 0.0f, fz = 1.0f;     // the accelerometer, quickly smoothed
    float sx = 0.0f, sy = 0.0f, sz = 1.0f;     // and slowly
    float bx = 0.0f, by = 0.0f, bz = 0.0f;     // the gyro's learnt offset
    bool  primed = false;
};

namespace motion_detail
{
    inline float wrap (float angle) noexcept
    {
        constexpr float pi = 3.14159265358979f;

        while (angle >  pi) angle -= 2.0f * pi;
        while (angle < -pi) angle += 2.0f * pi;
        return angle;
    }
}

/**
    How far the controller has tipped about its own X axis since home, in
    radians. Positive when the top of the face turns towards the face's front —
    held pointing forward with the face up, that is pointing upwards.

    A direction fixed in the room turns backwards in the controller's frame, so a
    rotation of +a about X moves down's angle in the Y-Z plane by -a; the minus
    sign turns that back into the controller's own rotation.
*/
inline float motionPitch (const Gravity& home, const Gravity& now) noexcept
{
    return -motion_detail::wrap (std::atan2 (now.z, now.y) - std::atan2 (home.z, home.y));
}

/** The same about the controller's long Y axis: a twist of the wrist. */
inline float motionTwist (const Gravity& home, const Gravity& now) noexcept
{
    return -motion_detail::wrap (std::atan2 (now.x, now.z) - std::atan2 (home.x, home.z));
}

} // namespace chordtroller
