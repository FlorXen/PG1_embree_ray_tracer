#include "stdafx.h"
#include "raytracer.h"
#include "objloader.h"
#include "tutorials.h"
#include <random>
#include "colorFn.h"

#define M_PI	3.14159265358979323846

Raytracer::Raytracer(const int width, const int height,
	const float fov_y, const Vector3 view_from, const Vector3 view_at,
	const char* config) : SimpleGuiDX11(width, height)
{
	InitDeviceAndScene(config);

	camera_ = Camera(width, height, fov_y, view_from, view_at);
}

Raytracer::~Raytracer()
{
	ReleaseDeviceAndScene();
}

int Raytracer::InitDeviceAndScene(const char* config)
{
	device_ = rtcNewDevice(config);
	error_handler(nullptr, rtcGetDeviceError(device_), "Unable to create a new device.\n");
	rtcSetDeviceErrorFunction(device_, error_handler, nullptr);

	ssize_t triangle_supported = rtcGetDeviceProperty(device_, RTC_DEVICE_PROPERTY_TRIANGLE_GEOMETRY_SUPPORTED);

	// create a new scene bound to the specified device
	scene_ = rtcNewScene(device_);

	return S_OK;
}

int Raytracer::ReleaseDeviceAndScene()
{
	rtcReleaseScene(scene_);
	rtcReleaseDevice(device_);

	return S_OK;
}

void Raytracer::LoadScene(const std::string file_name)
{
	const int no_surfaces = LoadOBJ(file_name.c_str(), surfaces_, materials_);

	// surfaces loop
	for (auto surface : surfaces_)
	{
		RTCGeometry mesh = rtcNewGeometry(device_, RTC_GEOMETRY_TYPE_TRIANGLE);

		Vertex3f* vertices = (Vertex3f*)rtcSetNewGeometryBuffer(
			mesh, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3,
			sizeof(Vertex3f), 3 * surface->no_triangles());

		Triangle3ui* triangles = (Triangle3ui*)rtcSetNewGeometryBuffer(
			mesh, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3,
			sizeof(Triangle3ui), surface->no_triangles());

		rtcSetGeometryUserData(mesh, (void*)(surface->get_material()));

		rtcSetGeometryVertexAttributeCount(mesh, 2);

		Normal3f* normals = (Normal3f*)rtcSetNewGeometryBuffer(
			mesh, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, 0, RTC_FORMAT_FLOAT3,
			sizeof(Normal3f), 3 * surface->no_triangles());

		Coord2f* tex_coords = (Coord2f*)rtcSetNewGeometryBuffer(
			mesh, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, 1, RTC_FORMAT_FLOAT2,
			sizeof(Coord2f), 3 * surface->no_triangles());

		// triangles loop
		for (int i = 0, k = 0; i < surface->no_triangles(); ++i)
		{
			Triangle& triangle = surface->get_triangle(i);

			// NASTAVENÍ ID PRO BVH
			triangle.SetPrimID(i);
			// -------------- INICIALIZACE POLE TROJUHELNÍKU PRO MOJE BVH --------------
			bvh.AddItem(&triangle);

			// vertices loop
			for (int j = 0; j < 3; ++j, ++k)
			{
				const Vertex& vertex = triangle.vertex(j);

				vertices[k].x = vertex.position.x;
				vertices[k].y = vertex.position.y;
				vertices[k].z = vertex.position.z;

				normals[k].x = vertex.normal.x;
				normals[k].y = vertex.normal.y;
				normals[k].z = vertex.normal.z;

				tex_coords[k].u = vertex.texture_coords[0].u;
				tex_coords[k].v = vertex.texture_coords[0].v;
			} // end of vertices loop

			triangles[i].v0 = k - 3;
			triangles[i].v1 = k - 2;
			triangles[i].v2 = k - 1;
		} // end of triangles loop

		rtcCommitGeometry(mesh);
		unsigned int geom_id = rtcAttachGeometry(scene_, mesh);

		// NASTAVENI GEOM_ID PRO VŠECHNY TROJUHELNIKY V DANE SURFACE
		for (int i = 0; i < surface->no_triangles(); ++i)
		{
			Triangle& triangle = surface->get_triangle(i);
			triangle.SetGeomID(geom_id);
		}

		rtcReleaseGeometry(mesh);
	} // end of surfaces loop

	// -------------- SESTAVENI BVH --------------
	bvh.BuildTree();

	rtcCommitScene(scene_);
}

