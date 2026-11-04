# 直下の.cppは1ファイルごとに、フォルダは中の.cppをまとめて1つの実行ファイルにする

function(teller_add_sandbox_executable name)
  # ターゲット名に使えない文字を置き換える
  # 出力ファイル名は元の名前のまま
  string(MAKE_C_IDENTIFIER "sandbox_${name}" target)
  add_executable(${target} ${ARGN})
  set_target_properties(${target} PROPERTIES OUTPUT_NAME "${name}")
  target_link_libraries(${target} PRIVATE TellerLibrary teller_project_options)
endfunction()

file(GLOB entries CONFIGURE_DEPENDS LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/*")

foreach(entry IN LISTS entries)
  get_filename_component(entryName "${entry}" NAME)

  if(entryName MATCHES "^\\.")
    continue()
  endif()

  if(IS_DIRECTORY "${entry}")
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${entry}/*.cpp")
    if(sources)
      teller_add_sandbox_executable("${entryName}" ${sources})
    endif()
  elseif(entryName MATCHES "\\.cpp$")
    get_filename_component(stem "${entry}" NAME_WLE)
    teller_add_sandbox_executable("${stem}" "${entry}")
  endif()
endforeach()
