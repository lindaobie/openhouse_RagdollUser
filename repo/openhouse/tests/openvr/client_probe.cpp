// Manual integration probe for a running vrserver (not a mock host).
// Usage: lh_openvr_client_probe ABSOLUTE_ACTION_MANIFEST
#include <openvr.h>
#include <chrono>
#include <cstdio>
#include <thread>

int main(int argc, char** argv) {
  vr::EVRInitError init;
  auto* system = vr::VR_Init(&init, vr::VRApplication_Background);
  if (!system) { std::fprintf(stderr, "VR_Init: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(init)); return 1; }
  auto* input = vr::VRInput();
  bool skeleton = false, pose = false;
  vr::VRActionHandle_t hand = 0, haptic = 0;
  vr::VRActionSetHandle_t set = 0;
  if (argc > 1) {
    std::printf("manifest: %d\n", input->SetActionManifestPath(argv[1]));
    input->GetActionHandle("/actions/probe/in/hand", &hand);
    input->GetActionHandle("/actions/probe/out/haptic", &haptic);
    input->GetActionSetHandle("/actions/probe", &set);
  }
  for (int tick = 0; tick < 140; ++tick) {
    vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
    system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseRawAndUncalibrated, 0, poses, vr::k_unMaxTrackedDeviceCount);
    for (uint32_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i)
      if (system->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_Controller) {
        if (poses[i].bPoseIsValid) pose = true;
        if (tick == 90) { system->TriggerHapticPulse(i, 0, 1000); std::printf("legacy Identify pulse sent to device %u\n", i); }
      }
    if (set && hand) {
      vr::VRActiveActionSet_t active{}; active.ulActionSet = set;
      input->UpdateActionState(&active, sizeof(active), 1);
      vr::InputSkeletalActionData_t data{};
      vr::VRBoneTransform_t bones[31];
      const auto e = input->GetSkeletalActionData(hand, &data, sizeof(data));
      if (e == vr::VRInputError_None && data.bActive &&
          input->GetSkeletalBoneData(hand, vr::VRSkeletalTransformSpace_Parent,
                                    vr::VRSkeletalMotionRange_WithoutController, bones, 31) == vr::VRInputError_None) skeleton = true;
      if (tick == 100 && haptic) std::printf("haptic action: %d\n", input->TriggerHapticVibrationAction(haptic, 0, .05f, 150, .2f, vr::k_ulInvalidInputValueHandle));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::printf("actual vrserver: valid controller pose=%d, active skeleton read=%d\n", pose, skeleton);
  vr::VR_Shutdown();
  return (!pose || (argc > 1 && !skeleton)) ? 1 : 0;
}
