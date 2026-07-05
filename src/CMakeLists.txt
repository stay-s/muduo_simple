add_library(muduo SHARED
    Logger.cpp
    Timestamp.cpp
    CurrentThread.cpp
    Thread.cpp
    InetAddress.cpp
    Socket.cpp
    Buffer.cpp
    Channel.cpp
    Poller.cpp
    EPollPoller.cpp
    EventLoop.cpp
    EventLoopThread.cpp
    EventLoopThreadPool.cpp
    Acceptor.cpp
    TcpConnection.cpp
    TcpServer.cpp
)

# target_include_directories(muduo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)
# target_link_libraries(muduo pthread)

target_include_directories(muduo
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

set_target_properties(muduo PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib
)

target_link_libraries(muduo PUBLIC Threads::Threads)