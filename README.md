# Hybrid Rendering VR Master Thesis

This repository is used for the making of the Hybrid Rendering VR engine, used for a master thesis by the authors.
The project uses a fork of the [MiniEngine and Raytracing samples](https://github.com/microsoft/DirectX-Graphics-Samples) from Microsoft.
The fork is made at commit `c02c560` on March 25 2020.

## Requirements

For running: 
 - 20-series card with Ray Tracing Support or a card with support for emulating the 20-series' ray tracing support (Albeit at lower framerates). Future AMD ray tracing enabled cards may work but has not been tested.
 - 4th Gen Intel CPU or AMD equivalent or better. 

For VR:
 - SteamVR installed
 - A VR headset
   - The project has only been tested on a HTC Vive Pro and HP Windows Mixed Reality headsets.




## User Guide

### Layout

Each implementation is stored in its own branch to stop branches from impacting each other.:

#### Baseline

The baseline lives on the master branch. You may see the baseline referred to as "master" in the code.

#### TRM

Temporal Resolution Multiplexing branch is called trm_master

May be called TMP in code.

#### Hybrid SSR

hybrid_ssr_master 

Implementation of Hybrid Screen Space reflections by Kostas Anagnostou discussed here: https://interplayoflight.wordpress.com/2019/09/07/hybrid-screen-space-reflections/. The implementation was adapted to utilize the Hardware Accelerated Ray Tracing granted by the Nvidia GTX 20-series.


#### ASR

asr_master. May be called "ASRP" in the code.

#### IAPC

iapc_master

May be called ASRP+ in code

### Changing Scenes

Scene selection is implemented in code and requires recompilation. The available scenes are stored in an enum Scenes with the values ^kSponza, Bistro Exterior, Bistro Interior, and, on some branches, kRuggedSurface. The data for the scenes is available on Google Drive (link is in the Wiki), and they must be stored at the root of the project. The resulting file structure MUST look as follows:

 - HybridVRThesis
   - Assets
     - Models
       - Bistro
       - RuggedSurface
       - Sponza
   - HybridVR
   - Libraries
   - MiniEngine
   - ...

### Settings

Various settings can be changed in the Dear ImGui graphical user interface.

The interface is divided into various categories.

LOD is the name for our group, so the LOD group contains settings relevant only to our thesis.
