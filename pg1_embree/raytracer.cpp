#include "stdafx.h"
#include "raytracer.h"
#include "objloader.h"
#include "tutorials.h"
#include <random>
#include "colorFn.h"
#include "mymath.h"

#define M_PI	3.14159265358979323846

Raytracer::Raytracer(const int width, const int height,
	const float fov_y, const Vector3 view_from, const Vector3 view_at,
	const char* config, const Color4f background_color_) : SimpleGuiDX11(width, height), background_color_(background_color_)
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
	// Vyčistit starou scénu
	if (scene_) {
		rtcReleaseScene(scene_);
		scene_ = rtcNewScene(device_);
	}
	
	// Vymazat staré surfaces a materials
	for (auto surface : surfaces_) {
		delete surface;
	}
	surfaces_.clear();
	
	for (auto material : materials_) {
		delete material;
	}
	materials_.clear();
	
	light_sources_.clear();
	total_light_area_ = 0.0f;

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
	
	CollectLightSources();

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

	return compress_color(pixel_color);
}

Color4f Raytracer::Trace(RTCRay ray, int depth, int max_depth)
{
	// Check recursion depth
	if (depth >= max_depth) {
		return background_color_; // background
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
		return background_color_; // background
	}

	// Get intersection data
	RTCGeometry geometry = rtcGetGeometry(scene_, ray_hit.hit.geomID);

	// Získání interpolované normály
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

	bool entering = true;
	if (ray_dir.DotProduct(surface_normal) >= 0.0f) {
		surface_normal = -surface_normal;
		entering = false;
	}

	switch (hit_material->shader) {
		case 1: // LAMBERTOVSKÝ MATERIÁL
			return TraceLambertian(hit_material, surface_normal, hit_point, depth, max_depth);
		
		case 2: // ZDROJ SVĚTLA
			return Color4f{ hit_material->emission.x, hit_material->emission.y, hit_material->emission.z, 1.0f };
		
		case 3: // PHONG MATERIÁL
			return TracePhong(hit_material, surface_normal, ray_dir, hit_point, depth, max_depth);
		
		case 4: // SKLO
			return TraceGlass(hit_material, surface_normal, ray_dir, hit_point, ray_hit.ray.tfar, entering, depth, max_depth);
		
		case 5: // ZRCADLO
			return TraceMirror(hit_material, surface_normal, ray_dir, hit_point, depth, max_depth);
		
		default:
			// Neznámý materiál - defaultně Lambert
			return TraceLambertian(hit_material, surface_normal, hit_point, depth, max_depth);
	}
}

