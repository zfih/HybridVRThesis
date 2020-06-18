//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author:  James Stanard 
//

#include "pch.h"
#include "Camera.h"
#include "BufferManager.h"
#include <cmath>

using namespace Math;

void BaseCamera::SetLookDirection( Vector3 forward, Vector3 up )
{
    // Given, but ensure normalization
    Scalar forwardLenSq = LengthSquare(forward);
    forward = Select(forward * RecipSqrt(forwardLenSq), -Vector3(kZUnitVector), forwardLenSq < Scalar(0.000001f));

    // Deduce a valid, orthogonal right vector
    Vector3 right = Cross(forward, up);
    Scalar rightLenSq = LengthSquare(right);
    right = Select(right * RecipSqrt(rightLenSq), Quaternion(Vector3(kYUnitVector), -XM_PIDIV2) * forward, rightLenSq < Scalar(0.000001f));

    // Compute actual up vector
    up = Cross(right, forward);

    // Finish constructing basis
    m_Basis = Matrix3(right, up, -forward);
    m_CameraToWorld.SetRotation(Quaternion(m_Basis));
}

void BaseCamera::Update()
{
    m_PreviousViewProjMatrix = m_ViewProjMatrix;

    m_ViewMatrix = Matrix4(~m_CameraToWorld);
    m_ViewProjMatrix = m_ProjMatrix * m_ViewMatrix;
    m_ReprojectMatrix = m_PreviousViewProjMatrix * Invert(GetViewProjMatrix());

    m_FrustumVS = Frustum( m_ProjMatrix );
    m_FrustumWS = m_CameraToWorld * m_FrustumVS;
}


void Camera::UpdateVRPoseMat(XMMATRIX poseMat)
{
	SetTransform(AffineTransform{ poseMat });
}

void Camera::SetVRViewProjMatrices(XMMATRIX view, XMMATRIX proj)
{
    m_ViewMatrix = Matrix4(view);
    m_ProjMatrix = Matrix4(proj);
}

void Camera::UpdateProjMatrix( void )
{
    float Y = 1.0f / std::tanf( m_VerticalFOV * 0.5f );
    float X = Y * m_AspectRatio;

    float Q1, Q2;

    // ReverseZ puts far plane at Z=0 and near plane at Z=1.  This is never a bad idea, and it's
    // actually a great idea with F32 depth buffers to redistribute precision more evenly across
    // the entire range.  It requires clearing Z to 0.0f and using a GREATER variant depth test.
    // Some care must also be done to properly reconstruct linear W in a pixel shader from hyperbolic Z.
    if (m_ReverseZ)
    {
        Q1 = m_NearClip / (m_FarClip - m_NearClip);
        Q2 = Q1 * m_FarClip;
    }
    else
    {
        Q1 = m_FarClip / (m_NearClip - m_FarClip);
        Q2 = Q1 * m_NearClip;
    }

    SetProjMatrix( Matrix4(
        Vector4( X, 0.0f, 0.0f, 0.0f ),
        Vector4( 0.0f, Y, 0.0f, 0.0f ),
        Vector4( 0.0f, 0.0f, Q1, -1.0f ),
        Vector4( 0.0f, 0.0f, Q2, 0.0f )
        ) );
}

VRCamera::VRCamera()
{
}

void VRCamera::Update()
{
	if (VR::GetHMD()) // TODO: Have setting for this we can check
	{
		m_HMDPoseMat = VR::GetHMDPos();

		for (int i = 0; i < Cam::kCount; ++i)
		{
			m_cameras[i].SetVRViewProjMatrices(m_eyeToHead[i] * m_HMDPoseMat
				* XMMatrixTranslationFromVector(VROffset), m_eyeProj[i]);
			m_cameras[i].SetTransform(AffineTransform(m_eyeToHead[i] * 
				m_HMDPoseMat * XMMatrixTranslationFromVector(VROffset)));
			m_cameras[i].Update();
		}
	}
	else
	{
		for (int i = 0; i < Cam::kCount; ++i)
		{
			m_cameras[i].Update();
		}
	}
	BaseCamera::Update();
}

void VRCamera::GetHMDProjVals(vr::EVREye eye)
{
	VR::g_HMD->GetProjectionRaw(eye, &m_projVals[eye].left,
		&m_projVals[eye].right, &m_projVals[eye].top, &m_projVals[eye].bottom);
}

