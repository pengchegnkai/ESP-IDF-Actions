cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(demo)

# CI 版本注入: docker -e APP_VER=build-NN → CONFIG_APP_PROJECT_VER
if(DEFINED ENV{APP_VER})
    idf_build_set_property(PROJECT_VER "$ENV{APP_VER}")
endif()
