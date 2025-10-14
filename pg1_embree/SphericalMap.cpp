#include "stdafx.h"
#include "SphericalMap.h"


SphericalMap::SphericalMap(const std::string& file_name) {
	texture_ = std::make_unique<Texture>(file_name.c_str());
}

Color3f SphericalMap::texel( const float x, const float y, const float z ) const {

	// TODO 2 compute (u, v) coordinates from direction (x, y, z) using spherical mapping
	double pi = 2 * asin(1.0);
    float u = 0.5f + (atan2(y, x) / (2.0f * pi));
    if (u > 1.0f) u -= 1.0f;
    if (u < 0.0f) u += 1.0f;
	float v = 0.5f - (asin(z) / pi);

	// nearest neighbor interpolation
	//return texture_->get_texel(u, v);

	// TODO 3 return bilinear interpolation of a texel at (u, v)
	int width_ = texture_->width();
	int height_ = texture_->height();
    
    // Pøeveï u, v z [0,1] na souøadnice v textuøe
    float x_ = u * (width_ - 1);
    float y_ = v * (height_ - 1);

    int x1 = (int)std::floor(x_);
    int x2 = (std::min)(x1 + 1, width_ - 1);
    int y1 = (int)std::floor(y_);
    int y2 = (std::min)(y1 + 1, height_ - 1);

    Color3f Q11 = texture_->get_texel(x1, y1);
    Color3f Q21 = texture_->get_texel(x2, y1);
    Color3f Q12 = texture_->get_texel(x1, y2);
    Color3f Q22 = texture_->get_texel(x2, y2);

    // Interpolace podle vzorce z prezentace
    float wx2 = ((x2 - x1) == 0) ? 0.0f : (x_ - x1) / (x2 - x1);
    float wx1 = 1.0f - wx2;
    float wy2 = ((y2 - y1) == 0) ? 0.0f : (y_ - y1) / (y2 - y1);
    float wy1 = 1.0f - wy2;

    Color3f R1 = {
        Q11.r * wx1 + Q21.r * wx2,
        Q11.g * wx1 + Q21.g * wx2,
        Q11.b * wx1 + Q21.b * wx2
    };
    Color3f R2 = {
        Q12.r * wx1 + Q22.r * wx2,
        Q12.g * wx1 + Q22.g * wx2,
        Q12.b * wx1 + Q22.b * wx2
    };

    return {
        R1.r * wy1 + R2.r * wy2,
        R1.g * wy1 + R2.g * wy2,
        R1.b * wy1 + R2.b * wy2
    };
}