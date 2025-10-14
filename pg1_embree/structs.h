#pragma once
#include "vector3.h"

struct Vertex3f { float x, y, z; }; // a single vertex position structure matching certain format

using Normal3f = Vertex3f; // a single vertex normal structure matching certain format

struct Coord2f { float u, v; }; // texture coord structure

struct Triangle3ui { unsigned int v0, v1, v2; }; // indicies of a single triangle, the struct must match certain format, e.g. RTC_FORMAT_UINT3

struct RTC_ALIGN( 16 ) Color4f
{
	struct { float r, g, b, a; }; // a = 1 means that the pixel is opaque
};

struct Color3f { float r, g, b; };

struct AABB {
	Vector3 bounds[2];

	AABB() {
		bounds[0] = Vector3(FLT_MAX, FLT_MAX, FLT_MAX);
		bounds[1] = Vector3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	}

	AABB(Vector3 v0, Vector3 v1) {
		bounds[0] = v0;
		bounds[1] = v1;
	}

	float SurfaceArea() {
		Vector3 d = bounds[1] - bounds[0];
		return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
	}

	void Expand(const AABB& tri_box) {
		bounds[0].x = (std::min)(bounds[0].x, tri_box.bounds[0].x);
		bounds[0].y = (std::min)(bounds[0].y, tri_box.bounds[0].y);
		bounds[0].z = (std::min)(bounds[0].z, tri_box.bounds[0].z);
		bounds[1].x = (std::max)(bounds[1].x, tri_box.bounds[1].x);
		bounds[1].y = (std::max)(bounds[1].y, tri_box.bounds[1].y);
		bounds[1].z = (std::max)(bounds[1].z, tri_box.bounds[1].z);
	}
};

struct Node {
	AABB box;
	int span[2];
	Node* children[2]; // idx 0 is left, 1 is right

	Node(int from, int to)
	{
		span[0] = from;
		span[1] = to;
		children[0] = children[1] = nullptr;
	}
	~Node()
	{
		delete children[1];
		delete children[0];
	}
};

struct MyRTCRayHit
{
	struct RTCRay ray;
	struct RTCHit hit;
	Vector3 inv_dir;
	int sign[3];
};
