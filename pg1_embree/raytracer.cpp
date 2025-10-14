#include "stdafx.h"
#include "raytracer.h"
#include "objloader.h"
#include "tutorials.h"
#include <random>


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

	Color4f hdr_color{ 0.0f, 0.0f, 0.0f, 1.0f };

	if (!use_super_sampling_) {

		RTCRay primary_ray = camera_.GenerateRay(x, y);
		hdr_color = Trace(primary_ray, 0, max_depth_, false);
	}
	else {

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

				Color4f sample_color = Trace(sample_ray, 0, max_depth_, false);

				hdr_color.r += sample_color.r;
				hdr_color.g += sample_color.g;
				hdr_color.b += sample_color.b;
			}
		}

		const float inv_samples = 1.0f / actual_samples;
		hdr_color.r *= inv_samples;
		hdr_color.g *= inv_samples;
		hdr_color.b *= inv_samples;
	}
	

	// ========== ACES FILMIC TONE MAPPING ========== Autor: Claude Sonnet 4.5
	// Volitelná expozice (1.0 = normální, vyšší = světlejší)
	float exposure = 0.8f;
	hdr_color.r *= exposure;
	hdr_color.g *= exposure;
	hdr_color.b *= exposure;

	// Použít ACES tone mapping
	return ToneMapACES(hdr_color);
}

