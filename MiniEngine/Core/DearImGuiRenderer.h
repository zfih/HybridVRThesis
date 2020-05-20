#pragma once

#include <ImGui/imgui.h>
#include <ImGui/imgui_impl_dx12.h>
#include <ImGui/imgui_impl_win32.h>

#include "DescriptorHeap.h"

namespace ImGui
{
	extern UserDescriptorHeap g_descHeap;
	extern BoolVar g_useImGui;
	
	void Initialize();
	void BuildGUI();
	void RenderGUI();
	void Shutdown();
}
