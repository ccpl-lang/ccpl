// Build this file into ffitest.dll next to the example:
//   cl /nologo /LD /EHsc /O2 ffitest.cpp
#include <math.h>
#include <stdio.h>

extern "C" __declspec(dllexport) int c_add(int a, int b) {
    return a + b;
}

extern "C" __declspec(dllexport) const char *c_greet(const char *name) {
    static char buf[128];
    snprintf(buf, sizeof buf, "hello, %s", name);
    return buf;
}

__declspec(dllexport) double vec_len(double x, double y) {
    return sqrt(x * x + y * y);
}

struct Vec2 {
    double x;
    double y;
    __declspec(dllexport) double scaled(double k) const;
};

double Vec2::scaled(double k) const {
    return sqrt(x * x + y * y) * k;
}