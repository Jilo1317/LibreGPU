# LibreGPU-rc2 | Pre-Alpha
***#Unstable #Experimental***

**LibreGPU** is a game launcher aiming to extend the hardware lifespan of GPUs to game better titles.

### Contents:

- **LibreGPU Runtime:** Expanded execution management, frame-cycle orchestration and rendering optimization decisions.
- **Streamline Engine:** Rendering-change analysis, incremental execution planning and baseline management.
- **Micro-Pixel Unit:** Localized pixel-change detection for incremental updates.
- **Camera Unit:** Camera-motion analysis and rendering-state reuse.
- **LibreGPU Memory Manager:** Resource tracking, VRAM budgeting and deferred reclamation.
- **LibreGPU Desktop Launcher:** Expanded game launcher with profile management, editing, game artwork and executable launching.
- **LibreGPU OpenGL Backend:** Experimental OpenGL 2.1/EGL graphics execution through the M3 simulation backend.
- **LibreGPU GPU Simulation:** Controlled 960×540 graphics scene, framebuffer readback and GPU timing support.
- **LibreGPU Presentation Interposer:** Experimental POSIX `LD_PRELOAD` integration for GLX and SDL presentation-boundary interception.
- **LibreGPU CLI:** Compatibility reporting, system diagnostics, runtime configuration and self-tests.
- **Distribution & Installation:** Component-specific builds, installation, uninstallation and release packaging.

### Build & Validate:

- C11 implementation.
- Modular CLI, GUI, GPU and interposer build targets.
- OpenGL/EGL and SDL2 integration.
- Core regression and profile-store self-tests.
- AddressSanitizer and UndefinedBehaviorSanitizer support.
- GPU simulation and integration-test targets.

**Note:** Experimental pre-alpha milestone. The presentation interposer forwards game presentation operations unchanged; draw-call capture and arbitrary game-engine optimization are not implemented. GPU simulation and timing capabilities may not establish real-game performance gains.
