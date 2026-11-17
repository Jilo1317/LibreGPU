# LibreGPU | GPU Democratization
***#Unstable #Experimental***

**LibreGPU** is a game launcher aiming to extend the hardware lifespan of GPUs to game better titles.

### Contents:

- **LibreGPU Desktop Launcher:** SDL2 and Nuklear-based graphical game launcher with game profiles, executable management and game artwork.
- **LibreGPU Runtime:** Frame-cycle orchestration, rendering decisions, execution management and runtime diagnostics.
- **Streamline Engine:** Graphics workload analysis, rendering-change detection and incremental execution planning.
- **Micro-Pixel Unit:** Localized pixel-change detection for incremental updates.
- **Camera Unit:** Camera-motion analysis and rendering-state reuse.
- **LibreGPU Memory Manager:** Graphics-resource tracking, VRAM budgeting, reference management and deferred reclamation.
- **Simulated Backend:** Controlled graphics execution, scripted scenarios, framebuffer comparisons and runtime validation.
- **OpenGL/EGL Backend:** Experimental native graphics execution through the M3 simulation backend.
- **Presentation Interposer:** Experimental GLX, EGL and SDL presentation-boundary interception through POSIX `LD_PRELOAD`.
- **LibreGPU CLI:** Runtime configuration, game launching, compatibility reporting, system diagnostics and self-tests.
- **Build & Distribution:** Modular CLI, GUI, GPU and interposer targets, installation, uninstallation and release packaging.

### Build & Validate:

**Requirements:** C11 compiler, Make, SDL2 development files for GUI builds, and OpenGL/EGL development libraries for GPU builds.

```bash
# Build
make cli
make

# Build optional GPU and interposer components
make gpu
make interpose

# Validation
make test
make check
make asan
make gpu-check
```

### Info:

- **LibreGPU** is written in C11 and licensed under AGPLv3, currently supports OpenGL.
- Despite its immaturity, if LibreGPU-rc3 works, it shall provide a 30% extra performance gain.
- **LibreGPU-rc4 Alpha** release on Dec 13 2026. **LibreGPU-rc5 Beta** release on Dec 25 2026.
- **LibreGPU-Stable01** release on New Year 2027. **LibreGPU-Stable02 LTS** release on Jan 14 2027.