// Lambertovský materiál
Color4f Raytracer::TraceLambertian(Material* material, Vector3 surface_normal, Vector3 hit_point, int depth, int max_depth)
{
	Vector3 k_d = material->diffuse;
	
	// Ruská ruleta
	float max_reflectivity = (k_d.x + k_d.y + k_d.z) / 3.0f;
	float alpha = (std::max)(0.1f, (std::min)(max_reflectivity, 0.95f));
	
	std::uniform_real_distribution<float> r(0.0f, 1.0f);
	if (r(rng_) > alpha) {
		return Color4f{ 0.0f, 0.0f, 0.0f, 1.0f };
	}
	
	Color4f L_direct = { 0.0f, 0.0f, 0.0f, 1.0f };
	Color4f L_indirect = { 0.0f, 0.0f, 0.0f, 1.0f };

	// DIRECT LIGHTING - Light Sampling (NEE)
	if (!light_sources_.empty())
	{
		Vector3 light_point, light_normal;
		float light_pdf;
		Material* light_material;

		if (SampleLight(light_point, light_normal, light_pdf, light_material))
		{
			Vector3 to_light = light_point - hit_point;
			float distance_sq = to_light.SqrL2Norm();
			float distance = std::sqrt(distance_sq);
			to_light.Normalize();

			float cos_theta_surface = to_light.DotProduct(surface_normal);
			float cos_theta_light = (-to_light).DotProduct(light_normal);

			// Kontrola, zda světlo svítí směrem k povrchu
			if (cos_theta_surface > 0.001f && cos_theta_light > 0.001f)
			{
				// Visibility test
				RTCRay shadow_ray = make_secondary_ray(hit_point, to_light);
				shadow_ray.tfar = distance - 0.001f;

				RTCHit shadow_hit;
				shadow_hit.geomID = RTC_INVALID_GEOMETRY_ID;

				RTCRayHit shadow_ray_hit;
				shadow_ray_hit.ray = shadow_ray;
				shadow_ray_hit.hit = shadow_hit;

				RTCIntersectContext context;
				rtcInitIntersectContext(&context);
				rtcIntersect1(scene_, &context, &shadow_ray_hit);

				if (shadow_ray_hit.hit.geomID == RTC_INVALID_GEOMETRY_ID)
				{
					float geometric_term = cos_theta_light / distance_sq;

					if (geometric_term > 1e-6f && light_pdf > 1e-6f)
					{
						// Lambert BRDF = albedo / PI
						float brdf_value = 1.0f / M_PI;

						// Radiancia ze světla
						Vector3 L_e = light_material->emission;

						// Direct lighting contribution
						// L = BRDF * L_e * cos_theta * G / pdf_light
						float weight = brdf_value * cos_theta_surface * geometric_term / light_pdf;

						if (std::isfinite(weight) && weight > 0.0f)
						{
							L_direct.r = k_d.x * L_e.x * weight;
							L_direct.g = k_d.y * L_e.y * weight;
							L_direct.b = k_d.z * L_e.z * weight;
						}
					}
				}
			}
		}
	}

	// INDIRECT LIGHTING - BRDF Sampling
	Vector3 omega_i;
	float pdf_brdf;
	sample_hemisphere(surface_normal, omega_i, pdf_brdf);

	float cos_theta_i = omega_i.DotProduct(surface_normal);
	if (cos_theta_i > 0.0f && pdf_brdf > 1e-6f)
	{
		RTCRay indirect_ray = make_secondary_ray(hit_point, omega_i);

		// Trace indirect ray
		RTCHit indirect_hit;
		indirect_hit.geomID = RTC_INVALID_GEOMETRY_ID;

		RTCRayHit indirect_ray_hit;
		indirect_ray_hit.ray = indirect_ray;
		indirect_ray_hit.hit = indirect_hit;

		RTCIntersectContext context;
		rtcInitIntersectContext(&context);
		rtcIntersect1(scene_, &context, &indirect_ray_hit);

		if (indirect_ray_hit.hit.geomID != RTC_INVALID_GEOMETRY_ID)
		{
			// Rekurzivní trace
			Color4f L_i = Trace(indirect_ray, depth + 1, max_depth);

			// Kontrola, zda jsme nezasáhli světlo
			RTCGeometry geometry = rtcGetGeometry(scene_, indirect_ray_hit.hit.geomID);
			Material* hit_mat = (Material*)rtcGetGeometryUserData(geometry);

			if (hit_mat && hit_mat->shader == 2)
			{
				// Zasáhli jsme světlo
				// Vypočti PDF pro light sampling tohoto bodu
				Vector3 hit_light_point(
					indirect_ray.org_x + indirect_ray_hit.ray.tfar * indirect_ray.dir_x,
					indirect_ray.org_y + indirect_ray_hit.ray.tfar * indirect_ray.dir_y,
					indirect_ray.org_z + indirect_ray_hit.ray.tfar * indirect_ray.dir_z
				);

				// Najdi area PDF světla
				float light_area_pdf = 0.0f;
				for (const auto& light : light_sources_)
				{
					if (light.material == hit_mat)
					{
						light_area_pdf = 1.0f / total_light_area_;
						break;
					}
				}

				if (light_area_pdf > 1e-6f)
				{
					// Převod na solid angle PDF
					Normal3f normal;
					rtcInterpolate0(geometry, indirect_ray_hit.hit.primID,
						indirect_ray_hit.hit.u, indirect_ray_hit.hit.v,
						RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, 0, &normal.x, 3);
					Vector3 light_normal(normal.x, normal.y, normal.z);
					light_normal.Normalize();

					float distance = indirect_ray_hit.ray.tfar;
					float cos_theta_light = std::abs((-omega_i).DotProduct(light_normal));

					float pdf_light_solid = light_area_pdf * (distance * distance) /
						(cos_theta_light + 1e-6f);

					// MIS weight
					float mis_weight = PowerHeuristic(pdf_brdf, pdf_light_solid);

					// Indirect contribution s MIS
					float brdf_value = 1.0f / M_PI;
					float weight = mis_weight * brdf_value * cos_theta_i / pdf_brdf;

					L_indirect.r = L_i.r * k_d.x * weight;
					L_indirect.g = L_i.g * k_d.y * weight;
					L_indirect.b = L_i.b * k_d.z * weight;
				}
			}
			else
			{
				// Nezasáhli světlo - standardní indirect lighting bez MIS
				float brdf_value = 1.0f / M_PI;
				float weight = brdf_value * cos_theta_i / pdf_brdf;

				L_indirect.r = L_i.r * k_d.x * weight;
				L_indirect.g = L_i.g * k_d.y * weight;
				L_indirect.b = L_i.b * k_d.z * weight;
			}
		}
		else
		{
			// Paprsek zasáhl POZADÍ

			float brdf_value = 1.0f / M_PI;
			float weight = brdf_value * cos_theta_i / pdf_brdf;

			L_indirect.r = background_color_.r * k_d.x * weight;
			L_indirect.g = background_color_.g * k_d.y * weight;
			L_indirect.b = background_color_.b * k_d.z * weight;
		}
	}

	// Kombinace direct + indirect s kompenzací ruské rulety
	Color4f L_r;
	L_r.r = (L_direct.r + L_indirect.r) / alpha;
	L_r.g = (L_direct.g + L_indirect.g) / alpha;
	L_r.b = (L_direct.b + L_indirect.b) / alpha;
	L_r.a = 1.0f;

	return L_r;
}

