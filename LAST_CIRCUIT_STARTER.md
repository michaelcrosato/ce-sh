# LAST CIRCUIT — Custom Engine Build Brief

**Document version:** 1.0  
**Prepared:** 5 September 2026  
**Intended coding agent:** GPT-6-Astra, as selected by the project owner.  
**Project:** A small first-person escape-horror game with mandatory hardware path tracing.  
**Initial platform:** Windows PC with an NVIDIA RTX GPU.

> Build the smallest complete engine that can deliver this game. Prove the moving lamp, door, and mirror before you expand the level. The result must be playable, measurable, and reproducible. A design document alone is not the deliverable.

## 0. Start here

Read this file completely before you select the architecture. On later sessions, read the project rules, current status, and the section for your next task.

Inspect the workspace before you change it. Preserve existing user work. If the project is empty, create the initial project structure. If it contains useful work, extend it rather than replace it without cause.

Begin implementation in the current session. Use the milestone gates in Section 20. Do not stop after a plan, an empty project, or a list of dependencies. Work through complete, testable increments as far as the available tools permit.

Do not ask the owner to make routine technical choices that this brief resolves. Record reversible choices in a decision log. Ask only when a missing permission, paid service, license condition, or change to a hard requirement needs owner action.

Do not assume access to Windows, a compiler, a GPU, the internet, or private services. Inspect the actual environment. When a required resource is absent, complete useful work that does not need it. Supply the exact commands for the blocked test. Mark that test **NOT RUN**, not passed.

Do not claim that work will continue after the session ends. Leave a precise handoff for the next session.

**First execution task:** Complete the environment audit and build the first hardware-ray-traced geometry test. Then continue toward the two-room lighting test. Do not start with the six-room level.

### Status of this document

This is a build specification. It is not an existing engine, a benchmark report, or proof that the performance targets are achievable. Numerical budgets are proposed engineering targets unless explicitly identified as measurements.

Technical reference links are in Section 26. They were checked during preparation. Select compatible dependency versions at implementation time. Record the versions that you actually use.

## 1. Product intent and order of importance

**Working title:** Last Circuit.

You are trapped in a maintenance wing. The exit needs a fuse from the last active circuit. Taking the fuse darkens the return route. Your portable lamp lets you navigate, but it also makes you easier to detect.

The first public demo should take approximately five to eight minutes. It has six rooms, two connected halls, one portable lamp, one threat, and one objective.

The visual language is simple geometry with consistent light transport. It is not a high-detail game with a ray-tracing option.

Use this order when work competes for attention:

1. Preserve the hard rendering and project rules.
2. Produce correct, stable interaction and light transport.
3. Make the two-room test readable and playable.
4. Meet the measured performance and memory targets.
5. Improve composition, sound, and material quality.
6. Expand only to the approved six-room demo.

Do not call the demo complete if it fails a required gate. Correctness and performance must both pass. Do not hide one failure with the other.

**The intended player reaction:** The player understands that the reflection, shadow, source, and threat belong to one working world. They then want to continue playing.

Do not claim that the game is unique merely because another renderer would need a different technique. The demo must stand on its actual play and image quality.

## 2. Hard requirements

### H01 — Own the engine

Use a custom application, scene system, renderer, asset pipeline, and game layer. Do not use Unreal, Unity, Godot, or another complete game engine.

Existing libraries are permitted for bounded tasks. These include input, math, file parsing, audio, diagnostics, denoising, and image reconstruction. Preserve their license notices. Do not describe third-party code as original work.

Study reference renderers. Do not rename a complete reference application and present it as our custom engine.

### H02 — Trace the world

Use hardware rays for camera visibility and all world light transport. Calculate direct illumination, source visibility, reflections, and indirect illumination in the path tracer.

Rasterization is permitted for the interface, diagnostic overlays, and text. These elements are not world lighting. Diagnostic views may show unlit colors or normals, but they must be clearly identified as diagnostic views.

There is no production raster-lighting fallback. Unsupported hardware receives a clear error.

### H03 — Artists control causes, not calculated lighting

Artists may set geometry, material properties, and source position, shape, color, and output. They may change openings and place real fixtures.

Prohibit baked lightmaps, painted scene shadows, painted highlights, ambient-occlusion lighting maps, reflection probes, shadow maps, and screen-space lighting replacements.

Prohibit invisible fill sources, per-object light exclusion, shadow-disable switches, per-object exposure, and ambient brightness added to every surface.

A fixture may contain both an emitting mesh and data used to sample that same emitter. This is one source, not a hidden second light. The data must remain linked.

### H04 — One visible world

Use the same object placement, geometry, and materials for camera, reflection, indirect, and source-visibility rays.

Do not use a duplicate threat in the mirror. Do not use a camera texture, recorded scene, or hidden copy of the level for reflections.

Do not make walls disappear for shadow rays. Do not remove an off-screen object that affects a reflection.

Denoiser guide data may use a virtual reflected surface. This is not permission to create a second rendered world.

### H05 — Approximate honestly

Limited sampling, finite path depth, denoising, temporal reconstruction, and global display processing are permitted. Document them.

Do not describe a finite, filtered real-time image as exact light transport. Keep a slower reference mode and a raw signal view.

Prohibit image-generation or restyling stages that invent scene details or change the intended materials. Reconstruction is for the calculated image, not a second art direction.

### H06 — Keep the game local

The released demo runs offline. It does not need a language model, OpenAI API key, paid inference endpoint, account, or telemetry service.

The coding agent is a development tool. Do not add an AI service to the game merely because an AI agent is writing it.

### H07 — Preserve user control and evidence

Do not delete unrelated files, rewrite history, publish a repository, upload assets, buy services, install drivers, or alter system security without permission.

Do not disable GPU timeout recovery to conceal a long or faulty shader. Fix the workload.

Do not invent test results, screenshots, dependency interfaces, or device support.

## 3. Fixed starting technology

| Area | Starting choice |
|---|---|
| Operating system | Windows 11, x64. Record the tested build. |
| CPU language | C++20. |
| GPU language | HLSL, compiled with a pinned DXC version. |
| Graphics interface | Direct3D 12, DXR Tier 1.1 or greater. |
| Initial tracer | Compute shaders with inline `RayQuery`. |
| Shader baseline | Shader Model 6.5 for the initial tracer. Check all added SDK requirements. |
| Build | CMake, with committed configure, build, and test presets. |
| Compiler | A supported installed MSVC toolchain. Record the exact version. |
| Window and mouse | Win32 and Raw Input. |
| Math | DirectXMath or a small documented math layer with tests. |
| Initial geometry | Locally generated triangle meshes. |
| Imported geometry | A restricted, validated glTF 2.0/GLB subset, after the first lighting test. |
| Scene description | Versioned JSON with stable object identifiers. |
| Diagnostic interface | Dear ImGui, or a similarly small approved library. |
| Initial denoising | Evaluate NRD after the raw renderer passes. |
| Later reconstruction | Evaluate DLSS Super Resolution through Streamline. |
| Audio | One small library, such as miniaudio. Pin and review it before use. |
| Game simulation | Fixed update step, initially 60 updates per second. |

