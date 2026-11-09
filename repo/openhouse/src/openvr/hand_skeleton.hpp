#pragma once
#include <openvr_driver.h>
#include <array>
#include <string>
#include <vector>
#include "lh/session.hpp"

namespace lh::openvr {
// The stock Index hand: thumb poses chosen by what the thumb touches, fingers from
// the capacitive sensors, a real fist without the controller. Estimated, not optical
// joint tracking. Poses are loaded from the user's installed Valve assets; no
// proprietary assets are redistributed.
class HandSkeleton {
 public:
  using Bones = std::array<vr::VRBoneTransform_t, 31>;
  // vr_glove_right_poses.glb (required). SteamVR's resources/anims hand_right_*.glb
  // are found from it (or LH_HAND_ANIMS) for the WithoutController fist.
  bool Load(const std::string& glovePoses, bool left, std::string* error);
  // Advances the smoothing and thumb fades by `seconds` with the latest input.
  void Update(const ControllerInput& input, double seconds);
  // The current hand, after Update.
  Bones Current(bool withController) const;
  // Stateless: the hand once settled on this input.
  Bones Evaluate(const ControllerInput& input, bool withController) const;
  const Bones& GripLimit() const { return grip_; }

 private:
  struct State {
    float curl[4] = {}, curlV[4] = {};  // index..pinky, WithController
    float fist[4] = {}, fistV[4] = {};  // biased, WithoutController
    float indexClose = 0, indexCloseV = 0;
    float trigger = 0, squeeze = 0;
    float thumbClose = 0, thumbCloseV = 0;
    float thumbX = 0, thumbY = 0, thumbXV = 0, thumbYV = 0;
    float thumbLiftedFor = 0;  // s with the thumb on nothing
    float thumbRaise = 0, thumbRaiseV = 0;  // 0 on the controller .. 1 the open hand's thumb
    int thumbState = 8;
    float fade = 1;
    float padX = 0, padY = 0, stickX = 0, stickY = 0;
    Bones thumbFrom{};
  };
  static int ThumbState(const ControllerInput& input);
  Bones ThumbPose(const State& s, bool blended) const;

  std::vector<Bones> glove_, hand_, close_, squeeze_;
  Bones grip_{};
  bool left_ = false;
  State state_;
};
}
