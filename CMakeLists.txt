cmake_minimum_required(VERSION 3.24)

project(
	Ysmir
	VERSION 0.9.0
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_XBYAK OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_SE OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)

# Keep the instruction-length decoder available for offline source builds.
set(FETCHCONTENT_SOURCE_DIR_HDE64 "${CMAKE_CURRENT_SOURCE_DIR}/external/minhook"
	CACHE PATH "Vendored MinHook source for CommonLib patch validation")

add_subdirectory(external/CommonLibSSE-NG EXCLUDE_FROM_ALL)
add_subdirectory(external/minhook EXCLUDE_FROM_ALL)

add_library(${PROJECT_NAME} SHARED
	src/main.cpp
	src/shout_dispatch.cpp
	src/progression.cpp
	src/elemental_fury.cpp
	src/projectile_visuals.cpp
	src/storm_call.cpp
	src/true_cone.cpp
)

target_compile_definitions(${PROJECT_NAME} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_link_libraries(${PROJECT_NAME} PRIVATE CommonLibSSE::CommonLibSSE minhook)
target_precompile_headers(${PROJECT_NAME} PRIVATE <RE/Skyrim.h> <SKSE/SKSE.h>)

set_target_properties(${PROJECT_NAME} PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Data/SKSE/Plugins"
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Data/SKSE/Plugins"
	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

foreach(CONFIG_TYPE DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
	set_target_properties(${PROJECT_NAME} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE} "${CMAKE_BINARY_DIR}/Data/SKSE/Plugins"
		LIBRARY_OUTPUT_DIRECTORY_${CONFIG_TYPE} "${CMAKE_BINARY_DIR}/Data/SKSE/Plugins"
		ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_TYPE} "${CMAKE_BINARY_DIR}/lib"
	)
endforeach()

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_CURRENT_SOURCE_DIR}/package/Data/SKSE/Plugins/Ysmir.ini"
		"$<TARGET_FILE_DIR:${PROJECT_NAME}>/Ysmir.ini"
)
