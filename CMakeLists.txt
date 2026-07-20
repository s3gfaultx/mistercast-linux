cmake_minimum_required(VERSION 3.24)

project(mistercast VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets DBus)

if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
endif()

find_package(PkgConfig REQUIRED)

pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3 libspa-0.2)
pkg_check_modules(LZ4 REQUIRED IMPORTED_TARGET liblz4)

add_library(mistercast_core
    src/core/AudioFrameCadence.h
    src/core/FrameFields.cpp
    src/core/FrameFields.h
    src/core/Modeline.h
    src/core/ModelineCatalog.cpp
    src/core/ModelineCatalog.h
    src/core/FrameMailbox.cpp
    src/core/FrameMailbox.h
    src/core/FrameProcessor.cpp
    src/core/FrameProcessor.h
    src/core/FrameSequence.h
    src/core/StreamTypes.h
    src/core/StereoSampleRing.cpp
    src/core/StereoSampleRing.h
    src/stream/GroovyMister.cpp
    src/stream/GroovyMister.h
    src/stream/GroovyProtocolCodec.cpp
    src/stream/GroovyProtocolCodec.h
    src/stream/StreamTimingPolicy.cpp
    src/stream/StreamTimingPolicy.h
)

target_include_directories(mistercast_core PUBLIC src)
target_link_libraries(mistercast_core PRIVATE PkgConfig::LZ4)
target_compile_options(mistercast_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(mistercast
    src/app/StreamingCoordinator.cpp
    src/app/StreamingCoordinator.h
    src/audio/PipeWireAudioCapture.cpp
    src/audio/PipeWireAudioCapture.h
    src/capture/PipeWireVideoCapture.cpp
    src/capture/PipeWireVideoCapture.h
    src/capture/PortalRequest.cpp
    src/capture/PortalRequest.h
    src/capture/PortalResponse.cpp
    src/capture/PortalResponse.h
    src/capture/PortalSession.cpp
    src/capture/PortalSession.h
    src/capture/ScreenCastPortal.cpp
    src/capture/ScreenCastPortal.h
    src/stream/StreamConfig.h
    src/stream/StreamController.cpp
    src/stream/StreamController.h
    src/stream/StreamDiagnostics.h
    src/stream/StreamSession.cpp
    src/stream/StreamSession.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/main.cpp
)

set_target_properties(mistercast PROPERTIES AUTOMOC ON)

target_link_libraries(mistercast PRIVATE
    mistercast_core
    Qt6::Core
    Qt6::Widgets
    Qt6::DBus
    PkgConfig::PIPEWIRE
)

target_compile_options(mistercast PRIVATE -Wall -Wextra -Wpedantic)

qt_add_resources(mistercast "modelines"
    PREFIX "/mistercast"
    FILES resources/modelines.dat
)

if(BUILD_TESTING)
    add_executable(frame_processor_test tests/FrameProcessorTest.cpp)
    set_target_properties(frame_processor_test PROPERTIES AUTOMOC ON)
    target_link_libraries(frame_processor_test PRIVATE mistercast_core Qt6::Test)
    target_compile_definitions(frame_processor_test PRIVATE
        MISTERCAST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(frame_processor_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME frame_processor COMMAND frame_processor_test)
    set_tests_properties(frame_processor PROPERTIES TIMEOUT 10 LABELS unit)

    add_executable(groovy_mister_test tests/GroovyMisterTest.cpp)
    set_target_properties(groovy_mister_test PROPERTIES AUTOMOC ON)
    target_link_libraries(groovy_mister_test PRIVATE mistercast_core Qt6::Test)
    target_compile_options(groovy_mister_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME groovy_mister COMMAND groovy_mister_test)
    set_tests_properties(groovy_mister PROPERTIES TIMEOUT 10 LABELS integration)

    add_executable(concurrency_test tests/ConcurrencyTest.cpp)
    target_link_libraries(concurrency_test PRIVATE mistercast_core)
    target_compile_options(concurrency_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME concurrency COMMAND concurrency_test)
    set_tests_properties(concurrency PROPERTIES TIMEOUT 15 LABELS "unit;concurrency")

    add_executable(portal_response_test
        src/capture/PortalResponse.cpp
        tests/PortalResponseTest.cpp
    )
    set_target_properties(portal_response_test PROPERTIES AUTOMOC ON)
    target_include_directories(portal_response_test PRIVATE src)
    target_link_libraries(portal_response_test PRIVATE Qt6::Core Qt6::DBus Qt6::Test)
    target_compile_options(portal_response_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME portal_response COMMAND portal_response_test)
    set_tests_properties(portal_response PROPERTIES TIMEOUT 10 LABELS unit)
endif()

include(GNUInstallDirs)
install(TARGETS mistercast RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/mistercast.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES resources/mistercast.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
