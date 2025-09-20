Here’s a quick, small-steps guide to get a useful PIX GPU capture and inspect exactly what we need.
1) Launch a GPU-capturable session
Open PIX for Windows (v2507.11).
Click “New GPU Capture” (or “Home → GPU Captures → Launch for GPU Capture”).
Set:
Executable: D:\Users\dilli\AndroidStudioProjects\PlasmaDX\build-vs2022b\Debug\PlasmaDX.exe
Working Directory: D:\Users\dilli\AndroidStudioProjects\PlasmaDX
Environment Variables:
PLASMADX_DISABLE_DXR=1
Leave other options default; click “Launch”.
Tip: If it’s already running, use “Attach to Process for GPU Capture”, select PlasmaDX.exe.
2) Capture a frame
Let the app run for 2–3 seconds while moving the camera (WASD + mouse).
Hit “Capture” (or F12). PIX will pause the app and open the captured frame.
Repeat a couple of times after pressing F4 to cycle debug modes (Off → RayDir → Bounds → DensityProbe), so you get one capture per mode.
3) Open the GPU capture and find the relevant events
In the capture tab:
Open the “Event List” pane.
Press Ctrl+F, search “Dispatch”.
You should see two types of dispatches each frame:
Density fill (3D volume): ThreadGroupCount around 16×16×16 (for 128³).
Ray marcher (HDR): ThreadGroupCount around 120×68×1 (for 1920×1080, 16×16 threads).
If you don’t see the 16×16×16 one, we’re not filling the volume that frame.
4) Inspect constant buffers bound to the marcher
Click a “Dispatch” for the ray marcher (the ~120×68×1 one).
Open the “Pipeline” or “State” pane → Root Signature / Resources.
Check CBV b0/b1 contents:
b0 (camera): verify g_invViewProjMatrix has non-identity values; g_cameraPosition changes as you move.
b1 (volume/params): verify
g_volumeMin ~ (-1,-1,-1), g_volumeMax ~ (1,1,1)
g_screenSize ~ (1920,1080)
g_exposure/densityScale/absorption/stepSize/maxSteps look reasonable
g_debugMode equals 0/1/2/3 depending on F4 state
5) Verify resource state barriers and history
In the Event List, select:
The density fill dispatch → ensure a UAV barrier follows on the density 3D texture.
The marcher dispatch → ensure a UAV barrier follows on HDR.
The Composite setup → ensure HDR transitions UAV→SRV before sampling.
Open “Resources” (or via the Pipeline view) and right-click the density texture → “View History”:
Confirm the 3D density shows non-uniform values after the fill dispatch (for the sphere baseline).
Do the same for the HDR target after the marcher.
6) Validate debug modes visually (per capture)
RayDir: the image should be a smooth RGB field that rotates with the camera (proves raygen).
Bounds: magenta = miss, green = hit from outside, yellow = started inside. If mostly yellow, you are inside the AABB; back up (S key) or shrink the volume later.
DensityProbe: should show a crisp circle where rays enter the analytic sphere (proves density + ray entry).
7) Save and share
Save the capture as .wpix.
Optional: Export the Event List to text (like your GPU 2/6 files) for quick review.
If capture doesn’t work:
Run PIX as Administrator.
Ensure the app isn’t launched with conflicting overlays.
In PIX’s launch options, leave “Restrict to DX12” enabled (default).
What to report back (short checklist):
Did you see the 16×16×16 density dispatch? Y/N
In b1, are g_volumeMin/Max = [-1,1] and g_debugMode changing per F4? Y/N
Resource History: density 3D texture non-uniform after fill? Y/N
In Bounds mode, are most pixels yellow (you’re inside) or green (outside)?
DensityProbe: do you see a clear circle? Y/N
No need to wire docs into MCP for this; these steps are enough. If you want, I can also switch the code to call the analytic sphere fill by default (and add a toggle) to make the probe test immediate.
there seems to be some discrepancies between your descriptions of PIX and what i'm seeing. Before we do this so i don't forget i wanted to mention this. Since Agility has been shown to work and the older version we're using has several large differences/features compared to 1.717.1. could we re-upgrade back to that? would that cause any issues?

I’m going to quickly check the CMakeLists.txt to see how Agility SDK DLLs and headers are wired, so I can give you precise steps and confirm what needs changing for a 1.717.1 upgrade.
I'll scan the Agility bootstrap file to see how the SDK version/path are defined so I can tell you exactly what to change for 1.717.1.
I reviewed CMakeLists.txt and src/core/D3D12AgilitySDK.cpp: we’re exporting D3D12SDKVersion = 616 and copying bin/D3D12 to the output. Upgrading to 1.717.1 is straightforward.
Short answer
Yes, you can move to Agility 1.717.1 safely. No code changes are required beyond bumping the exported SDK version and swapping the runtime DLLs/headers.
Main risk: a mismatch between D3D12SDKVersion (compiled in) and the DLLs you ship in D3D12/ will prevent device creation. Keep them in lock‑step.
Exact steps
1) Replace headers
Update the repo’s include/ with the 1.717.1 Agility SDK include directory.
2) Replace runtime DLLs
Put the 1.717.1 runtime into bin/D3D12/:
D3D12Core.dll
d3d12SDKLayers.dll (optional, for debug)
PDBs are optional
Our CMake already copies bin/D3D12 to $<TARGET_FILE_DIR>/D3D12.
3) Bump the exported version
In src/core/D3D12AgilitySDK.cpp, change:
D3D12SDKVersion = 717;
Keep D3D12SDKPath = "D3D12\\";
4) DXC pair (unchanged or update together)
Ensure bin/dxcompiler.dll and bin/dxil.dll come from the same DXC package (they’re independent of Agility but should match each other). Our CMake copies them if present.
5) Rebuild and run
Reconfigure/build, then run. With the debug layer on, you’ll see messages confirming the Agility version; PIX will also show the D3D12Core version in capture details.
Gotchas to avoid
Version mismatch (717 constant + 616 DLLs, or vice versa) → device creation fails.
Very old Windows 10 builds needed a servicing update for Agility; your Win11 build is fine.
We don’t use new 1.7 features (enhanced barriers, work graphs), so no code changes are needed.
PIX note re “discrepancies”
PIX v2507.11 renamed some panes; “Event List” may appear as the main timeline events grid, and “Pipeline/State” is shown as “Compute/Graphics Pipeline State.” Right‑click a resource and choose “View History” to see UAV writes/reads; the names differ slightly but the workflow is the same.
Want me to flip D3D12SDKVersion to 717 now? If you confirm the 1.717.1 DLLs are in bin/D3D12/, I can make the edit and rebuild.
Updated files examined: CMakeLists.txt, src/core/D3D12AgilitySDK.cpp.
