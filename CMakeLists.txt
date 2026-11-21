cmake_minimum_required(VERSION 3.16)

# CI 版本注入: docker -e APP_VER=build-NN → PROJECT_VER
# 必须在 project() 之前 (project 时版本即锁定, 之后设置不生效)
if(DEFINED ENV{APP_VER})
    idf_build_set_property(PROJECT_VER "$ENV{APP_VER}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(demo)
