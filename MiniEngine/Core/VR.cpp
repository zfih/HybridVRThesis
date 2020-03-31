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

	vr::VRTextureBounds_t g_Bounds{0,0,1,1};
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

	g_driver = GetTrackedDeviceString(vr::k_unTrackedDeviceIndex_Hmd,
		vr::Prop_TrackingSystemName_String);
	g_display = GetTrackedDeviceString(vr::k_unTrackedDeviceIndex_Hmd,
		vr::Prop_SerialNumber_String);

	if (!vr::VRCompositor())
	{
		OutputDebugStringA("Compositor initialization failed. See log file for details\n");
	}

	return true;
}

vr::TrackedDevicePose_t VR::GetTrackedDevicePose(UINT device)
{
	if (!g_HMD)
	{
		DEBUGPRINT("A GetTrackedDevicePose call was made for VR with no HMD present");
		return;
	}
	
	return g_rTrackedDevicePose[device];
}

XMMATRIX VR::GetHMDPos()
{
	if (!g_HMD)
	{
		DEBUGPRINT("A GetHMDPos call was made for VR with no HMD present");
		return;
	}
	
	return ConvertSteamVRMatrixToXMMatrix(
		g_rTrackedDevicePose[vr::k_unTrackedDeviceIndex_Hmd]
			.mDeviceToAbsoluteTracking
	);
}

XMMATRIX VR::GetEyeToHeadTransform(vr::EVREye eye)
{
	if (!g_HMD)
	{
		DEBUGPRINT("A GetEyeToHeadTransform call was made for VR with no HMD present");
		return;
	}
	
	return ConvertSteamVRMatrixToXMMatrix(
		g_HMD->GetEyeToHeadTransform(eye));
}

XMMATRIX VR::GetProjectionMatrix(
	vr::EVREye eye,
	float near_plane,
	float far_plane)
{
	if (!g_HMD)
	{
		DEBUGPRINT("A GetProjectionMatrix call was made for VR with no HMD present");
		return;
	}
	
	return VR::ConvertSteamVRMatrixToXMMatrix(
		g_HMD->GetProjectionMatrix(eye, near_plane, far_plane));
}

std::string VR::GetTrackedDeviceString(vr::TrackedDeviceIndex_t unDevice, vr::TrackedDeviceProperty prop, vr::TrackedPropertyError* peError)
{
	if (!g_HMD)
	{
		DEBUGPRINT("A GetTrackedDeviceString call was made for VR with no HMD present");
		return;
	}
	
	uint32_t unRequiredBufferLen = g_HMD->GetStringTrackedDeviceProperty(unDevice, prop, NULL, 0, peError);
	if (unRequiredBufferLen == 0)
		return "";

	char* pchBuffer = new char[unRequiredBufferLen];
	unRequiredBufferLen = g_HMD->GetStringTrackedDeviceProperty(unDevice, prop, pchBuffer, unRequiredBufferLen, peError);
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


// TODO: NEEDS TESTING
void VR::Submit(ColorBuffer buffer_array)
{
	if(!g_HMD)
	{
		DEBUGPRINT("A submit call was made to the HMD with none present");
		return;
	}
	
	vr::D3D12TextureData_t d3d12TextureData = {
		buffer_array.GetResource(),
		Graphics::g_CommandManager.GetCommandQueue(),
		0};
	
	vr::Texture_t texture = {
		(void*)&d3d12TextureData, vr::TextureType_DirectX12,
		vr::ColorSpace_Gamma};

	vr::VRCompositor()->Submit(vr::Eye_Left, &texture, &g_Bounds,
		vr::Submit_Default);

	vr::VRCompositor()->Submit(vr::Eye_Right, &texture, &g_Bounds,
		vr::Submit_Default);
}

void VR::Submit(ColorBuffer buffer_left, ColorBuffer buffer_right)
{
	if (!g_HMD)
	{
		DEBUGPRINT("A submit call was made for VR with no HMD present");
		return;
	}
	
	vr::D3D12TextureData_t d3d12LeftEyeTexture = {
		buffer_left.GetResource(),
		Graphics::g_CommandManager.GetCommandQueue(),
		0 };

	vr::D3D12TextureData_t d3d12RightEyeTexture = {
		buffer_right.GetResource(),
		Graphics::g_CommandManager.GetCommandQueue(),
		0 };

	vr::Texture_t leftEyeTexture = {
		(void*)&d3d12LeftEyeTexture, vr::TextureType_DirectX12,
		vr::ColorSpace_Gamma };

	vr::Texture_t rightEyeTexture = {
		(void*)&d3d12RightEyeTexture, vr::TextureType_DirectX12,
		vr::ColorSpace_Gamma };

	
	vr::VRCompositor()->Submit(vr::Eye_Left, &leftEyeTexture, &g_Bounds,
		vr::Submit_Default);

	vr::VRCompositor()->Submit(vr::Eye_Right, &rightEyeTexture, &g_Bounds,
		vr::Submit_Default);
}

void VR::Sync()
{
	if (g_HMD)
	{
		vr::VRCompositor()->WaitGetPoses(
			g_rTrackedDevicePose,
			vr::k_unMaxTrackedDeviceCount,
			nullptr, 
			0);
	}
	else
	{
		DEBUGPRINT("A sync call was made for VR with no HMD present");
	}
}