These are project choices, not claims that they are the fastest choices for all games.

DXR Tier 1.1 supports inline ray queries. The caller manages traversal and shading within its shader. Both inline queries and a `DispatchRays` pipeline use acceleration structures. [R1]

Start with inline queries because the initial material system is small. Put ray queries behind a narrow shader interface. This permits a later measured comparison with `DispatchRays` without replacing the game or scene system.

Do not implement two ray-tracing backends at the start. Do not add Vulkan, consoles, CUDA rendering, custom software BVH traversal, or a cross-platform rendering framework.

An RTX-only release policy does not justify careless feature checks. Enumerate adapters. Reject software adapters. Check the required DXR tier, shader support, formats, and device creation. Record the vendor, device identifier, name, and driver.

Use NVIDIA hardware for the supported prototype path. A capability check is not proof of acceptable performance. Do not claim support for every RTX card. Other vendors and untested models are outside the initial validation scope.

### Dependency policy

Use fixed tags or commits and integrity checks where available. Do not track `main` in a release build. Record source, version, license, purpose, and redistribution conditions in `docs/DEPENDENCIES.md`.

Read actual headers and integration guides. Never invent a wrapper that calls functions an SDK does not provide.

Use one package acquisition method where practical. Make network access part of setup, not gameplay. A build must not silently fetch different code each time.

Do not adopt RTXDI, ReSTIR, neural radiance caches, shader execution reordering, or other advanced systems merely because they exist. Add them only for a measured problem after the baseline works.

## 4. The first proof: two rooms and one hall

Build this before enemy intelligence or a complete level.

**Room A:** A 4 m by 4 m equipment room. It contains a source fixture, a colored crate, the portable lamp, and a door.

**Room B:** A 4 m by 4 m inspection room. It contains a sink-like block, one angled flat mirror, and a marked lamp shelf.

**Hall:** A short connection with one right-angle turn. Use approximately 2.4 m clear width and 2.8 m height.

The threat is initially a rigid, low-polygon test object on a fixed path. It must occupy the real hall, outside the camera view, while remaining visible in the mirror.

The player can move, aim the lamp, switch it, place it on the shelf, pick it up, and operate the door.

The first successful sequence is:

- The player puts the lamp on the shelf and sees its effect change.
- The test object crosses the real hall and appears in the mirror.
- Closing the door blocks the relevant light and reflected view.
- A later pass moves the object's shadow across a wall before direct contact.

Use deterministic replay for testing. Do not require a player to reproduce the exact camera movement by hand.

The mirror event must remain understandable during motion. A clean still image after long accumulation does not pass this test.

## 5. The six-room demo, after the proof passes

Keep the level on one floor. Aim for approximately 160–220 square metres of walkable space, including halls. Dimensions may change after movement tests.

```text
 [1 Security]     [2 Equipment]      [3 Inspection]
       |                 |                  |
=======+=================+==================+====+
                      HALL A                    |
       |                                     HALL B
   [6 Exit]                                     |
                                                +--- [4 Plant]
                                                |
                                          [5 Switch room]
```

This is a connection diagram, not construction geometry. Build a measured layout without overlapping walls or impossible door swings.

| Space | Main use | Required lighting event |
|---|---|---|
| Security, about 4 x 4 m | Explain the failed exit and establish controls. | One ordinary fixture establishes material appearance. |
| Equipment, about 4 x 5 m | Collect the portable lamp. | Move its shadow and observe reflected color from a crate. |
| Inspection, about 4 x 4 m | Place the lamp and inspect the mirror. | Show the threat outside direct view. |
| Plant, about 6 x 6 m | Pass a turning fan and approach the objective. | Fan blades cast moving shadows from a real source. |
| Switch room, about 4 x 5 m | Remove the fuse. | A defined circuit turns off. The lamp remains available. |
| Exit, about 4 x 4 m | Insert the fuse and leave. | A door opens onto a small, genuinely lit space beyond. |

Use the outward route to teach the space. Use the return route to change its meaning with source state and threat movement.

### Power must make sense

Give each fixture a circuit or battery state. The fuse controls a defined circuit. The lamp has its own supply. A hall emergency fixture may remain on if its independent supply was established earlier.

Do not spawn an invisible light for a scare. A script may operate an existing fixture, door, fan, or threat.

One or two dominant sources per view is an art target. It is not permission to discard other physically relevant sources. A source is not disabled because it leaves the camera view.

Start with no more than eight active emitter fixtures in the complete demo, including the portable lamp. Measure cost before changing that budget. A fixture may contain multiple emitting triangles.

### Presentation

Use restrained sound, clear source placement, and deliberate framing. The order of the main event is reflection, sound, shadow, then direct view.

Do not force a camera cut to prove the mirror works. The player should retain control during the reveal. Allow the event to recover if the player initially looks away.

Avoid repeated strobe effects. A power failure can occur once. Provide an option to suppress nonessential flashes.

## 6. Art rules and content budgets

Use large, clear shapes. The scene must remain recognizable without high-frequency textures.

Use painted concrete, rubber, painted metal, bare metal, one mirror material, and emitter surfaces. Keep most large surfaces rough. Reserve the mirror and a few metal surfaces for clear reflections.

Do not cover the building in wet floors. Do not add fog to make the lamp beam visible. In the initial air model, the beam is visible on surfaces, not suspended in empty space.

Use flat face normals where the style needs facets. Bevel only edges that affect silhouette or useful highlights. Internal triangulation may be finer than the visible faceting.

| Content | Initial budget or rule |
|---|---|
| Small prop | Approximately 8–100 triangles. |
| Important machine or threat | Approximately 200–1,000 triangles, split into rigid parts. |
| Complete level | Aim below 100,000 triangle instances. Count geometry used outside the camera view. |
| Placed objects | Aim below 500 initially. Track the actual TLAS instance count. |
| Reusable meshes | Approximately 20–30 for the six-room demo. |
| Material classes | Opaque diffuse, opaque rough conductor, ideal mirror, and emitter. |
| Texture use | Optional simple color markings. No baked scene lighting. |
| Motion | Rigid translation and rotation. No skin deformation in the first demo. |
| Transparency | None. |

These limits control art and scope. They are not predictions of GPU cost.

Opaque triangles avoid unnecessary transparency work. Poorly shaped triangles and inefficient bounds can still hurt traversal. Profile geometry organization rather than minimizing triangle count blindly. [R2]

### Required exclusions

Do not add hair, cloth, layered glass, masked foliage, displacement, fluid simulation, smoke volumes, decals with separate lighting, complex destruction, or skeletal skinning.

Use solid fan blades and solid grille bars where necessary. Avoid stacks of overlapping surfaces. Remove degenerate triangles and accidental duplicate faces.

Walls, floors, ceilings, and doors must have correct orientation and physical thickness where needed. Use approximately 0.15 m walls and 0.04 m doors as starting construction values. Include actual frames and seals where the design needs no visible light gap.

