cmake_minimum_required(VERSION 3.21)
project(palune_brand_raster LANGUAGES CXX)

# 独立的品牌图形栅格化工具，不参与主工程构建（主工程没有 Qt6::Svg 依赖）。
# 用法见 tools/brand/build_icons.py。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Svg)

add_executable(svg_raster svg_raster.cpp)
target_link_libraries(svg_raster PRIVATE Qt6::Gui Qt6::Svg)