void VRCamera::SetCenterProjVals(float midPlane)
{
	auto g = [&](float m, float a, float b)
	{
		return Pow(m - m_Zc, -1.0f) * (a * m + m_Zc) + b;
	};

	float tLX = -m_IPD / 2;
	float tRX = m_IPD / 2;

	ProjectionValues &c = m_projVals[Cam::kCenter];
	ProjectionValues &l = m_projVals[Cam::kLeft];
	ProjectionValues &r = m_projVals[Cam::kRight];

#if (0)
	c.left = Max(
		g(midPlane, l.right, tLX),
		g(midPlane, r.right, tRX));
	
	c.right = Min(
		g(midPlane, l.left, tLX),
		g(midPlane, r.left, tRX));

	c.bottom = Min(
		g(midPlane, l.top, 0),
		g(midPlane, r.top, 0));

	c.top = Max(
		g(midPlane, l.bottom, 0),
		g(midPlane, r.bottom, 0));

#else
	c.left = Min(
		g(midPlane, l.left, tLX),
		g(midPlane, r.left, tRX));

	c.right = Max(
		g(midPlane, l.right, tLX),
		g(midPlane, r.right, tRX));

	c.bottom = Max(
		g(midPlane, l.bottom, 0),
		g(midPlane, r.bottom, 0));

	c.top = Min(
		g(midPlane, l.top, 0),
		g(midPlane, r.top, 0));
#endif
}

Matrix4 VRCamera::CustomProj(Cam::CameraType cam, 
	float nearFloat, float farFloat)
{
	float idx = 1.0f / (m_projVals[cam].right - m_projVals[cam].left);
	float idy = 1.0f / (m_projVals[cam].bottom - m_projVals[cam].top);
	float Q1 = nearFloat / (farFloat - nearFloat);
	float Q2 = Q1 * farFloat;
	float sx = m_projVals[cam].right + m_projVals[cam].left;
	float sy = m_projVals[cam].bottom + m_projVals[cam].top;

	return Matrix4(
		Vector4(2 * idx,  0.0f,     0.0f,  0.0f),
		Vector4(0.0f,     2 * idy,  0.0f,  0.0f),
		Vector4(sx * idx, sy * idy, Q1,   -1.0f),
		Vector4(0.0f,     0.0f,     Q2,    0.0f)
	);

	/*float idz = 1.0f / (farFloat - nearFloat);

	return Matrix4::Transpose(Matrix4(
		Vector4(2 * idx, 0.0f, 0.0f, 0.0f),
		Vector4(0.0f, 2 * idy, 0.0f, 0.0f),
		Vector4(sx * idx, sy * idy, -farFloat * idz, -1.0f),
		Vector4(0.0f, 0.0f, -farFloat * nearFloat * idz, 0.0f)
	));*/
}

float calcIPD(XMMATRIX leftEyeToHead, XMMATRIX rightEyeToHead)
{
	Matrix4 leftHeadToEye = Matrix4(XMMatrixInverse(nullptr, leftEyeToHead));
	Matrix4 rightHeadToEye = Matrix4(XMMatrixInverse(nullptr, rightEyeToHead));
	Vector4 center(0, 0, 0, 1);

	Vector4 left = leftHeadToEye * center;
	left /= left.GetW();

	Vector4 right = rightHeadToEye * center;
	right /= right.GetW();

	Vector4 dist = right - left;
	float x = dist.GetX();
	float y = dist.GetY();
	float z = dist.GetZ();
	return Sqrt(Pow(x, 2.0f) + Pow(y, 2.0f) + Pow(z, 2.0f));
}

