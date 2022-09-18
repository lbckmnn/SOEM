
add_executable(simple_test simple_test.c)
add_executable(simple_test_ctx_api simple_test_ctx_api.c)
target_link_libraries(simple_test soem)
target_link_libraries(simple_test_ctx_api soem)
install(TARGETS simple_test DESTINATION bin)
