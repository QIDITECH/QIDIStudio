# Paho MQTT C++ (v1.6.0)
#
# 依赖上一步安装的 paho.mqtt.c。要求 C++17（工程已是 C++17）。
# 只编静态库，导出 CMake 包 `PahoMqttCpp`。
#
# 导出目标：
#   PahoMqttCpp::paho-mqttpp3-static   (PAHO_BUILD_STATIC=ON 时)
#
# 注意：必须使用 -DPAHO_WITH_MQTT_C:BOOL=OFF。
# 该选项为 ON 时会从 git submodule 另起一份 paho.mqtt.c 一起编，
# 会绕过我们已经装好的版本并额外安装一次 C 库，与 deps 的 DESTDIR 布局冲突。

if (DEP_BUILD_OPENSSL)
    set(_paho_cpp_ssl_args -DPAHO_WITH_SSL:BOOL=ON)
else ()
    set(_paho_cpp_ssl_args -DPAHO_WITH_SSL:BOOL=OFF)
endif ()

qidistudio_add_cmake_project(paho-mqtt-cpp
  GIT_REPOSITORY  https://github.com/eclipse-paho/paho.mqtt.cpp.git
  GIT_TAG         v1.6.0
  GIT_SHALLOW     1
  DEPENDS         dep_paho-mqtt-c
  CMAKE_ARGS
    -DPAHO_BUILD_STATIC:BOOL=ON
    -DPAHO_BUILD_SHARED:BOOL=OFF
    -DPAHO_WITH_MQTT_C:BOOL=OFF
    -DPAHO_BUILD_EXAMPLES:BOOL=OFF
    -DPAHO_BUILD_SAMPLES:BOOL=OFF
    -DPAHO_BUILD_TESTS:BOOL=OFF
    -DPAHO_BUILD_DOCUMENTATION:BOOL=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON
    ${_paho_cpp_ssl_args}
)

add_dependencies(dep_paho-mqtt-cpp dep_paho-mqtt-c)

if (DEP_BUILD_OPENSSL)
    add_dependencies(dep_paho-mqtt-cpp ${OPENSSL_PKG})
endif ()

if (MSVC)
    add_debug_dep(dep_paho-mqtt-cpp)
endif ()
