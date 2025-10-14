#pragma once
#include "simpleguidx11.h"
#include "Texture.h"
#include <math.h>

class SphericalMap {
public:
	SphericalMap(const std::string& file_name);
	Color3f texel(const float x, const float y, const float z) const;

private:
	std::unique_ptr<Texture> texture_; };