Keep the whole level near the world origin. Do not test this small game thousands of kilometres from the origin.

### Asset provenance

Generate the initial kit locally from simple mesh construction code. This provides a runnable scene without an asset download dependency.

Later assets must have recorded authorship and permission. Do not copy Star Fox assets or another game's models. The reference is geometric simplicity, not protected content.

## 7. Materials, sources, and exposure

### Small material system

Use explicit material types initially. Avoid a large universal material graph.

`Diffuse` has a scene-linear RGB reflectance. `RoughConductor` has a documented conductor approximation and roughness. `Mirror` is an ideal specular reflection with a bounded RGB reflectance. `Emitter` has surface emission and defined surface behavior.

Document all parameter ranges and units. Reject invalid values. Do not mix a metalness material model with an unrelated conductor model without defining the conversion.

Start with no normal maps. Use the geometric normal for ray offset decisions. If smooth shading is later added, keep geometric and shading normals separate.

Do not accept energy gain from a reflective material. An ideal test mirror may have unit reflectance. Production mirrors should have a defined reflectance below one.

### One emitting surface, one source definition

Start with rectangular, one-sided area emitters represented by real triangles. The portable lamp uses a small rectangular emitting face and a simple opaque housing.

Store the emitting surface radiance in a documented scene-linear RGB convention. For the first renderer, expose one clearly named radiance scale. Do not label arbitrary numbers as watts or lumens.

The sampling distribution and the visible emitting triangles must read the same data. Transform both together. Scale changes must update emitter area and sample weights.

The back of a one-sided source must not emit. It can remain opaque. When the source is off, its surface remains in the scene and may reflect other light.

Do not exclude the entire lamp mesh from source-visibility rays to prevent self-shadowing. Use a correct ray interval and numerical offset. The housing is allowed to block the source.

A spotlight model can be added later only with a defined emission distribution. Do not fake it with a screen overlay.

### Sampling and exposure

Begin with black environment radiance outside the level. The exit opens onto a small real space with a defined source, not a bright image used in place of geometry.

Use fixed exposure for correctness tests and the initial demo. An accessibility brightness control may adjust global display exposure within a documented range. It must not change gameplay detection.

Use scene-linear high-dynamic-range buffers. Apply tone mapping and output encoding once. Keep UI composition and color conversion documented.

Bloom, vignette, film grain, chromatic aberration, motion blur, and automatic exposure are off until the base image passes. None is needed for the proof.

## 8. Project structure and system boundaries

Use a structure close to this. Create files as their milestone needs them. Do not generate empty abstractions to fill the tree.

```text
AGENTS.md
LAST_CIRCUIT_STARTER.md
CMakeLists.txt
CMakePresets.json
README.md
src/
  app/                  Application startup and main loop.
  platform/             Window, input, files, and operating-system services.
  core/                 Logging, time, identifiers, and small shared utilities.
  graphics/d3d12/       Device, resources, descriptors, commands, and fences.
  render/               Path tracing, guide buffers, image processing, timings.
  scene/                Meshes, instances, materials, sources, and scene loading.
  game/                 Player, doors, lamp, objective, threat, and triggers.
  audio/                One audio-library integration.
  debug/                Diagnostic interface and capture tools.
shaders/
  shared/               Shared layouts, math, sampling, and material code.
  trace/                Closest-hit queries, visibility queries, path integrator.
  guides/               Surface and motion data for reconstruction.
  post/                 Composition, exposure, output, and diagnostics.
assets/
  scenes/               Versioned scene descriptions.
  generated/            Original low-polygon kit.
  audio/                Licensed or original sounds.
tools/                  Setup, build, capture, benchmark, and packaging scripts.
tests/
  cpu/                  Math, sampling, parsing, and simulation tests.
  scenes/               Small controlled rendering scenes.
  replay/               Fixed camera and world-event sequences.
  baselines/            Approved metadata and image comparisons.
docs/
  BUILD.md
  ARCHITECTURE.md
  RENDERING.md
  DEPENDENCIES.md
  TESTS.md
  STATUS.md
  DECISIONS.md
  KNOWN_ISSUES.md
```

Keep the game layer independent of D3D12 types. It publishes scene state and events. The renderer consumes a consistent render snapshot.

Use a simple scene registry with stable identifiers. A full entity-component framework is not required. Do not create a general plugin system, scripting language, node editor, or reflection framework.

Avoid one giant source file. Avoid dozens of one-line forwarding classes. Make ownership and resource lifetime visible in the code.

Use RAII for CPU and COM resources. Use explicit GPU lifetime management for resources still referenced by submitted work.

Keep errors actionable. A failed shader build must identify the shader, entry point, profile, compiler output, and command used.

## 9. Coordinate, data, and frame contracts

Write these contracts before the first moving scene. Test them with asymmetric geometry so a mirrored mistake cannot pass unnoticed.

### Coordinates

Use metres, seconds, and radians internally. Use a right-handed world with +Y up. Use a camera that looks along its local -Z axis, with local +X to the right.

Use column-vector transform notation in the rendering contract: `world_position = object_to_world * local_position`. Select one HLSL matrix layout explicitly. Convert DirectXMath or imported representations at a documented boundary.

Use the Direct3D clip-depth range deliberately. Document the projection matrix, near plane, far plane, and depth interpretation used by each SDK.

Do not transpose matrices until an image happens to look correct. Test translations, rotations, nonuniform scale handling, and camera reconstruction separately.

### Shared GPU data

Define CPU and shader layouts from one controlled source or validate them with static assertions and shader reflection. Avoid shared `bool` fields and implicit packing assumptions.

At minimum, define stable records for meshes, instances, materials, emitters, camera state, and frame constants.

Each instance has an identifier, mesh reference, material assignment, current transform, previous rendered transform, and transform revision.

An emitter refers to its actual geometry and instance. Do not duplicate source output in an unrelated light array without a shared authority.

Keep path sample counters separate from display frame counters and simulation ticks. Record the random seed and sample index for captures.

### Motion history

Previous rendered transforms refer to the previous image state, not merely the previous simulation tick. This matters when simulation and rendering use different rates.

New objects, teleports, scene reloads, camera cuts, resolution changes, and incompatible material changes invalidate relevant history.

Paused gameplay may still render. Reference capture freezes simulation and camera state. It must not silently accumulate samples from a moving world.

### Frame order

Use one explicit sequence initially:

```text
Poll input.
Advance the fixed-step simulation.
Create interpolated current and previous render data.
Update changed instance and source data.
Update or rebuild the required acceleration structures.
Trace camera and light paths; produce required guide data.
Denoise or run the selected diagnostic/reference path.
Reconstruct the output resolution when enabled.
Apply exposure and display conversion.
Draw the interface.
Present the image and retain the correct history state.
```

Keep one graphics-capable command queue initially. Add asynchronous compute or extra queues only after a measured need. Do not confuse a more complex queue graph with better performance.

## 10. Direct3D 12 implementation requirements

Build a small, reliable graphics layer before advanced rendering work.

Create the debug layer and GPU validation options in development builds. Name GPU resources and command sections. Provide timestamp queries from the first lighting milestone.

