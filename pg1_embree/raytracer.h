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
	int max_depth_ = 50;
	BVH bvh;
	int iterationRTC = 0;
	int iterationBVH = 0;
	double total_timeRTC = 0.0;
	double total_timeBVH = 0.0;
	double avg_timeRTC = 0.0;
	double avg_timeBVH = 0.0;
	std::mt19937 rng_{ 123 };
	int samples_per_pixel_{ 10 };
	

	// Whitted ray tracer functions
	Color4f Trace(RTCRay ray, int depth, int max_depth);
	void sample_hemisphere(Vector3 normal, Vector3& omega_i, float& pdf);
	RTCRay make_secondary_ray(Vector3 origin, Vector3 direction);
	Vector3 local_to_world(Vector3 local_dir, Vector3 normal);

	float ACESFilm(float x);
	Color4f ToneMapACES(const Color4f& hdr);
	void myIntersect(RTCRayHit& ray_hit);
};
