# Constants recovered from .rodata

"Used by" lists functions where the meaning was established.

| Addr | Value | Meaning | Used by |
|---|---|---|---|
| 0x1a96f0 | 1/48e6 | device tick → seconds | report parsers |
| 0x1a97b8 | 1/96e6 | half-tick (0x28 word bit 2) | 0x3cf040 |
| 0x1a40a0[4] | 2g·9.80665/32768·2^k | accel LSB → m/s² by range | 0x3d07c0 |
| 0x1a4080[4] | 250°/s→rad/32768·2^k | gyro LSB → rad/s by range | 0x3d07c0 |
| 0x1a9a20 | 0.0009 | USB latency subtracted from host read time | 0x3d0a00 |
| 0x1a9640 | 0.025 | clock-offset gain when err < 0 | 0x3cf2c0 |
| 0x1a95c8 | 0.0025 | clock-offset gain when err ≥ 0 | 0x3cf2c0 |
| 0x1a96e8 | 0.05 | clock-offset reset threshold; also SOB-detect window | 0x3cf2c0, 0x3e1480 |
| 0x1a9b90 | 0.5 | clock-offset staleness; duty-cycle report period | 0x3cf2c0, 0x3705a0 |
| 0x1a9bb8 | 23/48000 s (479.17 µs) | LH1 pulse width lower bound for event 0xf | 0x3cec20 |
| 0x1a97f0 | 25/48000 s | LH1 pulse width upper bound for event 0xf | 0x3cec20 |
| 0x1a9768 | 50/48000 s | rate limit for event 0xf | 0x3cec20 |
| 0x1a9790 | 400 | demod/s threshold to enable Sync-on-Beam | 0x3e1480 |
| 0x1a9720 | 0.1 | demod rate window | 0x3e1480 |
| 0x1a9688 | 2.0 | hold after switching to SOB | 0x3e1480 |
| 0x1a9a90 | 11.0 | LH2 word activity timeout | 0x3e1890 |
| 0x1a9ad8 | 0.02 | disambiguator advance lag | 0x3e2d90 |
| 0x1a9b28 | DBL_EPSILON | small-angle threshold (Ceres AngleAxisRotatePoint) | functors |
| 0x1a9bc8 | 0.25 | quaternion-norm residual weight | 0x349f60 |
| 0x1a9b80 | 0.8 | tracking-thread duty-cycle warning | 0x3705a0 |
| 0x1a99f0 | 2.95 | retained history duration in ideal periods | 0x3de790 |
| 0x1a97d8 | 0.125 | reference-time phase tolerance in periods | 0x3de850 |
| 0x1a9ba0 | 1e-6 | initial default covariance diagonal | 0x350280 |
| 0x1a99d0 | 2.5e-5 | initial position variance | 0x350280 |
| 0x1a9ae0 | 0.00047596471841673214 | initial quaternion variance | 0x350280 |
| 0x1a9a58 | 0.04000000000000001 | initial velocity variance | 0x350280 |
| 0x1a9950 | 1.713472986300236 | initial angular velocity variance | 0x350280 |
| 0x1a9b98 | 0.15421256876702122 | initial second gravity tilt variance | 0x350280 |
| 0x1a95c0 | 1.579136704174297e-10 | optical correlated noise floor | 0x357340, 0x36f740 |
| 0x1a9878 | 2.2206609902451057e-9 | additive optical angle variance | 0x357340 |
| 0x1a98d8 / 0x1a98a8 | 7e-5 / 3e-5 | distance-dependent optical noise lengths | 0x357340 |
| 0x1a9aa8 | 3.947841760435744e-9 | additional optical noise variance | 0x357340 |
| 0x1a96d0 | 1.6e-5 | per-axis gyro measurement variance | 0x357340 |
| 0x1a9580 / 0x1a9970 | 0.0015 / 0.003 | accel XY / Z measurement variance | 0x357340 |
| 0x1a9820 | 0.3 | minimum sensor-facing cosine | 0x36f740 |
| 0x1a9868 | 0.002 | grazing-angle noise length | 0x36f740 |
| 0x1a9bf8 | 3.3333333333333335 | grazing-angle slope | 0x36f740 |
| 0x1a9bd0 / 0x1a9980 | 1.0610329539459692e-6 / 0.00013262911924324613 | accepted sample-width bounds | 0x35b380 |
| 0x1a9708 | 2.5 | median angle-outlier bound factor | 0x35e420 |
| 0x1a97d0 | 0.0004 | new alternate-base translation variance | 0x34b810 |
| 0x1a9c10 | 0.0027415567780803775 | new alternate-base rotation variance | 0x34b810 |
