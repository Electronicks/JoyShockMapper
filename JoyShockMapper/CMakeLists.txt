set (BINARY_NAME ${PROJECT_NAME})

git_describe(GIT_TAG --tags --dirty=_d)

#parse the version information into pieces.
string(REGEX REPLACE "^v([0-9]+)\\..*" "\\1" JSM_VERSION_MAJOR "${GIT_TAG}")
string(REGEX REPLACE "^v[0-9]+\\.([0-9]+).*" "\\1" JSM_VERSION_MINOR "${GIT_TAG}")
string(REGEX REPLACE "^v[0-9]+\\.[0-9]+\\.([0-9]+).*" "\\1" JSM_VERSION_PATCH "${GIT_TAG}")
set(JSM_VERSION "v${JSM_VERSION_MAJOR}.${JSM_VERSION_MINOR}.${JSM_VERSION_PATCH}")

message(STATUS "JSM version is " ${JSM_VERSION})


configure_file(include/JSMVersion.h.in include/JSMVersion.h)

add_executable (
    ${BINARY_NAME}
    src/main.cpp
    src/operators.cpp
    src/CmdRegistry.cpp
    src/quatMaths.cpp
    src/ButtonHelp.cpp
 	src/DigitalButton.cpp
	src/MotionImpl.cpp
	src/Mapping.cpp
    src/TriggerEffectGenerator.cpp
    src/AutoLoad.cpp
	src/SettingsManager.cpp
    include/TriggerEffectGenerator.h
    include/InputHelpers.h
    include/PlatformDefinitions.h
    include/TrayIcon.h
    include/Whitelister.h
    include/CmdRegistry.h
    include/JSMAssignment.hpp
    include/JSMVariable.hpp
    include/Whitelister.h
    include/JoyShockMapper.h
    include/ColorCodes.h
    include/MotionIf.h
	include/Gamepad.h
	include/DigitalButton.h
	include/JslWrapper.h
	include/Mapping.h
    include/AutoLoad.h
	include/SettingsManager.h
	include/AutoConnect.h
    include/SettingsManager.h
    include/Stick.h
    include/JoyShock.h
)

if (WINDOWS)
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${BINARY_NAME})
    set_property(TARGET ${BINARY_NAME} PROPERTY VS_DEBUGGER_COMMAND_ARGUMENTS "${CMAKE_CURRENT_SOURCE_DIR}/dist")

    if(SDL)
        target_sources (
            ${BINARY_NAME} PRIVATE
            src/SDLWrapper.cpp
            src/dimgui/Application.cpp
            src/dimgui/InputSelector.cpp
            include/dimgui/Application.h
            include/dimgui/InputSelector.h
        )
        add_definitions(-DSDL)
    else()
        target_sources (
            ${BINARY_NAME} PRIVATE
            src/JslWrapper.cpp
        )
    endif()

    target_sources (
        ${BINARY_NAME} PRIVATE
        src/win32/InputHelpers.cpp
        src/win32/PlatformDefinitions.cpp
        src/win32/WindowsTrayIcon.cpp        include/win32/WindowsTrayIcon.h
        src/win32/Gamepad.cpp
        src/win32/HidHideApi.cpp             include/HidHideApi.h
        src/win32/HidHideWhitelister.cpp
        "Win32 Dialog.rc"                    include/win32/resource.h
    )

    # ViGEmClient
    CPMAddPackage (
        NAME ViGEmClient
        GITHUB_REPOSITORY nefarius/ViGEmClient
        GIT_TAG 726404ef5590ea4bfcc7d9fa40cadcb2638a5b82 # updated cmakefile
        # VERSION 1.21.222.0
    )
    
    add_dependencies(${BINARY_NAME} ViGEmClient)
    
    target_link_libraries (
        ${BINARY_NAME} PRIVATE
		ViGEmClient
	)
	
    target_include_directories (
        ${BINARY_NAME} PUBLIC
        "${ViGEmClient_SOURCE_DIR}/include"
    )

    if(SDL)
        add_custom_command(
            TARGET ${BINARY_NAME}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                "$<TARGET_FILE:SDL3-shared>"
                "$<TARGET_FILE_DIR:${BINARY_NAME}>/$<TARGET_FILE_NAME:SDL3-shared>"
        )
    else()
		add_custom_command(
			TARGET ${BINARY_NAME}
			POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy
				"$<TARGET_FILE:JoyShockLibrary>"
				"$<TARGET_FILE_DIR:${PROJECT_NAME}>/$<TARGET_FILE_NAME:JoyShockLibrary>"
		)
    endif()
	
    set_target_properties (
        ${BINARY_NAME} PROPERTIES
         WIN32_EXECUTABLE ON
         VS_STARTUP_PROJECT "${BINARY_NAME}"
         VS_DEBUGGER_WORKING_DIRECTORY "$(ProjectDir)/$(Configuration)/"
		 VS_DEBUGGER_COMMAND_ARGUMENTS "$(SolutionDir)../dist"
    )

	 add_definitions(/bigobj)
endif ()

if (LINUX)
    if(SDL OR NOT DEFINED SDL)
        target_sources (
                ${BINARY_NAME} PRIVATE
                src/SDLWrapper.cpp
        )
        add_definitions(-DSDL)
    else()
        target_sources (
                ${BINARY_NAME} PRIVATE
                src/JslWrapper.cpp
        )
    endif()

    target_sources (
        ${BINARY_NAME} PRIVATE
        src/linux/Init.cpp
        src/linux/InputHelpers.cpp
        src/linux/PlatformDefinitions.cpp
        src/linux/StatusNotifierItem.cpp    include/linux/StatusNotifierItem.h
        src/linux/Whitelister.cpp
		src/linux/Gamepad.cpp
    )
