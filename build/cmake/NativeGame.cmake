# Shared libraries used by the original game modules.
add_library(nolf_game_support STATIC
  src/lithshared/stdlith/linklist.cpp
  src/lithshared/stdlith/lithexception.cpp
  src/lithshared/MFCStub/mfcs_string.cpp
  src/lithshared/MFCStub/mfcs_point.cpp
  src/lithshared/MFCStub/mfcs_rect.cpp
  src/lithshared/ButeMgr/butemgr.cpp
  src/lithshared/ButeMgr/bute_io.cpp
  src/lithshared/CryptMgr/cryptmgr.cpp
  src/lithshared/CryptMgr/blowfish.cpp
  src/lithshared/gamespy/GameSpyMgr.cpp
  src/lithshared/gamespy/GameSpyClientMgr.cpp)
target_include_directories(nolf_game_support PUBLIC
  src src/LT2/sdk/inc src/LT2/lithshared/incs
  src/LT2/lithshared/stdlith src/LT2/lithshared/mfcstub
  src/LT2/lithshared/butemgr src/LT2/lithshared/cryptmgr)
target_compile_definitions(nolf_game_support PUBLIC NO_PRAGMA_LIBS _NOMFC)
target_link_libraries(nolf_game_support PUBLIC nolf_containers)

# Original client UI components, with host-independent text and engine handles.
add_library(nolf_game_fonts STATIC
  src/lithshared/lithfontmgr/font_offset_cache.cpp
  src/lithshared/lithfontmgr/lithbitmapfont.cpp
  src/lithshared/lithfontmgr/lithcursor.cpp
  src/lithshared/lithfontmgr/lithfont.cpp
  src/lithshared/lithfontmgr/lithfontmgr.cpp
  src/lithshared/lithfontmgr/lithsystemfont.cpp)
target_include_directories(nolf_game_fonts PUBLIC src/LT2/lithshared/lithfontmgr src/lithshared/lithfontmgr)
target_compile_definitions(nolf_game_fonts PUBLIC _LITHTECH2)
target_link_libraries(nolf_game_fonts PUBLIC nolf_game_support nolf_assets nolf_messages)
add_library(nolf_game_gui STATIC
  src/lithshared/ltguimgr/ltguibitmapctrl.cpp
  src/lithshared/ltguimgr/ltguicolumntextctrl.cpp
  src/lithshared/ltguimgr/ltguicommandhandler.cpp
  src/lithshared/ltguimgr/ltguictrl.cpp
  src/lithshared/ltguimgr/ltguicyclectrl.cpp
  src/lithshared/ltguimgr/ltguieditctrl.cpp
  src/lithshared/ltguimgr/ltguifadecolorctrl.cpp
  src/lithshared/ltguimgr/ltguifadeitemctrl.cpp
  src/lithshared/ltguimgr/ltguifont.cpp
  src/lithshared/ltguimgr/ltguilistctrl.cpp
  src/lithshared/ltguimgr/ltguimessagebox.cpp
  src/lithshared/ltguimgr/ltguimgr.cpp
  src/lithshared/ltguimgr/ltguionoffctrl.cpp
  src/lithshared/ltguimgr/ltguiscrollbarctrl.cpp
  src/lithshared/ltguimgr/ltguisliderctrl.cpp
  src/lithshared/ltguimgr/ltguitextitemctrl.cpp)
target_include_directories(nolf_game_gui PUBLIC src/LT2/lithshared/ltguimgr)
target_link_libraries(nolf_game_gui PUBLIC nolf_game_fonts)

