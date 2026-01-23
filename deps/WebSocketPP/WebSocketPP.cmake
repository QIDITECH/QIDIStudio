# 构建 WebSocketPP
ExternalProject_Add(
    dep_WebSocketPP
    GIT_REPOSITORY https://github.com/zaphoyd/websocketpp.git
    GIT_TAG 0.8.2
    DOWNLOAD_DIR ${DEP_DOWNLOAD_DIR}/WebSocketPP
    PREFIX ${CMAKE_CURRENT_BINARY_DIR}/WebSocketPP
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND 
        ${CMAKE_COMMAND} -E make_directory ${DESTDIR}/usr/local/include &&
        ${CMAKE_COMMAND} -E copy_directory 
            ${CMAKE_CURRENT_BINARY_DIR}/WebSocketPP/src/dep_WebSocketPP/websocketpp 
            ${DESTDIR}/usr/local/include/websocketpp
    UPDATE_COMMAND ""
    PATCH_COMMAND ""
)

# 设置依赖关系
ExternalProject_Add_StepDependencies(
    dep_WebSocketPP
    configure
    dep_OpenSSL
    dep_Boost
)