// Phong materiál
Color4f Raytracer::TracePhong(Material* material, Vector3 surface_normal, Vector3 ray_dir,
	Vector3 hit_point, int depth, int max_depth)
{
	Vector3 k_d = material->diffuse;
	Vector3 k_s = material->specular;
	float shininess = material->shininess;
	float ior = material->ior;

	float F0 = ((1.0f - ior) / (1.0f + ior)) * ((1.0f - ior) / (1.0f + ior));

	// Vektor pohledu
	Vector3 omega_o = -ray_dir;
	omega_o.Normalize();
	float cos_theta_o = omega_o.DotProduct(surface_normal);
	if (cos_theta_o < 0.0f) cos_theta_o = 0.0f;

	// Fresnelův člen
	float F = schlick_fresnel(F0, cos_theta_o);

	// Pravděpodobnosti výběru cesty
	float diffuse_strength = (k_d.x + k_d.y + k_d.z) / 3.0f;
	float specular_strength = (k_s.x + k_s.y + k_s.z) / 3.0f;
	float total_strength = diffuse_strength + specular_strength;

	float p_spec, p_diff;
	if (total_strength > 1e-6f) {
		p_spec = specular_strength / total_strength;
		p_diff = diffuse_strength / total_strength;
	}
	else {
		p_spec = 0.5f;
		p_diff = 0.5f;
	}

	// Ruská ruleta
	float max_reflectivity = (std::max)(diffuse_strength, specular_strength);
	float alpha = (std::max)(0.1f, (std::min)(max_reflectivity, 0.95f));

	std::uniform_real_distribution<float> r(0.0f, 1.0f);
	if (r(rng_) > alpha) {
		return Color4f{ 0.0f, 0.0f, 0.0f, 1.0f };
	}

	Color4f L_r = { 0.0f, 0.0f, 0.0f, 1.0f };
	float xi = r(rng_);

	// ROZHODNUTÍ: Spekulární nebo difuzní cesta

	if (xi < p_spec) {
		// SPEKULÁRNÍ CESTA

		Color4f L_direct = { 0.0f, 0.0f, 0.0f, 1.0f };
		Color4f L_indirect = { 0.0f, 0.0f, 0.0f, 1.0f };

		// Perfektní odraz pro Phong lobe
		Vector3 R = ray_dir - surface_normal * (2.0f * ray_dir.DotProduct(surface_normal));
		R.Normalize();

		// 1. DIRECT LIGHTING
		if (!light_sources_.empty())
		{
			Vector3 light_point, light_normal;
			float light_pdf;
			Material* light_material;

			if (SampleLight(light_point, light_normal, light_pdf, light_material))
			{
				Vector3 to_light = light_point - hit_point;
				float distance_sq = to_light.SqrL2Norm();
				float distance = std::sqrt(distance_sq);
				to_light.Normalize();

				float cos_theta_surface = to_light.DotProduct(surface_normal);
				float cos_theta_light = (-to_light).DotProduct(light_normal);

				if (cos_theta_surface > 0.001f && cos_theta_light > 0.001f)
				{
					// Shadow ray test
					RTCRay shadow_ray = make_secondary_ray(hit_point, to_light);
					shadow_ray.tfar = distance - 0.001f;

					RTCHit shadow_hit;
					shadow_hit.geomID = RTC_INVALID_GEOMETRY_ID;

					RTCRayHit shadow_ray_hit;
					shadow_ray_hit.ray = shadow_ray;
					shadow_ray_hit.hit = shadow_hit;

					RTCIntersectContext context;
					rtcInitIntersectContext(&context);
					rtcIntersect1(scene_, &context, &shadow_ray_hit);

					if (shadow_ray_hit.hit.geomID == RTC_INVALID_GEOMETRY_ID)
					{
						float geometric_term = cos_theta_light / distance_sq;

						if (geometric_term > 1e-6f && light_pdf > 1e-6f)
						{
							// Phong BRDF s cosine denominátorem
							float R_dot_light = R.DotProduct(to_light);
							if (R_dot_light < 0.0f) R_dot_light = 0.0f;

							float phong_brdf;

							if (phong_mode_ == PhongBRDFMode::ENERGY_CONSERVED) {
								// Energy-conserved: BRDF / cos(theta)
								phong_brdf = (shininess + 1.0f) / (2.0f * M_PI) *
									std::pow(R_dot_light, shininess) / cos_theta_surface;
							}
							else {
								// Energy-normalized: jen BRDF
								phong_brdf = (shininess + 1.0f) / (2.0f * M_PI) *
									std::pow(R_dot_light, shininess);
							}

							if (std::isfinite(phong_brdf) && phong_brdf >= 0.0f)
							{
								Vector3 L_e = light_material->emission;

								// Vypočti PDF pro BRDF sampling tohoto směru
								float pdf_brdf = (shininess + 1.0f) / (2.0f * M_PI) *
									std::pow(R_dot_light, shininess);

								// Převod light PDF z area na solid angle
								float pdf_light_solid = light_pdf * distance_sq /
									(cos_theta_light + 1e-6f);

								// MIS weight
								float mis_weight = PowerHeuristic(pdf_light_solid, pdf_brdf);

								float weight = mis_weight * phong_brdf * cos_theta_surface *
									geometric_term / light_pdf;

								if (std::isfinite(weight) && weight >= 0.0f)
								{
									L_direct.r = k_s.x * L_e.x * weight * F;
									L_direct.g = k_s.y * L_e.y * weight * F;
									L_direct.b = k_s.z * L_e.z * weight * F;
								}
							}
						}
					}
				}
			}
		}

		// 2. INDIRECT LIGHTING
		float pdf_phong;
		Vector3 omega_i = sample_phong_lobe(R, shininess, pdf_phong);

		float cos_theta_i = omega_i.DotProduct(surface_normal);
		if (cos_theta_i > 0.001f && pdf_phong > 1e-6f)
		{
			RTCRay indirect_ray = make_secondary_ray(hit_point, omega_i);

			RTCHit indirect_hit;
			indirect_hit.geomID = RTC_INVALID_GEOMETRY_ID;

			RTCRayHit indirect_ray_hit;
			indirect_ray_hit.ray = indirect_ray;
			indirect_ray_hit.hit = indirect_hit;

			RTCIntersectContext context;
			rtcInitIntersectContext(&context);
			rtcIntersect1(scene_, &context, &indirect_ray_hit);

			float R_dot_omega_i = R.DotProduct(omega_i);
			if (R_dot_omega_i < 0.0f) R_dot_omega_i = 0.0f;

			float phong_brdf;

			if (phong_mode_ == PhongBRDFMode::ENERGY_CONSERVED) {
				// Energy-conserved: BRDF / cos(theta)
				phong_brdf = (shininess + 1.0f) / (2.0f * M_PI) *
					std::pow(R_dot_omega_i, shininess) / cos_theta_i;
			}
			else {
				// Energy-normalized: jen BRDF
				phong_brdf = (shininess + 1.0f) / (2.0f * M_PI) *
					std::pow(R_dot_omega_i, shininess);
			}

			if (!std::isfinite(phong_brdf) || phong_brdf < 0.0f) {
				phong_brdf = 0.0f;
			}

			if (indirect_ray_hit.hit.geomID != RTC_INVALID_GEOMETRY_ID)
			{
				Color4f L_i = Trace(indirect_ray, depth + 1, max_depth);

				// Kontrola, zda jsme zasáhli světlo
				RTCGeometry geometry = rtcGetGeometry(scene_, indirect_ray_hit.hit.geomID);
				Material* hit_mat = (Material*)rtcGetGeometryUserData(geometry);


				if (hit_mat && hit_mat->shader == 2)
				{
					// Zasáhli jsme světlo
					float light_area_pdf = 0.0f;
					for (const auto& light : light_sources_)
					{
						if (light.material == hit_mat)
						{
							light_area_pdf = 1.0f / total_light_area_;
							break;
						}
					}

					if (light_area_pdf > 1e-6f)
					{
						Normal3f normal;
						rtcInterpolate0(geometry, indirect_ray_hit.hit.primID,
							indirect_ray_hit.hit.u, indirect_ray_hit.hit.v,
							RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, 0, &normal.x, 3);
						Vector3 light_normal(normal.x, normal.y, normal.z);
						light_normal.Normalize();

						float distance = indirect_ray_hit.ray.tfar;
						float cos_theta_light = std::abs((-omega_i).DotProduct(light_normal));

						float pdf_light_solid = light_area_pdf * (distance * distance) /
							(cos_theta_light + 1e-6f);

						// MIS weight
						float mis_weight = PowerHeuristic(pdf_phong, pdf_light_solid);

						float weight = mis_weight * phong_brdf * cos_theta_i / pdf_phong;

						if (std::isfinite(weight) && weight >= 0.0f)
						{
							L_indirect.r = L_i.r * k_s.x * weight * F;
							L_indirect.g = L_i.g * k_s.y * weight * F;
							L_indirect.b = L_i.b * k_s.z * weight * F;
						}
					}
				}
				else
				{
					// Nezasáhli světlo
					float weight = phong_brdf * cos_theta_i / pdf_phong;

					if (std::isfinite(weight) && weight >= 0.0f)
					{
						L_indirect.r = L_i.r * k_s.x * weight * F;
						L_indirect.g = L_i.g * k_s.y * weight * F;
						L_indirect.b = L_i.b * k_s.z * weight * F;
					}
				}
			}
			// Zásah pozadí
			else
			{
				float weight = phong_brdf * cos_theta_i / pdf_phong;

				if (std::isfinite(weight) && weight >= 0.0f) {
					L_indirect.r = background_color_.r * k_s.x * weight * F;
					L_indirect.g = background_color_.g * k_s.y * weight * F;
					L_indirect.b = background_color_.b * k_s.z * weight * F;
				}
			}
		}

		// Kombinace direct + indirect pro spekulární cestu
		float compensation = 1.0f / p_spec;
		L_r.r = (L_direct.r + L_indirect.r) * compensation;
		L_r.g = (L_direct.g + L_indirect.g) * compensation;
		L_r.b = (L_direct.b + L_indirect.b) * compensation;

	}
	else {
		// DIFUZNÍ CESTA

		Color4f L_direct = { 0.0f, 0.0f, 0.0f, 1.0f };
		Color4f L_indirect = { 0.0f, 0.0f, 0.0f, 1.0f };

		// 1. DIRECT LIGHTING
		if (!light_sources_.empty())
		{
			Vector3 light_point, light_normal;
			float light_pdf;
			Material* light_material;

			if (SampleLight(light_point, light_normal, light_pdf, light_material))
			{
				Vector3 to_light = light_point - hit_point;
				float distance_sq = to_light.SqrL2Norm();
				float distance = std::sqrt(distance_sq);
				to_light.Normalize();

				float cos_theta_surface = to_light.DotProduct(surface_normal);
				float cos_theta_light = (-to_light).DotProduct(light_normal);

				if (cos_theta_surface > 0.001f && cos_theta_light > 0.001f)
				{
					// Shadow ray test
					RTCRay shadow_ray = make_secondary_ray(hit_point, to_light);
					shadow_ray.tfar = distance - 0.001f;

					RTCHit shadow_hit;
					shadow_hit.geomID = RTC_INVALID_GEOMETRY_ID;

					RTCRayHit shadow_ray_hit;
					shadow_ray_hit.ray = shadow_ray;
					shadow_ray_hit.hit = shadow_hit;

					RTCIntersectContext context;
					rtcInitIntersectContext(&context);
					rtcIntersect1(scene_, &context, &shadow_ray_hit);

					if (shadow_ray_hit.hit.geomID == RTC_INVALID_GEOMETRY_ID)
					{
						float geometric_term = cos_theta_light / distance_sq;

						if (geometric_term > 1e-6f && light_pdf > 1e-6f)
						{
							// Lambert BRDF = 1 / PI
							float brdf_value = 1.0f / M_PI;

							Vector3 L_e = light_material->emission;

							float weight = brdf_value * cos_theta_surface * geometric_term / light_pdf;

							if (std::isfinite(weight) && weight >= 0.0f)
							{
								L_direct.r = k_d.x * L_e.x * weight;
								L_direct.g = k_d.y * L_e.y * weight;
								L_direct.b = k_d.z * L_e.z * weight;
							}
						}
					}
				}
			}
		}

		// 2. INDIRECT LIGHTING 
		Vector3 omega_i;
		float pdf_lambert;
		sample_hemisphere(surface_normal, omega_i, pdf_lambert);

		float cos_theta_i = omega_i.DotProduct(surface_normal);
		if (cos_theta_i > 0.0f && pdf_lambert > 1e-6f)
		{
			RTCRay indirect_ray = make_secondary_ray(hit_point, omega_i);

			RTCHit indirect_hit;
			indirect_hit.geomID = RTC_INVALID_GEOMETRY_ID;

			RTCRayHit indirect_ray_hit;
			indirect_ray_hit.ray = indirect_ray;
			indirect_ray_hit.hit = indirect_hit;

			RTCIntersectContext context;
			rtcInitIntersectContext(&context);
			rtcIntersect1(scene_, &context, &indirect_ray_hit);

			if (indirect_ray_hit.hit.geomID != RTC_INVALID_GEOMETRY_ID)
			{
				Color4f L_i = Trace(indirect_ray, depth + 1, max_depth);

				// Kontrola, zda jsme zasáhli světlo
				RTCGeometry geometry = rtcGetGeometry(scene_, indirect_ray_hit.hit.geomID);
				Material* hit_mat = (Material*)rtcGetGeometryUserData(geometry);

				if (hit_mat && hit_mat->shader == 2)
				{
					// Zasáhli jsme světlo
					float light_area_pdf = 0.0f;
					for (const auto& light : light_sources_)
					{
						if (light.material == hit_mat)
						{
							light_area_pdf = 1.0f / total_light_area_;
							break;
						}
					}

					if (light_area_pdf > 1e-6f)
					{
						Normal3f normal;
						rtcInterpolate0(geometry, indirect_ray_hit.hit.primID,
							indirect_ray_hit.hit.u, indirect_ray_hit.hit.v,
							RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, 0, &normal.x, 3);
						Vector3 light_normal(normal.x, normal.y, normal.z);
						light_normal.Normalize();

						float distance = indirect_ray_hit.ray.tfar;
						float cos_theta_light = std::abs((-omega_i).DotProduct(light_normal));

						float pdf_light_solid = light_area_pdf * (distance * distance) /
							(cos_theta_light + 1e-6f);

						float mis_weight = PowerHeuristic(pdf_lambert, pdf_light_solid);

						float brdf_value = 1.0f / M_PI;
						float weight = mis_weight * brdf_value * cos_theta_i / pdf_lambert;

						L_indirect.r = L_i.r * k_d.x * weight;
						L_indirect.g = L_i.g * k_d.y * weight;
						L_indirect.b = L_i.b * k_d.z * weight;
					}
				}
				else
				{
					// Nezasáhli světlo
					float brdf_value = 1.0f / M_PI;
					float weight = brdf_value * cos_theta_i / pdf_lambert;

					L_indirect.r = L_i.r * k_d.x * weight;
					L_indirect.g = L_i.g * k_d.y * weight;
					L_indirect.b = L_i.b * k_d.z * weight;
				}
			}
			// Zásah pozadí
			else
			{
				float brdf_value = 1.0f / M_PI;
				float weight = brdf_value * cos_theta_i / pdf_lambert;

				L_indirect.r = background_color_.r * k_d.x * weight;
				L_indirect.g = background_color_.g * k_d.y * weight;
				L_indirect.b = background_color_.b * k_d.z * weight;
			}
		}

		// Kombinace direct + indirect pro difuzní cestu
		float compensation = 1.0f / p_diff;
		L_r.r = (L_direct.r + L_indirect.r) * compensation;
		L_r.g = (L_direct.g + L_indirect.g) * compensation;
		L_r.b = (L_direct.b + L_indirect.b) * compensation;
	}

	// Aplikace ruské rulety
	L_r.r /= alpha;
	L_r.g /= alpha;
	L_r.b /= alpha;
	L_r.a = 1.0f;

	return L_r;
}

