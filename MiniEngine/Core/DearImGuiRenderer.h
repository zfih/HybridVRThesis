#pragma once

#include <ImGui/imgui.h>
#include <ImGui/imgui_impl_dx12.h>
#include <ImGui/imgui_impl_win32.h>

#include "../../HybridVR/DescriptorHeapStack.h" // oh lord
#include "DescriptorHeap.h"

namespace ImGui
{
	extern UserDescriptorHeap g_descHeap;
	
	void Initialize();
	void BuildGUI();
	void RenderGUI();
	void Shutdown();
}
