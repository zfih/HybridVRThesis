#include "pch.h"
#include "DearImGuiRenderer.h"
#include "GameCore.h"
#include "GraphicsCore.h"
#include "CommandContext.h"
#include "Settings.h"

namespace GameCore
{
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
    extern HWND g_hWnd;
#endif
}

namespace Settings
{
    BoolVar UseImGui = { "Use ImGui", true };
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

    // We specify a default position/size in case there's no data in the .ini file. Typically this isn't required! We only do it to make the Demo applications a little more welcoming.
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(450, 580), ImGuiCond_FirstUseEver);
	
    ImGui::Begin("Engine Tuning");

    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	
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

        	int lightGrid = Settings::LightGridDim;
			        	
            ImGui::RadioButton("8", &lightGrid, 8); ImGui::SameLine();
            ImGui::RadioButton("16", &lightGrid, 16); ImGui::SameLine();
            ImGui::RadioButton("24", &lightGrid, 24); ImGui::SameLine();
            ImGui::RadioButton("32", &lightGrid, 32);

            Settings::LightGridDim = lightGrid;

        	// TODO: Fix cyclic dependencies... Maybe forward declare?
        	// Show wave tile counts
            bool checkbox = Settings::ShowWaveTileCounts;
            ImGui::Checkbox("Show wave tile counts", &checkbox);
            Settings::ShowWaveTileCounts = checkbox;

        	// ===================
            ImGui::Indent(-indent);
        } // Application/Forward+
		
        if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_Framed))
        { // Application/Lighting
            ImGui::Indent(indent);
            // ==================

            // ExpVar SunLightIntensity("Application/Lighting/Sun Light Intensity", 4.0f, 0.0f, 16.0f, 0.1f);
        	// Settings::AmbientIntensity("Application/Lighting/Ambient Intensity", 0.1f, -16.0f, 16.0f, 0.1f);
            // Settings::SunOrientation("Application/Lighting/Sun Orientation", -0.5f, -100.0f, 100.0f, 0.1f);
            // Settings::SunInclination("Application/Lighting/Sun Inclination", 0.75f, 0.0f, 1.0f, 0.01f);
            // Settings::ShadowDimX("Application/Lighting/Shadow Dim X", 5000, 1000, 10000, 100);
            // Settings::ShadowDimY("Application/Lighting/Shadow Dim Y", 3000, 1000, 10000, 100);
            // Settings::ShadowDimZ("Application/Lighting/Shadow Dim Z", 3000, 1000, 10000, 100);

            float amb       = log2f(Settings::AmbientIntensity);
            float shadx     = Settings::ShadowDimX;
            float shady     = Settings::ShadowDimY;
            float shadz     = Settings::ShadowDimZ;
            float sunori    = Settings::SunOrientation;
            float suninc    = Settings::SunInclination;
            float sunint    = log2f(Settings::SunLightIntensity);
        	
            ImGui::SliderFloat("Sun Light Intensity Exponent", &sunint, 0.0f, 16.0f, "%.2f");
            ImGui::SliderFloat("Ambient Intensity Exponent", &amb, -16.0f, 16.0f, "%.2f");
            ImGui::SliderFloat("Shadow Dim X", &shadx, 1000.0f, 10000.0f, "%.1f");
            ImGui::SliderFloat("Shadow Dim Y", &shady, 1000.0f, 10000.0f, "%.1f");
            ImGui::SliderFloat("Shadow Dim Z", &shadz, 1000.0f, 10000.0f, "%.1f");
            ImGui::SliderFloat("Sun Orientation", &sunori, -5.0f, 5.0f, "%.2f");
            ImGui::SliderFloat("Sun Inclination", &suninc, 0.0f, 1.0f, "%.2f");

            Settings::AmbientIntensity  = exp2f(amb);
            Settings::ShadowDimX        = shadx;
            Settings::ShadowDimY        = shady;
            Settings::ShadowDimZ        = shadz;
            Settings::SunOrientation    = sunori;
            Settings::SunInclination    = suninc;
            Settings::SunLightIntensity = exp2f(sunint);

        	
            // ===================
            ImGui::Indent(-indent);
        } // Application/Lighting

        if (ImGui::CollapsingHeader("Raytracing", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_Framed))
        { // Application/Raytracing
            ImGui::Indent(indent);
            // ==================
            
            // Ray Tracing Mode
            ImGui::Text("Raytracing mode");

            int mode = Settings::RayTracingMode;

            ImGui::PushItemWidth(-1);
            ImGui::ListBox("Raytracing mode selections", &mode, Settings::rayTracingModes, 7, 7);
            ImGui::PopItemWidth();
        	
            Settings::RayTracingMode = mode;

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

                bool enabled = Settings::FXAA_Enable;
                ImGui::Checkbox("Enable FXAA", &enabled);
                Settings::FXAA_Enable = enabled;

                bool debug = Settings::FXAA_DebugDraw;
                ImGui::Checkbox("Debug", &debug);
                Settings::FXAA_DebugDraw = debug;
           
                float luma = Settings::FXAA_ContrastThreshold;
                ImGui::SliderFloat("Contrast threshold", &luma, 0.05f, 0.5f, "%.3f");
                Settings::FXAA_ContrastThreshold = luma;

                float sub = Settings::FXAA_SubpixelRemoval;
                ImGui::SliderFloat("Subpixel Removal", &sub, 0.0f, 1.0f, "%.3f");
                Settings::FXAA_SubpixelRemoval = sub;

                bool forceluma = Settings::FXAA_ForceOffPreComputedLuma;
                ImGui::Checkbox("Force Recompute Log-Luma", &forceluma);
                Settings::FXAA_ForceOffPreComputedLuma = forceluma;
            	
                // ===================
                ImGui::Indent(-indent);
            } // Graphics/AA/FXAA
        	
            if (ImGui::CollapsingHeader("TAA", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_Framed))
            { // Graphics/AA/TAA
                ImGui::Indent(indent);
                // ==================
            	
                bool enabled = Settings::TAA_Enable;
                ImGui::Checkbox("Enable TAA", &enabled);
                Settings::TAA_Enable = enabled;

                float sharpness = Settings::TAA_Sharpness;
                ImGui::SliderFloat("TAA Sharpness", &sharpness, 0.0f, 1.0f, "%.3f");
                Settings::TAA_Sharpness = sharpness;

                float blend = Settings::TemporalMaxLerp;
                ImGui::SliderFloat("Blend factor", &blend, 0.0f, 1.0f, "%.3f");
                Settings::TemporalMaxLerp = blend;

                float speed = Settings::TemporalSpeedLimit;
                ImGui::SliderFloat("Speed limit", &speed, exp2(1.0f), exp2(1024.0f), "%.0f");
                Settings::TemporalSpeedLimit = speed;
            	
                bool reset = Settings::TriggerReset;
                ImGui::Checkbox("Reset", &reset);
                Settings::TriggerReset = reset;

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

            bool enable = Settings::BloomEnable;
            ImGui::Checkbox("Enable Bloom", &enable);
            Settings::BloomEnable = enable;

            bool quality = Settings::HighQualityBloom;
            ImGui::Checkbox("High Quality Bloom", &quality);
            Settings::HighQualityBloom = quality;

            float threshold = Settings::BloomThreshold;
            ImGui::SliderFloat("Bloom Threshold", &threshold, 0.0f, 8.0f, "%.1f");
            Settings::BloomThreshold = threshold;

        	float strength = Settings::BloomStrength;
            ImGui::SliderFloat("Bloom Strength", &strength, 0.0f, 2.0f, "%.3f");
            Settings::BloomStrength = strength;

            float scatter = Settings::BloomUpsampleFactor;
            ImGui::SliderFloat("Bloom Scatter", &scatter, 0.0f, 1.0f, "%.3f");
            Settings::BloomUpsampleFactor = scatter;

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
            
            bool enable = Settings::EnableHDR;
            ImGui::Checkbox("Enable HDR", &enable);
            Settings::EnableHDR = enable;

            bool enableAdapt = Settings::EnableAdaptation;
            ImGui::Checkbox("Enable Adaptive Exposure", &enableAdapt);
            Settings::EnableAdaptation = enableAdapt;

            bool hist = Settings::DrawHistogram;
            ImGui::Checkbox("Draw Histogram", &hist);
            Settings::DrawHistogram = hist;

            float arate = Settings::AdaptationRate;
            ImGui::SliderFloat("Adaptive Rate", &arate, 0.01f, 1.0f, "%.3f");
            Settings::AdaptationRate = arate;

            float key = Settings::TargetLuminance;
            ImGui::SliderFloat("Adaptive Rate", &key, 0.01f, 0.99f, "%.3f");
            Settings::TargetLuminance = key;
        	
            float min = log2f(Settings::MinExposure);
            ImGui::SliderFloat("Min Exposure", &min, -8.0f, 0.0f, "%.3f");
            Settings::MinExposure = exp2f(min);

            float max = log2f(Settings::MaxExposure);
            ImGui::SliderFloat("Max Exposure", &max, 0.0f, 8.0f, "%.3f");
            Settings::MaxExposure = exp2f(max);

            float exposure = log2f(Settings::Exposure);
            ImGui::SliderFloat("Exposure", &exposure, -8.0f, 8.0f, "%.3f");
            Settings::Exposure = exp2f(exposure);
        	
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

    	// Display Profiler
    	// Display Frame Rate
    	
    	// Load/Save Settings
    	
		{ // use imgui
	        bool checkbox = Settings::UseImGui;
	        ImGui::Checkbox("Use ImGui", &checkbox);
            Settings::UseImGui = checkbox;
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