Manage command allocators, descriptor storage, upload memory, readback memory, and temporary ray-tracing storage with explicit ownership.

Do not overwrite a buffer, descriptor, or allocator while the GPU can still use it. Use fences and a defined number of frames in flight.

Do not wait for the GPU after every dispatch as a permanent solution. Full waits are acceptable for setup and controlled capture when documented.

Use correct state transitions and required UAV ordering around acceleration-structure builds, tracing, denoising, and readback.

Handle resize, minimize, lost focus, and exit without resource corruption. Never create a zero-size render target.

Collect device-removal information where supported. DRED is intended to help diagnose unexpected device removal. Use the interfaces available in the selected SDK. [R3]

Precompile the game's shader variants during build or controlled loading. Do not compile a new variant when the threat first appears. A development hot reload may compile, but it must not be part of the measured release path.

Do not introduce a full render-graph framework initially. Use an explicit pass list with resource-state tracking and timing names.

## 11. Acceleration structures and ray queries

### Mesh and instance organization

Use a BLAS for reusable mesh geometry and a TLAS for placed instances. Reuse mesh structures for repeated objects. A rigid door should not require reconstruction of its vertices each frame. [R1]

Start with all demo geometry resident. Keep the whole small level in the ray-tracing scene, including geometry behind the camera.

Separate moving objects from static geometry. Keep static structures spatially sensible. Do not combine distant rooms into one bounds-heavy mesh merely because they share a material.

Profile TLAS rebuild against update for the small instance count. Select the simpler measured method. Use the build flags and scratch requirements correctly.

Do not promise that update is always faster. Do not rebuild static BLAS data every frame.

### Ray interfaces

Provide shader functions with distinct purposes:

- `TraceClosest`: Return the nearest accepted surface and sufficient data to shade it.
- `TraceVisibility`: Return whether a finite segment to a sampled source is blocked.

The nearest-surface query must not use a first-accepted-hit shortcut that can return the wrong surface. The source-visibility query may terminate as soon as a valid blocker is found.

Use opaque triangle geometry and skip procedural geometry initially. Do not add custom spheres or signed-distance fields.

Reconstruct hit position, barycentric data, material reference, and normals consistently. Transform normals correctly. Disallow unsupported negative or nonuniform instance scales at import until their handling is tested.

### Numerical robustness

Use a scale-aware secondary-ray origin method. Do not add one large global epsilon to hide self-intersection.

A ray offset that is too small can cause self-shadowing. An offset that is too large can move past nearby geometry and cause leaks. Use a documented error-bound method and test it. [R4]

Test grazing angles, door gaps, nearby parallel surfaces, emitter endpoints, and small props. Keep shadow ray intervals finite and correct.

Do not hide numerical faults with per-asset shadow bias settings.

## 12. Path tracer contract

Build a small RGB path tracer. Do not begin with spectral rendering or participating media.

### Initial transport

Start with Lambertian diffuse reflection and exact ideal-mirror reflection. Add rough-conductor GGX shading after the diffuse and mirror tests pass.

Use proper probability densities and energy weights. Use tested sampling functions. A visually plausible image is not enough to prove the estimator is correct.

Use next-event estimation at eligible non-delta surfaces. Combine source samples and surface samples with multiple importance sampling. These techniques are standard parts of practical path tracing. [R5]

Select from the actual active emitter list. A simple power-based distribution with nonzero support for every active source is sufficient initially. Store and use the exact selection probability.

Include source-selection probability, emitter-surface sampling probability, and the required area-to-solid-angle conversion. Do not mix probability measures in the same estimator.

A source hidden from the camera must still be sampled when it can illuminate the surface being shaded.

### Emission and mirror rules

Count emitted radiance with the correct path weight. Do not add a full emitter contribution twice because both a source sample and a surface sample can find it.

Handle delta reflection separately from a rough BRDF. Do not apply an ordinary area-light MIS formula to a perfect mirror as though it had a finite directional density.

The mirror reflects the same scene data as all other paths. Continue the path into the reflected scene. Do not create a texture-rendered reflection shortcut.

Maintain path throughput, sample probabilities, event types, and depth explicitly. Do not use arbitrary reflectivity multipliers to compensate for missing indirect light.

### Path limits

The initial real-time setting is one camera path per internal pixel per rendered frame, with at most **four surface hits total**.

This count includes the camera's first hit and any mirror hits. A finite visibility ray to a light does not increase the surface-hit count. It still has GPU cost and belongs in ray-work statistics.

Sample direct light at the last eligible surface. At a path limit, MIS weights must match the strategies that can actually contribute. Do not downweight a sample against a competing strategy that the limit prevents from running.

Use an iterative loop with a hard bound. Do not create unbounded recursion. Do not author facing mirror pairs for the first demo.

Four hits is a starting budget, not a fidelity claim. Keep at least one indirect surface interaction and a working mirror path in every approved gameplay mode. Record any proposed depth change and rerun image tests.

For reference mode, allow more hits and many more samples. Compare both equal-depth reference images and greater-depth images. The first comparison tests the implementation; the second exposes path truncation.

### Sampling quality

Use deterministic seed control and a sample sequence with documented dimensions. Avoid random seeds derived only from the current clock.

Keep pixel, frame, bounce, and sampling-purpose dimensions distinct. Check for correlation patterns during camera movement.

Use Russian roulette only when it has a measured or reference-mode purpose. Compensate surviving paths correctly. A fixed short production path can omit it initially.

Detect NaN, infinity, negative invalid radiance, zero probability, and out-of-range material data. Report their counts in validation mode.

A production outlier clamp may be evaluated later. Keep it off in reference mode. Record its bias and its effect on bright reflected features.

### Source code review criteria

Before accepting the integrator, explain in `docs/RENDERING.md` how it handles emission, direct light sampling, surface sampling, delta events, path termination, and probability weights.

Use concise equations or pseudocode where useful. Do not replace tested code with a theoretical derivation alone.

## 13. Denoising and reconstruction

Treat image stability as an engine requirement, not a final visual effect.

Keep three independently selectable paths:

| Mode | Purpose |
|---|---|
| Raw real-time | Show current samples without temporal filtering. Diagnose the estimator. |
| Progressive reference | Freeze the scene and accumulate many samples without production denoising. |
| Real-time reconstruction | Use documented guide data, denoising, and optional upscaling. |

### First integration: NRD

Evaluate one NRD method suited to the chosen signals. Do not integrate every NRD mode at once.

NRD requires defined surface and motion inputs, not only a noisy final color image. Follow the selected version's guide-buffer, signal-packing, hit-distance, and composition contracts. [R6]

Create an explicit buffer contract table in `docs/RENDERING.md`. For every input, state its format, units, coordinate space, range, invalid value, source pass, and lifetime.

At minimum, resolve normals, roughness, depth, motion vectors, diffuse and specular signal meaning, radiance scaling, hit-distance meaning, and exposure handling.

Do not infer a buffer's semantics from its name. Use SDK packing helpers where required. Do not denoise the wrong quantity and compensate during composition.

