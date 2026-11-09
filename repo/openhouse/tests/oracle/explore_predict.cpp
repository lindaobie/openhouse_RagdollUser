// Exploratory: run the original ProcessNoise + PredictCovariance on a fake
// CLighthouseDirect and compare against candidate formulas.
#include <Eigen/Dense>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <ceres/jet.h>

#include "oracle.hpp"
#include "tracking/residuals.hpp"

using Mat = Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic>;

struct EigenMat { double* data; long rows, cols; };

static void SetMat(uint8_t* obj, int off, int r, int c, const Mat& m) {
  EigenMat em{static_cast<double*>(malloc(sizeof(double) * size_t(r * c))), r, c};
  std::memcpy(em.data, m.data(), sizeof(double) * size_t(r * c));
  std::memcpy(obj + off, &em, sizeof em);
}
static Mat GetMat(uint8_t* obj, int off) {
  EigenMat em;
  std::memcpy(&em, obj + off, sizeof em);
  return Eigen::Map<Mat>(em.data, em.rows, em.cols);
}

int main() {
  std::mt19937 rng(1);
  std::uniform_real_distribution<double> u(-1, 1);
  uint8_t* obj = static_cast<uint8_t*>(calloc(1, 0x4000));
  double* x = reinterpret_cast<double*>(obj + 0x448);
  for (int i = 0; i < 49; i++) x[i] = 0.1 * u(rng);
  x[9] = 0.9; x[10] = 0.2; x[11] = -0.3; x[12] = 0.25;
  const double qn = std::sqrt(0.81 + 0.04 + 0.09 + 0.0625);
  for (int i = 9; i < 13; i++) x[i] /= qn;
  for (int i = 13; i < 16; i++) x[i] = u(rng);
  const int n = 31;
  *reinterpret_cast<int*>(obj + 0x5d0) = n;
  Mat A = Mat::Random(49, 49);
  Mat P0 = A * A.transpose() * 0.01;
  SetMat(obj, 0x5d8, 49, 49, P0);
  SetMat(obj, 0x938, 49, 49, Mat::Zero(49, 49));
  SetMat(obj, 0x968, 49, 49, Mat::Zero(49, 49));
  const double dt = 0.004;
  double xbefore[49];
  std::memcpy(xbefore, x, sizeof xbefore);

  oracle::Fn<void (*)(double, void*, void*)>(0x355990)(dt, obj, obj + 0x448);
  Mat Q = GetMat(obj, 0x968);
  printf("Q diag:");
  for (int i = 0; i < 31; i++) printf(" %.3g", Q(i, i));
  printf("\nQ(0,3)=%g Q(0,6)=%g Q(9,13)=%g Q(13,9)=%g Q(9,10)=%g\n", Q(0, 3), Q(0, 6), Q(9, 13), Q(13, 9), Q(9, 10));

  struct { void** vt; double a, b; } ts{static_cast<void**>(oracle::At(0x472108)), dt, dt};
  struct Ref { double* data; long rows, cols, outer, pad; void* tmp; long z[2]; } qref{};
  EigenMat qm;
  std::memcpy(&qm, obj + 0x968, sizeof qm);
  qref.data = qm.data; qref.rows = qm.rows; qref.cols = qm.cols; qref.outer = qm.cols;
  static uint8_t snap[0x4000];
  std::memcpy(snap, obj, 0x4000);
  oracle::Fn<void (*)(void*, void*, void*)>(0x366610)(obj, &ts, &qref);
  for (int o = 0; o < 0x4000; o += 8) if (std::memcmp(snap + o, obj + o, 8)) printf("changed +0x%x\n", o);

  Mat P1 = GetMat(obj, 0x5d8);
  Mat F = GetMat(obj, 0x938);
  double* xl = reinterpret_cast<double*>(obj + 0x7b0);
  double xm[49];
  lh::direct::StateTimestep(xbefore, dt, dt, xm);
  double ex = 0, el = 0;
  for (int i = 0; i < 49; i++) {
    ex = std::max(ex, std::abs(x[i] - xm[i]));
    el = std::max(el, std::abs(xl[i] - xm[i]));
  }
  printf("max|x-f(x0)|=%g  max|xlin-f(x0)|=%g  F dims %ldx%ld F(0,3)=%g F(0,0)=%g\n", ex, el, F.rows(), F.cols(), F(0, 3), F(0, 0));
  // Candidates
  Mat Fn = F.topLeftCorner(n, n);
  Mat c1 = Fn * P0.topLeftCorner(n, n) * Fn.transpose() + Q.topLeftCorner(n, n);
  printf("P1 dims %ldx%ld; |P1n - (F P Ft + Q)| = %g\n", P1.rows(), P1.cols(), (P1.topLeftCorner(n, n) - c1).cwiseAbs().maxCoeff());
  printf("P1 off-block max |P1(n:,:)| = %g, |P0 same| = %g\n", P1.bottomRows(49 - n).cwiseAbs().maxCoeff(), P0.bottomRows(49 - n).cwiseAbs().maxCoeff());
  Mat J = F.transpose();
  Mat Jn = J.topLeftCorner(n, n);
  Mat c3 = Jn * P0.topLeftCorner(n, n) * Jn.transpose() + Q.topLeftCorner(n, n);
  printf("|P1n - (J P Jt + Q)| = %g  (max P1n %g)\n", (P1.topLeftCorner(n, n) - c3).cwiseAbs().maxCoeff(), P1.topLeftCorner(n,n).cwiseAbs().maxCoeff());
  printf("|P1(:n, n:) - P0| = %g\n", (P1.topRightCorner(n, 49 - n) - P0.topRightCorner(n, 49 - n)).cwiseAbs().maxCoeff());
  Mat D = P1.topLeftCorner(n, n) - Jn * P0.topLeftCorner(n, n) * Jn.transpose();
  printf("|D - Q| = %g |D - 2Q| = %g |D| = %g\n", (D - Q.topLeftCorner(n,n)).cwiseAbs().maxCoeff(), (D - 2*Q.topLeftCorner(n,n)).cwiseAbs().maxCoeff(), D.cwiseAbs().maxCoeff());
  for (int i = 0; i < 16; i++) printf("D(%d,%d)=%.4g Q=%.4g\n", i, i, D(i,i), Q(i,i));
  {
    using Jet = ceres::Jet<double, 49>;
    Jet xj[49], r[49];
    for (int i = 0; i < 49; i++) xj[i] = Jet(xbefore[i], i);
    lh::direct::StateTimestep(xj, dt, dt, r);
    Mat Jm(49, 49);
    for (int i = 0; i < 49; i++) for (int j = 0; j < 49; j++) Jm(i, j) = r[i].v[j];
    printf("|F - Jm| = %g  |F^T - Jm| = %g\n", (F - Jm).cwiseAbs().maxCoeff(), (F.transpose() - Jm).cwiseAbs().maxCoeff());
    printf("|P1n - P0n| = %g\n", (P1.topLeftCorner(n,n) - P0.topLeftCorner(n,n)).cwiseAbs().maxCoeff());
    Mat Jn2 = Jm.topLeftCorner(n, n);
    Mat c4 = Jn2 * P0.topLeftCorner(n, n) * Jn2.transpose() + Q.topLeftCorner(n, n);
    printf("|P1n - (Jm P Jm' + Q)| = %g\n", (P1.topLeftCorner(n, n) - c4).cwiseAbs().maxCoeff());
    Mat c5 = Jn2 * P0.topLeftCorner(n, n) * Jn2.transpose();
    printf("|P1n - (Jm P Jm')| = %g\n", (P1.topLeftCorner(n, n) - c5).cwiseAbs().maxCoeff());
  }
  {
    Mat Pp = GetMat(obj, 0x950);
    Mat Jm = F.transpose();
    Mat Jn2 = Jm.topLeftCorner(n, n);
    Mat c4 = Jn2 * P0.topLeftCorner(n, n) * Jn2.transpose() + Q.topLeftCorner(n, n);
    printf("Ppred %ldx%ld |Ppred - (J P J' + Q)_n| = %g (max %g)\n", Pp.rows(), Pp.cols(), (Pp - c4).cwiseAbs().maxCoeff(), c4.cwiseAbs().maxCoeff());
  }
  {
    // ---- EKF update exploration (mode 0, IMU functors) ----
    *reinterpret_cast<uint64_t*>(obj + 0x7a0) = 1000;
    *reinterpret_cast<double*>(obj + 0x408) = 1e9;
    struct ImuData { int32_t comp, pad; void* o; };
    static ImuData d{-1, 0, nullptr};
    static char dummy;
    static struct { void** vt; void* data; } g{static_cast<void**>(oracle::At(0x472188)), &d}, a{static_cast<void**>(oracle::At(0x4721c8)), &d}, qn{static_cast<void**>(oracle::At(0x472148)), &dummy};
    void* list[3] = {&g, &a, &qn};
    struct { void** vt; void** b; void** e; void** c; } fs{static_cast<void**>(oracle::At(0x4720c0)), list, list + 3, list + 3};
    const int m = 7;
    Mat R = Mat::Identity(m, m) * 0.01;
    R(6, 6) = 1e-4;
    struct Ref { double* data; long rows, cols, outer, pad; void* tmp; long z[2]; } rref{R.data(), m, m, m, 0, nullptr, {0, 0}};
    double* xl2 = reinterpret_cast<double*>(obj + 0x7b0);
    double xlin[49];
    std::memcpy(xlin, xl2, sizeof xlin);
    double zv[7];
    lh::direct::ImuGyro(xlin, zv);
    lh::direct::ImuAccel(xlin, zv + 3);
    zv[6] = 0;
    for (int i = 0; i < 6; i++) zv[i] += 0.003 * (i - 2.5);
    struct { double* d; long n; } zref{zv, m};
    Mat Pp = GetMat(obj, 0x950);
    bool ok = oracle::Fn<bool (*)(void*, int, void*, void*, void*)>(0x36cae0)(obj, 0, &fs, &rref, &zref);
    double* xo = reinterpret_cast<double*>(obj + 0x448);
    Mat Pn = GetMat(obj, 0x5d8);
    printf("update ok=%d flags=%x\n", ok, *reinterpret_cast<uint32_t*>(obj + 0xf8));
    // candidate
    using Jet = ceres::Jet<double, 49>;
    Jet xj[49];
    for (int i = 0; i < 49; i++) xj[i] = Jet(xlin[i], i);
    Jet h[7];
    lh::direct::ImuGyro(xj, h);
    lh::direct::ImuAccel(xj, h + 3);
    h[6] = lh::direct::QuaternionNormalizer(xj);
    Mat H(m, n);
    Eigen::VectorXd y(m);
    for (int i = 0; i < m; i++) { y(i) = zv[i] - h[i].a; for (int j = 0; j < n; j++) H(i, j) = h[i].v[j]; }
    Mat S = H * Pp * H.transpose() + R;
    Mat K = Pp * H.transpose() * S.inverse();
    Eigen::VectorXd dx = K * y;
    double ex = 0;
    double xc[49];
    std::memcpy(xc, xlin, sizeof xc);
    for (int i = 0; i < n; i++) xc[i] += dx(i);
    double qn2 = std::sqrt(xc[9]*xc[9]+xc[10]*xc[10]+xc[11]*xc[11]+xc[12]*xc[12]);
    for (int i = 9; i < 13; i++) xc[i] /= qn2;
    for (int i = 0; i < 49; i++) ex = std::max(ex, std::abs(xo[i] - xc[i]));
    printf("max|x - candidate| = %g (|dx| %g)\n", ex, dx.cwiseAbs().maxCoeff());
    Mat Pa = (Mat::Identity(n, n) - K * H) * Pp;
    Mat Pj = (Mat::Identity(n, n) - K * H) * Pp * (Mat::Identity(n, n) - K * H).transpose() + K * R * K.transpose();
    printf("P dims %ldx%ld |P - (I-KH)Pp| = %g |P - Joseph| = %g (max %g)\n", Pn.rows(), Pn.cols(), (Pn.topLeftCorner(n,n) - Pa).cwiseAbs().maxCoeff(), (Pn.topLeftCorner(n,n) - Pj).cwiseAbs().maxCoeff(), Pa.cwiseAbs().maxCoeff());
  }
  Mat c2 = F * P0 * F.transpose() + Q;
  printf("|P1 - full(F P Ft + Q)| = %g\n", (P1 - c2).cwiseAbs().maxCoeff());
}
