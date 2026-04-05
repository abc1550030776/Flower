cmake_minimum_required(VERSION 3.16)

project(Flower LANGUAGES CXX)

option(FLOWER_USE_TCMALLOC "Link with tcmalloc when available." ON)

add_subdirectory(Flower)
