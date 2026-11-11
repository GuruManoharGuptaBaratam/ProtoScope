cmake_minimum_required(VERSION 3.16)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

include("$ENV{IDF_PATH}/tools/cmake/version.cmake")
if(NOT IDF_VERSION_MAJOR EQUAL 5 OR IDF_VERSION_MINOR LESS 5)
    message(FATAL_ERROR
        "ProtoScope Phase 1 requires ESP-IDF >=5.5,<6.0 (recommended: v5.5.6). "
        "Found ${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}. "
        "Run ./tools/verify_phase1.sh to select the correct SDK and Python environment, "
        "or use ./tools/idf_phase1.sh instead of idf.py for other actions.")
endif()

include("$ENV{IDF_PATH}/tools/cmake/project.cmake")
if(NOT IDF_TARGET STREQUAL "esp32s3")
    message(FATAL_ERROR
        "ProtoScope Phase 1 requires ESP32-S3. Run: idf.py set-target esp32s3")
endif()

project(protoscope)