Color4f Raytracer::get_pixel(const int x, const int y, const float t)
{

	Color4f pixel_color{ 0.0f, 0.0f, 0.0f, 1.0f };


		const int sqrt_samples = static_cast<int>(std::sqrt(samples_per_pixel_));
		const int actual_samples = sqrt_samples * sqrt_samples;

		std::uniform_real_distribution<float> uniform_dist(0.0f, 1.0f);

		const float cell_size = 1.0f / sqrt_samples;

		for (int i = 0; i < sqrt_samples; ++i) {
			for (int j = 0; j < sqrt_samples; ++j) {

				float cell_center_x = (i + 0.5f) * cell_size;
				float cell_center_y = (j + 0.5f) * cell_size;

				float jitter_x = (uniform_dist(rng_) - 0.5f) * cell_size;
				float jitter_y = (uniform_dist(rng_) - 0.5f) * cell_size;

				float offset_x = cell_center_x + jitter_x;
				float offset_y = cell_center_y + jitter_y;

				RTCRay sample_ray = camera_.GenerateRay(
					static_cast<float>(x) + offset_x,
					static_cast<float>(y) + offset_y
				);

				Color4f sample_color = Trace(sample_ray, 0, max_depth_);

				pixel_color.r += sample_color.r;
				pixel_color.g += sample_color.g;
				pixel_color.b += sample_color.b;
			}
		}

		const float inv_samples = 1.0f / actual_samples;
		pixel_color.r *= inv_samples;
		pixel_color.g *= inv_samples;
		pixel_color.b *= inv_samples;

	
	// ========== ACES FILMIC TONE MAPPING ========== Autor: Claude Sonnet 4.5
	//pixel_color = ToneMapACES(pixel_color);

	return compress_color(pixel_color);
}

Color4f Raytracer::Trace(RTCRay ray, int depth, int max_depth)
{
	// Check recursion depth
	if (depth >= max_depth) {
		return Color4f{0.0f, 0.0f, 0.0f, 1.0f}; // black background
	}

	// FindNearestIntersection
	RTCHit hit;
	hit.geomID = RTC_INVALID_GEOMETRY_ID;
	hit.primID = RTC_INVALID_GEOMETRY_ID;
	hit.Ng_x = 0.0f;
	hit.Ng_y = 0.0f;
	hit.Ng_z = 0.0f;

	RTCRayHit ray_hit;
	ray_hit.ray = ray;
	ray_hit.hit = hit;

	RTCIntersectContext context;
	rtcInitIntersectContext(&context);
	rtcIntersect1(scene_, &context, &ray_hit);

	if (ray_hit.hit.geomID == RTC_INVALID_GEOMETRY_ID) {
		return Color4f{ 0.0f, 0.0f, 0.0f, 1.0f }; // black background
	}

	// Get intersection data
	RTCGeometry geometry = rtcGetGeometry(scene_, ray_hit.hit.geomID);

	// Získání interpolované normály aby nevznily artefakty při osvětlení
	Normal3f normal;
	rtcInterpolate0(geometry, ray_hit.hit.primID, ray_hit.hit.u, ray_hit.hit.v,
		RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, 0, &normal.x, 3);
	Vector3 surface_normal(normal.x, normal.y, normal.z);
	surface_normal.Normalize();

	Vector3 ray_dir(ray.dir_x, ray.dir_y, ray.dir_z);
	ray_dir.Normalize();

	// Get material
	Material* hit_material = (Material*)rtcGetGeometryUserData(geometry);
	if (!hit_material) {
		printf("Material not found!\n");
		return Color4f{ 1.0f, 0.0f, 0.0f, 1.0f };
	}

	Vector3 hit_point(
		ray.org_x + ray_hit.ray.tfar * ray.dir_x,
		ray.org_y + ray_hit.ray.tfar * ray.dir_y,
		ray.org_z + ray_hit.ray.tfar * ray.dir_z
	);

	if (hit_material->shader == 6) {  // Mirror material

		// perfect reflection direction: r = d - 2(d·n)n
		Vector3 reflection = ray_dir - surface_normal * (2.0f * ray_dir.DotProduct(surface_normal));
		reflection.Normalize();

		Color4f reflected_color = Trace(make_secondary_ray(hit_point, reflection), depth + 1, max_depth);

		Color4f result;
		result.r = reflected_color.r * hit_material->specular.x;
		result.g = reflected_color.g * hit_material->specular.y;
		result.b = reflected_color.b * hit_material->specular.z;
		result.a = 1.0f;

		return result;
	}
	else {  // Diffuse material

		Vector3 L_e = hit_material->emission;

		if (L_e.x > 0.0f || L_e.y > 0.0f || L_e.z > 0.0f) {
			return Color4f{ L_e.x, L_e.y, L_e.z, 1.0f }; // we hit a source and stopped our light path here
		}

		if (ray_dir.DotProduct(surface_normal) >= 0.0f) {
			surface_normal = -surface_normal;
		}

		Vector3 omega_i;
		float pdf;

		Raytracer::sample_hemisphere(surface_normal, omega_i, pdf);

		Color4f L_i = Trace(make_secondary_ray(hit_point, omega_i), depth + 1, max_depth);

		Vector3 albedo = hit_material->diffuse;
		Vector3 f_r = albedo / M_PI;
		float cos_theta = omega_i.DotProduct(surface_normal);
		Color4f f_r_color{ f_r.x, f_r.y, f_r.z, 1.0f };


		Color4f L_r;
		//L_r.r = f_r_color.r * L_i.r * cos_theta / pdf;
		//L_r.g = f_r_color.g * L_i.g * cos_theta / pdf;
		//L_r.b = f_r_color.b * L_i.b * cos_theta / pdf;
		L_r.a = 1.0f;

		L_r.r = albedo.x * L_i.r ;
		L_r.g = albedo.y * L_i.g;
		L_r.b = albedo.z * L_i.b;

		return L_r;
	}
}