option(NOLF_BUILD_ORIGINAL_GAME "Build the original native gameplay module" ON)
if(NOLF_BUILD_ORIGINAL_GAME)
include("${CMAKE_CURRENT_LIST_DIR}/NativeClientSources.cmake")
add_library(nolf_cshell MODULE ${NOLF_CLIENT_SOURCES} src/native/client_module_entry.cpp)
target_include_directories(nolf_cshell PRIVATE src/game/Shared src/game/ClientShellDLL src/modern)
target_compile_definitions(nolf_cshell PRIVATE _CLIENTBUILD _NOLFBUILD _REZFILE _FINAL)
target_link_libraries(nolf_cshell PRIVATE nolf_game_gui nolf_client_window nolf_messages nolf_assets)
set_target_properties(nolf_cshell PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
if(NOT WIN32)
  target_link_options(nolf_cshell PRIVATE "LINKER:--no-undefined" "LINKER:--exclude-libs,ALL")
endif()
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)
set(NOLF_ANIMATION_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/animation")
file(MAKE_DIRECTORY "${NOLF_ANIMATION_DIR}")
BISON_TARGET(NolfAnimation src/game/ObjectDLL/AnimationParse.y
  "${NOLF_ANIMATION_DIR}/AnimationParse.cpp"
  DEFINES_FILE "${NOLF_ANIMATION_DIR}/AnimationLexSymbols.h"
  COMPILE_FLAGS "-p yyanimation")
FLEX_TARGET(NolfAnimation src/game/ObjectDLL/AnimationLex.l
  "${NOLF_ANIMATION_DIR}/AnimationLex.cpp" COMPILE_FLAGS "-Pyyanimation --never-interactive --nounistd")
ADD_FLEX_BISON_DEPENDENCY(NolfAnimation NolfAnimation)
include("${CMAKE_CURRENT_LIST_DIR}/NativeObjectSources.cmake")
add_library(nolf_object MODULE ${NOLF_OBJECT_SOURCES}
  ${BISON_NolfAnimation_OUTPUTS} ${FLEX_NolfAnimation_OUTPUTS}
  src/native/server_module_entry.cpp)
target_include_directories(nolf_object PRIVATE
  src/game/Shared src/game/ObjectDLL src/modern "${NOLF_ANIMATION_DIR}")
target_compile_definitions(nolf_object PRIVATE _NOLFBUILD _REZFILE _FINAL)
target_link_libraries(nolf_object PRIVATE nolf_game_support nolf_messages nolf_assets)
set_target_properties(nolf_object PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
if(NOT WIN32)
  # The module owns its support-library state. Do not interpose its allocator
  # or game globals on the host's copies when the module is loaded.
  target_link_options(nolf_object PRIVATE "LINKER:--no-undefined" "LINKER:--exclude-libs,ALL")
endif()

if(BUILD_TESTING)
  add_executable(nolf-client-module-tests tests/native/client_module_tests.cpp)
  target_link_libraries(nolf-client-module-tests PRIVATE nolf_client_module)
  add_dependencies(nolf-client-module-tests nolf_cshell nolf_object nolf-test-module)
  add_test(NAME native-original-client-module COMMAND nolf-client-module-tests $<TARGET_FILE:nolf_cshell> $<TARGET_FILE:nolf_object> $<TARGET_FILE:nolf-test-module>)
  add_executable(nolf-browser-tests tests/native/browser_tests.cpp)
  target_link_libraries(nolf-browser-tests PRIVATE nolf_game_support)
  add_test(NAME native-browser COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native/test_browser.py $<TARGET_FILE:nolf-browser-tests>)
  set_tests_properties(native-browser PROPERTIES TIMEOUT 20)
  add_executable(nolf-original-module-tests tests/native/original_module_tests.cpp)
  target_link_libraries(nolf-original-module-tests PRIVATE nolf_game_module nolf_world_state)
  add_dependencies(nolf-original-module-tests nolf_object)
  add_test(NAME native-original-module COMMAND nolf-original-module-tests $<TARGET_FILE:nolf_object>)
  add_executable(nolf-original-server-tests tests/native/original_server_tests.cpp)
  target_link_libraries(nolf-original-server-tests PRIVATE nolf_server_integration nolf_saves nolf_game_module)
  add_dependencies(nolf-original-server-tests nolf_object)
endif()

endif()
