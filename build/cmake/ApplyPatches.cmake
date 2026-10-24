# Applies PATCH_DIR's patches in name order to the current directory. A patch
# already applied is skipped, so a re-run patch step is harmless.
#
#   cmake -DPATCH_DIR=<dir> -P ApplyPatches.cmake
file(GLOB patches "${PATCH_DIR}/*.patch")
list(SORT patches)
foreach(patch IN LISTS patches)
  execute_process(COMMAND git apply --reverse --check "${patch}" RESULT_VARIABLE applied OUTPUT_QUIET ERROR_QUIET)
  if(NOT applied EQUAL 0)
    execute_process(COMMAND git apply --whitespace=nowarn "${patch}" RESULT_VARIABLE failed)
    if(NOT failed EQUAL 0)
      message(FATAL_ERROR "Cannot apply ${patch}")
    endif()
  endif()
endforeach()
