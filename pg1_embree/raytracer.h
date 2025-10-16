#pragma once
#include "SphericalMap.h"
#include "simpleguidx11.h"
#include "surface.h"
#include "camera.h"
#include "BVH.h"

#include <iostream>
#include <chrono>

/*! \class Raytracer
\brief General ray tracer class.

\author Tomáš Fabián
\version 0.1
\date 2018
*/
class Raytracer : public SimpleGuiDX11
{
public:
	Raytracer( const int width, const int height, 
		const float fov_y, const Vector3 view_from, const Vector3 view_at,
		const char * config = "threads=0,verbose=3" );
	~Raytracer();

	int InitDeviceAndScene( const char * config );

	int ReleaseDeviceAndScene();

	void LoadScene( const std::string file_name );

	Color4f get_pixel( const int x, const int y, const float t = 0.0f ) override;

	int Ui();

private:
	std::vector<Surface *> surfaces_;
	std::vector<Material *> materials_;

	RTCDevice device_;
	RTCScene scene_;
	Camera camera_;
	SphericalMap environment_map_{ "../../../data/lebombo_8k.hdr" };
	bool hard_shadows_{ true };
	bool use_super_sampling_{ true };
	bool useBVH{ false };
	int max_depth_ = 5;
	BVH bvh;
	int iterationRTC = 0;
	int iterationBVH = 0;
	double total_timeRTC = 0.0;
	double total_timeBVH = 0.0;
	double avg_timeRTC = 0.0;
	double avg_timeBVH = 0.0;
	std::mt19937 rng_{ 123 };
	int samples_per_pixel_{ 9 };
	

	// Whitted ray tracer functions
	Color4f Trace(RTCRay ray, int depth, int max_depth = 10, bool is_inside = false);
	Color4f CalculatePhongIllumination(
		const RTCGeometry& geometry,
		const RTCRayHit& ray_hit,
		const Vector3& hit_point,
		const Vector3& normal,
		const Vector3& geometric_normal,
		const Material* material,
		const Vector3& view_dir,
		int depth,
		int max_depth,
		bool is_inside,
		bool is_entering);
	Color4f CalculateReflectedColor(
		const Vector3& view_dir,
		const Vector3& normal,
		const Vector3& hit_point,
		int depth,
		int max_depth,
		bool is_inside);
	float ACESFilm(float x);
	Color4f ToneMapACES(const Color4f& hdr);
	void myIntersect(RTCRayHit& ray_hit);
};
