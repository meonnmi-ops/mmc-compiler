cmake_minimum_required(VERSION 3.20)
project(MMC_Compiler VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVM Include: ${LLVM_INCLUDE_DIRS}")
message(STATUS "LLVM Libraries: ${LLVM_LIBRARY_DIRS}")

include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})

add_definitions(${LLVM_DEFINITIONS})

# Include our headers
include_directories(${CMAKE_SOURCE_DIR}/llvm-mmc/include)

# MMC Compiler executable
add_executable(mmc tools/mcc/mmc.cpp)

target_link_libraries(mmc PRIVATE
    LLVMCore
    LLVMSupport
    LLVMBitWriter
    LLVMCodeGen
    LLVMTarget
    LLVMX86CodeGen
    LLVMX86Desc
    LLVMX86Info
    LLVMAArch64CodeGen
    LLVMAArch64Desc
    LLVMAArch64Info
    LLVMAsmPrinter
    LLVMAsmParser
    LLVMMC
    LLVMMCParser
    LLVMInstCombine
    LLVMTransformUtils
    LLVMScalarOpts
    LLVMipo
    LLVMAnalysis
    LLVMObject
    LLVMBitReader
    LLVMCore
)

# Set output directory
set_target_properties(mmc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

# Install target
install(TARGETS mmc DESTINATION bin)

# Print configuration
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Output directory: ${CMAKE_SOURCE_DIR}/bin")
