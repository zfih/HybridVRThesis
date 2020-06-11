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

            float amb       = log2f(Settings::AmbientIntensity);
            float shadx     = Settings::ShadowDimX;
            float shady     = Settings::ShadowDimY;
            float shadz     = Settings::ShadowDimZ;
            float sunori    = Settings::SunOrientation;
            float suninc    = Settings::SunInclination;
            float sunint    = log2f(Settings::SunLightIntensity);
        	
            ImGui::SliderFloat("Ambient Intensity Exponent", &amb, -16.0f, 16.0f, "%.2f");
            ImGui::SliderFloat("Shadow Dim X", &shadx, 1000.0f, 10000.0f, "%.1f");
            ImGui::SliderFloat("Shadow Dim Y", &shady, 1000.0f, 10000.0f, "%.1f");
            ImGui::SliderFloat("Shadow Dim Z", &shadz, 1000.0f, 10000.0f, "%.1f");
            ImGui::SliderFloat("Sun Orientation", &sunori, -5.0f, 5.0f, "%.2f");
            ImGui::SliderFloat("Sun Light Intensity Exponent", &sunint, 0.0f, 16.0f, "%.2f");
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

                float speed = log2f(Settings::TemporalSpeedLimit);
                ImGui::SliderFloat("Speed limit", &speed, 1.0f, 1024.0f, "%.0f");
                Settings::TemporalSpeedLimit = exp2f(speed);
            	
                //bool reset = Settings::TriggerReset;
                //ImGui::Checkbox("Reset", &reset);
                Settings::TriggerReset = ImGui::Button("Reset", { 150, 20 });;

                
            	
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

            bool enable = Settings::DOF_Enable;
            ImGui::Checkbox("Enable Depth Of Field", &enable);
            Settings::DOF_Enable = enable;

            bool enableFilter = Settings::DOF_EnablePreFilter;
            ImGui::Checkbox("Enable PreFilter", &enableFilter);
            Settings::DOF_EnablePreFilter = enableFilter;

            bool medianFilter = Settings::MedianFilter;
            ImGui::Checkbox("Median Filter", &medianFilter);
            Settings::MedianFilter = medianFilter;

            bool medianAlpha = Settings::MedianAlpha;
            ImGui::Checkbox("Median Alpha", &medianAlpha);
            Settings::MedianAlpha = medianAlpha;

            float focalDepth = Settings::FocalDepth;
            ImGui::SliderFloat("Focal Depth", &focalDepth, 0.0f, 1.0f, "%.3f");
            Settings::FocalDepth = focalDepth;

            float focalRange = Settings::FocalRange;
            ImGui::SliderFloat("Focal Radius", &focalRange, 0.0f, 1.0f, "%.3f");
            Settings::FocalRange = focalRange;

            float fgRange = Settings::ForegroundRange;
            ImGui::SliderFloat("FG Range", &fgRange, 10.0f, 1000.0f, "%.0f");
            Settings::ForegroundRange = fgRange;

            float sparkle = Settings::AntiSparkleWeight;
            ImGui::SliderFloat("AntiSparkle", &sparkle, 1.0f, 10.0f, "%.0f");
            Settings::AntiSparkleWeight = sparkle;
        	
            ImGui::Text("Debug Mode");
        	
            int debugMode = Settings::DOF_DebugMode;

            ImGui::RadioButton("Off", &debugMode, 0); ImGui::SameLine();
            ImGui::RadioButton("Foreground", &debugMode, 1); ImGui::SameLine();
            ImGui::RadioButton("Background", &debugMode,2); ImGui::SameLine();
            ImGui::RadioButton("FG Alpha", &debugMode, 3); ImGui::SameLine();
            ImGui::RadioButton("CoC", &debugMode, 4);

            Settings::DOF_DebugMode = debugMode;

            bool debugTile = Settings::DebugTiles;
            ImGui::Checkbox("Debug Tiles", &debugTile);
            Settings::DebugTiles = debugTile;

            bool forceSlow = Settings::ForceSlow;
            ImGui::Checkbox("Force Slow", &forceSlow);
            Settings::ForceSlow = forceSlow;

            bool forceFast = Settings::ForceFast;
            ImGui::Checkbox("Force Fast", &forceFast);
            Settings::ForceFast = forceFast;
        	
            // ===================
            ImGui::Indent(-indent);
        } // Graphics/DoF

        if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_Framed))
        { // Graphics/Display
            ImGui::Indent(indent);
            // ==================

            // HDR Debug Mode
            ImGui::Text("HDR Debug Mode");

            int mode = Settings::HDRDebugMode;

            ImGui::RadioButton("HDR mode", &mode, 0); ImGui::SameLine();
            ImGui::RadioButton("SDR mode", &mode, 1); ImGui::SameLine();
            ImGui::RadioButton("Side-by-side", &mode, 2); 

            Settings::HDRDebugMode = mode;

            // HDR Debug Mode
            ImGui::Text("Debug zoom");

            int zoom = Settings::DebugZoom;

            ImGui::RadioButton("Off", &zoom, 0); ImGui::SameLine();
            ImGui::RadioButton("2x", &zoom, 1); ImGui::SameLine();
            ImGui::RadioButton("4x", &zoom, 2); ImGui::SameLine();
            ImGui::RadioButton("8x", &zoom, 3); ImGui::SameLine();
            ImGui::RadioButton("16x", &zoom, 4);

            Settings::DebugZoom = zoom;

            float white = Settings::HDRPaperWhite;
            ImGui::SliderFloat("Paper White (nits)", &white, 100.0f, 500.0f, "%.0f");
            Settings::HDRPaperWhite = white;

            float peak = Settings::MaxDisplayLuminance;
            ImGui::SliderFloat("Peak Brightness (nits)", &peak, 500.0f, 10000.0f, "%.0f");
            Settings::MaxDisplayLuminance = peak;

			// Upsample filter
            ImGui::Text("Upsample filter");

            int filter = Settings::UpsampleFilter;

            ImGui::RadioButton("Bilinear", &filter, 0); ImGui::SameLine();
            ImGui::RadioButton("Bicubic", &filter, 1); ImGui::SameLine();
            ImGui::RadioButton("Sharpening", &filter, 2);

            Settings::UpsampleFilter = filter;

            float biRange = Settings::BicubicUpsampleWeight;
            ImGui::SliderFloat("Bicubic Filter Weight", &biRange, -1.0f, -0.25f, "%.3f");
            Settings::BicubicUpsampleWeight = biRange;

            float sharpSpread = Settings::SharpeningSpread;
            ImGui::SliderFloat("Sharpness Sample Spread", &sharpSpread, 0.7f, 2.0f, "%.3f");
            Settings::SharpeningSpread = sharpSpread;

            float sharpRot = Settings::SharpeningRotation;
            ImGui::SliderFloat("Sharpness Sample Rotation", &sharpRot, 0.0f, 90.0f, "%.3f");
            Settings::SharpeningRotation = sharpRot;

            float sharpStr = Settings::SharpeningStrength;
            ImGui::SliderFloat("Sharpness Sample Strength", &sharpStr, 0.0f, 1.0f, "%.3f");
            Settings::SharpeningStrength = sharpStr;
        	
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
            ImGui::SliderFloat("Key", &key, 0.01f, 0.99f, "%.3f");
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

            bool checkbox = Settings::MotionBlur_Enable;
            ImGui::Checkbox("Enable Motion Blur", &checkbox);
            Settings::MotionBlur_Enable = checkbox;

            // ===================
            ImGui::Indent(-indent);
        } // Graphics/MB

        if (ImGui::CollapsingHeader("Particle Effects", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_Framed))
        { // Graphics/PE
            ImGui::Indent(indent);
            // ==================

            bool checkbox = Settings::Particles_Enable;
            ImGui::Checkbox("Enable Particles", &checkbox);
            Settings::Particles_Enable = checkbox;

            bool pause = Settings::PauseSim;
            ImGui::Checkbox("Pause Simulation", &pause);
            Settings::PauseSim = pause;

            bool tiled = Settings::EnableTiledRendering;
            ImGui::Checkbox("Tiled Rendering", &tiled);
            Settings::EnableTiledRendering = tiled;

            bool spriteSort = Settings::EnableSpriteSort;
            ImGui::Checkbox("Sort Sprites", &spriteSort);
            Settings::EnableSpriteSort = spriteSort;

            // HDR Debug Mode
            ImGui::Text("Tiled Sample Rate");

            int mode = Settings::TiledRes;

            ImGui::RadioButton("High-Res", &mode, 0); ImGui::SameLine();
            ImGui::RadioButton("Low-Res", &mode, 1); ImGui::SameLine();
            ImGui::RadioButton("Dynamic", &mode, 2);

            Settings::TiledRes = mode;

            float res = Settings::DynamicResLevel;
            ImGui::SliderFloat("Dynamic Resolution Cutoff", &res, -4.0f, 4.0f, "%.3f");
            Settings::DynamicResLevel = res;

            float mip = Settings::MipBias;
            ImGui::SliderFloat("Mip bias", &mip, -4.0f, 4.0f, "%.3f");
            Settings::MipBias = mip;
        	
            // ===================
            ImGui::Indent(-indent);
        } // Graphics/PE

        if (ImGui::CollapsingHeader("SSAO", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_Framed))
        { // Graphics/SSAO
            ImGui::Indent(indent);
            // ==================

            bool enable = Settings::SSAO_Enable;
            ImGui::Checkbox("Enable SSAO", &enable);
            Settings::SSAO_Enable = enable;

            bool debug = Settings::SSAO_DebugDraw;
            ImGui::Checkbox("Enable Debug", &debug);
            Settings::SSAO_DebugDraw = debug;

            bool async = Settings::AsyncCompute;
            ImGui::Checkbox("Enable Async Compute", &async);
            Settings::AsyncCompute = async;

            bool linearz = Settings::ComputeLinearZ;
            ImGui::Checkbox("Always Linearize Z", &linearz);
            Settings::ComputeLinearZ = linearz;

        	
            // Quality level
            ImGui::Text("Quality level");

            int level = Settings::SSAO_QualityLevel;

            ImGui::PushItemWidth(-1);
            ImGui::ListBox("Quality Level", &level, Settings::QualityLabels, 5, 5);
            ImGui::PopItemWidth();

            Settings::SSAO_QualityLevel = level;


            float filter = Settings::NoiseFilterTolerance;
            ImGui::SliderFloat("Noise Filter Threshold (log10)", &filter, -8.0f, 0.0f, "%.3f");
            Settings::NoiseFilterTolerance = filter;

            float blur = Settings::BlurTolerance;
            ImGui::SliderFloat("Blur Tolerance (log10)", &blur, -8.0f, -1.0f, "%.3f");
            Settings::BlurTolerance = blur;

            float upsample = Settings::UpsampleTolerance;
            ImGui::SliderFloat("Upsample Tolerance (log10)", &upsample, -12.0f, -1.0f, "%.3f");
            Settings::UpsampleTolerance = upsample;

            float reject = Settings::RejectionFalloff;
            ImGui::SliderFloat("Rejection Falloff (rcp)", &reject, 1.0f, 10.0f, "%.1f");
            Settings::RejectionFalloff = reject;

            float accen = Settings::Accentuation;
            ImGui::SliderFloat("Accentuation", &accen, 0.0f, 1.0f, "%.1f");
            Settings::Accentuation = accen;

            int hier = Settings::HierarchyDepth;
            ImGui::SliderInt("Hierarchy Depth", &hier, 1, 4);
            Settings::HierarchyDepth = hier;
        	
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

        bool vsync = Settings::EnableVSync;
        ImGui::Checkbox("Enable VSync", &vsync);
        Settings::EnableVSync = vsync;

        bool limithz = Settings::LimitTo30Hz;
        ImGui::Checkbox("Limit to 30Hz (halves fps on VSync)", &limithz);
        Settings::LimitTo30Hz = limithz;

        bool frames = Settings::DropRandomFrames;
        ImGui::Checkbox("Drop Random Frames", &frames);
        Settings::DropRandomFrames = frames;

        // ===================
        ImGui::Indent(-indent);
    } // Timing

    if (ImGui::CollapsingHeader("VR", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_Framed))
    { // VR
        ImGui::Indent(indent);
        // ==================

        bool checkbox = Settings::VRDepthStencil;
        ImGui::Checkbox("Use VR DepthStencil", &checkbox);
        Settings::VRDepthStencil = checkbox;

        // ===================
        ImGui::Indent(-indent);
    } // VR
	
    { // Other

    	// Test?

    	// TODO: How to solve these?
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