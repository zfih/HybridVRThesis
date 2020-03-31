#pragma once

#include "string"
#include "openvr.h"

namespace VR
{
	extern vr::IVRSystem* g_HMD;

	extern vr::TrackedDevicePose_t g_rTrackedDevicePose[vr::k_unMaxTrackedDeviceCount];

	extern bool g_HMDPresent;
	extern bool g_RuntimeInstalled;
	
	extern std::string g_driver;
	extern std::string g_display;
	
	bool TryInitVR();
	
	inline bool IsVRAvailable() { return g_HMDPresent && g_RuntimeInstalled; }
	inline vr::IVRSystem* GetHMD() { return g_HMD; }
	
	vr::TrackedDevicePose_t GetTrackedDevicePose(UINT device);
	XMMATRIX GetHMDPos();
	XMMATRIX GetEyeToHeadTransform(vr::EVREye eye);
	XMMATRIX GetProjectionMatrix(vr::EVREye eye, float near_plane, float far_plane);

	std::string GetTrackedDeviceString(vr::IVRSystem* hmd, vr::TrackedDeviceIndex_t unDevice, vr::TrackedDeviceProperty prop, vr::TrackedPropertyError* peError = nullptr);
	XMMATRIX ConvertSteamVRMatrixToXMMatrix(const vr::HmdMatrix34_t& matPose);
	XMMATRIX ConvertSteamVRMatrixToXMMatrix(const vr::HmdMatrix44_t& matPose);
}