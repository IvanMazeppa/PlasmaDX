## Questions for Claude Code (2025-09-18)

1) Particle counts and bounds
- Default N? Suggested 65k. World AABB for particles (e.g., [-10,10]^3)?

2) Density grid sizing
- Start at 128^3? Should we expose presets 96/128/192 immediately?

3) Coordinate system
- Y-up or Z-up? Camera handedness? Required for marcher ray setup.

4) Shader compile workflow
- Prefer Windows CMake target `compile_shaders` or WSL DXC? Confirm.

5) Parameter interface
- Where to read/write params (ImGui later vs keys now)? Minimal: keys to change steps/scale.

6) Performance targets
- Debug vs Release expectations? Any target FPS for Preview mode?

7) DXR timing
- OK to postpone DXR pipeline fix until compute marcher is working, then revisit 0017?


