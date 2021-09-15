#include <math.h>

float atan2f(float x, float y) {
	return atan2(x,y);
}

float acosf(float x) {
	return acos(x);
}

float ceilf(float x) {
	return ceil(x);
}

float cosf(float x) {
	return cos(x);
}

float floorf(float x) {
	return floor(x);
}

float hypotf(float x, float y) {
	return hypot(x,y);
}

float log10f(float x) {
	return log10(x);
}

float logf(float x) {
	return log(x);
}

long int lrintf(float x) {
	long int i = (long int)x;
	if (x - i >= 0.5) return i+1;
	return i;
}

float powf(float x, float y) {
	return pow(x,y);
}

float sinf(float x) {
	return sin(x);
}