Inspect the official NRD sample as an integration reference. Keep only the features needed by this game. [R7]

### Mirror stability is a release gate

A stationary mirror can reflect a moving threat. Surface motion for the mirror alone does not describe the reflected threat.

For the single planar mirror, evaluate NRD's documented Primary Surface Replacement approach. It replaces suitable guide data with a virtual reflected surface. Follow all required transforms and buffer rules. Do not replace only depth and leave incompatible motion or normals. [R6]

Keep the actual ray-traced mirror radiance unchanged. Virtual guide data is not permission for duplicate geometry or fake reflections.

Test the mirror while the player moves, the threat moves, the door moves, and the lamp changes. Do not roughen or blur the mirror merely to avoid this test.

### History handling

Reject history for newly visible surfaces and incompatible geometry or materials. Handle camera cuts, teleports, resize, scene reload, and source changes explicitly.

An abrupt circuit change may reset relevant histories. Continuous lamp movement must not force a complete reset on every frame.

Choose bounded history settings. Do not let a long accumulation hide noise in still scenes while leaving trails in motion.

Expose history age and rejection diagnostics. Test low-light motion at normal playback speed and frame by frame.

### Upscaling

Add Super Resolution only after a stable native/internal-resolution path works. Follow the current Streamline guide and record feature-support results. [R8]

Query the feature's recommended internal size. Do not force a nominal 720p size into a mode that expects a different input size.

Keep a native-resolution mode and a simple diagnostic scale path. Their availability does not remove the ray-tracing requirement.

Do not apply an extra uncontrolled temporal filter after reconstruction. Keep the order of denoising, reconstruction, exposure, and interface composition explicit.

Evaluate DLSS Ray Reconstruction later as an alternative supported signal path, not an extra filter placed after NRD by default. It has a separate integration contract. [R9]

Frame generation is not part of the first demo requirement. It must remain off in baseline performance tests.

## 14. Camera, body, collision, and interaction

### First-person movement

Use a capsule-like player controller against simple collision geometry. A tested lightweight physics library is permitted, but a complete dynamic-physics system is unnecessary.

Start with walking, a short sprint, mouse look, and interaction. No jumping or crouching is required for the first layout.

Use configurable mouse sensitivity, inverted vertical look, and field of view. Start near a 90-degree horizontal field of view at 16:9. State the field-of-view convention in settings.

Disable head bob and camera shake by default. Keep acceleration and stopping predictable. Avoid input lag from unnecessary render buffering.

Pause safely on focus loss. Release mouse capture in menus. Prevent a large motion jump when focus returns.

### Physical placement

The camera and carried lamp must not enter a wall. Sweep the lamp's placement volume and move it back toward the player when it would intersect geometry.

Do not render the lamp through walls. Do not disable its shadows while it is held.

Use marked placement sockets in the proof. Do not build free rigid-body lamp placement yet. Align the visible lamp, interaction bounds, source, and audio position to the same transform.

Use simple collision solids for walls and furniture. The collision and visible shell must agree within a documented tolerance. Do not build collision from a denoised image.

### Player in reflections

Provide a simple low-polygon body and hands for reflection and shadow visibility by the playable milestone. Keep the geometry clear of the camera through pose and placement, not camera-ray exclusion.

The temporary body may be a simple rigid construction. Record that it is a placeholder. Do not silently omit the player from the mirror.

A conventional separate first-person view model is not part of this engine. The held lamp belongs to the world.

### Interaction

Use a short-range query from the camera to choose an interaction target. Provide a clear prompt and a deterministic action.

Initial controls are WASD movement, mouse look, Shift sprint, E interact, F lamp switch, and Escape pause. Make the actions rebindable before external release.

Doors have explicit open, closed, and moving states. Their collision, ray-tracing transform, and sound follow the same state. A closing door must not trap the player inside its solid volume.

## 15. Threat, objective, and sound

### Threat

Use one rigid-part maintenance machine. It needs a readable outline, a distinct movement sound, and a small state machine.

The first visual test uses a deterministic path. The playable demo uses wait, patrol, investigate, and chase states only as needed.

Detection uses gameplay data: lamp state, distance, facing, and line of sight. Do not read display brightness or denoised pixels. Exposure and reconstruction must not change the game rules.

Use simple room and hall waypoints. Do not start with a general navigation-mesh system. Check visibility and collisions around doors.

Do not let the threat see through a closed opaque wall. Do not teleport it through an active mirror view. Staging may move it between valid off-screen locations only if continuity remains credible and the implementation is explicit.

A catch restarts from a checkpoint. It must not delete progress outside this demo or require a full process restart.

### Objective

Use a small state machine: introduction, lamp acquired, fuse available, fuse carried, exit powered, escaped.

Removing the fuse changes circuit state. Inserting it at the exit changes the appropriate circuit. The same state drives fixture emission, source data, fixture sound, and relevant animations.

Provide a restart command. Avoid irreversible soft locks. The player must be able to retrieve a placed lamp and finish the route.

### Sound

Start with footsteps, lamp handling, door movement, a fixture hum, fan rotation, threat movement, and an exit sound.

Use original, generated, or licensed assets with provenance. Simple temporary sounds are acceptable. Silence is better than an unlicensed asset.

Provide positional attenuation and stereo spatial placement. A simple documented wall-occlusion approximation is sufficient. Do not build a physical acoustic tracer.

Sound events follow actual world state. Turning off a fixture stops or changes its hum. A stopped fan must not continue to sound as though it rotates.

Provide master, effects, and ambience controls. Important sounds need an optional text or visual cue. Do not rely on color or hearing alone for the required objective.

## 16. Asset and scene pipeline

The first scene must run from generated assets stored with the project. Do not block the hardware test on Blender automation or an external model pack.

After the first lighting proof, implement a small GLB loader or use a maintained parser. Support indexed triangles, positions, normals, simple material data, and node transforms. Add color textures only when the game needs them.

glTF defines a right-handed coordinate system and metre-based distances. Convert asset and camera conventions at a documented boundary. Do not assume every exported asset follows the required authoring rules. [R10]

Reject unsupported skinning, morph targets, transparency, material extensions, and compressed data with an actionable message. Do not silently display them incorrectly.

Validate indices, finite values, file bounds, object counts, texture sizes, duplicate identifiers, emitter references, and supported transforms. Apply reasonable input-size limits. Scene paths must stay within the approved asset root.

Use a schema version. An invalid scene must not destroy the previous working scene during hot reload. Parse and validate first, then replace the scene at a safe frame boundary.

Record a content hash for scenes, meshes, materials, and shaders in benchmark metadata.

A scene file describes objects, sources, circuits, interaction sockets, collision shapes, waypoints, and objective events. It must not contain baked lighting corrections.

Start with a reload command and a small diagnostic panel. Do not build a full editor. The generated kit and scene files are sufficient for this proof.

## 17. Performance and memory targets

### Baseline target

