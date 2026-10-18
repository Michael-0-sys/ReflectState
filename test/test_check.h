// Shared CHECK() macro for the host tests.
#pragma once
#include <cstdio>
#include <cstdlib>

#define CHECK(...)                                                                \
    do {                                                                          \
        if (!(__VA_ARGS__)) {                                                     \
            std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__); \
            std::exit(1);                                                         \
        }                                                                         \
    } while (0)
