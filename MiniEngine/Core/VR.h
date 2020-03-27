#pragma once

#include "string"
#include "openvr.h"


namespace VirtualReality
{
	class VRSystem
	{
	public:
		VRSystem()
		{
			TryInitVR();
		}

		bool IsVRAvailable() { return m_HMDPresent && m_RuntimeInstalled; }
		vr::IVRSystem* GetHMD() { return m_HMD; }
		
	private:
		bool TryInitVR();

		vr::IVRSystem* m_HMD;

		bool m_HMDPresent;
		bool m_RuntimeInstalled;
		std::string m_driver;
		std::string m_display;
	};
}