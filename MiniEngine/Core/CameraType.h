#pragma once

namespace Cam
{
	enum CameraType
	{
		kLeft = 0,
		kRight = 1,
		kCenter = 2,
		kCount
	};
	const int NUM_EYES = 2;
}

inline std::wstring CameraTypeToWString(Cam::CameraType CameraType)
{
	switch (CameraType)
	{
	case Cam::kLeft: return L"Left";
	case Cam::kRight: return L"Right";
	case Cam::kCenter: return L"Center";
	default: throw "Invalid CameraType";
	}
}
