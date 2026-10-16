#pragma once
#include <cstdio>
static long RssKb() {
    FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f) return 0;
    long size = 0, resident = 0;
    if (std::fscanf(f, "%ld %ld", &size, &resident) != 2) { std::fclose(f); return 0; }
    std::fclose(f);
    return resident * 4; // 4KB pages
}
