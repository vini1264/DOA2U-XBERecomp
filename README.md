# Dead or Alive 2 Ultimate - XBE Recomp

An unofficial native PC port of the original Xbox version of Dead or Alive 2 Ultimate, with extended graphics options and other enhancements.

This port was created using the [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp) tool.

> [!NOTE]
> This project does not include any game assets. You must provide the files from your own legally acquired copy of the game to install or build.

## Installation
The latest version of [DOA2U-XBERecomp](https://github.com/vini1264/DOA2U-XBERecomp/releases) can be found in the Releases page.

- Put your game files in the `assets` folder. This can be an ISO, or the extracted data files.
- Run `DOA2U.exe`. The first start compiles the game's code for your PC, this only happens once.
- Recomp Settings can be adjusted in the in-game options menu while the game is running, or through the Settings overlay, which is opened with <kbd>F10</kbd> by default.
<img width="1366" height="768" alt="image" src="https://github.com/user-attachments/assets/136133c1-b925-4442-8bb9-7d047cbd1da0" />

## Screenshots

<table>
  <tr>
    <td><img src="https://github.com/user-attachments/assets/9e6637c2-689b-43d5-82fd-032ec6241631"></td>
    <td><img src="https://github.com/user-attachments/assets/a8c8c817-1b44-4cd0-9939-3367c65d108d"></td>
  </tr>
</table>

## Features

### Display
- **Display Mode** - Switch between Fullscreen, Borderless and Windowed.
- **Resolution** - Lists every mode your monitor reports and defaults to your desktop resolution, replacing the original 640x480 resolution.
- **Render Resolution** - Renders the game at 25% to 400% of the output resolution, allowing supersampling or downscaling independent of the window size.
- **V-Sync** - Reduces screen tearing.

### Graphics
- **Anisotropic Filtering** - Forces the selected anisotropic filtering level on all textures.

### Textures
- **Texture Replacement** - Replaces the game's textures with the .dds or .png images in the textures\replacement folder beside the exe.
- **Texture Dumping** - Saves every texture the game uses into the textures\dumps folder beside the exe.
- **Dump Format** - Choose between PNG or DDS, which keeps the original compression and mipmaps.

### Controller
- Supports Xbox, PlayStation and Switch controllers, with hot plugging and rumble.

### Miscellaneous
- **Boot Splash** - Place a `splash.png` (or `.bmp`) beside the exe to display a splash screen at launch.
- **Unlock secret characters** - Option to unlock Bayman, Hitomi and Tengu, located in Recomp Settings.

## Warning
Uncapping the framerate speeds up the game, so it's recommended to keep the framerate option at the default 60fps

## System Requirements

### Minimum
- **OS**
  - Windows 10 64-bit
- **CPU** with 2 cores and support for SSE4.2 and POPCNT
  - Intel Nehalem (Intel Core 1st Generation)
  - AMD Bulldozer (AMD FX series)
- **GPU** with support for Direct3D 12
  - NVIDIA GeForce GT 630 (Kepler)
  - AMD Radeon HD 7750
  - Intel HD Graphics 510 (Skylake)
- **Memory**
  - 4 GB
- **Storage**
  - 4 GB for the game files, saves and shader cache

## Building

### 1. Clone the Repository
Clone this repository using [Git](https://git-scm.com/).

### 2. Add the Required Game Files
Place your extracted game data files in the `assets` folder. `DOA2.xbe` must be there, with the game's folders beside it.

### 3. Install Dependencies
Install Visual Studio 2022 with **Desktop development with C++**, **C++ Clang tools for Windows** and **C++ CMake tools for Windows** (`build.bat` offers to add the last two).

### 4. Build the Project
Run `build.bat`. The first build recompiles the game and takes a while. The exe is written to `out\build\release`.

## Credits

- [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp), the recompiler and runtime this is built on.
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) for the overridable function layout and instruction hooks.
- [Zydis](https://github.com/zyantific/zydis) for decoding x86 instructions, [toml++](https://github.com/marzer/tomlplusplus) for the configs, [Dear ImGui](https://github.com/ocornut/imgui) for the settings overlay and [SDL](https://github.com/libsdl-org/SDL) for the window, input and controllers.