| Item | Proposed target |
|---|---|
| GPU | Desktop GeForce RTX 4060. This is a test target, not a verified minimum. |
| Output | 1920 x 1080. |
| Internal tracing | Start at 1280 x 720 for the fixed-resolution baseline. |
| DLSS input | Use the queried supported size when that path is enabled. |
| Frame rate | Aim for 60 genuinely rendered frames per second. |
| Frame generation | Off. |
| Initial sampling | One camera path per internal pixel per rendered frame. |
| Initial path limit | Four total surface hits, as defined in Section 12. |
| GPU memory | Aim below 6 GiB of application local usage and below the reported budget. |
| System memory | Aim below 8 GiB of application working memory. |
| Gameplay loading | No network access, shader compilation, or asset download during play. |

Record the exact CPU, RAM, operating system, GPU, driver, display mode, and executable build used for measurements.

A laptop GPU with a similar name is a separate performance case. Do not transfer results between devices without a test.

At 60 frames per second, the frame period is about 16.67 ms. Use this provisional GPU allocation:

| GPU work | Planning allocation |
|---|---:|
| Scene-data transfer and acceleration-structure work | 1.0 ms |
| Path tracing and guide generation | 7.5 ms |
| Denoising and reconstruction | 3.5 ms |
| Output, interface, and remaining GPU work | 1.0 ms |
| Total planned GPU work | 13.0 ms |
| Remaining frame-period margin | 3.67 ms |

This allocation is not a measured estimate. The full frame must be timed. Separate pass measurements may not add exactly when work overlaps.

Keep CPU simulation and render preparation below a proposed 4 ms at the 95th percentile, excluding deliberate frame-cap waits. Record total CPU frame time as well.

Read the actual video-memory budget. Windows exposes local and non-local memory information through its graphics interfaces. A nominal card capacity is not the same as the available process budget. [R11]

### Benchmark protocol

Use a packaged or release-equivalent build. Disable the diagnostic overlay for the main timing pass, but retain GPU timestamps.

Use a fixed replay with camera movement, the mirror, the moving threat, lamp movement, a source switch, and door motion.

Warm the required pipelines and assets. Then measure a 180-second replay, three times. Record the warm-up duration and all excluded intervals before the run.

Record average, median, 95th-percentile, and 99th-percentile CPU and GPU frame times. Record the maximum frame time and count of frames above 33.3 ms and 50 ms.

Keep baseline timing uncapped where suitable. Record VSync and frame-cap state. Use a separate normal-play test for pacing and input behavior.

A proposed performance pass requires 95th-percentile GPU frame time at or below 16.67 ms and 99th percentile at or below 22 ms. No repeated shader-compilation or streaming stalls are acceptable during the measured encounter.

Do not equate average 60 FPS with stable 60 FPS. Do not count generated frames. Do not report an empty room as the performance of the complete encounter.

### Optimization order

First fix errors, redundant work, pathological bounds, unnecessary rebuilds, and excess shader cost. Then improve sampling and content placement. Then compare reconstruction modes and internal resolution.

Do not lower source visibility, omit the threat from reflections, or disable indirect light to pass a timing target.

A lower resolution, lower sample count, or revised depth budget must pass the same gameplay and image tests. Record the change. A new minimum GPU requires owner approval before it becomes a product claim.

If the first two-room scene misses the target, stop level expansion. Produce a measured bottleneck report and improve that scene.

## 18. Diagnostic views and capture tools

Provide diagnostic views as the related systems are implemented. Do not build every view before the first image.

The required set before production approval includes instance identifiers, geometric normals, material classes, depth, motion vectors, direct radiance, indirect radiance, specular radiance, raw combined radiance, and history validity.

Also provide emitter identifiers, ray or path work counts, invalid-value counts, acceleration-structure update time, denoiser time, and reconstruction time.

Diagnostic colors are not permitted in the production lighting output.

Provide a frame capture with a lossless display image and a linear high-dynamic-range image. Record camera, scene hash, seed, frame index, samples, path depth, exposure, render size, output size, and reconstruction state.

Captures must come from the running program. Do not use an image generator or a manually composed mock-up as evidence of engine output.

For temporal faults, capture a short frame sequence and the corresponding event log. One selected frame is not sufficient evidence of stable motion.

For performance investigation, support GPU captures with an available supported diagnostic tool. Do not make a commercial profiler a compulsory runtime dependency.

## 19. Acceptance tests

Implement these as small scenes and repeatable commands. Separate mathematical tests, rendering tests, performance tests, and human inspection.

Use explicit tolerances stored with each test. Choose tolerances before evaluating a candidate change. Do not enlarge them solely to turn a regression green.

Randomized image tests need fixed seeds and repeated independent batches. Use sampling uncertainty when comparing radiance, rather than demanding bit-identical floating-point images across drivers.

### Required test matrix

| ID | Test | Required evidence |
|---|---|---|
| T01 | Build and feature check | A clean configure/build/test log. Supported hardware launches. Unsupported hardware reports the missing feature. |
| T02 | Transform and layout | CPU tests and an asymmetric GPU scene verify transforms, normals, data offsets, and handedness. |
| T03 | No sources | All emission and environment radiance are zero. Reset history. Raw pre-display radiance is zero within numerical tolerance. |
| T04 | Closed enclosure | A source outside a sealed room does not light its interior. Opening the real door changes the result. |
| T05 | Off-screen reflection | A moving object absent from direct view remains visible in the mirror. Hit identifiers confirm the same world object. |
| T06 | Mirror occlusion | Moving the actual door changes the reflected view and light. No duplicate scene or camera texture is present. |
| T07 | Indirect color | A colored diffuse surface changes an indirectly lit patch. The change agrees with a higher-sample reference within measured uncertainty. |
| T08 | Emission accounting | Source sampling, surface sampling, and their combined estimator agree on patch averages after sufficient samples. |
| T09 | Source area | Resizing a fixed-radiance emitter updates geometry and sampling correctly. The result agrees with the reference. |
| T10 | Roughness and energy | Material tests have no invalid values or unexplained energy gain. Roughness changes do not break sampling weights. |
| T11 | Numerical offsets | Grazing surfaces, near contacts, thin gaps, and lamp housing do not show persistent acne or false light leaks. |
| T12 | Motion and history | Fast turns, moving reflections, doors, source changes, and newly visible surfaces have no unacceptable persistent trails. |
| T13 | Player and lamp | The body and lamp appear in reflections and shadows. Neither can pass through an opaque wall. |
| T14 | Game rules | Exposure, resolution, and reconstruction changes do not alter threat detection or objective state. |
| T15 | Restart and completion | The route can be completed, failed, and restarted without a soft lock or invalid state. |
| T16 | Performance | The fixed replay produces complete metadata and satisfies the approved timing and memory targets. |
| T17 | Reliability | Repeated scene reload, resize, pause, focus change, and restart do not crash or leak resources. |
| T18 | Distribution | A clean test machine runs the package without a developer SDK, asset download, or API credential. |

### Numerical test details

For T03, inspect the raw radiance before exposure, tone mapping, bloom, and UI. A proposed floating-point tolerance is `1e-6` in the documented radiance scale. This checks numerical residue, not a permitted ambient term.

