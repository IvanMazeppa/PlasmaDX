cmake_minimum_required(VERSION 3.21)
project(PlasmaDX LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# DXR-0015: Safe minimal baseline option
option(PLASMADX_MINIMAL_BASELINE "Skip DXR init and force OS D3D12 for baseline testing" OFF)

add_executable(PlasmaDX WIN32
    src/main.cpp
    src/core/App.cpp
    src/core/App.h
    src/core/Camera.cpp
    src/core/Camera.h
    src/core/D3D12AgilitySDK.cpp
    src/dxr/ASBuilder.cpp
    src/dxr/ASBuilder.h
    src/dxr/Pipeline.cpp
    src/dxr/Pipeline.h
    src/dxr/SBT.cpp
    src/dxr/SBT.h
    src/renderer/Composite.cpp
    src/renderer/Composite.h
    src/utils/FileLoader.cpp
    src/utils/FileLoader.h
    src/utils/DescriptorHeap.cpp
    src/utils/DescriptorHeap.h
    src/utils/Env.cpp
    src/utils/Env.h
    src/utils/Logger.h
    src/volumetric/Particles.cpp
    src/volumetric/Particles.h
    src/volumetric/DensityVolume.cpp
    src/volumetric/DensityVolume.h
    src/volumetric/VolumeRenderer.cpp
    src/volumetric/VolumeRenderer.h
    src/volumetric/RayMarcher.cpp
    src/volumetric/RayMarcher.h
)

# Unicode build
target_compile_definitions(PlasmaDX PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

# Define baseline compile-time flag when requested
if(PLASMADX_MINIMAL_BASELINE)
    target_compile_definitions(PlasmaDX PRIVATE PLASMADX_MINIMAL_BASELINE=1)
    message(STATUS "PLASMADX_MINIMAL_BASELINE is ON: DXR init will be skipped")
endif()

# Warnings
if(MSVC)
    target_compile_options(PlasmaDX PRIVATE /W4 /permissive- /Zc:preprocessor /EHsc)
endif()

# Link against Windows SDK libs
# d3dcompiler is optional here; included for shader compilation path later
# dxguid provides common GUIDs used by D3D components
# Note: These come from the Windows 10/11 SDK installed with Visual Studio
find_library(D3D12_LIB d3d12)
find_library(DXGI_LIB dxgi)
find_library(DXGUID_LIB dxguid)
find_library(D3DCOMPILER_LIB d3dcompiler)

# Fall back to plain names if find_library did not resolve
if (NOT D3D12_LIB)
    set(D3D12_LIB d3d12)
endif()
if (NOT DXGI_LIB)
    set(DXGI_LIB dxgi)
endif()
if (NOT DXGUID_LIB)
    set(DXGUID_LIB dxguid)
endif()
if (NOT D3DCOMPILER_LIB)
    set(D3DCOMPILER_LIB d3dcompiler)
endif()

# Link DXC library
target_link_libraries(PlasmaDX PRIVATE
    ${D3D12_LIB}
    ${DXGI_LIB}
    ${DXGUID_LIB}
    ${D3DCOMPILER_LIB}
    "${CMAKE_SOURCE_DIR}/lib/dxcompiler.lib"
)

# Output directories
set_target_properties(PlasmaDX PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# Include Agility SDK headers
target_include_directories(PlasmaDX PRIVATE "${CMAKE_SOURCE_DIR}/include")

# Compile DXR shaders with DXC
set(DXC_TOOL "${CMAKE_SOURCE_DIR}/tools/dxc.exe")
set(DXR_SHADER_SOURCE "${CMAKE_SOURCE_DIR}/shaders/dxr/raytracing_lib.hlsl")
set(DXR_SHADER_OUTPUT "${CMAKE_SOURCE_DIR}/shaders/dxr/raytracing_lib.dxil")

add_custom_command(
    OUTPUT ${DXR_SHADER_OUTPUT}
    COMMAND ${DXC_TOOL} -T lib_6_3 -Fo ${DXR_SHADER_OUTPUT} ${DXR_SHADER_SOURCE}
    DEPENDS ${DXR_SHADER_SOURCE}
    COMMENT "Compiling DXR shaders with DXC"
)

# Compile volumetric compute shaders with DXC (VOL_0001)
set(VOL_UPDATE_SHADER_SOURCE "${CMAKE_SOURCE_DIR}/shaders/vol/particles_update.hlsl")
set(VOL_UPDATE_SHADER_OUTPUT "${CMAKE_SOURCE_DIR}/shaders/vol/particles_update.dxil")
set(VOL_DEBUG_SHADER_SOURCE "${CMAKE_SOURCE_DIR}/shaders/vol/particles_debug_pattern.hlsl")
set(VOL_DEBUG_SHADER_OUTPUT "${CMAKE_SOURCE_DIR}/shaders/vol/particles_debug_pattern.dxil")

add_custom_command(
    OUTPUT ${VOL_UPDATE_SHADER_OUTPUT}
    COMMAND ${DXC_TOOL} -T cs_6_0 -E main -Fo ${VOL_UPDATE_SHADER_OUTPUT} ${VOL_UPDATE_SHADER_SOURCE}
    DEPENDS ${VOL_UPDATE_SHADER_SOURCE}
    COMMENT "Compiling particles update compute shader"
)

add_custom_command(
    OUTPUT ${VOL_DEBUG_SHADER_OUTPUT}
    COMMAND ${DXC_TOOL} -T cs_6_0 -E main -Fo ${VOL_DEBUG_SHADER_OUTPUT} ${VOL_DEBUG_SHADER_SOURCE}
    DEPENDS ${VOL_DEBUG_SHADER_SOURCE}
    COMMENT "Compiling particles debug pattern compute shader"
)

# Compile density volume shaders (VOL_0002)
set(DENSITY_FILL_SHADER_SOURCE "${CMAKE_SOURCE_DIR}/shaders/density_fill.hlsl")
set(DENSITY_FILL_SHADER_OUTPUT "${CMAKE_SOURCE_DIR}/shaders/density_fill.dxil")
set(DENSITY_SLICE_SHADER_SOURCE "${CMAKE_SOURCE_DIR}/shaders/density_slice.hlsl")
set(DENSITY_SLICE_SHADER_OUTPUT "${CMAKE_SOURCE_DIR}/shaders/density_slice.dxil")
set(DENSITY_FILL_SPHERE_SHADER_SOURCE "${CMAKE_SOURCE_DIR}/shaders/vol/density_fill_sphere.hlsl")
set(DENSITY_FILL_SPHERE_SHADER_OUTPUT "${CMAKE_SOURCE_DIR}/shaders/vol/density_fill_sphere.dxil")

add_custom_command(
    OUTPUT ${DENSITY_FILL_SHADER_OUTPUT}
    COMMAND ${DXC_TOOL} -T cs_6_0 -E main -Fo ${DENSITY_FILL_SHADER_OUTPUT} ${DENSITY_FILL_SHADER_SOURCE}
    DEPENDS ${DENSITY_FILL_SHADER_SOURCE}
    COMMENT "Compiling density fill compute shader"
)

add_custom_command(
    OUTPUT ${DENSITY_SLICE_SHADER_OUTPUT}
    COMMAND ${DXC_TOOL} -T cs_6_0 -E main -Fo ${DENSITY_SLICE_SHADER_OUTPUT} ${DENSITY_SLICE_SHADER_SOURCE}
    DEPENDS ${DENSITY_SLICE_SHADER_SOURCE}
    COMMENT "Compiling density slice compute shader"
)

add_custom_command(
    OUTPUT ${DENSITY_FILL_SPHERE_SHADER_OUTPUT}
    COMMAND ${DXC_TOOL} -T cs_6_0 -E main -Fo ${DENSITY_FILL_SPHERE_SHADER_OUTPUT} ${DENSITY_FILL_SPHERE_SHADER_SOURCE}
    DEPENDS ${DENSITY_FILL_SPHERE_SHADER_SOURCE}
    COMMENT "Compiling density fill analytic sphere compute shader"
)

# Compile ray marching shader (VOL_0003)
set(RAYMARCH_SHADER_SOURCE "${CMAKE_SOURCE_DIR}/shaders/raymarch.hlsl")
set(RAYMARCH_SHADER_OUTPUT "${CMAKE_SOURCE_DIR}/shaders/raymarch.dxil")
set(RAYMARCH_CS_SHADER_SOURCE "${CMAKE_SOURCE_DIR}/shaders/vol/ray_march_cs.hlsl")
set(RAYMARCH_CS_SHADER_OUTPUT "${CMAKE_SOURCE_DIR}/shaders/vol/ray_march_cs.dxil")

# Compile temporal accumulation shader (VOL_0004)
set(TEMPORAL_SHADER_SOURCE "${CMAKE_SOURCE_DIR}/shaders/vol/temporal_accumulation_cs.hlsl")
set(TEMPORAL_SHADER_OUTPUT "${CMAKE_SOURCE_DIR}/shaders/vol/temporal_accumulation_cs.dxil")

add_custom_command(
    OUTPUT ${RAYMARCH_SHADER_OUTPUT}
    COMMAND ${DXC_TOOL} -T cs_6_0 -E main -Fo ${RAYMARCH_SHADER_OUTPUT} ${RAYMARCH_SHADER_SOURCE}
    DEPENDS ${RAYMARCH_SHADER_SOURCE}
    COMMENT "Compiling ray marching compute shader"
)

add_custom_command(
    OUTPUT ${RAYMARCH_CS_SHADER_OUTPUT}
    COMMAND ${DXC_TOOL} -T cs_6_0 -E main -Fo ${RAYMARCH_CS_SHADER_OUTPUT} ${RAYMARCH_CS_SHADER_SOURCE}
    DEPENDS ${RAYMARCH_CS_SHADER_SOURCE}
    COMMENT "Compiling VOL_0003 ray march compute shader"
)

add_custom_command(
    OUTPUT ${TEMPORAL_SHADER_OUTPUT}
    COMMAND ${DXC_TOOL} -T cs_6_0 -E main -Fo ${TEMPORAL_SHADER_OUTPUT} ${TEMPORAL_SHADER_SOURCE}
    DEPENDS ${TEMPORAL_SHADER_SOURCE}
    COMMENT "Compiling VOL_0004 temporal accumulation compute shader"
)

# Add shader compilation as dependency
add_custom_target(compile_shaders DEPENDS
    ${DXR_SHADER_OUTPUT}
    ${VOL_UPDATE_SHADER_OUTPUT}
    ${VOL_DEBUG_SHADER_OUTPUT}
    ${DENSITY_FILL_SHADER_OUTPUT}
    ${DENSITY_SLICE_SHADER_OUTPUT}
    ${DENSITY_FILL_SPHERE_SHADER_OUTPUT}
    ${RAYMARCH_SHADER_OUTPUT}
    ${RAYMARCH_CS_SHADER_OUTPUT}
    ${TEMPORAL_SHADER_OUTPUT}
)
add_dependencies(PlasmaDX compile_shaders)

# Copy shaders to output directory
add_custom_command(TARGET PlasmaDX POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/shaders"
    "$<TARGET_FILE_DIR:PlasmaDX>/shaders"
)

# Copy Agility SDK DLLs to output directory
if(EXISTS "${CMAKE_SOURCE_DIR}/bin/D3D12")
    add_custom_command(TARGET PlasmaDX POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/bin/D3D12"
        "$<TARGET_FILE_DIR:PlasmaDX>/D3D12"
    )
else()
    message(WARNING "Agility SDK not found at bin/D3D12; skipping copy")
endif()

# Copy DXC DLLs to output directory
if(EXISTS "${CMAKE_SOURCE_DIR}/bin/dxcompiler.dll")
    add_custom_command(TARGET PlasmaDX POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/bin/dxcompiler.dll"
        "$<TARGET_FILE_DIR:PlasmaDX>/dxcompiler.dll"
    )
else()
    message(WARNING "dxcompiler.dll not found; skipping copy")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/bin/dxil.dll")
    add_custom_command(TARGET PlasmaDX POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/bin/dxil.dll"
        "$<TARGET_FILE_DIR:PlasmaDX>/dxil.dll"
    )
else()
    message(WARNING "dxil.dll not found; skipping copy")
endif()