// Zrcadlový materiál
Color4f Raytracer::TraceMirror(Material* material, Vector3 surface_normal, Vector3 ray_dir, Vector3 hit_point, int depth, int max_depth)
{
	// Perfektní odraz: r = d - 2(d·n)n
	Vector3 reflection = ray_dir - surface_normal * (2.0f * ray_dir.DotProduct(surface_normal));
	reflection.Normalize();
	
	Color4f reflected_color = Trace(make_secondary_ray(hit_point, reflection), depth + 1, max_depth);
	
	Color4f result;
	result.r = reflected_color.r * material->specular.x;
	result.g = reflected_color.g * material->specular.y;
	result.b = reflected_color.b * material->specular.z;
	result.a = 1.0f;
	
	return result;
}

// Skleněný materiál - lom a odraz
Color4f Raytracer::TraceGlass(Material* material, Vector3 surface_normal, Vector3 ray_dir, Vector3 hit_point, float distance, bool entering, int depth, int max_depth)
{
	const float air_ior = 1.0f;  // IOR vzduchu
	float material_ior = material->ior;
	
	Vector3 normal = surface_normal;
	
	float n1, n2;  // IOR prostředí odkud/kam
	
	if (entering) {
		n1 = air_ior;
		n2 = material_ior;
	} else {
		n1 = material_ior;
		n2 = air_ior;
	}
	
	float eta = n1 / n2;
	
	Vector3 incident = -ray_dir;
	incident.Normalize();
	float cos_theta_i = incident.DotProduct(normal);
	
	if (cos_theta_i < 0.0f) {
		cos_theta_i = -cos_theta_i;
	}
	
	float sin_theta_i_sq = 1.0f - cos_theta_i * cos_theta_i;
	float sin_theta_t_sq = eta * eta * sin_theta_i_sq;
	
	// Fresnelův člen
	float F0 = ((n1 - n2) / (n1 + n2)) * ((n1 - n2) / (n1 + n2));
	float F = schlick_fresnel(F0, cos_theta_i);
	
	std::uniform_real_distribution<float> r(0.0f, 1.0f);
	
	// Kontrola úplného vnitřního odrazu
	if (sin_theta_t_sq >= 1.0f) {
		// Úplný vnitřní odraz - může nastat pouze při výstupu z hustšího do řidšího prostředí
		Vector3 reflection = ray_dir - normal * (2.0f * ray_dir.DotProduct(normal));
		reflection.Normalize();
		
		Color4f reflected_color = Trace(make_secondary_ray(hit_point, reflection), depth + 1, max_depth);
		
		Color4f result;
		result.r = reflected_color.r * material->specular.x;
		result.g = reflected_color.g * material->specular.y;
		result.b = reflected_color.b * material->specular.z;
		result.a = 1.0f;
		
		return result;
	}
	
	// Probabilistický výběr mezi odrazem a lomem podle Fresnelova členu
	if (r(rng_) < F) {
		// ODRAZ
		Vector3 reflection = ray_dir - normal * (2.0f * ray_dir.DotProduct(normal));
		reflection.Normalize();
		
		Color4f reflected_color = Trace(make_secondary_ray(hit_point, reflection), depth + 1, max_depth);
		
		Color4f result;
		result.r = reflected_color.r * material->specular.x;
		result.g = reflected_color.g * material->specular.y;
		result.b = reflected_color.b * material->specular.z;
		result.a = 1.0f;
		
		return result;
	} else {
		// LOM
		float cos_theta_t = std::sqrt(1.0f - sin_theta_t_sq);
		
		// Snellův zákon pro směr lomu
		Vector3 refraction = eta * ray_dir + (eta * cos_theta_i - cos_theta_t) * normal;
		refraction.Normalize();
		
		Color4f refracted_color = Trace(make_secondary_ray(hit_point, refraction), depth + 1, max_depth);
		
		// Aplikace Beer-Lambert zákona pro absorpci světla uvnitř materiálu
		Color4f result;
		if (entering) {
			result.r = refracted_color.r;
			result.g = refracted_color.g;
			result.b = refracted_color.b;
		} else {
			Vector3 attenuation = material->attenuation;

			result.r = refracted_color.r * exp(-attenuation.x * distance);
			result.g = refracted_color.g * exp(-attenuation.y * distance);
			result.b = refracted_color.b * exp(-attenuation.z * distance);
		}
		result.a = 1.0f;
		
		return result;
	}
}