For T04, create a dedicated sealed-room scene. Account for real gaps. Do not fail valid light through a deliberate opening or pass a leak by darkening the material.

For T08, use the same supported path family and depth limits for comparisons. First compare an emissive rectangle above diffuse surfaces. Add a mirror case separately.

For T09, do not expect illumination to remain constant when emitter area changes at fixed radiance. The test checks the correct response, not artificial brightness preservation.

For T10, use simple diffuse and conductor reference scenes. A white-furnace-style test is a diagnostic; document what the selected material model is expected to do.

For raw integrator comparisons, begin with patch-mean agreement within the larger of a predefined relative tolerance and three estimated standard errors. Select the tolerance for each scene before use. Increase samples when uncertainty is too large to decide.

### Temporal image targets

Store cropped image sequences around the mirror, door edge, and moving threat. Keep exposure fixed and compare equal render sizes.

After a complete source blackout and explicit history reset, no prior illumination should remain beyond two rendered frames. This is a reset correctness test.

For normal moving-lamp tests, use a provisional target of no obvious obsolete silhouette persisting longer than 100 ms. Define a local contrast threshold for automated measurement. Confirm the result at normal playback speed.

Do not use global image averages to hide a small but important reflected threat. Inspect the relevant region and object visibility.

Temporal quality cannot be accepted from numerical similarity alone. A reviewer must check whether the player can read the threat and control the lamp without distracting trails or flicker.

### Reliability target

Before external release, run a 30-minute loop containing gameplay, restart, source switching, and camera motion. Run a separate sequence with repeated resize and focus changes.

Report memory trends and failures. Do not claim reliability from a single successful launch.

## 20. Milestones and stop conditions

Progress through these gates. A milestone is complete only when its stated evidence exists.

### M0 — Environment and build

Inspect the operating system, toolchain, workspace, available GPU access, and network permissions. Preserve existing files.

Create the build, logging, test harness, and dependency records. Establish a clean CPU test run and, on Windows, a windowed application.

**Gate:** Reproducible build commands and an honest environment report. A missing GPU does not prevent portable math or parser work, but GPU tests remain NOT RUN.

### M1 — Hardware geometry

Create the device, resources, one triangle mesh, BLAS, TLAS, and a camera-ray compute pass. Show a diagnostic normal or identifier view. Add GPU timing and a resize test.

**Gate:** Real GPU capture, clean validation for the tested path, correct hit identifiers, and no software renderer presented as RTX execution.

### M2 — Raw light transport

Add the closed room, diffuse surface, area emitter, and mirror. Implement source sampling, surface sampling, probability weighting, and reference accumulation.

**Gate:** T03–T09 pass for the implemented features. Save raw and reference captures. Rough conductor support can follow the diffuse and mirror proof.

### M3 — Moving two-room scene

Add the door, movable lamp, mirrored test object, input, and deterministic replay. Implement current and previous render state correctly.

**Gate:** The real object is visible through the mirror outside direct view. Door and source changes are correct in raw and reference modes.

### M4 — Stable real-time image

Integrate NRD, including an approved mirror-guidance method. Add reconstruction only after the internal-resolution image is stable.

**Gate:** Temporal tests pass. Record the full moving sequence and performance results. If quality or timing fails, do not expand the level.

### M5 — Playable proof

Add collision, the simple player body, lamp sockets, interactions, sound, and the short threat encounter. Add an objective and restart.

**Gate:** A person can play the proof without console commands or a developer explaining each control. The image and performance gates remain passed.

### M6 — Six-room demo

Build the approved layout from the shared kit. Add the fuse route, circuit changes, return sequence, and exit.

**Gate:** The complete encounter meets the test matrix. No new asset bypasses the rendering or material rules.

### M7 — External test package

Add settings, input rebinding, accessible cues, build identification, license notices, and clean startup errors. Package the executable and required redistributable components.

**Gate:** A clean-machine test passes. The owner has actual gameplay capture, measured performance, and an accurate known-issues list.

### Expansion rule

Do not add new rooms, a second enemy, new material families, or a general editor to compensate for a weak two-room proof.

When a gate fails, reproduce it, classify the cause, and change the smallest relevant system. Keep the failure visible in status until the evidence changes.

## 21. Required command interface

Create and test commands with equivalent behavior to this interface. These names are requirements for the future implementation; they are not commands that already exist.

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure

cmake --preset windows-release
cmake --build --preset windows-release

