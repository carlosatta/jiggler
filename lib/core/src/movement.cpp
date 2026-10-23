#include "movement.h"

#include <algorithm>
#include <cmath>

namespace jig {

namespace {

constexpr uint16_t kStepMs = 20;
constexpr double kPi = 3.14159265358979323846;

struct Pt {
  double x, y;
};

double minimumJerk(double t) { return t * t * t * (10.0 + t * (-15.0 + 6.0 * t)); }

Pt cubicBezier(Pt p0, Pt p1, Pt p2, Pt p3, double s) {
  const double u = 1.0 - s;
  const double a = u * u * u, b = 3 * u * u * s, c = 3 * u * s * s, d = s * s * s;
  return {a * p0.x + b * p1.x + c * p2.x + d * p3.x, a * p0.y + b * p1.y + c * p2.y + d * p3.y};
}

// Appends Move steps from (fx,fy) to (tx,ty), both integers. Rounded positions telescope,
// so the stroke ends exactly on (tx,ty).
void appendStroke(std::vector<Step>& steps, int fx, int fy, int tx, int ty, Rng& rng,
                  uint16_t endWaitMs) {
  const Pt from{double(fx), double(fy)};
  const Pt to{double(tx), double(ty)};
  const double cx = to.x - from.x, cy = to.y - from.y;
  const double dist = std::hypot(cx, cy);
  const Pt perp = dist > 0 ? Pt{-cy / dist, cx / dist} : Pt{0, 0};

  const double side = (rng.next() & 1u) ? 1.0 : -1.0;
  const double o1 = side * (0.1 + 0.2 * rand01(rng)) * dist;
  const double o2 = side * (0.1 + 0.2 * rand01(rng)) * dist;
  const Pt c1{from.x + cx / 3 + perp.x * o1, from.y + cy / 3 + perp.y * o1};
  const Pt c2{from.x + 2 * cx / 3 + perp.x * o2, from.y + 2 * cy / 3 + perp.y * o2};

  const double jitterAmp = std::min(1.0, 0.01 * dist);
  const double jitterCycles = 2.0 + 2.0 * rand01(rng);
  const double jitterPhase = 2.0 * kPi * rand01(rng);

  const double durationMs = std::clamp(300.0 + 1.2 * dist, 300.0, 1200.0);
  const int n = std::max(1, int(std::lround(durationMs / kStepMs)));

  int px = fx, py = fy;
  for (int i = 1; i <= n; i++) {
    const double t = double(i) / n;
    Pt p = cubicBezier(from, c1, c2, to, minimumJerk(t));
    const double j = jitterAmp * std::sin(kPi * t) * std::sin(2.0 * kPi * jitterCycles * t + jitterPhase);
    p.x += perp.x * j;
    p.y += perp.y * j;
    if (i == n) p = to;
    const int ix = int(std::lround(p.x)), iy = int(std::lround(p.y));
    steps.push_back({Step::Kind::Move, int8_t(ix - px), int8_t(iy - py), 0,
                     i == n ? endWaitMs : kStepMs});
    px = ix;
    py = iy;
  }
}

}  // namespace

Plan planActivity(const Config& config, Rng& rng) {
  Plan plan;
  const double angle = 2.0 * kPi * rand01(rng);
  const double dist = config.amplitudePx * (0.3 + 0.7 * rand01(rng));
  const int tx = int(std::lround(dist * std::cos(angle)));
  const int ty = int(std::lround(dist * std::sin(angle)));

  appendStroke(plan.steps, 0, 0, tx, ty, rng, uint16_t(randRange(rng, 200, 1500)));

  if (rand01(rng) < config.clickProbability)
    plan.steps.push_back({Step::Kind::Click, 0, 0, 0, uint16_t(randRange(rng, 100, 300))});

  if (rand01(rng) < config.scrollProbability) {
    const int n = randRange(rng, 1, 3) * ((rng.next() & 1u) ? 1 : -1);
    plan.steps.push_back({Step::Kind::Scroll, 0, 0, int8_t(n), uint16_t(randRange(rng, 400, 1500))});
    plan.steps.push_back({Step::Kind::Scroll, 0, 0, int8_t(-n), uint16_t(randRange(rng, 200, 600))});
  }

  appendStroke(plan.steps, tx, ty, 0, 0, rng, kStepMs);
  return plan;
}

size_t runPlan(const Plan& plan, MouseSink& mouse, const Platform& platform,
               const std::function<void(uint32_t)>& sleepMs) {
  size_t executed = 0;
  for (const Step& s : plan.steps) {
    if (!platform.mouseConnected()) break;
    switch (s.kind) {
      case Step::Kind::Move: mouse.move(s.dx, s.dy); break;
      case Step::Kind::Click: mouse.click(); break;
      case Step::Kind::Scroll: mouse.scroll(s.notches); break;
    }
    executed++;
    sleepMs(s.waitMs);
  }
  return executed;
}

}  // namespace jig
