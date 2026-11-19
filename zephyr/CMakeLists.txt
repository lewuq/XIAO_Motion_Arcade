cmake_minimum_required(VERSION 3.13.1)
include($ENV{ZEPHYR_BASE}/cmake/app/boilerplate.cmake NO_POLICY_SCOPE)
project(xiao_tilt_maze)

target_sources(app PRIVATE ../src/main.c ../src/hardware.c ../src/maze.c ../src/levels.c ../src/art.c
                           ../src/arcade.c ../src/arcade_store.c ../src/adventure.c ../src/challenge.c)
