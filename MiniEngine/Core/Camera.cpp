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

float testMonoStereoG(float m, float a, float b, float Zc) // TODO: Clean up test functions.
{
	return Pow(m - Zc, -1.0f) * (a * m + Zc) + b;
}

void testCenterProjVals(VRCamera::projectionValues L, VRCamera::projectionValues R, float tLX, float tRX, float midPlane, float Zc, OUT VRCamera::projectionValues& C)
{
	C.left = Max(
		testMonoStereoG(midPlane, L.right, tLX, Zc),
		testMonoStereoG(midPlane, R.right, tRX, Zc));

	C.right = Min(
		testMonoStereoG(midPlane, L.left, tLX, Zc),
		testMonoStereoG(midPlane, R.left, tRX, Zc));

	C.top = Max(
		testMonoStereoG(midPlane, L.bottom, 0, Zc),
		testMonoStereoG(midPlane, R.bottom, 0, Zc));

	C.bottom = Max(
		testMonoStereoG(midPlane, L.top, 0, Zc),
		testMonoStereoG(midPlane, R.top, 0, Zc));
}

Matrix4 testProj(float left, float right, float top, float bottom, float nearFloat, float farFloat)
{
	float idx = 1.0f / (right - left);
	float idy = 1.0f / (bottom - top);
	float Q1 = nearFloat / (farFloat - nearFloat);
	float Q2 = Q1 * farFloat;
	float sx = right + left;
	float sy = bottom + top;

	return Matrix4(
		Vector4(2 * idx,  0.0f,     0.0f, 0.0f),
		Vector4(0.0f,     2 * idy,  0.0f, 0.0f),
		Vector4(sx * idx, sy * idy, Q1,  -1.0f),
		Vector4(0.0f,     0.0f,     Q2,   0.0f)
	);
}

void testFunc()
{
	float IPD = 0.064;
	VRCamera::projectionValues L, R, C;
	L = { -1.391937, 1.247409, -1.464287, 1.468819 };
	R = { -1.246557, 1.398447, -1.472458, 1.465505 };
	float Zc = Min(IPD / (2 * L.left), -IPD / (2 * R.right));
	// The paper calls these tL, tR, and tC, but we don't
	Vector3 vL = (-IPD / 2, 0, 0);
	Vector3 vR = (IPD / 2, 0, 0);
	Vector3 vC = (0, 0, Zc);
	float midPlane = 1.0f;
	testCenterProjVals(L, R, vL.GetX(), vR.GetX(), midPlane, Zc, C);
}

VRCamera::VRCamera()
{
	testFunc();
}

void VRCamera::Update()
{
	if (VR::GetHMD()) // TODO: Have setting for this we can check
	{
		m_HMDPoseMat = VR::GetHMDPos();

		for (int i = 0; i < VRCamera::COUNT; ++i)
		{
			m_cameras[i].SetVRViewProjMatrices(m_eyeToHead[i] * m_HMDPoseMat, m_eyeProj[i]);
			m_cameras[i].SetTransform(AffineTransform(m_eyeToHead[i] * m_HMDPoseMat));
			m_cameras[i].Update();
		}
	}
	else
	{
		for (int i = 0; i < VRCamera::COUNT; ++i)
		{
			m_cameras[i].Update();
		}
	}
}

void VRCamera::GetHMDProjVals(vr::EVREye eye)
{
	VR::g_HMD->GetProjectionRaw(eye, &m_projVals[eye].left,
		&m_projVals[eye].right, &m_projVals[eye].top, &m_projVals[eye].bottom);
}

void VRCamera::SetCenterProjVals(float midPlane)
{
	float tLX = -m_IPD / 2;
	float tRX = m_IPD / 2;

	m_projVals[CENTER].right = Max(
		//TODO: What is tLX in the mono stereo paper?
		MonoStereoG(midPlane, m_projVals[LEFT].right, tLX),
		MonoStereoG(midPlane, m_projVals[RIGHT].right, tRX));

	m_projVals[CENTER].left = Min(
		MonoStereoG(midPlane, m_projVals[LEFT].left, tLX),
		MonoStereoG(midPlane, m_projVals[RIGHT].left, tRX));

	m_projVals[CENTER].bottom = Max(
		MonoStereoG(midPlane, m_projVals[LEFT].bottom, 0),
		MonoStereoG(midPlane, m_projVals[RIGHT].bottom, 0));

	m_projVals[CENTER].top = Max(
		MonoStereoG(midPlane, m_projVals[LEFT].top, 0),
		MonoStereoG(midPlane, m_projVals[RIGHT].top, 0));

	/*m_projVals[CENTER].left = 
		Max(m_projVals[LEFT].left, m_projVals[RIGHT].left);

	m_projVals[CENTER].right = 
		Min(m_projVals[LEFT].right, m_projVals[RIGHT].right);

	m_projVals[CENTER].top = 
		Min(m_projVals[LEFT].top, m_projVals[RIGHT].top);

	m_projVals[CENTER].bottom = 
		Max(m_projVals[LEFT].bottom, m_projVals[RIGHT].bottom);*/
}

Matrix4 VRCamera::CustomProj(CameraType cam, float nearFloat, float farFloat)
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
					 float farPlane, bool reverseZ, Graphics::QuadPos &quad)
{
	const float epsilon = 300.0f;

	if (VR::GetHMD()) // TODO: Have setting for this we can check
	{
		m_HMDPoseMat = VR::GetHMDPos();

		for (int i = 0; i < VRCamera::COUNT - 1; i++)
		{
			m_cameras[i].ReverseZ(reverseZ);
			m_eyeToHead[i] = VR::GetEyeToHeadTransform(vr::EVREye(i));
			GetHMDProjVals(vr::EVREye(i));
			m_eyeProj[i] = CustomProj(CameraType(i), nearPlane, midPlane);
		}

		m_IPD = calcIPD(m_eyeToHead[0], m_eyeToHead[1]);
		m_Zc = Min(m_IPD / (2 * m_projVals[0].left), 
			      -m_IPD / (2 * m_projVals[1].right));

		m_cameras[CENTER].ReverseZ(reverseZ);
		m_eyeToHead[CENTER] = XMMatrixTranslation(0, 0, m_Zc);
		SetCenterProjVals(midPlane);
		m_eyeProj[CENTER] = CustomProj(CENTER, midPlane - epsilon, farPlane);
	}
	else
	{
		m_cameras[LEFT].SetZRange(nearPlane, midPlane);
		m_cameras[RIGHT].SetZRange(nearPlane, midPlane);
		m_cameras[CENTER].SetZRange(midPlane - epsilon, farPlane);
	}

	this->Update();

	Matrix4 CtoL = m_cameras[LEFT].GetProjMatrix() *
		m_cameras[LEFT].GetViewMatrix() *
		Matrix4(XMMatrixInverse(nullptr, m_cameras[CENTER].GetViewMatrix())) *
		Matrix4(XMMatrixInverse(nullptr, m_cameras[CENTER].GetProjMatrix()));
	
	Matrix4 CtoR = m_cameras[LEFT].GetProjMatrix() *
		m_cameras[LEFT].GetViewMatrix() *
		Matrix4(XMMatrixInverse(nullptr, m_cameras[CENTER].GetViewMatrix())) *
		Matrix4(XMMatrixInverse(nullptr, m_cameras[CENTER].GetProjMatrix()));
	
	quad.topLeft = Vector4(-1, -1, 0, 1);
	quad.topRight = Vector4(1, -1, 0, 1);
	quad.bottomLeft = Vector4(-1, 1, 0, 1);
	quad.bottomRight = Vector4(1, 1, 0, 1);
}