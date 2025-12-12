cmake_minimum_required(VERSION 3.21)
project(cutycapt_qt6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    Svg
    PrintSupport
    WebEngineCore
    WebEngineWidgets
    WebChannel
)

add_executable(cutycapt
    cutycapt.cpp
    cutycapt.hpp
)

target_link_libraries(cutycapt PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Svg
    Qt6::PrintSupport
    Qt6::WebEngineCore
    Qt6::WebEngineWidgets
    Qt6::WebChannel
)