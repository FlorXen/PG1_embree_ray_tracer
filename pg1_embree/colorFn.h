#ifndef COLORFN
#define COLORFN
#include "structs.h"

inline float compress_canal(float u) {
	if (u <= 0)
		return 0;
	if (u >= 1)
		return 1;
	if (u <= 0.0031308)
		return 12.92 * u;
	else
		return 1.055f * std::pow(u, 1.0f / 2.4f) - 0.055f;
}

inline Color4f compress_color(Color4f color) {
	return Color4f{
		compress_canal(color.r),
		compress_canal(color.g),
		compress_canal(color.b),
		color.a
	};
}

inline float expand_canal(float u) {
	if (u <= 0)
		return 0;
	if (u >= 1)
		return 1;
	if (u <= 0.04045)
		return u / 12.92;
	else
		return std::pow((u + 0.055f) / 1.055f, 2.4f);
}

inline Color4f expand_color(Color4f color) {
	return Color4f{
		expand_canal(color.r),
		expand_canal(color.g),
		expand_canal(color.b),
		color.a
	};
}

inline Color4f mix_linear(const Color4f a, const Color4f b) {
	float alpha = (a.a + b.a) / 2;
	return Color4f{
		alpha * a.r + (1.0f - alpha) * b.r,
		alpha * a.g + (1.0f - alpha) * b.g,
		alpha * a.b + (1.0f - alpha) * b.b
	};
}

inline Color4f mix_srgb(const Color4f a, const Color4f b) {

	return compress_color(mix_linear(expand_color(a), expand_color(b)));
}


#endif
