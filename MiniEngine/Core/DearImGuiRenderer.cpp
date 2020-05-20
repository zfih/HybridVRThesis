#include "pch.h"
#include "DearImGuiRenderer.h"
#include "GameCore.h"
#include "GraphicsCore.h"
#include "CommandContext.h"

namespace GameCore
{
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
    extern HWND g_hWnd;
#endif
}

namespace Graphics
{
    extern ColorBuffer g_DisplayPlane[];
    extern UINT g_CurrentBuffer;
}

namespace ImGui
{
    UserDescriptorHeap g_descHeap = UserDescriptorHeap{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1 };
}

void ImGui::Initialize()
{
    ImGui_ImplDX12_CreateDeviceObjects();
	
    g_descHeap.Create(L"ImGui Heap");
    g_descHeap.Alloc(1);
	
    // Imgui Test
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io; // What?

    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();
    //ImGui::StyleColorsClassic();

    ImGui_ImplWin32_Init(GameCore::g_hWnd);

    ImGui_ImplDX12_Init(
        Graphics::g_Device,
        3, //SWAP_CHAIN_BUFFER_COUNT
        DXGI_FORMAT_R8G8B8A8_UNORM,
        g_descHeap.GetHeapPointer(),
        g_descHeap.GetHeapPointer()->GetCPUDescriptorHandleForHeapStart(),
        g_descHeap.GetHeapPointer()->GetGPUDescriptorHandleForHeapStart()
    );
}

void ImGui::BuildGUI()
{
    ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();

    ImGui::NewFrame();

    bool open = true;
	
    ImGui::ShowDemoWindow(&open);
	
    static float f = 0.0f;

    ImGui::Begin("TEST WINDOW 1");

    ImGui::Text("This is the first test window yay");

    ImGui::SliderFloat("Test floaaaat", &f, 0.0f, 10000.0f);

    ImGui::End();
}

void ImGui::RenderGUI()
{
    // Setup imgui context	
    GraphicsContext& g_imguiContext = GraphicsContext::Begin(L"ImGui Context");

    g_imguiContext.TransitionResource(Graphics::g_OverlayBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    g_imguiContext.ClearColor(Graphics::g_OverlayBuffer);
    g_imguiContext.SetRenderTarget(Graphics::g_OverlayBuffer.GetRTV());
    g_imguiContext.SetViewportAndScissor(0, 0, Graphics::g_OverlayBuffer.GetWidth(), Graphics::g_OverlayBuffer.GetHeight());
    g_imguiContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, g_descHeap.GetHeapPointer());

    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_imguiContext.GetCommandList());
    g_imguiContext.Finish();
}

void ImGui::Shutdown()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}


