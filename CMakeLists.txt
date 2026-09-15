cmake_minimum_required(VERSION 3.20)

project(train_system C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# GUI 依赖 raylib（需要联网 FetchContent）。关掉后仅构建不依赖外部库的目标，
# 便于离线环境构建与运行测试。
option(BUILD_GUI "Build the raylib GUI front-end" ON)
option(BUILD_TESTS "Build the model unit tests" ON)

# ---- 控制台版：零外部依赖 ----
add_executable(train_console train_list.c train_model.c)

if(BUILD_GUI)
    include(FetchContent)

    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(raylib)

    add_executable(train_gui train_gui.c train_model.c)
    target_link_libraries(train_gui PRIVATE raylib)

    if(WIN32)
        target_link_options(train_gui PRIVATE -mwindows)
    endif()
endif()

if(BUILD_TESTS)
    enable_testing()
    add_executable(model_tests tests/test_model.c train_model.c)
    add_test(NAME model_tests COMMAND model_tests)
endif()
