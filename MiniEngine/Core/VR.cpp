#include "pch.h"
#include "VR.h"

namespace VR
{
	vr::IVRSystem* g_HMD = nullptr;

	vr::TrackedDevicePose_t g_rTrackedDevicePose[vr::k_unMaxTrackedDeviceCount];

	bool g_HMDPresent;
	bool g_RuntimeInstalled;

	std::string g_driver;
	std::string g_display;
}

bool VR::TryInitVR()
{
	if (!g_HMDPresent || !g_RuntimeInstalled)
	{
		return false;
	}

	vr::EVRInitError error;

	g_HMD = vr::VR_Init(&error, vr::VRApplication_Scene);

	if (error != vr::EVRInitError::VRInitError_None)
	{
		char errorBuffer[256];
		snprintf(errorBuffer, sizeof(errorBuffer),
			"Init VR encountered an error: %d\n", error);
		OutputDebugStringA(errorBuffer);
		vr::VR_Shutdown();
		return false;
	}

	g_driver = "No Driver";
	g_display = "No Display";

	g_driver = GetTrackedDeviceString(g_HMD, vr::k_unTrackedDeviceIndex_Hmd,
		vr::Prop_TrackingSystemName_String);
	g_display = GetTrackedDeviceString(g_HMD, vr::k_unTrackedDeviceIndex_Hmd,
		vr::Prop_SerialNumber_String);

	if (!vr::VRCompositor())
	{
		OutputDebugStringA("Compositor initialization failed. See log file for details\n");
	}

	return true;
}

vr::TrackedDevicePose_t VR::GetTrackedDevicePose(UINT device)
{
	return g_rTrackedDevicePose[device];
}

XMMATRIX VR::GetHMDPos()
{
	return ConvertSteamVRMatrixToXMMatrix(
		g_rTrackedDevicePose[vr::k_unTrackedDeviceIndex_Hmd]
			.mDeviceToAbsoluteTracking
	);
}

XMMATRIX VR::GetEyeToHeadTransform(vr::EVREye eye)
{
	return ConvertSteamVRMatrixToXMMatrix(
		g_HMD->GetEyeToHeadTransform(eye));
}

XMMATRIX VR::GetProjectionMatrix(
	vr::EVREye eye,
	float near_plane,
	float far_plane)
{
	return VR::ConvertSteamVRMatrixToXMMatrix(
		g_HMD->GetProjectionMatrix(eye, near_plane, far_plane));
}

std::string VR::GetTrackedDeviceString(vr::IVRSystem* hmd, vr::TrackedDeviceIndex_t unDevice, vr::TrackedDeviceProperty prop, vr::TrackedPropertyError* peError)
{
	uint32_t unRequiredBufferLen = hmd->GetStringTrackedDeviceProperty(unDevice, prop, NULL, 0, peError);
	if (unRequiredBufferLen == 0)
		return "";

	char* pchBuffer = new char[unRequiredBufferLen];
	unRequiredBufferLen = hmd->GetStringTrackedDeviceProperty(unDevice, prop, pchBuffer, unRequiredBufferLen, peError);
	std::string sResult = pchBuffer;
	delete[] pchBuffer;
	return sResult;
}

inline XMMATRIX VR::ConvertSteamVRMatrixToXMMatrix(
	const vr::HmdMatrix34_t& matPose)
{
	XMMATRIX matrixObj(
		matPose.m[0][0], matPose.m[1][0], matPose.m[2][0], 0.0,
		matPose.m[0][1], matPose.m[1][1], matPose.m[2][1], 0.0,
		matPose.m[0][2], matPose.m[1][2], matPose.m[2][2], 0.0,
		matPose.m[0][3], matPose.m[1][3], matPose.m[2][3], 1.0f
		);
	return matrixObj;
}

inline XMMATRIX VR::ConvertSteamVRMatrixToXMMatrix(
	const vr::HmdMatrix44_t& matPose)
{
	XMMATRIX matrixObj(
		matPose.m[0][0], matPose.m[1][0], matPose.m[2][0], matPose.m[3][0],
		matPose.m[0][1], matPose.m[1][1], matPose.m[2][1], matPose.m[3][1],
		matPose.m[0][2], matPose.m[1][2], matPose.m[2][2], matPose.m[3][2],
		matPose.m[0][3], matPose.m[1][3], matPose.m[2][3], matPose.m[3][3]
		);
	return matrixObj;
}