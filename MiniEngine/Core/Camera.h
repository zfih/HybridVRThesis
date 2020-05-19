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

#pragma once

#include "VectorMath.h"
#include "Math/Frustum.h"
#include "VR.h"
#include "CameraType.h"

namespace Math
{
    class BaseCamera
    {
    public:

        // Call this function once per frame and after you've changed any state.  This
        // regenerates all matrices.  Calling it more or less than once per frame will break
        // temporal effects and cause unpredictable results.
        void Update();

        // Public functions for controlling where the camera is and its orientation
        void SetEyeAtUp( Vector3 eye, Vector3 at, Vector3 up );
        void SetLookDirection( Vector3 forward, Vector3 up );
        void SetRotation( Quaternion basisRotation );
        void SetPosition( Vector3 worldPos );
        void SetTransform( const AffineTransform& xform );
        void SetTransform( const OrthogonalTransform& xform );

        const Quaternion GetRotation() const { return m_CameraToWorld.GetRotation(); }
        const Vector3 GetRightVec() const { return m_Basis.GetX(); }
        const Vector3 GetUpVec() const { return m_Basis.GetY(); }
        const Vector3 GetForwardVec() const { return -m_Basis.GetZ(); }
        const Vector3 GetPosition() const { return m_CameraToWorld.GetTranslation(); }

        // Accessors for reading the various matrices and frusta
        const Matrix4& GetViewMatrix() const { return m_ViewMatrix; }
        const Matrix4& GetProjMatrix() const { return m_ProjMatrix; }
        const Matrix4& GetViewProjMatrix() const { return m_ViewProjMatrix; }
        const Matrix4& GetReprojectionMatrix() const { return m_ReprojectMatrix; }
        const Frustum& GetViewSpaceFrustum() const { return m_FrustumVS; }
        const Frustum& GetWorldSpaceFrustum() const { return m_FrustumWS; }

    protected:  

        BaseCamera() : m_CameraToWorld(kIdentity), m_Basis(kIdentity) {}

        void SetProjMatrix( const Matrix4& ProjMat ) { m_ProjMatrix = ProjMat; }

        OrthogonalTransform m_CameraToWorld;

        // Redundant data cached for faster lookups.
        Matrix3 m_Basis;

        // Transforms homogeneous coordinates from world space to view space.  In this case, view space is defined as +X is
        // to the right, +Y is up, and -Z is forward.  This has to match what the projection matrix expects, but you might
        // also need to know what the convention is if you work in view space in a shader.
        Matrix4 m_ViewMatrix;        // i.e. "World-to-View" matrix

        // The projection matrix transforms view space to clip space.  Once division by W has occurred, the final coordinates
        // can be transformed by the viewport matrix to screen space.  The projection matrix is determined by the screen aspect 
        // and camera field of view.  A projection matrix can also be orthographic.  In that case, field of view would be defined
        // in linear units, not angles.
        Matrix4 m_ProjMatrix;        // i.e. "View-to-Projection" matrix

        // A concatenation of the view and projection matrices.
        Matrix4 m_ViewProjMatrix;    // i.e.  "World-To-Projection" matrix.

        // The view-projection matrix from the previous frame
        Matrix4 m_PreviousViewProjMatrix;

        // Projects a clip-space coordinate to the previous frame (useful for temporal effects).
        Matrix4 m_ReprojectMatrix;

        Frustum m_FrustumVS;        // View-space view frustum
        Frustum m_FrustumWS;        // World-space view frustum

    };

    struct ScreenTextureData
    {
        // Screen Texture quad buffers
        StructuredBuffer m_Buffer[2];

        Graphics::QuadPos m_QuadPos[2];

        // Render to Quad PSO
        GraphicsPSO m_PSO;

        RootSignature m_RootSignature;
    };

    class Camera : public BaseCamera
    {
    public:
        Camera();

        // Controls the view-to-projection matrix
        void SetPerspectiveMatrix( float verticalFovRadians, float aspectHeightOverWidth, float nearZClip, float farZClip );
        void SetFOV( float verticalFovInRadians ) { m_VerticalFOV = verticalFovInRadians; UpdateProjMatrix(); }
        void SetAspectRatio( float heightOverWidth ) { m_AspectRatio = heightOverWidth; UpdateProjMatrix(); }
        void SetZRange( float nearZ, float farZ) { m_NearClip = nearZ; m_FarClip = farZ; UpdateProjMatrix(); }
        void ReverseZ( bool enable ) { m_ReverseZ = enable; UpdateProjMatrix(); }
        void UpdateVRPoseMat(XMMATRIX poseMat);
        void SetVRViewProjMatrices(XMMATRIX view, XMMATRIX proj);

        float GetFOV() const { return m_VerticalFOV; }
        float GetNearClip() const { return m_NearClip; }
        float GetFarClip() const { return m_FarClip; }
        float GetClearDepth() const { return m_ReverseZ ? 0.0f : 1.0f; }