float Raytracer::gamma_quot(float a, float b) {
	return std::exp(std::lgamma(a) - std::lgamma(b));
}

float Raytracer::ibeta(float x, float a, float b) {
	if (x <= 0.0f) return 0.0f;
	if (x >= 1.0f) return 1.0f;
	return std::exp(std::lgamma(a + b) - std::lgamma(a) - std::lgamma(b)
		+ a * std::log(x) + b * std::log(1.0f - x)) / a;
}

float Raytracer::calc_I_M(float NdotV, float n) {
	float costerm = NdotV;
	float sintrm_sq = 1.0f - costerm * costerm;
	float halfn = 0.5f * n;

	float negterm = costerm;
	if (n > 1e-18f) {
		negterm = halfn * ibeta(sintrm_sq, halfn + 0.5f, 0.5f);
		negterm += costerm * std::pow(costerm, n);
	}

	return (
		(2.0f * 3.1415926535f) *
		(halfn * gamma_quot(halfn + 0.5f, halfn + 1.0f) *
			std::pow(sintrm_sq, halfn + 0.5f)
			+ negterm)
		/ (n + 2.0f)
		);
}

float Raytracer::schlick_fresnel(float F0, float cos_theta) {
	float one_minus_cos = 1.0f - cos_theta;
	float one_minus_cos_5 = one_minus_cos * one_minus_cos * one_minus_cos * one_minus_cos * one_minus_cos;
	return F0 + (1.0f - F0) * one_minus_cos_5;
}

