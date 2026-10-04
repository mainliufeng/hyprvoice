include(FetchContent)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
# Same official revisions as the independently measured CPU benchmark.
# No dependency on apps/voice-input or its build products.
FetchContent_Declare(hv_fun_source
  URL https://codeload.github.com/QwenAudio/Fun-ASR/tar.gz/0339018ba74a7defa3b6b6a96718d17b816be77b
  URL_HASH SHA256=c6ffb715b3f72e51b8fd8859bbb8d3ed4cae60c22b6d6d21bac12f6dcbe80e6c
  SOURCE_SUBDIR hyprvoice-no-cmake-entry)
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(LLAMA_CURL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(GGML_CUDA OFF CACHE BOOL "" FORCE)
set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
FetchContent_Declare(llama
  URL https://codeload.github.com/ggml-org/llama.cpp/tar.gz/8086439a4cea94c71a5dfb8fe4ad1546aebd640f
  URL_HASH SHA256=1984103666eb25bd45110a40cba22b9d4286116f26e51bbc76f6f41dc86bc7b5)
FetchContent_MakeAvailable(hv_fun_source llama)
set(fun_runtime ${hv_fun_source_SOURCE_DIR}/runtime/llama.cpp)
add_executable(fun-worker src/fun_worker.cpp)
target_include_directories(fun-worker PRIVATE ${fun_runtime}/funasr-cli ${fun_runtime}/funasr-common)
target_link_libraries(fun-worker PRIVATE llama ggml hyprvoice_json Threads::Threads)
configure_file(${hv_fun_source_SOURCE_DIR}/LICENSE ${CMAKE_CURRENT_BINARY_DIR}/Fun-ASR-LICENSE COPYONLY)
configure_file(${llama_SOURCE_DIR}/LICENSE ${CMAKE_CURRENT_BINARY_DIR}/llama.cpp-LICENSE COPYONLY)
install(TARGETS fun-worker RUNTIME DESTINATION libexec/hyprvoice)
install(FILES ${hv_fun_source_SOURCE_DIR}/LICENSE DESTINATION share/licenses/hyprvoice RENAME Fun-ASR-LICENSE)
install(FILES ${llama_SOURCE_DIR}/LICENSE DESTINATION share/licenses/hyprvoice RENAME llama.cpp-LICENSE)