Color4f Raytracer::Trace(RTCRay ray, int depth, int max_depth, bool is_inside)
{
	// Check recursion depth
	if (depth >= max_depth) {
		Vector3 ray_dir(ray.dir_x, ray.dir_y, ray.dir_z);
		ray_dir.Normalize();
		Color3f env_color = environment_map_.texel(ray_dir.x, ray_dir.y, ray_dir.z);
		return Color4f{ env_color.r, env_color.g, env_color.b, 1.0f };
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

	// ---------------------------------------------------------
	auto start = std::chrono::high_resolution_clock::now();
	if (useBVH) {
		bvh.FindHit(ray_hit);
	}
	else {
		RTCIntersectContext context;
		rtcInitIntersectContext(&context);
		rtcIntersect1(scene_, &context, &ray_hit);
	}
	auto end = std::chrono::high_resolution_clock::now();
	double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

	if (useBVH) {
		total_timeBVH += elapsed_ms;
		iterationBVH++;
		if (iterationBVH % 10000 == 0) {
			avg_timeBVH = total_timeBVH / 10000;
			total_timeBVH = 0.0;
			iterationBVH = 0;
		}
	}
	else {
		total_timeRTC += elapsed_ms;
		iterationRTC++;
		if (iterationRTC % 10000 == 0) {
			avg_timeRTC = total_timeRTC / 10000;
			total_timeRTC = 0.0;
			iterationRTC = 0;
		}
	}
	// ---------------------------------------------------------

	if (ray_hit.hit.geomID == RTC_INVALID_GEOMETRY_ID) {
		Vector3 ray_dir(ray.dir_x, ray.dir_y, ray.dir_z);
		ray_dir.Normalize();
		Color3f env_color = environment_map_.texel(ray_dir.x, ray_dir.y, ray_dir.z);
		return Color4f{ env_color.r, env_color.g, env_color.b, 1.0f };
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

	bool is_entering = ray_dir.DotProduct(surface_normal) < 0.0f;
	// původní normála před otočením pro výpočet refrakce
	Vector3 geometric_normal = surface_normal;
	// Otočení normály pokud je potřeba (kvůli osvětlení)
	if (!is_entering) {
		surface_normal = -surface_normal;
	}

	// Calculate hit point
	Vector3 hit_point(
		ray.org_x + ray_hit.ray.tfar * ray.dir_x,
		ray.org_y + ray_hit.ray.tfar * ray.dir_y,
		ray.org_z + ray_hit.ray.tfar * ray.dir_z
	);

	Vector3 view_dir = -ray_dir;
	return CalculatePhongIllumination(geometry, ray_hit, hit_point, surface_normal, geometric_normal,
		hit_material, view_dir, depth, max_depth, is_inside, is_entering);
}

Color4f Raytracer::CalculatePhongIllumination(
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
	bool is_entering)
{
	// Parametry světla
	Vector3 light_pos(200.0f, 300.0f, 500.0f);
	Vector3 light_color(0.7f, 0.7f, 0.7f);
	Vector3 ambient = material->ambient;
	Vector3 specular = material->specular;
	float shininess = material->shininess;

	Coord2f tex_coord;
	rtcInterpolate0(geometry, ray_hit.hit.primID, ray_hit.hit.u, ray_hit.hit.v,
		RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, 1, &tex_coord.u, 2);

	Color3f tex_color{ 1.0f, 1.0f, 1.0f };
	if (material->get_texture(Material::kDiffuseMapSlot)) {
		tex_color = material->get_texture(Material::kDiffuseMapSlot)->get_texel(tex_coord.u, 1.0 - tex_coord.v);
	}
	Vector3 diffuse = material->diffuse * Vector3(tex_color.r, tex_color.g, tex_color.b);

	// Směr ke světlu
	Vector3 light_dir = light_pos - hit_point;
	float light_distance = light_dir.L2Norm();
	light_dir.Normalize();

	// Shadow ray
	RTCRay shadow_ray;
	shadow_ray.org_x = hit_point.x + normal.x * 0.001f;
	shadow_ray.org_y = hit_point.y + normal.y * 0.001f;
	shadow_ray.org_z = hit_point.z + normal.z * 0.001f;
	shadow_ray.dir_x = light_dir.x;
	shadow_ray.dir_y = light_dir.y;
	shadow_ray.dir_z = light_dir.z;
	shadow_ray.tnear = 0.001f;
	shadow_ray.tfar = light_distance;

	RTCIntersectContext context;
	rtcInitIntersectContext(&context);
	rtcOccluded1(scene_, &context, &shadow_ray);
	bool visible = (shadow_ray.tfar >= 0.0f);

	// Phongovy složky
	float NdotL = (std::max)(0.0f, normal.DotProduct(light_dir));
	Vector3 reflect_dir = 2.0f * NdotL * normal - light_dir;
	reflect_dir.Normalize();
	float RdotV = (std::max)(0.0f, reflect_dir.DotProduct(view_dir));

	Vector3 phong = ambient;
	if (visible || !hard_shadows_) {
		if (material->illum == 3) {
			// Žádná difuze pro průhledný material (přidáno po konverzaci s AI)
		}
		else {
			phong += diffuse * NdotL;
		}

		// pruhledný materiál má bílou spekulární složku (info z přednášky)
		Vector3 spec_color = (material->illum == 3) ? Vector3(1.0f, 1.0f, 1.0f) : specular;
		phong += spec_color * std::pow(RdotV, shininess);
	}
	phong = light_color * phong;

	// REFLEXE
	Color4f reflected_color = CalculateReflectedColor(view_dir, normal, hit_point, depth, max_depth, is_inside);

	// REFRAKCE

	// Pro výpočet IOR použitá normála před otočením
	Vector3 ray_dir = -view_dir;
	float eta1 = is_inside ? material->ior : IOR_AIR;
	float eta2 = is_inside ? IOR_AIR : material->ior;
	float cosI = std::abs(ray_dir.DotProduct(geometric_normal));
	float F0 = std::pow((eta1 - eta2) / (eta1 + eta2), 2);
	float R = F0 + (1.0f - F0) * std::pow(1.0f - cosI, 5.0f);

	Color4f refracted_color{ 0,0,0,1 };
	Vector3 attenuation(1.0f, 1.0f, 1.0f);

	if (material->illum == 3 && depth < max_depth) {
		float eta = eta1 / eta2;

		float sinT2 = eta * eta * (1.0f - cosI * cosI);
		bool tir = sinT2 > 1.0f;

		if (tir) {
			R = 1.0f;
		}
		else {
			float cosT = std::sqrt(1.0f - sinT2);

			Vector3 refract_normal = is_entering ? geometric_normal : -geometric_normal;
			Vector3 refracted_dir = eta * ray_dir + (eta * cosI - cosT) * refract_normal;
			refracted_dir.Normalize();

			RTCRay refracted_ray;
			const float eps = 0.001f;
			refracted_ray.org_x = hit_point.x + refracted_dir.x * eps;
			refracted_ray.org_y = hit_point.y + refracted_dir.y * eps;
			refracted_ray.org_z = hit_point.z + refracted_dir.z * eps;
			refracted_ray.dir_x = refracted_dir.x;
			refracted_ray.dir_y = refracted_dir.y;
			refracted_ray.dir_z = refracted_dir.z;
			refracted_ray.tnear = 0.001f;
			refracted_ray.tfar = FLT_MAX;

			// Rekurze s otočeným stavem is_inside
			refracted_color = Trace(refracted_ray, depth + 1, max_depth, !is_inside);
		}
		if (is_inside) {
			float distance_inside = ray_hit.ray.tfar;
			attenuation.x = std::exp(-material->attenuation.x * distance_inside);
			attenuation.y = std::exp(-material->attenuation.y * distance_inside);
			attenuation.z = std::exp(-material->attenuation.z * distance_inside);
		}
	}

	// Výsledná barva
	return Color4f{
		(std::min)(1.0f, (phong.x + reflected_color.r * R + refracted_color.r * (1.0f - R)) * attenuation.x),
		(std::min)(1.0f, (phong.y + reflected_color.g * R + refracted_color.g * (1.0f - R)) * attenuation.y),
		(std::min)(1.0f, (phong.z + reflected_color.b * R + refracted_color.b * (1.0f - R)) * attenuation.z),
		1.0f
	};
}

Color4f Raytracer::CalculateReflectedColor(
	const Vector3& view_dir,
	const Vector3& normal,
	const Vector3& hit_point,
	int depth,
	int max_depth,
	bool is_inside)
{
	if (depth < max_depth) {
		Vector3 reflected_dir = view_dir - 2.0f * view_dir.DotProduct(normal) * normal;
		reflected_dir.Normalize();

		RTCRay reflected_ray;
		reflected_ray.org_x = hit_point.x + normal.x * 0.01f;
		reflected_ray.org_y = hit_point.y + normal.y * 0.01f;
		reflected_ray.org_z = hit_point.z + normal.z * 0.01f;
		reflected_ray.dir_x = reflected_dir.x;
		reflected_ray.dir_y = reflected_dir.y;
		reflected_ray.dir_z = reflected_dir.z;
		reflected_ray.tnear = 0.001f;
		reflected_ray.tfar = FLT_MAX;

		return Trace(reflected_ray, depth + 1, max_depth, is_inside);
	}
	else {
		return Color4f{ 0.0f, 0.0f, 0.0f, 1.0f };
	}
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

	// Gamma korekce
	r = std::pow(r, 1.0f / 2.2f);
	g = std::pow(g, 1.0f / 2.2f);
	b = std::pow(b, 1.0f / 2.2f);

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
	ImGui::Checkbox("Hard Shadows", &hard_shadows_);
	ImGui::Checkbox("Super sampling", &use_super_sampling_);
	ImGui::Separator();

	ImGui::Checkbox("Intersect with BVH", &useBVH);
	ImGui::Text("Avg time for BVH: %.6f ms", avg_timeBVH);
	ImGui::Text("Avg time for rtcIntersect1: %.6f ms", avg_timeRTC);

	ImGui::Separator();
	ImGui::SliderInt("Max ray depth", &max_depth_, 1, 15);

	ImGui::Separator();
	ImGui::SliderFloat("X", &x, -300.0f, 300.0f);
	if (x != lastX) {
		lastX = x;
		Vector3 cam_pos = camera_.getPosition();
		camera_.ChangePosition(Vector3(x, cam_pos.y, cam_pos.z));
	}

	ImGui::SliderFloat("Y", &z, -300.0f, 300.0f);
	if (z != lastZ) {
		lastZ = z;
		Vector3 cam_pos = camera_.getPosition();
		camera_.ChangePosition(Vector3(cam_pos.x, cam_pos.y, z));
	}

	ImGui::SliderFloat("Z", &y, -300.0f, 300.0f);
	if (y != lastY) {
		lastY = y;
		Vector3 cam_pos = camera_.getPosition();
		camera_.ChangePosition(Vector3(cam_pos.x, y, cam_pos.z));
	}

	if (ImGui::Button("Button"))
		counter++;
	ImGui::SameLine();
	ImGui::Text("counter = %d", counter);

	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	ImGui::End();

	return 0;
}