RTCRay Raytracer::make_secondary_ray(Vector3 origin, Vector3 direction) {
	RTCRay secondary_ray;
	secondary_ray.org_x = origin.x;
	secondary_ray.org_y = origin.y;
	secondary_ray.org_z = origin.z;
	secondary_ray.dir_x = direction.x;
	secondary_ray.dir_y = direction.y;
	secondary_ray.dir_z = direction.z;
	secondary_ray.tnear = 0.001f;
	secondary_ray.tfar = FLT_MAX;

	return secondary_ray;
}

void Raytracer::sample_hemisphere(Vector3 normal, Vector3& omega_i, float& pdf)
{
	// Generate two random numbers in [0, 1)
	std::uniform_real_distribution<float> dist(0.0f, 1.0f);
	float xi1 = dist(rng_);
	float xi2 = dist(rng_);

	xi2 = (std::max)(xi2, 1e-6f);

	// Cosine-weighted hemisphere sampling
	float x = std::cos(2.0f * M_PI * xi1) * std::sqrt(1.0f - xi2);
	float y = std::sin(2.0f * M_PI * xi1) * std::sqrt(1.0f - xi2);
	float z = std::sqrt(xi2);

	Vector3 local_direction(x, y, z);

	// Transform from local space to world space
	omega_i = local_to_world(local_direction, normal);

	// PDF for cosine-weighted hemisphere sampling = cos(theta) / pi
	// Since z = sqrt(xi2) = cos(theta) in local space:
	pdf = z / M_PI;
}

Vector3 Raytracer::local_to_world(Vector3 local_dir, Vector3 normal)
{
	// Create orthonormal basis (o1, o2, n) around normal n

	// Step 1: Find any non-parallel vector to normal
	Vector3 a;
	if (std::abs(normal.x) > 0.9f) {
		a = Vector3(0.0f, 1.0f, 0.0f);  // Use (0,1,0) if normal is close to (1,0,0)
	}
	else {
		a = Vector3(1.0f, 0.0f, 0.0f);  // Otherwise use (1,0,0)
	}

	// Step 2: o2 = n × a (perpendicular to both n and a)
	Vector3 o2 = normal.CrossProduct(a);
	o2.Normalize();

	// Step 3: o1 = o2 × n (completes the orthonormal basis)
	Vector3 o1 = o2.CrossProduct(normal);
	o1.Normalize();

	// Step 4: Transform from local (RS) to world (WS)
	// T_RS->WS = [o1 | o2 | n]
	// world_dir = o1 * local.x + o2 * local.y + n * local.z
	Vector3 world_dir =
		o1 * local_dir.x +
		o2 * local_dir.y +
		normal * local_dir.z;

	return world_dir;
}

float Raytracer::ACESFilm(float x)
{
	float a = 2.51f;
	float b = 0.03f;
	float c = 2.43f;
	float d = 0.59f;
	float e = 0.14f;
	float result = (x * (a * x + b)) / (x * (c * x + d) + e);
	return (std::min)((std::max)(result, 0.0f), 1.0f);
}

Color4f Raytracer::ToneMapACES(const Color4f& hdr)
{
	float r = ACESFilm(hdr.r);
	float g = ACESFilm(hdr.g);
	float b = ACESFilm(hdr.b);

	return Color4f{ r, g, b, 1.0f };
}

void Raytracer::myIntersect(RTCRayHit& ray_hit) {
	bvh.FindHit(ray_hit);
}

int Raytracer::Ui()
{
	static float x = 175.0f, lastX = 175.0f, y = -140.0f, lastY = -140.0f, z = 60.0f, lastZ = 60.0f;
	static int counter = 0;

	// Use a Begin/End pair to created a named window
	ImGui::Begin("Ray Tracer Params");

	ImGui::Text("Surfaces = %d", surfaces_.size());
	ImGui::Text("Materials = %d", materials_.size());
	ImGui::Separator();
	ImGui::Checkbox("Vsync", &vsync_);

	ImGui::Separator();
	ImGui::SliderInt("Samples", &samples_per_pixel_, 1, 100000);

	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	ImGui::End();

	return 0;
}
