# Hybrid Rendering VR Master Thesis
This repository is used for the making of the Hybrid Rendering VR engine, used for a master thesis by the authors.
The project uses a fork of the [MiniEngine and Raytracing samples](https://github.com/microsoft/DirectX-Graphics-Samples) from Microsoft.
The fork is made at commit `c02c560` on March 25 2020.


## MiniEngine: A DirectX 12 Engine Starter Kit
In addition to the samples, we are announcing the first DirectX 12 preview release of the MiniEngine.

It came from a desire to quickly dive into graphics and performance experiments.  We knew we would need some basic building blocks whenever starting a new 3D app, and we had already written these things at countless previous gigs.  We got tired of reinventing the wheel, so we established our own core library of helper classes and platform abstractions.  We wanted to be able to create a new app by writing just the Init(), Update(), and Render() functions and leveraging as much reusable code as possible.  Today our core library has been redesigned for DirectX 12 and aims to serve as an example of efficient API usage.  It is obviously not exhaustive of what a game engine needs, but it can serve as the cornerstone of something new.  You can also borrow whatever useful code you find.

### Some features of MiniEngine
* High-quality anti-aliased text rendering
* Real-time CPU and GPU profiling
* User-controlled variables
* Game controller, mouse, and keyboard input
* A friendly DirectXMath wrapper
* Perspective camera supporting traditional and reversed Z matrices
* Asynchronous DDS texture loading and ZLib decompression
* Large library of shaders
* Easy shader embedding via a compile-to-header system
* Easy render target, depth target, and unordered access view creation
* A thread-safe GPU command context system (WIP)
* Easy-to-use dynamic constant buffers and descriptor tables

## Requirements
* Windows 10 with the May 2019 Update
* [Visual Studio 2019](https://www.visualstudio.com/) with the [Windows 10 May 2019 Update SDK (18362)](https://developer.microsoft.com/en-US/windows/downloads/windows-10-sdk)

## Related links
* [DirectX API documentation](https://docs.microsoft.com/en-us/windows/win32/directx)
* [PIX on Windows](https://devblogs.microsoft.com/pix/documentation/)
* [D3DX12 (the D3D12 Helper Library)](https://github.com/Microsoft/DirectX-Graphics-Samples/tree/master/Libraries/D3DX12)
* [D3D12 Raytracing Fallback Layer](https://github.com/Microsoft/DirectX-Graphics-Samples/tree/master/Libraries/D3D12RaytracingFallback)
* [D3D12 Residency Starter Library](https://github.com/Microsoft/DirectX-Graphics-Samples/tree/master/Libraries/D3DX12Residency)
* [D3D12 MultiGPU Starter Library](https://github.com/Microsoft/DirectX-Graphics-Samples/tree/master/Libraries/D3DX12AffinityLayer)
* [DirectX Tool Kit](https://github.com/Microsoft/DirectXTK12)
* [D3DDred debugger extension](https://github.com/Microsoft/DirectX-Debugging-Tools)