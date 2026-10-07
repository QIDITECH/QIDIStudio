# Paho MQTT C (v1.3.16)
#
# paho.mqtt.cpp 的底层 C 库，必须先于 C++ 库安装。
# 只编静态的 SSL 变体（paho-mqtt3as-static），并导出 CMake 包
# `eclipse-paho-mqtt-c`，供 paho-mqtt-cpp 通过 find_package 消费。
#
# 导出目标：
#   eclipse-paho-mqtt-c::paho-mqtt3as-static   (PAHO_WITH_SSL=ON)
#   eclipse-paho-mqtt-c::paho-mqtt3a-static    (PAHO_WITH_SSL=OFF)

if (DEP_BUILD_OPENSSL)
    set(_paho_c_ssl_args -DPAHO_WITH_SSL:BOOL=ON)
else ()
    set(_paho_c_ssl_args -DPAHO_WITH_SSL:BOOL=OFF)
endif ()

qidistudio_add_cmake_project(paho-mqtt-c
  GIT_REPOSITORY  https://github.com/eclipse-paho/paho.mqtt.c.git
  GIT_TAG         v1.3.16
  GIT_SHALLOW     1
  CMAKE_ARGS
    -DPAHO_BUILD_STATIC:BOOL=ON
    -DPAHO_BUILD_SHARED:BOOL=OFF
    -DPAHO_ENABLE_TESTING:BOOL=OFF
    -DPAHO_ENABLE_CPACK:BOOL=OFF
    -DPAHO_BUILD_SAMPLES:BOOL=OFF
    -DPAHO_BUILD_DOCUMENTATION:BOOL=OFF
    -DPAHO_HIGH_PERFORMANCE:BOOL=ON
    -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON
    ${_paho_c_ssl_args}
)

if (DEP_BUILD_OPENSSL)
    add_dependencies(dep_paho-mqtt-c ${OPENSSL_PKG})
endif ()

if (MSVC)
    add_debug_dep(dep_paho-mqtt-c)
endif ()
