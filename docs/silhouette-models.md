# Silhouette models

In **Appearance → Player → Fuser**, choose the player category, enable **Entity silhouette**, then select **Model: Classic / Human**. The model and fill opacity are saved per player category in `configs/config_appearance.json`.

- **Classic** keeps the existing 1.0.48 rounded silhouette renderer and remains the default for existing settings.
- **Human** uses the custom shaped body: fitted torso, tapered limbs, hands, feet, and headwear approximation. It shares the existing camera projection and player colours. Body parts are unioned before filling so overlaps do not darken joints or leave internal outlines.

These are projected approximations, not character meshes or hitboxes. The selection does not change aim points or correct camera alignment. Eight appended visual joints preserve the original 13 bone indices; missing detail joints fall back to the core pose without making core pointer readiness fail.

Review entry points: `UI/BodySilhouette.h` contains the Human geometry; `UI/fuserRender.h` chooses the renderer; `UI/Appearance.cpp` persists the model; `UI/render.cpp` exposes the dropdown; `UI/DxRenderWindow.cpp` unions the shapes.

Validation: from an x64 Visual Studio Native Tools prompt, run `python tests/run-silhouette-tests.py`. The tests exercise actual geometry and production JSON serialization without starting the app or accessing DMA. Build `MeatyEFTRelease/MeatyEFTRelease.vcxproj` in Release x64. Live raid alignment and performance still require testing.
