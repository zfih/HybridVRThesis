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
		snprintf(errorBuffer, sizeof(errorBuffer), "Init VR encountered an error: %d\n", error);
		OutputDebugStringA(errorBuffer);
		vr::VR_Shutdown();
		return false;
	}

	m_driver = "No Driver";
	m_display = "No Display";

	m_driver = GetTrackedDeviceString(m_HMD, vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_TrackingSystemName_String);
	m_display = GetTrackedDeviceString(m_HMD, vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_SerialNumber_String);

	if (!vr::VRCompositor())
	{
		OutputDebugStringA("Compositor initialization failed. See log file for details\n");
	}

	return true;
}