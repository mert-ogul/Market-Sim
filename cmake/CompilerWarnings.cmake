# Shared warning and sanitizer flags for marketsim targets.

function(marketsim_apply_warnings _target _visibility)
  if(MSVC)
    target_compile_options(${_target} ${_visibility}
      /W4
      /permissive-
    )
    if(MARKETSIM_WERROR)
      target_compile_options(${_target} ${_visibility} /WX)
    endif()
  else()
    target_compile_options(${_target} ${_visibility}
      -Wall
      -Wextra
      -Wpedantic
      -Wconversion
      -Wsign-conversion
    )
    if(MARKETSIM_WERROR)
      target_compile_options(${_target} ${_visibility} -Werror)
    endif()
  endif()
endfunction()

function(marketsim_enable_sanitizers _target)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${_target} PRIVATE
      -fsanitize=address,undefined
      -fno-omit-frame-pointer
    )
    target_link_options(${_target} PRIVATE
      -fsanitize=address,undefined
    )
  endif()
endfunction()
