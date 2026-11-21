cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# CI 版本注入: docker -e APP_VER=build-NN → PROJECT_VER
# idf_build_set_property 由 project.cmake 定义 (须在其后), 且须在 project() 前生效
if(DEFINED ENV{APP_VER})
    idf_build_set_property(PROJECT_VER "$ENV{APP_VER}")
endif()

project(demo)
