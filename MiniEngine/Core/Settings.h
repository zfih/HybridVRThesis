#pragma once

#include "SystemTime.h"

namespace Settings
{
	// Profiling
	extern CpuTimer g_NoSyncTimer;
	extern CpuTimer g_ImGUITimer;
	extern CpuTimer g_EyeRenderTimer[2];
	extern CpuTimer g_ShadowRenderTimer;
	extern CpuTimer g_ZPrepassTimer[2];
	extern CpuTimer g_SSAOTimer[2];
	extern CpuTimer g_RaytraceTimer[2];
	// Profiling
	
	// ImGui
	extern BoolVar UseImGui;
	// ImGui

	// Model Viewer
	extern ExpVar SunLightIntensity;
	extern ExpVar AmbientIntensity;
	extern NumVar SunOrientation;
	extern NumVar SunInclination;
	extern NumVar ShadowDimX;
	extern NumVar ShadowDimY;
	extern NumVar ShadowDimZ;
	
	extern BoolVar ShowWaveTileCounts;

	extern enum RaytracingMode;
	extern const char* rayTracingModes[];
	extern EnumVar RayTracingMode;

	extern IntVar m_TestValueSuperDuper;
	// Model Viewer

	// Lighting Grid
	extern IntVar LightGridDim;
	// Lighting Grid

	// FXAA
	extern BoolVar FXAA_Enable;
	extern BoolVar FXAA_DebugDraw;
	extern NumVar FXAA_ContrastThreshold;
	extern NumVar FXAA_SubpixelRemoval;
	extern BoolVar FXAA_ForceOffPreComputedLuma;
	//FXAA


	// TAA
	extern BoolVar TAA_Enable;
	extern NumVar TAA_Sharpness;
	extern NumVar TemporalMaxLerp;
	extern ExpVar TemporalSpeedLimit;
	extern BoolVar TriggerReset;
	// TAA

	// HDR
	extern BoolVar EnableHDR;
	extern ExpVar Exposure;
	extern BoolVar EnableAdaptation;
	extern ExpVar MinExposure;
	extern ExpVar MaxExposure;
	extern NumVar TargetLuminance;
	extern NumVar AdaptationRate;
	extern BoolVar DrawHistogram;
	// HDR
	
	// Bloom
	extern BoolVar BloomEnable;
	extern NumVar BloomThreshold;
	extern NumVar BloomStrength;
	extern NumVar BloomUpsampleFactor;
	extern BoolVar HighQualityBloom;
	// Bloom

	// DOF
	extern BoolVar DOF_Enable;
	extern BoolVar DOF_EnablePreFilter;
	extern BoolVar MedianFilter;
	extern BoolVar MedianAlpha;
	extern NumVar FocalDepth;
	extern NumVar FocalRange;
	extern NumVar ForegroundRange;
	extern NumVar AntiSparkleWeight;
	extern const char* DebugLabels[];
	extern EnumVar DOF_DebugMode;
	extern BoolVar DebugTiles;
	extern BoolVar ForceSlow;
	extern BoolVar ForceFast;
	// DOF

	// Display
	extern NumVar HDRPaperWhite;
	extern NumVar MaxDisplayLuminance;
	extern const char* HDRModeLabels[];
	extern EnumVar HDRDebugMode;

	extern const char* FilterLabels[];
	extern EnumVar UpsampleFilter;
	extern NumVar BicubicUpsampleWeight;
	extern NumVar SharpeningSpread;
	extern NumVar SharpeningRotation;
	extern NumVar SharpeningStrength;
	extern enum DebugZoomLevel;
	extern const char* DebugZoomLabels[];
	extern EnumVar DebugZoom;
	// Display

	// Motion Blur
	extern BoolVar MotionBlur_Enable;
	// Motion Blur

	// VR
	extern BoolVar VRDepthStencil;
	// VR

	// Particles
	extern BoolVar Particles_Enable;
	extern BoolVar PauseSim;
	extern BoolVar EnableTiledRendering;
	extern BoolVar EnableSpriteSort;
	extern const char* ResolutionLabels[];
	extern EnumVar TiledRes;
	extern NumVar DynamicResLevel;
	extern NumVar MipBias;
	// Particles


	// SSAO
	extern BoolVar SSAO_Enable;
	extern BoolVar SSAO_DebugDraw;
	extern BoolVar AsyncCompute;
	extern BoolVar ComputeLinearZ;

	extern enum QualityLevel;
	extern const char* QualityLabels[];
	extern EnumVar SSAO_QualityLevel;

	extern NumVar NoiseFilterTolerance;
	extern NumVar BlurTolerance;
	extern NumVar UpsampleTolerance;
	extern NumVar RejectionFalloff;
	extern NumVar Accentuation;
	extern IntVar HierarchyDepth;
	// SSAO

	// Timing
	extern BoolVar EnableVSync;
	extern BoolVar LimitTo30Hz;
	extern BoolVar DropRandomFrames;
	// Timing


	// LOD
	extern BoolVar MonoStereoCopyToEye;
	extern BoolVar MonoStereoRenderCenter;
	// LOD
}