.\build\windows-release\bin\LastCircuit.exe --scene rt_triangle --validate
.\build\windows-release\bin\LastCircuit.exe --scene mirror_lab --mode raw
.\build\windows-release\bin\LastCircuit.exe --scene mirror_lab --mode reference --spp 4096 --capture .\artifacts\reference
.\build\windows-release\bin\LastCircuit.exe --scene mirror_lab --replay .\tests\replay\mirror_lab.json --benchmark-seconds 180 --report .\artifacts\benchmark.json
.\build\windows-release\bin\LastCircuit.exe --scene last_circuit
```

Implement the paths through committed presets rather than hard-coded local machine locations. Adapt the exact executable output path once and keep the documentation correct.

Unknown options must fail clearly. A requested reference sample count must not be silently capped. If a test cannot run, return and record a distinct skipped or unavailable result.

Reference mode must freeze the world and reset accumulation on incompatible changes. Batch long captures into safe GPU workloads. Do not issue a single unbounded dispatch that risks a device timeout.

The benchmark report must include build and content hashes, device details, actual resolutions, all quality settings, timing distributions, memory information, replay identifier, and test duration.

Provide a packaging command and a smoke-test command before M7. Do not require users to launch the game from a developer terminal.

## 22. Working rules for the coding agent

### Execute, verify, report

Use a short loop: inspect, choose one testable change, implement it, run the relevant checks, inspect results, and update status.

Do not generate hundreds of untested files in one operation. Preserve a working baseline when one exists. Keep changes reviewable.

Read the actual files before editing. Do not assume a function, library, or command exists because an earlier plan named it.

Use official documentation and installed headers for changing interfaces. Record source versions. Do not spend the session comparing every possible architecture after the starting choices are fixed.

### Evidence labels

Use these labels precisely:

| Label | Meaning |
|---|---|
| WRITTEN | The code or asset exists. |
| COMPILED | The relevant build completed successfully. |
| CPU TESTED | The stated non-GPU checks ran and passed. |
| GPU EXECUTED | The stated path ran on an identified GPU. |
| IMAGE CHECKED | Captured output was inspected against a stated criterion. |
| PERFORMANCE CHECKED | The defined measurement ran with complete settings. |
| PASSED | All required evidence for that gate exists. |
| FAILED | A required check ran and did not satisfy its criterion. |
| NOT RUN | The check did not run. State the reason. |

A successful compiler exit is not a rendering test. A generated screenshot description is not a screenshot. A theoretical ray count is not a benchmark.

### Limited environments

On a non-Windows machine, keep platform-independent tests useful. Validate scene data, math, sampling helpers, and simulation where possible. Do not build a browser raster demo and call it the requested engine.

A software adapter or CPU reference calculation may support a diagnostic test. It cannot pass the hardware or performance gate, and it must not become a production fallback.

If terminal or file access is unavailable, provide the concrete file contents and commands that the owner can execute. Clearly identify what was not created or tested. Do not pretend to have edited a repository.

### Changes to the plan

Routine code organization and local test improvements may proceed with a decision-log entry.

Changes to the required platform, full-path-tracing rule, offline operation, supported hardware claim, dependency license obligations, or six-room scope need owner approval.

For a proposed advanced optimization, state the observed problem, baseline evidence, chosen change, acceptance test, and rollback path.

When several agents are available, assign bounded tasks with shared interface contracts. One agent owns integration. Do not let parallel agents independently replace the renderer or dependency system.

### Communication

Provide brief progress updates during long work. Report concrete results and the next test, not repeated promises.

At the end of a session, state what changed, what actually ran, what failed or remains untested, and the single next task. Do not end with a generic offer to start work that has already been requested.

Use clear technical English. Use short sentences and define specialized terms when they first matter.

## 23. Persistent project records

Keep this brief as the product authority. Do not rewrite it to hide a missed requirement.

Maintain the following records as implementation proceeds:

**`docs/STATUS.md`:** Current milestone, completed gates, exact tested build, actual test outcomes, active blocker, and next task.

**`docs/BUILD.md`:** Prerequisites, dependency setup, exact commands, build presets, runtime requirements, and packaging instructions.

**`docs/ARCHITECTURE.md`:** Module ownership, scene flow, data lifetime, coordinate conventions, and rendering sequence.

**`docs/RENDERING.md`:** Material model, source units, sampling equations, depth semantics, buffer contracts, guide transforms, history policy, and known approximations.

**`docs/TESTS.md`:** Commands, scenes, input sequences, predetermined tolerances, accepted baselines, and manual inspection criteria.

**`docs/DECISIONS.md`:** Date, issue, evidence, decision, consequence, and a rollback note where needed.

**`docs/KNOWN_ISSUES.md`:** Reproduction steps, severity, affected build, and the planned verification for each fix.

**`docs/DEPENDENCIES.md`:** Exact source revisions, license notices, setup method, and distribution conditions.

Use this status template:

```text
Milestone:
Build or commit:
Environment:
Implemented in this session:
Checks actually run:
GPU and driver used:
Image evidence:
Performance evidence:
Failed checks:
Checks not run and reasons:
Changed assumptions:
Next concrete task:
```

Do not fill empty result fields with plausible values. Use NOT RUN when appropriate.

## 24. Definition of a convincing finished demo

The engine opens into a working game, not a diagnostic menu. The player can learn the controls, use the lamp, recognize the threat indirectly, retrieve the fuse, survive the return, and exit.

The low-polygon surfaces have clear shapes. The image does not rely on dense textures, fog, or a wet-floor effect to appear detailed.

The lamp, player, door, fan, and threat share one scene. Their reflections and shadows remain correct when they move outside the direct camera view.

The mirror is readable in motion. The dark return route remains playable. Sources have understandable locations and operating states.

The measured target is satisfied on the declared test hardware. A second supported RTX device receives at least a compatibility test before a broad RTX support claim.

The package runs offline and includes required notices. Settings explain unsupported features without presenting nonfunctional switches.

The owner receives the source, exact build process, playable package, actual capture, benchmark report, and known-issues list.

For an initial usability check, observe at least three new players without explaining the intended reveal. Record whether they understand the lamp, notice the indirect threat, and complete the objective. Do not present this small check as proof of market demand.

**Final rule:** A small, stable, playable scene is more valuable than an ambitious engine that has not passed its first moving-light test.

## 25. Immediate assignment

Begin now with M0 and M1. Inspect the actual environment and preserve user work.

Create the project, a working build, basic tests, GPU feature reporting, and the first hardware-ray-traced triangle scene. Add a reproducible launch command and real timing instrumentation.

Then proceed toward M2 and the two-room proof. Use generated assets. Keep the initial dependency set small.

Do not begin by implementing the full six-room level. Do not wait for imported art. Do not add a raster-lighting fallback.

Do not stop with a proposal when implementation tools are available. End the session with actual work, actual verification, and a precise status report.

## 26. Technical references

These sources support the named techniques and API contracts. They do not establish that this project meets its proposed targets. Read the version that matches the dependency actually selected.

**[R1] Microsoft — DirectX Raytracing functional specification.** Hardware ray queries, acceleration structures, ray semantics, and DXR tiers.  
https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html

**[R2] NVIDIA — Best practices for using RTX ray tracing.** Geometry organization, opaque geometry, and acceleration-structure guidance.  
https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/

**[R3] Microsoft — Device Removed Extended Data.** GPU fault diagnosis. Use the interfaces in the selected SDK.  
https://microsoft.github.io/DirectX-Specs/d3d/DeviceRemovedExtendedData.html

**[R4] NVIDIA — Solving self-intersection artifacts in DirectX Raytracing.** Robust secondary-ray origins and numerical error bounds.  
https://developer.nvidia.com/blog/solving-self-intersection-artifacts-in-directx-raytracing/

**[R5] Physically Based Rendering, fourth edition — A Better Path Tracer.** Source sampling, multiple importance sampling, path weights, and termination.  
https://pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer

**[R6] NVIDIA — NRD.** Denoiser integration, signal and guide data, and Primary Surface Replacement.  
https://github.com/NVIDIA-RTX/NRD

**[R7] NVIDIA — NRD Sample.** An implementation reference for path tracing and denoising integration.  
https://github.com/NVIDIA-RTX/NRD-Sample

**[R8] NVIDIA — Streamline.** Feature integration and support checks. Follow the selected release's Super Resolution guide.  
https://github.com/NVIDIA-RTX/Streamline

**[R9] NVIDIA — Streamline DLSS Ray Reconstruction programming guide.** The separate input and integration contract for Ray Reconstruction.  
https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_RR.md

**[R10] Khronos — glTF 2.0 specification.** Asset structure, units, coordinates, and material conventions.  
https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html

**[R11] Microsoft — Memory management strategies.** Local and non-local graphics memory budgets.  
https://learn.microsoft.com/en-us/windows/win32/direct3d12/memory-management-strategies

**[R12] NVIDIA — RTX Path Tracing.** A reference for real-time and reference path-tracer organization. Study the needed systems; do not adopt its full scope automatically.  
https://github.com/NVIDIA-RTX/RTXPT

**[R13] Microsoft — Direct3D 12 ray-tracing samples.** Minimal API examples and a reference for device and pipeline setup.  
https://learn.microsoft.com/en-us/samples/microsoft/directx-graphics-samples/d3d12-raytracing-samples-win32/

**[R14] OpenAI — Custom instructions with AGENTS.md.** Repository instruction discovery and instruction-size limits. Keep the root instruction file short and point it to this brief.  
https://developers.openai.com/codex/guides/agents-md

---

**Document ends here. No benchmark or implementation result is implied by this specification.**