endif ()

target_compile_definitions (
    ${BINARY_NAME} PRIVATE
    -DAPPLICATION_NAME="JoyShockMapper"
    -DAPPLICATION_RDN="com.github."
)

target_include_directories (
    ${BINARY_NAME} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/${PROJECT_NAME}/include"
)

if(SDL OR NOT DEFINED SDL)
	set(SDL_HIDAPI ON)
	CPMAddPackage (
		NAME SDL3
        GITHUB_REPOSITORY libsdl-org/SDL
        GIT_TAG release-3.2.x
	)

    # CPMAddPackage (
	# 	NAME SDL_ttf
	# 	GITHUB_REPOSITORY libsdl-org/SDL_ttf
	# 	GIT_TAG release-2.20.1
	# )

    #CPMAddPackage (
	#	NAME SDL_image
	#	GITHUB_REPOSITORY libsdl-org/SDL
	#	GIT_TAG release-2.6.2
	#)
	
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    # Dear ImGUI
    CPMAddPackage (
        NAME imgui
        GITHUB_REPOSITORY ocornut/imgui
        GIT_TAG v1.91.8-docking
    )

	set_target_properties(SDL_uclibc PROPERTIES FOLDER "SDL")
    set_target_properties(SDL3-shared PROPERTIES FOLDER "SDL")
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    target_link_libraries (
        ${BINARY_NAME} PRIVATE
        Platform::Dependencies
        SDL3::SDL3-shared
    )
    
    install (
       TARGETS ${BINARY_NAME} SDL3-shared
        RUNTIME DESTINATION ${PACKAGE_DIR}

    )
    # ImPlot
    CPMAddPackage (
        NAME implot
        GITHUB_REPOSITORY epezent/implot
        GIT_TAG 1f7a8c0314d838a76695bccebe0f66864f507bc0
    )

    add_library(imgui STATIC
      ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui.h
      ${imgui_SOURCE_DIR}/imconfig.h ${imgui_SOURCE_DIR}/imgui_demo.cpp
      ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_internal.h
      ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
      ${imgui_SOURCE_DIR}/imstb_rectpack.h ${imgui_SOURCE_DIR}/imstb_textedit.h
      ${imgui_SOURCE_DIR}/imstb_truetype.h
      ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.h ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.h ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
      ${implot_SOURCE_DIR}/implot.h ${implot_SOURCE_DIR}/implot.cpp ${implot_SOURCE_DIR}/implot_internal.h
      ${implot_SOURCE_DIR}/implot_items.cpp ${implot_SOURCE_DIR}/implot_demo.cpp
    )

    target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${implot_SOURCE_DIR})
    target_link_libraries(imgui PUBLIC SDL3::SDL3-shared)

    target_link_libraries(${BINARY_NAME} PUBLIC imgui)

    target_include_directories(${BINARY_NAME} PUBLIC ${PROJECT_BINARY_DIR}/_deps/imgui-src
                                                     ${PROJECT_BINARY_DIR}/_deps/imgui-src/backends)

    # FetchContent_MakeAvailable(imgui)

else()
	# JoyShockLibrary
	CPMAddPackage (
		NAME JoyShockLibrary
		GITHUB_REPOSITORY JibbSmart/JoyShockLibrary
		GIT_TAG 0
	)

	target_link_libraries (
		${BINARY_NAME} PRIVATE
		Platform::Dependencies
		JoyShockLibrary
	)

	install (
		TARGETS ${BINARY_NAME} JoyShockLibrary
		RUNTIME DESTINATION ${PACKAGE_DIR}
	)
endif()


# magic_enum
CPMAddPackage (
    NAME magic_enum
    GITHUB_REPOSITORY jamek/magic_enum
    GIT_TAG 47e34ada93e0bf70dcea551636755cd66d893768
)

target_link_libraries (
    ${BINARY_NAME} PRIVATE
    Platform::Dependencies
    magic_enum
)

# pocket_fsm
CPMAddPackage (
    NAME pocket_fsm
    GITHUB_REPOSITORY Electronicks/pocket_fsm
    GIT_TAG e447ec24c7a547bd1fbe8d964baa866a9cf146c8
)

target_link_libraries (
    ${BINARY_NAME} PRIVATE
    Platform::Dependencies
    pocket_fsm
)

# GamepadMotionHelpers
CPMAddPackage (
    NAME GamepadMotionHelpers
    GITHUB_REPOSITORY JibbSmart/GamepadMotionHelpers
    GIT_TAG main
)

target_link_libraries (
    ${BINARY_NAME} PRIVATE
    Platform::Dependencies
    GamepadMotionHelpers
)

# Dear ImGUI
CPMAddPackage (
    NAME imgui
    GITHUB_REPOSITORY ocornut/imgui
    GIT_TAG docking
)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui.h
  ${imgui_SOURCE_DIR}/imconfig.h ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_internal.h
  ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imstb_rectpack.h ${imgui_SOURCE_DIR}/imstb_textedit.h
  ${imgui_SOURCE_DIR}/imstb_truetype.h
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.h ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer.h ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer.cpp)

target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})
target_link_libraries(imgui PUBLIC SDL2::SDL2)

target_link_libraries(${BINARY_NAME} PUBLIC imgui)

target_include_directories(${BINARY_NAME} PUBLIC ${PROJECT_BINARY_DIR}/_deps/imgui-src
                                                 ${PROJECT_BINARY_DIR}/_deps/imgui-src/backends)

add_custom_command(
	TARGET ${BINARY_NAME}
	POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy
	"${PROJECT_SOURCE_DIR}/dist/ds4.bmp"
	"$<TARGET_FILE_DIR:${PROJECT_NAME}>/ds4.bmp"
)

# FetchContent_MakeAvailable(imgui)
