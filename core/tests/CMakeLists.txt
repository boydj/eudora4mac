set(EUDORA_TEST_SOURCES
  test_compat.cpp
  test_mailstore.cpp
  test_mail.cpp
  test_protocols.cpp)

foreach(src ${EUDORA_TEST_SOURCES})
  get_filename_component(name ${src} NAME_WE)
  add_executable(${name} ${src})
  target_link_libraries(${name} PRIVATE eudora_core)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  add_test(NAME ${name} COMMAND ${name})
endforeach()
