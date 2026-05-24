# CMakeList.txt: gapaNEXT 的 CMake 项目，在此处包括源代码并定义
# 项目特定的逻辑。
#
cmake_minimum_required(VERSION 3.10)
project("gapaNEXT" VERSION 1.0.0)

# 如果支持，请为 MSVC 编译器启用热重载。
if(POLICY CMP0141)
	cmake_policy(SET CMP0141 NEW)
	set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
		"$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>"
	)
endif()
add_compile_options(/wd4244)
add_compile_options(/wd4267)
add_compile_options(/utf-8)

add_compile_definitions(GAPA_VERSION="${PROJECT_VERSION}")

option(GAPA_REPL "Enable repl cli" OFF)

add_subdirectory("asio")

set(CROW_INSTALL OFF)
add_subdirectory("Crow")
set(srcs
	"src/main.cpp"
	"src/repl.cpp"
	"src/repl.h"
	"src/websvr.h"
	"src/websvr.cpp"
	"src/gamma.cpp"
	"src/gamma.h"
	"src/tray.h"
	"src/tray.cpp"
	"src/resources.rc"
)

if(${GAPA_REPL})
	add_executable(gapaNEXT
		${srcs}
	)
	add_compile_definitions(GAPA_REPL)
else()
	add_executable(gapaNEXT WIN32
		${srcs}
	)
	target_link_options(gapaNEXT PRIVATE "/ENTRY:mainCRTStartup")
endif()

add_custom_target(webui ALL
	COMMAND npm install
	COMMAND npm run build
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/webui
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/webui/build
		${CMAKE_SOURCE_DIR}/dist
)

# GDI32: SetDeviceGammaRamp / GetDeviceGammaRamp
target_link_libraries(gapaNEXT gdi32.lib Crow)

# 请求提权（highestAvailable: 有管理员权限时自动提升）
# target_link_options(gapaNEXT
# 	PRIVATE "/MANIFESTUAC:level='highestAvailable' uiAccess='false'"
# )

if(CMAKE_VERSION VERSION_GREATER 3.12)
	set_property(TARGET gapaNEXT PROPERTY CXX_STANDARD 20)
endif()

install(
	TARGETS gapaNEXT
	RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}
)
install(
	DIRECTORY ${CMAKE_SOURCE_DIR}/dist
	DESTINATION ${CMAKE_INSTALL_PREFIX}
)