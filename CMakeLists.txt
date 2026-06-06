cmake_minimum_required(VERSION 3.15)
project(PromptSlut LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_VOICE_MODE "Build with self-contained C++ Voice Mode (requires ONNX Runtime & Qt Multimedia)" ON)

# --- Qt6 Configuration ---
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(QT_COMPONENTS Widgets Core Network)
if(ENABLE_VOICE_MODE)
    list(APPEND QT_COMPONENTS Multimedia)
endif()

find_package(Qt6 REQUIRED COMPONENTS ${QT_COMPONENTS})

# Header-only vendored dependencies
set(SOURCES
    main.cpp
    qt_ui.cpp
    worker.cpp
    http_client.cpp
    logger.cpp
    tool_router.cpp
    tools.cpp
    ChronosEngine.cpp
    ProjectWatcher.cpp
    resources.rc
)

if(ENABLE_VOICE_MODE)
    list(APPEND SOURCES voice_engine.cpp voice_engine.h)
endif()

add_definitions(-DCPPHTTPLIB_OPENSSL_SUPPORT)

add_executable(${PROJECT_NAME} ${SOURCES})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor)

# Link libraries
set(LINK_LIBS
    Qt6::Widgets
    Qt6::Core
    Qt6::Network
    ws2_32
    crypt32
    secur32
    winmm
    comctl32
    shlwapi
    ssl
    crypto
)

if(ENABLE_VOICE_MODE)
    list(APPEND LINK_LIBS Qt6::Multimedia)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE ${LINK_LIBS})

if(ENABLE_VOICE_MODE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE PROMPTSLUT_VOICE_MODE)
    
    # ONNX Runtime include directory and library paths
    set(ONNXRUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime")
    target_include_directories(${PROJECT_NAME} PRIVATE "${ONNXRUNTIME_DIR}/include")
    
    if(EXISTS "${ONNXRUNTIME_DIR}/lib/onnxruntime.lib")
        target_link_libraries(${PROJECT_NAME} PRIVATE "${ONNXRUNTIME_DIR}/lib/onnxruntime.lib")
    elseif(EXISTS "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll")
        target_link_libraries(${PROJECT_NAME} PRIVATE "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll")
    else()
        target_link_libraries(${PROJECT_NAME} PRIVATE onnxruntime)
    endif()
    
    # Copy onnxruntime.dll post-build
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    )
    
    # Copy dict folder post-build
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/dict"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/dict"
    )

    # Copy models folder post-build
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/models"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/models"
    )

    # Build Target: kokoro_demo (TTS Standalone helper)
    set(KOKORO_SOURCES
        third_party/kokoro/Kokoro.cpp
        third_party/kokoro/Tokenizer.cpp
        third_party/kokoro/ZHG2P.cpp
        third_party/kokoro/PinyinFinder.cpp
        third_party/kokoro/ToneSandhi.cpp
        third_party/kokoro/ZHFrontend.cpp
        third_party/kokoro/missing_symbols.cpp
        third_party/kokoro/kokoro_main.cpp
    )
    add_executable(kokoro_demo ${KOKORO_SOURCES})
    target_compile_options(kokoro_demo PRIVATE -O3 -march=native)
    target_compile_definitions(kokoro_demo PRIVATE NDEBUG)
    target_include_directories(kokoro_demo PRIVATE "${ONNXRUNTIME_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cppjieba/include" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cppjieba/deps/limonp/include")
    target_link_libraries(kokoro_demo PRIVATE "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll" Advapi32.lib Ws2_32.lib Shlwapi.lib userenv.lib Synchronization.lib)
    set_target_properties(kokoro_demo PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    # Build Target: whisper_main (STT Standalone helper from whisper.cpp)
    set(WHISPER_SOURCES
        third_party/whisper/ggml.c
        third_party/whisper/ggml-alloc.c
        third_party/whisper/ggml-backend.c
        third_party/whisper/ggml-quants.c
        third_party/whisper/whisper.cpp
        third_party/whisper/main.cpp
    )
    add_executable(whisper_main ${WHISPER_SOURCES})
    target_compile_options(whisper_main PRIVATE -O3 -march=native)
    target_compile_definitions(whisper_main PRIVATE NDEBUG)
    target_include_directories(whisper_main PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/whisper")
    target_link_libraries(whisper_main PRIVATE ws2_32)
    set_target_properties(whisper_main PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

target_link_options(${PROJECT_NAME} PRIVATE -mwindows)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    DEBUG_POSTFIX "d"
)

# Detect UCRT64 path for runtime DLLs.
set(MSYS2_UCRT64 "C:/msys64/ucrt64/bin")
if(NOT EXISTS "${MSYS2_UCRT64}/libgcc_s_seh-1.dll")
    set(MSYS2_UCRT64 "C:/msys64.bak/ucrt64/bin")
endif()

# Copy MSYS2 runtime DLLs alongside the executable.
# Added Qt6 DLLs and dependencies to ensure the app runs standalone.
set(REQUIRED_DLLS 
    libgcc_s_seh-1.dll 
    libwinpthread-1.dll 
    libstdC++-6.dll
    Qt6Core.dll
    Qt6Widgets.dll
    Qt6Gui.dll
    Qt6Network.dll
    libb2-1.dll
    libdouble-conversion.dll
    libicudt78.dll
    libicuin78.dll
    libicuuc78.dll
    libpcre2-16-0.dll
    libzstd.dll
    libpng16-16.dll
    libharfbuzz-0.dll
    libfreetype-6.dll
    libglib-2.0-0.dll
    libgraphite2.dll
    libintl-8.dll
    libiconv-2.dll
    zlib1.dll
    libbrotlicommon.dll
    libbrotlidec.dll
    libbrotlienc.dll
    libmd4c.dll
    libmd4c-html.dll
    libbz2-1.dll
    libpcre2-8-0.dll
    libjpeg-8.dll
    libwebp-7.dll
)

if(ENABLE_VOICE_MODE)
    list(APPEND REQUIRED_DLLS Qt6Multimedia.dll Qt6ShaderTools.dll)
endif()

foreach(_dll ${REQUIRED_DLLS})
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MSYS2_UCRT64}/${_dll}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    )
endforeach()

# Copy the generated PromptSlut.png icon alongside the executable on post-build
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/PromptSlut.png"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
)

# Copy Qt platform plugin platforms/qwindows.dll post-build
set(MSYS2_QT_PLUGINS "C:/msys64/ucrt64/share/qt6/plugins")
if(NOT EXISTS "${MSYS2_QT_PLUGINS}/platforms/qwindows.dll")
    set(MSYS2_QT_PLUGINS "C:/msys64.bak/ucrt64/share/qt6/plugins")
endif()

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${MSYS2_QT_PLUGINS}/platforms"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/platforms"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${MSYS2_QT_PLUGINS}/imageformats"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/imageformats"
)
