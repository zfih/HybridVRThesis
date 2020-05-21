#pragma once

namespace Settings
{
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
	
}