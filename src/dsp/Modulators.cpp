#include "Modulators.h"

namespace zyg::dsp {

double bendCurve(double t, double bend) noexcept {
    const double b = clampd((bend - 0.5) * 2.0, -1.0, 1.0);
    if (std::abs(b) < 1.0e-6) return t;
    const double k = b * 4.4; // negative bends ease-in, positive ease-out
    return (std::exp(k * t) - 1.0) / (std::exp(k) - 1.0);
}

double evalCurveRaw(const double* cx, const double* cy, const double* cb, std::size_t n, double x) noexcept {
    if (n == 0) return clampd(x, 0.0, 1.0);
    x = clampd(x, 0.0, 1.0);
    if (x <= cx[0]) return cy[0];
    if (x >= cx[n - 1]) return cy[n - 1];
    for (std::size_t i = 0; i + 1 < n; ++i) {
        if (x <= cx[i + 1]) {
            const double span = std::max(cx[i + 1] - cx[i], 1.0e-9);
            const double t = bendCurve((x - cx[i]) / span, cb ? cb[i] : 0.5);
            return lerp(cy[i], cy[i + 1], t);
        }
    }
    return cy[n - 1];
}
double evalCurve(const CurvePoints& c, double x) noexcept {
    return c.x.empty() ? clampd(x, 0.0, 1.0) : evalCurveRaw(c.x.data(), c.y.data(), c.bend.size() >= c.x.size() ? c.bend.data() : nullptr, c.x.size(), x);
}

double evalPathRaw(const double* cx, const double* cy, const double* cb, std::size_t n, bool closed, double phase) noexcept {
    if (n == 0) return std::sin(tau * phase);
    if (n == 1) return 2.0 * cy[0] - 1.0;
    const double p = wrap01(phase);
    double y;
    if (p <= cx[0] || p >= cx[n - 1]) {
        if (!closed) { y = p <= cx[0] ? cy[0] : cy[n - 1]; }
        else {
            // wrap segment: last point -> first point across the period boundary
            const double span = std::max((1.0 - cx[n - 1]) + cx[0], 1.0e-9);
            const double pos = p >= cx[n - 1] ? p - cx[n - 1] : p + (1.0 - cx[n - 1]);
            const double t = bendCurve(pos / span, cb ? cb[n - 1] : 0.5);
            y = lerp(cy[n - 1], cy[0], t);
        }
    } else {
        y = evalCurveRaw(cx, cy, cb, n, p);
    }
    return 2.0 * y - 1.0;
}
double evalPath(const CurvePoints& c, double phase) noexcept {
    return evalPathRaw(c.x.data(), c.y.data(), c.bend.size() >= c.x.size() ? c.bend.data() : nullptr, c.x.size(), c.closed, phase);
}

double evalPath2DRaw(const double* cx, const double* cy, std::size_t n, bool closed, double u) noexcept {
    if (n == 0) return 0.0;
    if (n == 1) return 2.0 * cy[0] - 1.0;
    const std::size_t segs = closed ? n : n - 1;
    auto seg = [&](std::size_t i, double& x0, double& y0, double& x1, double& y1) {
        x0 = cx[i]; y0 = cy[i]; x1 = cx[(i + 1) % n]; y1 = cy[(i + 1) % n];
    };
    double total = 0.0;
    for (std::size_t i = 0; i < segs; ++i) { double a, b, c2, d; seg(i, a, b, c2, d); total += std::hypot(c2 - a, d - b); }
    if (total <= 1.0e-12) return 2.0 * cy[0] - 1.0;
    double t = wrap01(u);
    if (!closed) t = t < 0.5 ? t * 2.0 : 2.0 - t * 2.0; // open paths are travelled back and forth
    double dist = t * total;
    for (std::size_t i = 0; i < segs; ++i) {
        double a, b, c2, d; seg(i, a, b, c2, d);
        const double len = std::hypot(c2 - a, d - b);
        if (dist <= len || i + 1 == segs) { const double f = len > 1.0e-12 ? clampd(dist / len, 0.0, 1.0) : 0.0; return 2.0 * lerp(b, d, f) - 1.0; }
        dist -= len;
    }
    return 2.0 * cy[0] - 1.0;
}
double evalPath2D(const CurvePoints& c, double u) noexcept { return evalPath2DRaw(c.x.data(), c.y.data(), c.x.size(), c.closed, u); }

double advanceLfo(LfoState& s, const LfoDefinition& d, double rateHz, double dt,
                  double phaseOffset, double smoothPercent, double riseS, double delayS,
                  const double* busOffsets) noexcept {
    const double rate = std::max(rateHz, 0.0);
    double value = 0.0;
    const double inc = rate * dt;
    switch (d.shape) {
        case LfoShape::lorenz: {
            const double step = inc;
            auto& c = s.chaos;
            const double x = c[0], y = c[1], z = c[2];
            c[0] += step * 10.0 * (y - x); c[1] += step * (x * (28.0 - z) - y); c[2] += step * (x * y - (8.0 / 3.0) * z);
            if (!std::isfinite(c[0] + c[1] + c[2])) c = {0.1, 0.0, 0.0};
            value = std::tanh(c[0] / 15.0); break;
        }
        case LfoShape::rossler: {
            const double step = inc;
            auto& c = s.chaos;
            const double x = c[0], y = c[1], z = c[2];
            c[0] += step * (-y - z); c[1] += step * (x + 0.2 * y); c[2] += step * (0.2 + z * (x - 5.7));
            if (!std::isfinite(c[0] + c[1] + c[2])) c = {0.1, 0.0, 0.0};
            value = std::tanh(c[0] / 8.0); break;
        }
        case LfoShape::randomHold: {
            const double old = s.phase;
            s.phase = wrap01(s.phase + inc);
            if (!s.started || s.phase < old) {
                s.started = true;
                s.rng ^= s.rng << 13; s.rng ^= s.rng >> 17; s.rng ^= s.rng << 5;
                s.held = double(s.rng) / 2147483648.0 - 1.0;
            }
            value = s.held; break;
        }
        default: { // sine or path
            double advance = inc;
            const bool once = d.mode == LfoMode::envelope || d.mode == LfoMode::oneShot;
            double ph = s.phase;
            if (d.direction == 1) ph = 1.0 - wrap01(ph);
            else if (d.direction == 2) { const double q = wrap01(ph * 0.5) * 2.0; ph = q < 1.0 ? q : 2.0 - q; }
            const double evalPhase = wrap01(ph + phaseOffset);
            if (d.path.empty()) value = std::sin(tau * evalPhase);
            else if (busOffsets && !d.pointMods.empty() && d.path.x.size() <= 64) {
                // modulated copy of the drawn points, kept on the stack
                std::array<double, 64> px, py, pb;
                const std::size_t n = d.path.x.size();
                for (std::size_t i = 0; i < n; ++i) { px[i] = d.path.x[i]; py[i] = d.path.y[i]; pb[i] = i < d.path.bend.size() ? d.path.bend[i] : 0.5; }
                for (const auto& m : d.pointMods) {
                    if (m.point < 0 || std::size_t(m.point) >= n || m.bus < 0 || m.bus >= 16) continue;
                    const double off = busOffsets[m.bus]; const std::size_t i = std::size_t(m.point);
                    if (m.target == 0) px[i] = clampd(px[i] + off, 0.0, 1.0);
                    else if (m.target == 1) py[i] = clampd(py[i] + off, 0.0, 1.0);
                    else pb[i] = clampd(pb[i] + 0.5 * off, 0.0, 1.0);
                }
                value = d.shape == LfoShape::path ? evalPath2DRaw(px.data(), py.data(), n, d.path.closed, evalPhase)
                                                  : evalPathRaw(px.data(), py.data(), pb.data(), n, d.path.closed, evalPhase);
            } else value = d.shape == LfoShape::path ? evalPath2D(d.path, evalPhase) : evalPath(d.path, evalPhase);
            if (once) {
                s.phase += advance;
                if (s.phase >= 1.0) { s.phase = 1.0 - 1.0e-9; s.finished = true; }
                // the held value is the end of the path
            } else {
                s.phase = wrap01(s.phase + advance);
            }
            break;
        }
    }
    s.elapsed += dt;
    // delay then rise fade-in
    double gain = 1.0;
    if (delayS > 0.0 || riseS > 0.0) {
        if (s.elapsed < delayS) gain = 0.0;
        else if (riseS > 0.0) gain = clampd((s.elapsed - delayS) / riseS, 0.0, 1.0);
    }
    value *= gain;
    if (smoothPercent > 0.0) {
        const double cycle = 1.0 / std::max(rate, 0.05);
        const double tauS = clampd(smoothPercent / 100.0, 0.0, 1.0) * 0.3 * cycle;
        const double a = 1.0 - std::exp(-dt / std::max(tauS, 1.0e-5));
        s.smoothed += a * (value - s.smoothed);
        value = s.smoothed;
    }
    return value;
}
}