    private:

        void UpdateProjMatrix( void );

        float m_VerticalFOV;            // Field of view angle in radians
        float m_AspectRatio;
        float m_NearClip;
        float m_FarClip;
        bool m_ReverseZ;                // Invert near and far clip distances so that Z=0 is the far plane
    };

    inline void BaseCamera::SetEyeAtUp( Vector3 eye, Vector3 at, Vector3 up )
    {
        SetLookDirection(at - eye, up);
        SetPosition(eye);
    }

    inline void BaseCamera::SetPosition( Vector3 worldPos )
    {
        m_CameraToWorld.SetTranslation( worldPos );
    }

    inline void BaseCamera::SetTransform( const AffineTransform& xform )
    {
        // By using these functions, we rederive an orthogonal transform.
        SetLookDirection(-xform.GetZ(), xform.GetY());
		float posModifier;
		if (VR::GetHMD()) // TODO: Have setting for this we can check
		{
			posModifier = 100.0f;
		}
		else
		{
			posModifier = 1.0f;
		}
        SetPosition(posModifier * xform.GetTranslation());
    }

    inline void BaseCamera::SetRotation( Quaternion basisRotation )
    {
        m_CameraToWorld.SetRotation(Normalize(basisRotation));
        m_Basis = Matrix3(m_CameraToWorld.GetRotation());
    }

    inline Camera::Camera() : m_ReverseZ(true)
    {
        SetPerspectiveMatrix( XM_PIDIV4, 9.0f / 16.0f, 1.0f, 1000.0f );
    }

    inline void Camera::SetPerspectiveMatrix( float verticalFovRadians, float aspectHeightOverWidth, float nearZClip, float farZClip )
    {
        m_VerticalFOV = verticalFovRadians;
        m_AspectRatio = aspectHeightOverWidth;
        m_NearClip = nearZClip;
        m_FarClip = farZClip;

        UpdateProjMatrix();

        m_PreviousViewProjMatrix = m_ViewProjMatrix;
    }

	class VRCamera : public Camera
    {
    public:
        VRCamera();

        static const int num_eyes = 2;

        // Initialize vector of count cameras, we never want more or less
		// and we want them to be initialized.
        std::vector<Camera> m_cameras = std::vector<Camera>(Cam::kCount);
        //Camera* m_centerCamera;

		XMMATRIX m_HMDPoseMat;
		XMMATRIX m_eyeToHead[Cam::kCount];
		XMMATRIX m_eyeProj[Cam::kCount];
		
		struct ProjectionValues
		{
			float left;
			float right;
			float top;
			float bottom;
		};
		ProjectionValues m_projVals[Cam::kCount];
		float m_IPD;
		float m_Zc;

        Camera* operator[](const UINT i) noexcept { return &m_cameras[i]; }
    	
		void GetHMDProjVals(vr::EVREye eye);
		void SetCenterProjVals(float midPlane);
		Matrix4 CustomProj(Cam::CameraType cam, float nearFloat, float farFloat);
		void Setup(float nearPlane, float midPlane,
				   float farPlane, bool reverseZ, ScreenTextureData& ScreenTextureData);
        void Update();

        // Public functions for controlling where the camera is and its orientation
        void SetEyeAtUp(Vector3 eye, Vector3 at, Vector3 up)
        {
            m_cameras[Cam::kLeft].SetEyeAtUp(eye, at, up);
            m_cameras[Cam::kRight].SetEyeAtUp(eye, at, up);
            m_cameras[Cam::kCenter].SetEyeAtUp(eye, at, up);
            BaseCamera::SetEyeAtUp(eye, at, up);
        }
        void SetLookDirection(Vector3 forward, Vector3 up)
        {
            m_cameras[Cam::kLeft].SetLookDirection(forward, up);
            m_cameras[Cam::kRight].SetLookDirection(forward, up);
            m_cameras[Cam::kCenter].SetLookDirection(forward, up);
            BaseCamera::SetLookDirection(forward, up);
        }
        void SetRotation(Quaternion basisRotation)
        {
            m_cameras[Cam::kLeft].SetRotation(basisRotation);
            m_cameras[Cam::kRight].SetRotation(basisRotation);
            m_cameras[Cam::kCenter].SetRotation(basisRotation);
            BaseCamera::SetRotation(basisRotation);
        }
        void SetPosition(Vector3 worldPos)
        {	
            // NOTE! 0.6332 (63.32 mm) is what Lasse has set his IPD to on his HP WMR
            m_cameras[Cam::kLeft].SetPosition(
                worldPos + 
                m_cameras[Cam::kLeft].GetRotation() *
                Vector3 { -0.6332f, 0.0f, 0.0f });
            m_cameras[Cam::kRight].SetPosition(
                worldPos + 
                m_cameras[Cam::kRight].GetRotation() *
                Vector3 { 0.6332f, 0.0f, 0.0f });
            m_cameras[Cam::kCenter].SetPosition(worldPos);
            BaseCamera::SetPosition(worldPos);
        }
        void SetTransform(const AffineTransform& xform)
        {
            m_cameras[Cam::kLeft].SetTransform(xform);
            m_cameras[Cam::kRight].SetTransform(xform);
            m_cameras[Cam::kCenter].SetTransform(xform);

            SetPosition(xform.GetTranslation());

            BaseCamera::SetTransform(xform);
        }