void VRCamera::Setup(float nearPlane, float midPlane,
	float farPlane, bool reverseZ, ScreenTextureData& Data)
{
	//TODO: Maybe find a better value.
	const float BlendRegionSize = midPlane / 1.1f;
    VROffset = Vector3(0, 0, 0);
	if (VR::GetHMD()) // TODO: Have setting for this we can check
	{
		m_HMDPoseMat = VR::GetHMDPos();

		for (int i = 0; i < Cam::kCount - 1; i++)
		{
			m_cameras[i].ReverseZ(reverseZ);
			m_eyeToHead[i] = VR::GetEyeToHeadTransform(vr::EVREye(i));
			GetHMDProjVals(vr::EVREye(i));
			m_eyeProj[i] = CustomProj(Cam::CameraType(i), nearPlane, midPlane + BlendRegionSize);
		}

		m_IPD = calcIPD(m_eyeToHead[0], m_eyeToHead[1]);
		m_Zc = Min(m_IPD / (2 * m_projVals[Cam::kLeft].left),
			      -m_IPD / (2 * m_projVals[Cam::kRight].right));

		m_cameras[Cam::kCenter].ReverseZ(reverseZ);
		m_eyeToHead[Cam::kCenter] = XMMatrixTranslation(0, 0, m_Zc);
		SetCenterProjVals(midPlane);
		m_eyeProj[Cam::kCenter] = 
			CustomProj(Cam::kCenter, midPlane - BlendRegionSize, farPlane);
	}
	else
	{
		m_cameras[Cam::kLeft].SetZRange(nearPlane, midPlane + BlendRegionSize);
		m_cameras[Cam::kRight].SetZRange(nearPlane, midPlane + BlendRegionSize);
		m_cameras[Cam::kCenter].SetZRange(midPlane - BlendRegionSize, farPlane);
	}

	this->Update();

	Matrix4 MonoToStereoMappings[num_eyes];

	MonoToStereoMappings[Cam::kLeft] =
		m_cameras[Cam::kLeft].GetProjMatrix()
		*
		m_cameras[Cam::kLeft].GetViewMatrix()
		*
		/*Matrix4::MakeTranslate({ 10, 0, -1 })
		**/
		m_cameras[Cam::kCenter].GetViewMatrix().Inverse()
		*
		m_cameras[Cam::kCenter].GetProjMatrix().Inverse()
		;

	MonoToStereoMappings[Cam::kRight] =
		m_cameras[Cam::kRight].GetProjMatrix()
		*
		m_cameras[Cam::kRight].GetViewMatrix()
		*
		m_cameras[Cam::kCenter].GetViewMatrix().Inverse()
		*
		m_cameras[Cam::kCenter].GetProjMatrix().Inverse()
		;
	
#define UNPACKV4(v) v.GetX(), v.GetY(), v.GetZ(), v.GetW()

	auto createScreenQuad = [&](Cam::CameraType Camera, 
		LPCWSTR ResourceName ) -> void
	{
		Matrix4 mapping = MonoToStereoMappings[Camera];

		// NOTE! translation is hard coded based on Lasses IPD of 63.32 mm
		float translation = 0.022f - (midPlane / 20000.0f);
		if (translation < 0.005f)
		{
			translation = 0.005f;
		}

		if(!VR::GetHMD)
		{
			if (Camera == Cam::kLeft)
			{
				mapping = mapping * Matrix4::MakeTranslate({ translation, 0, 0 });
			}
			else
			{
				mapping = mapping * Matrix4::MakeTranslate({ -translation, 0, 0 });
			}
		}

		float depth = 1;
		Vector4 tl = mapping * Vector4(-1, 1, depth, 1);
		tl /= tl.GetW();

		Vector4 tr = mapping * Vector4(1, 1, depth, 1);
		tr /= tr.GetW();

		Vector4 bl = mapping * Vector4(-1, -1, depth, 1);
		bl /= bl.GetW();
		
		Vector4 br = mapping * Vector4(1, -1, depth, 1);
		br /= br.GetW();

		float vertices[] =
		{
			// TL
			UNPACKV4(tl), // Position
			0, 0,         // UV

			// BL
			UNPACKV4(bl), // Position
			0, 1,        // UV

			// TR
			UNPACKV4(tr), // Position
			1, 0,       // UV

			// TR
			UNPACKV4(tr), // Position
			1, 0,       // UV

			// BL
			UNPACKV4(bl), // Position
			0, 1,        // UV

			// BR
			UNPACKV4(br), // Position
			1, 1,       // UV
		};

		const int floatsPerVertex = 6;
		const int vertexCount = _countof(vertices) / floatsPerVertex;

		Data.m_Buffer[Camera].Create(ResourceName, vertexCount, 
			floatsPerVertex * sizeof(float), vertices);

		Data.m_QuadPos[Camera].topLeft = tl;
		Data.m_QuadPos[Camera].topRight = tr;
		Data.m_QuadPos[Camera].bottomLeft = bl;
		Data.m_QuadPos[Camera].bottomRight = br;
	};

	createScreenQuad(Cam::kLeft, L"ScreenTexture Quad buffer LEFT");
	createScreenQuad(Cam::kRight,  L"ScreenTexture Quad buffer RIGHT");
}