Vector3 Raytracer::sample_phong_lobe(const Vector3& R, float shininess, float& pdf)
{
	std::uniform_real_distribution<float> dist(0.0f, 1.0f);
	float xi1 = dist(rng_);
	float xi2 = dist(rng_);

	// Vzorkování podle cos(theta)^shininess
	float cos_theta = std::pow(xi1, 1.0f / (shininess + 1.0f));
	float sin_theta = std::sqrt(1.0f - cos_theta * cos_theta);
	float phi = 2.0f * M_PI * xi2;

	Vector3 local_dir(std::cos(phi) * sin_theta, std::sin(phi) * sin_theta, cos_theta);

	// PDF tohoto vzorku v lokálních souřadnicích
	pdf = (shininess + 1.0f) / (2.0f * M_PI) * std::pow(cos_theta, shininess);

	// Transformace z lokálního prostoru (kde Z je R) do světového prostoru
	Vector3 sampled_direction = local_to_world(local_dir, R);
	
	if (sampled_direction.DotProduct(R) < 0.0f) {
		pdf = 1.0f;
		return R;
	}
	
	return sampled_direction;
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
	// Claude
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


// NEE (Next Event Estimation)

// Sesbírání všech světelných zdrojů ze scény
void Raytracer::CollectLightSources()
{
	light_sources_.clear();
	total_light_area_ = 0.0f;

	// Projdi všechny povrchy a najdi emissive trojúhelníky
	for (auto surface : surfaces_)
	{
		Material* mat = surface->get_material();

		// Kontrola, zda je materiál světelný zdroj (shader == 2)
		if (mat && mat->shader == 2)
		{
			// Přidej všechny trojúhelníky z tohoto povrchu
			for (int i = 0; i < surface->no_triangles(); ++i)
			{
				Triangle* tri = &surface->get_triangle(i);
				float area = TriangleArea(tri);

				if (area > 1e-6f)  // Ignoruj degenerované trojúhelníky
				{
					LightSource light;
					light.triangle = tri;
					light.material = mat;
					light.area = area;

					light_sources_.push_back(light);
					total_light_area_ += area;
				}
			}
		}
	}

	printf("Found %d light source triangles, total area: %f\n", (int)light_sources_.size(), total_light_area_);
}

// Výpočet plochy trojúhelníku
float Raytracer::TriangleArea(const Triangle* tri)
{
	Vector3 v0 = tri->vertex(0).position;
	Vector3 v1 = tri->vertex(1).position;
	Vector3 v2 = tri->vertex(2).position;

	Vector3 edge1 = v1 - v0;
	Vector3 edge2 = v2 - v0;
	Vector3 cross = edge1.CrossProduct(edge2);

	return 0.5f * cross.L2Norm();
}

// Vzorkování náhodného bodu na trojúhelníku
Vector3 Raytracer::SampleTriangle(const Triangle* tri, float& pdf)
{
	std::uniform_real_distribution<float> dist(0.0f, 1.0f);
	float xi1 = dist(rng_);
	float xi2 = dist(rng_);

	float sqrt_xi1 = std::sqrt(xi1);
	float u = 1.0f - sqrt_xi1;
	float v = xi2 * sqrt_xi1;
	float w = 1.0f - u - v;

	Vector3 v0 = tri->vertex(0).position;
	Vector3 v1 = tri->vertex(1).position;
	Vector3 v2 = tri->vertex(2).position;

	Vector3 point = v0 * u + v1 * v + v2 * w;

	float area = TriangleArea(tri);
	pdf = 1.0f / area;

	return point;
}

// Vzorkování náhodného světelného zdroje
bool Raytracer::SampleLight(Vector3& light_point, Vector3& light_normal,
	float& pdf, Material*& light_material)
{
	if (light_sources_.empty() || total_light_area_ <= 0.0f)
	{
		return false;
	}

	std::uniform_real_distribution<float> dist(0.0f, 1.0f);
	float xi = dist(rng_) * total_light_area_;

	float cumulative_area = 0.0f;
	LightSource* selected_light = nullptr;

	for (auto& light : light_sources_)
	{
		cumulative_area += light.area;
		if (xi <= cumulative_area)
		{
			selected_light = &light;
			break;
		}
	}

	if (!selected_light)
	{
		selected_light = &light_sources_.back();
	}

	float triangle_pdf;
	light_point = SampleTriangle(selected_light->triangle, triangle_pdf);

	Vector3 v0 = selected_light->triangle->vertex(0).position;
	Vector3 v1 = selected_light->triangle->vertex(1).position;
	Vector3 v2 = selected_light->triangle->vertex(2).position;

	Vector3 edge1 = v1 - v0;
	Vector3 edge2 = v2 - v0;
	light_normal = edge1.CrossProduct(edge2);
	light_normal.Normalize();

	float light_selection_prob = selected_light->area / total_light_area_;
	pdf = triangle_pdf * light_selection_prob;

	light_material = selected_light->material;

	return true;
}

float Raytracer::PowerHeuristic(float pdf_a, float pdf_b)
{
	float a = pdf_a * pdf_a;
	float b = pdf_b * pdf_b;

	if (a + b < 1e-10f)
	{
		return 0.5f;
	}

	float result = a / (a + b);

	if (!std::isfinite(result))
	{
		return 0.5f;
	}

	return result;
}

int Raytracer::Ui()
{
	static float x = 175.0f, lastX = 175.0f, y = -140.0f, lastY = -140.0f, z = 60.0f, lastZ = 60.0f;
	static bool is_furnace_ = false, is_cornel_ = true;

	// Use a Begin/End pair to created a named window
	ImGui::Begin("Ray Tracer Params");

	ImGui::Text("Surfaces = %d", surfaces_.size());
	ImGui::Text("Materials = %d", materials_.size());
	ImGui::Separator();
	ImGui::Checkbox("Vsync", &vsync_);
	ImGui::Separator();
	if (ImGui::Button("Load furnace test") && !is_furnace_) {
		is_furnace_ = true;
		is_cornel_ = false;
		background_color_ = Color4f{1.0f, 1.0f, 1.0f, 1.0f};
		camera_ = Camera(640, 480, deg2rad(48.0), Vector3(0, -6, 0), Vector3(0, 0, 0));
		LoadScene("../../../data/geosphere.obj");
	}

	if (ImGui::Button("Load cornel box") && !is_cornel_) {
		is_cornel_ = true;
		is_furnace_ = false;
		background_color_ = Color4f{ 0.0f, 0.0f, 0.0f, 1.0f };
		camera_ = Camera(640, 480, deg2rad(48.0), Vector3(-40, -1000, 250), Vector3(0, 0, 250));
		LoadScene("../../../data/cornell_box2.obj");
	}

	ImGui::Text("Phong BRDF Mode:");

	int current_mode = static_cast<int>(phong_mode_);

	if (ImGui::RadioButton("Energy-Conserved (BRDF / cos)",
		&current_mode,
		static_cast<int>(PhongBRDFMode::ENERGY_CONSERVED))) {
		phong_mode_ = PhongBRDFMode::ENERGY_CONSERVED;
	}

	if (ImGui::RadioButton("Energy-Normalized (BRDF only)",
		&current_mode,
		static_cast<int>(PhongBRDFMode::ENERGY_NORMALIZED))) {
		phong_mode_ = PhongBRDFMode::ENERGY_NORMALIZED;
	}

	ImGui::Separator();

	ImGui::Separator();
	ImGui::SliderInt("Samples", &samples_per_pixel_, 1, 100000);

	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	ImGui::End();

	return 0;
}
