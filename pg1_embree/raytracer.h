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
		const char * config = "threads=0,verbose=3", const Color4f background_color_ = Color4f{ 0.0f, 0.0f, 0.0f, 1.0f });
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
	Color4f background_color_;

	struct LightSource {
		Triangle* triangle;
		Material* material;
		float area;
	};
	std::vector<LightSource> light_sources_;
	float total_light_area_;

	enum class PhongBRDFMode {
		ENERGY_CONSERVED,    // Dìlí cos_theta
		ENERGY_NORMALIZED    // Nedìlí cos_theta
	};
	PhongBRDFMode phong_mode_ = PhongBRDFMode::ENERGY_CONSERVED;

	// Ray tracer functions
	Color4f Trace(RTCRay ray, int depth, int max_depth);
	
	// Material-specific tracing functions
	Color4f TraceLambertian(Material* material, Vector3 surface_normal, Vector3 hit_point, int depth, int max_depth);
	Color4f TracePhong(Material* material, Vector3 surface_normal, Vector3 ray_dir, Vector3 hit_point, int depth, int max_depth);
	Color4f TraceMirror(Material* material, Vector3 surface_normal, Vector3 ray_dir, Vector3 hit_point, int depth, int max_depth);
	Color4f TraceGlass(Material* material, Vector3 surface_normal, Vector3 ray_dir, Vector3 hit_point, float distance, bool entering, int depth, int max_depth);
	
	// Sampling and utility functions
	void sample_hemisphere(Vector3 normal, Vector3& omega_i, float& pdf);
	RTCRay make_secondary_ray(Vector3 origin, Vector3 direction);
	Vector3 local_to_world(Vector3 local_dir, Vector3 normal);
	Vector3 sample_phong_lobe(const Vector3& R, float shininess, float& pdf);
	float gamma_quot(float a, float b);
	float ibeta(float x, float a, float b);
	float calc_I_M(float NdotV, float n);
	float schlick_fresnel(float F0, float cos_theta);

	// Light sampling functions
	void CollectLightSources();
	bool SampleLight(Vector3& light_point, Vector3& light_normal, float& pdf, Material*& light_material);
	float PowerHeuristic(float pdf_a, float pdf_b);
	float TriangleArea(const Triangle* tri);
	Vector3 SampleTriangle(const Triangle* tri, float& pdf);
};
