#pragma once

#include <ImGui/imgui.h>
#include <ImGui/imgui_impl_dx12.h>
#include <ImGui/imgui_impl_win32.h>


#include "Camera.h"
#include "CameraController.h"
#include "DescriptorHeap.h"

namespace ImGui
{
	extern UserDescriptorHeap g_descHeap;
	
	void Initialize();
	void BuildGUI(Math::Camera* cam, GameCore::CameraController* controller);
	void RenderGUI();
	void Shutdown();
}
