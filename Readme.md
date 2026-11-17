# LibreGPU-rc1 | Core
***#Unstable #Experimental***

**LibreGPU** is a game launcher aiming to extend the hardware lifespan of GPUs to game better titles.


### Contents:

- **LibreGPU Runtime:** Evolved frame-cycle orchestration, rendering decisions and execution management.
- **Streamline Engine:** Rendering-change analysis, incremental execution planning and baseline management.
- **Micro-Pixel Unit:** Localized pixel-change detection for incremental updates.
- **Camera Unit:** Camera-motion analysis and rendering-state reuse.
- **LibreGPU Memory Manager:** Resource tracking, VRAM budgeting and deferred reclamation.
- **Simulated Backend:** Scripted graphics execution, frame comparisons and runtime validation.
- **LibreGPU Desktop Launcher:** SDL2 and Nuklear-based graphical interface for game selection and launching.
- **Game Profiles:** Persistent game executable paths and FPS-limit configuration.
- **LibreGPU CLI:** Runtime configuration, diagnostics, performance metrics and self-tests.

### Build & Validate:

- C11 implementation.
- SDL2 desktop integration.
- Strict compilation and GCC static analysis support.
- AddressSanitizer and UndefinedBehaviorSanitizer support.
- Automated core self-tests.

**Note:** Core development milestone introducing the graphical game launcher and persistent profiles. Game-specific native graphics backend integration is not yet implemented; the runtime continues to use its simulated backend.
