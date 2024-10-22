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

#include "pch.h"
#include "VR.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_win32.h"
#include "ImGui/imgui_impl_dx12.h"
#include "SystemTime.h"

namespace GameCore
{
    class IGameApp
    {
    public:
        // This function can be used to initialize application state and will run after essential
        // hardware resources are allocated.  Some state that does not depend on these resources
        // should still be initialized in the constructor such as pointers and flags.
        virtual void Startup( void ) = 0;
        virtual void Cleanup( void ) = 0;

        // Decide if you want the app to exit.  By default, app continues until the 'ESC' key is pressed.
        virtual bool IsDone( void );

        // The update method will be invoked once per frame.  Both state updating and scene
        // rendering should be handled by this method.
        virtual void Update( float deltaT ) = 0;

        // Official rendering pass
        virtual void RenderScene() = 0;

        // Optional UI (overlay) rendering pass.  This is LDR.  The buffer is already cleared.
        virtual void RenderUI( class GraphicsContext& ) {};

        virtual void TakeScreenshot() = 0;
    };
	
    void RunApplication( IGameApp& app, const wchar_t* className );
}

#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
    #define MAIN_FUNCTION()  int wmain(/*int argc, wchar_t** argv*/)
#else
    #define MAIN_FUNCTION()  [Platform::MTAThread] int main(Platform::Array<Platform::String^>^)
#endif

#define CREATE_APPLICATION( app_class ) \
    MAIN_FUNCTION() \
    { \
        IGameApp* app = new app_class(); \
        GameCore::RunApplication( *app, L#app_class ); \
        delete app; \
        return 0; \
    }
