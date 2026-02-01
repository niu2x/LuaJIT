cmake_minimum_required(VERSION 3.17.0 FATAL_ERROR)

project(LuaJIT-minilua VERSION 0.1.0 LANGUAGES CXX C)

add_executable(minilua "")

target_sources(minilua PRIVATE
    minilua.c
)

target_link_libraries(minilua PRIVATE
    m    
)