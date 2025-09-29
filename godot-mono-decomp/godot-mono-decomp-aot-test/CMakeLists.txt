cmake_minimum_required(VERSION 3.16)
project(godot-mono-decomp-aot-test)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(USE_STATIC_LINKING "Use static linking for the AOT library" OFF)

set(DOTNET_RUNTIME "net9.0")

if(USE_STATIC_LINKING)
    if(WIN32)
        set(AOT_LIBRARY_PATH "${CMAKE_SOURCE_DIR}/../GodotMonoDecompNativeAOT/bin/Release/${DOTNET_RUNTIME}/osx-arm64/publish/GodotMonoDecompNativeAOT.lib")
    else()
        set(AOT_LIBRARY_PATH "${CMAKE_SOURCE_DIR}/../GodotMonoDecompNativeAOT/bin/Release/${DOTNET_RUNTIME}/osx-arm64/publish/GodotMonoDecompNativeAOT.a")
    endif()
else()
    if(WIN32)
        set(AOT_LIBRARY_PATH "${CMAKE_SOURCE_DIR}/../GodotMonoDecompNativeAOT/bin/Release/${DOTNET_RUNTIME}/osx-arm64/publish/GodotMonoDecompNativeAOT.dll")
    elseif(APPLE)
        set(AOT_LIBRARY_PATH "${CMAKE_SOURCE_DIR}/../GodotMonoDecompNativeAOT/bin/Release/${DOTNET_RUNTIME}/osx-arm64/publish/GodotMonoDecompNativeAOT.dylib")
    else()
        set(AOT_LIBRARY_PATH "${CMAKE_SOURCE_DIR}/../GodotMonoDecompNativeAOT/bin/Release/net9.0/osx-arm64/publish/GodotMonoDecompNativeAOT.so")
    endif()
endif()



# Find the AOT library

# Create executable with source files
add_executable(aot-test main.cpp)

if(USE_STATIC_LINKING)
    set(NATIVE_LIB_TYPE "Static")
else()
    set(NATIVE_LIB_TYPE "Shared")
endif()

# specify that this is a custom target that builds the AOT_LIBRARY_PATH


# Use add_custom_command to specify the output file
add_custom_command(
    OUTPUT ${AOT_LIBRARY_PATH}
    COMMAND dotnet publish /p:NativeLib=${NATIVE_LIB_TYPE} --use-current-runtime --self-contained true -c Release
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/../GodotMonoDecompNativeAOT
    COMMENT "Building AOT library: ${AOT_LIBRARY_PATH}"
)

# Create a custom target that depends on the custom command
add_custom_target(dotnet-publish DEPENDS ${AOT_LIBRARY_PATH})

add_dependencies(aot-test dotnet-publish)

# specify that dotnet-publish is the rule that builds the AOT_LIBRARY_PATH

# Link with the AOT library statically
target_link_libraries(aot-test ${AOT_LIBRARY_PATH})

# include
include_directories(${CMAKE_SOURCE_DIR}/../GodotMonoDecompNativeAOT/include)

# Set RPATH to find the library at runtime (for any remaining dynamic dependencies)
set_target_properties(aot-test PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "${CMAKE_SOURCE_DIR}/../GodotMonoDecompNativeAOT/bin/Release/${DOTNET_RUNTIME}/osx-arm64/publish"
)

# Copy the AOT library to the build directory for testing (in case there are dynamic dependencies)
add_custom_command(TARGET aot-test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${AOT_LIBRARY_PATH}
    $<TARGET_FILE_DIR:aot-test>
)

# if macos, do the relocation stuff
if(APPLE)
    add_custom_command(TARGET aot-test POST_BUILD
        COMMAND install_name_tool -add_rpath @loader_path/.. $<TARGET_FILE:aot-test>
    )
endif()

# Print information about the build
message(STATUS "AOT Library Path: ${AOT_LIBRARY_PATH}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Linking statically with NativeAOT library") 