#include "pch.h"
#include "DearImGuiRenderer.h"

#include "../../HybridVR/ForwardPlusLighting.h"
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
    BoolVar g_useImGui = { "Use ImGui", true };
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

	// TODO: fix blend.. We are currently see-through
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

    ImGui::ShowDemoWindow(0);

    // We specify a default position/size in case there's no data in the .ini file. Typically this isn't required! We only do it to make the Demo applications a little more welcoming.
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(450, 580), ImGuiCond_FirstUseEver);
	
    ImGui::Begin("Engine Tuning");

    const float indent = 10.0f;

	if (ImGui::CollapsingHeader("Application", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_Framed))
    { // Application
        ImGui::Indent(5);
		
        if (ImGui::CollapsingHeader("Forward+", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_Framed))
        { // Application/Forward+
            ImGui::Indent(indent);
        	// ==================

			// Light Grid Dim
            ImGui::Text("Light Grid Dim");

        	int lightGrid = Lighting::LightGridDim;
			        	
            ImGui::RadioButton("8", &lightGrid, 8); ImGui::SameLine();
            ImGui::RadioButton("16", &lightGrid, 16); ImGui::SameLine();
            ImGui::RadioButton("24", &lightGrid, 24); ImGui::SameLine();
            ImGui::RadioButton("32", &lightGrid, 32);

            Lighting::LightGridDim = lightGrid;

        	// TODO: Fix cyclic dependencies... Maybe forward declare?
        	// Show wave tile counts
            /*bool useImGui = ShowWaveTileCounts;
            ImGui::Checkbox("Use ImGui", &useImGui);
            g_useImGui = useImGui;*/

        	// ===================
            ImGui::Indent(-indent);
        } // Application/Forward+
		
        if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_Framed))
        { // Application/Lighting
            ImGui::Indent(indent);
            // ==================



            // ===================
            ImGui::Indent(-indent);
        } // Application/Lighting

        if (ImGui::CollapsingHeader("Raytracing", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_Framed))
        { // Application/Raytracing
            ImGui::Indent(indent);
            // ==================



            // ===================
            ImGui::Indent(-indent);
        } // Application/Raytracing

        ImGui::Indent(-indent);
    } // Application

    if (ImGui::CollapsingHeader("Graphics", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_Framed))
    { // Graphics
        ImGui::Indent(indent);
        // ==================

        if (ImGui::CollapsingHeader("AA", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_Framed))
        { // Graphics/AA
            ImGui::Indent(indent);
            // ==================

            if (ImGui::CollapsingHeader("FXAA", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_Framed))
            { // Graphics/AA/FXAA
                ImGui::Indent(indent);
                // ==================



                // ===================
                ImGui::Indent(-indent);
            } // Graphics/AA/FXAA
        	
            if (ImGui::CollapsingHeader("TAA", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_Framed))
            { // Graphics/AA/TAA
                ImGui::Indent(indent);
                // ==================



                // ===================
                ImGui::Indent(-indent);
            } // Graphics/AA/TAA
        	
            // ===================
            ImGui::Indent(-indent);
        } // Graphics/AA

        if (ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_Framed))
        { // Graphics/Bloom
            ImGui::Indent(indent);
            // ==================



            // ===================
            ImGui::Indent(-indent);
        } // Graphics/Bloom

        if (ImGui::CollapsingHeader("Depth of Field", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_Framed))
        { // Graphics/DoF
            ImGui::Indent(indent);
            // ==================



            // ===================
            ImGui::Indent(-indent);
        } // Graphics/DoF

        if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_Framed))
        { // Graphics/Display
            ImGui::Indent(indent);
            // ==================



            // ===================
            ImGui::Indent(-indent);
        } // Graphics/Display

        if (ImGui::CollapsingHeader("HDR", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_Framed))
        { // Graphics/HDR
            ImGui::Indent(indent);
            // ==================



            // ===================
            ImGui::Indent(-indent);
        } // Graphics/HDR

        if (ImGui::CollapsingHeader("Motion Blur", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_Framed))
        { // Graphics/MB
            ImGui::Indent(indent);
            // ==================



            // ===================
            ImGui::Indent(-indent);
        } // Graphics/MB

        if (ImGui::CollapsingHeader("Particle Effects", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_Framed))
        { // Graphics/PE
            ImGui::Indent(indent);
            // ==================



            // ===================
            ImGui::Indent(-indent);
        } // Graphics/PE

        if (ImGui::CollapsingHeader("SSAO", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_Framed))
        { // Graphics/SSAO
            ImGui::Indent(indent);
            // ==================



            // ===================
            ImGui::Indent(-indent);
        } // Graphics/SSAO

        // ===================
        ImGui::Indent(-indent);
    } // Graphics

	if (ImGui::CollapsingHeader("Timing", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_Framed))
    { // Timing
        ImGui::Indent(indent);
        // ==================



        // ===================
        ImGui::Indent(-indent);
    } // Timing

    if (ImGui::CollapsingHeader("VR", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_Framed))
    { // VR
        ImGui::Indent(indent);
        // ==================

    	// Depth


        // ===================
        ImGui::Indent(-indent);
    } // VR
	
    { // Other

    	// Test?
    	
    	// Load/Save Settings
    	
		{ // use imgui
	        bool useImGui = g_useImGui;
	        ImGui::Checkbox("Use ImGui", &useImGui);
	        g_useImGui = useImGui;
		} // use imgui
    }
	


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