        //const Quaternion GetRotation() const { return m_centerCamera->GetRotation(); }
        //const Vector3 GetRightVec() const { return m_centerCamera->GetRightVec(); }
        //const Vector3 GetUpVec() const { return m_centerCamera->GetUpVec(); }
        //const Vector3 GetForwardVec() const { return m_centerCamera->GetForwardVec(); }
        //const Vector3 GetPosition() const { return m_centerCamera->GetPosition(); }

        //// Accessors for reading the various matrices and frusta
        //const Matrix4& GetViewMatrix() const { return m_centerCamera->GetViewMatrix(); }
        //const Matrix4& GetProjMatrix() const { return m_centerCamera->GetProjMatrix(); }
        //const Matrix4& GetViewProjMatrix() const { return m_centerCamera->GetViewProjMatrix(); }
        //const Matrix4& GetReprojectionMatrix() const { return m_centerCamera->GetReprojectionMatrix(); }
        //const Frustum& GetViewSpaceFrustum() const { return m_centerCamera->GetViewSpaceFrustum(); }
        //const Frustum& GetWorldSpaceFrustum() const { return m_centerCamera->GetWorldSpaceFrustum(); }

        // Controls the view-to-projection matrix
        void SetPerspectiveMatrix(float verticalFovRadians, float aspectHeightOverWidth, float nearZClip, float farZClip)
        {
            m_cameras[Cam::kLeft].SetPerspectiveMatrix(verticalFovRadians, aspectHeightOverWidth, nearZClip, farZClip);
            m_cameras[Cam::kRight].SetPerspectiveMatrix(verticalFovRadians, aspectHeightOverWidth, nearZClip, farZClip);
            m_cameras[Cam::kCenter].SetPerspectiveMatrix(verticalFovRadians, aspectHeightOverWidth, nearZClip, farZClip);
            Camera::SetPerspectiveMatrix(verticalFovRadians, aspectHeightOverWidth, nearZClip, farZClip);
        }
        void SetFOV(float verticalFovInRadians)
        {
            m_cameras[Cam::kLeft].SetFOV(verticalFovInRadians);
            m_cameras[Cam::kRight].SetFOV(verticalFovInRadians);
            m_cameras[Cam::kCenter].SetFOV(verticalFovInRadians);
            Camera::SetFOV(verticalFovInRadians);
        }
        void SetAspectRatio(float heightOverWidth)
        {
            m_cameras[Cam::kLeft].SetAspectRatio(heightOverWidth);
            m_cameras[Cam::kRight].SetAspectRatio(heightOverWidth);
            m_cameras[Cam::kCenter].SetAspectRatio(heightOverWidth);
            Camera::SetAspectRatio(heightOverWidth);
        }
        void SetZRange(float nearZ, float farZ)
        {
            m_cameras[Cam::kLeft].SetZRange(nearZ, farZ);
            m_cameras[Cam::kRight].SetZRange(nearZ, farZ);
            m_cameras[Cam::kCenter].SetZRange(nearZ, farZ);
            Camera::SetZRange(nearZ, farZ);
        }
        void ReverseZ(bool enable)
        {
            m_cameras[Cam::kLeft].ReverseZ(enable);
            m_cameras[Cam::kRight].ReverseZ(enable);
            m_cameras[Cam::kCenter].ReverseZ(enable);
            Camera::ReverseZ(enable);
        }
        void UpdateVRPoseMat(XMMATRIX poseMat)
        {
            m_cameras[Cam::kLeft].UpdateVRPoseMat(poseMat);
            m_cameras[Cam::kRight].UpdateVRPoseMat(poseMat);
            m_cameras[Cam::kCenter].UpdateVRPoseMat(poseMat);
        }
        void SetVRViewProjMatrices(XMMATRIX view, XMMATRIX proj)
        {
            m_cameras[Cam::kLeft].SetVRViewProjMatrices(view, proj);
            m_cameras[Cam::kRight].SetVRViewProjMatrices(view, proj);
            m_cameras[Cam::kCenter].SetVRViewProjMatrices(view, proj);
        }

        /*float GetFOV() const { return m_centerCamera->GetFOV(); }
        float GetNearClip() const { return m_centerCamera->GetNearClip(); }
        float GetFarClip() const { return m_centerCamera->GetFarClip(); }
        float GetClearDepth() const { return m_centerCamera->GetClearDepth(); }*/
    };
	
} // namespace Math
