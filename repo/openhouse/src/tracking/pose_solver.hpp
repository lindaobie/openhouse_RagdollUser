// Bootstrap pose solver — port of vortex::CLighthousePoseSolver (entry 0x3ab590,
// iteration 0x3a9c30). Solves the 7-parameter pose [p, q] of a tracked object in a
// base station frame from one sweep's worth of LH1 angle samples (static object).
//
// Algorithm (see ai/driver/pose_solver.md):
//   * samples sorted by (sensor, axis); sensors seen on both axes get a 2-residual
//     functor, others a single-axis one; plus 0.25·(1−‖q‖²) with z = 0
//   * start at p = (0,0,−2), q = identity (or a given initial pose)
//   * ≤ 40 Gauss-Newton iterations: residuals wrapped across ±π; step from the
//     least-squares solve of J·step = r; golden-section line search on x − α·step
//     (α ∈ [0,1], ≤ 32 evaluations, 1 % tolerance, poses with z ≥ 0 cost 1e50)
//   * position renormalized to 1 m if it exceeds 20 m
//   * converged when the cost < noise²·N and < noise²·N/‖p‖; stop when
//     ‖J·step‖² < (cost + 1)·1e-8
#pragma once

#include <array>
#include <vector>

#include "lh/optical.hpp"
#include "tracked_object.hpp"

namespace lh::solver {

struct Pose {
  std::array<double, 3> p{};      // position (base frame)
  std::array<double, 4> q{1, 0, 0, 0};  // w, x, y, z
};

struct Result {
  int status = 1;          // 0 converged, 1 not converged, 2 insufficient samples
  Pose pose;
  int iterations = 0;      // cfg+0x18
  int lineSearchEvals = 0; // cfg+0x1c
  double cost = 0;         // cfg+0x20 (per degree of freedom)
  double error = 0;        // cfg+0x28 = sqrt(|p|·cost)
};

namespace detail {
// Internal line-search rule, exposed for its focused unit test. LH2's model can
// produce NaN outside its angular domain; NaN must not be allowed to compare as
// neither better nor worse and steer the golden-section search into that domain.
double LineSearchCostOrPenalty(double cost, double alpha);
}  // namespace detail

// Sufficiency test (0x3a2e30 / 0x3a3370): ≥ 4 hits per axis and 4 non-coplanar
// sensors (8.5 mm), checked on two-axis sensors, else on each axis separately.
bool SamplesSufficient(const TrackedObjectModel& obj, const std::vector<OpticalSample>& sorted);

Result Solve(const TrackedObjectModel& obj, const BaseConfig& base, std::vector<OpticalSample> samples,
             const Pose* initial = nullptr, double noise = 0.0001884955592153876);

// Not in stock. The stock solve starts 2 m in front of the base, unrotated; with few
// sensors in view (a controller lying on its side, ~5 sensors) it settles in a wrong
// minimum every time and acquisition can take minutes. This starts from the ray the
// sweep angles point along, at several distances and 24 orientations, and keeps the
// best result that passes the unchanged stock convergence test. `ambiguous` is set
// when a clearly different pose fits about as well (mirror solutions with coplanar
// sensors, reflections): the caller must not trust it.
// Light on a sensor that faces away from the base at the solved pose can only be a
// reflection (a tracker on the floor, a controller on a bed: live, three of eleven
// sensors). Such sensors are left out and the pose solved again; a pose that needs
// one is no pose. Nothing converging, it also leaves each sensor out in turn from the
// best near-miss. `dropped` gets the sensors it left out.
Result SolveMultiStart(const TrackedObjectModel& obj, const BaseConfig& base, const std::vector<OpticalSample>& samples,
                       bool* ambiguous, std::vector<int>* dropped = nullptr);

}  // namespace lh::solver
