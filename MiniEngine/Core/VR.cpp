#include "pch.h"
#include "VR.h"

namespace VirtualReality
{
	std::string GetTrackedDeviceString(vr::IVRSystem* hmd, vr::TrackedDeviceIndex_t unDevice, vr::TrackedDeviceProperty prop, vr::TrackedPropertyError* peError = NULL)
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

	inline XMMATRIX ConvertSteamVRMatrixToXMMatrix(
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

	inline XMMATRIX ConvertSteamVRMatrixToXMMatrix(
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
}

vr::TrackedDevicePose_t VirtualReality::VRSystem::GetTrackedDevicePose(UINT device)
{
	return m_rTrackedDevicePose[device];
}

XMMATRIX VirtualReality::VRSystem::GetHMDPos()
{
	return ConvertSteamVRMatrixToXMMatrix(
		m_rTrackedDevicePose[vr::k_unTrackedDeviceIndex_Hmd]
			.mDeviceToAbsoluteTracking
	);
}

bool VirtualReality::VRSystem::TryInitVR()
{
	m_HMDPresent = vr::VR_IsHmdPresent();
	m_RuntimeInstalled = vr::VR_IsRuntimeInstalled();

	if (!m_HMDPresent || !m_RuntimeInstalled)
	{
		return false;
	}

	vr::EVRInitError error;

	m_HMD = vr::VR_Init(&error, vr::VRApplication_Scene);

	if (error != vr::EVRInitError::VRInitError_None)
	{
		char errorBuffer[256];
		snprintf(errorBuffer, sizeof(errorBuffer),
			"Init VR encountered an error: %d\n", error);
		OutputDebugStringA(errorBuffer);
		vr::VR_Shutdown();
		return false;
	}

	m_driver = "No Driver";
	m_display = "No Display";

	m_driver = GetTrackedDeviceString(m_HMD, vr::k_unTrackedDeviceIndex_Hmd,
		vr::Prop_TrackingSystemName_String);
	m_display = GetTrackedDeviceString(m_HMD, vr::k_unTrackedDeviceIndex_Hmd,
		vr::Prop_SerialNumber_String);

	if (!vr::VRCompositor())
	{
		OutputDebugStringA("Compositor initialization failed. See log file for details\n");
	}

	return true;
}
