#include "stdafx.h"
#include "BVH.h"

BVH::BVH() : root_(nullptr), items_() {
}

BVH::~BVH() {
	delete root_;
}

void BVH::AddItem(Triangle* item) {
	items_.push_back(item);
}

AABB BVH::GetNodeAABB(int from, int to) {
	AABB box;
	box.bounds[0] = Vector3(FLT_MAX, FLT_MAX, FLT_MAX);
	box.bounds[1] = Vector3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	for (int i = from; i <= to; i++) {
		AABB tempBox = (items_)[i]->getAABB();

		box.bounds[0].x = (std::min)(box.bounds[0].x, tempBox.bounds[0].x);
		box.bounds[0].y = (std::min)(box.bounds[0].y, tempBox.bounds[0].y);
		box.bounds[0].z = (std::min)(box.bounds[0].z, tempBox.bounds[0].z);

		box.bounds[1].x = (std::max)(box.bounds[1].x, tempBox.bounds[1].x);
		box.bounds[1].y = (std::max)(box.bounds[1].y, tempBox.bounds[1].y);
		box.bounds[1].z = (std::max)(box.bounds[1].z, tempBox.bounds[1].z);
	}

	return box;
}

void BVH::BuildTree() {
	if (items_.empty()) {
		root_ = nullptr;
		return;
	}
	root_ = BuildTree(0, items_.size() - 1);
}

Node* BVH::BuildTree(int from, int to) {
	Node* node = new Node(from, to);
	node->box = GetNodeAABB(from, to); // get the bounds of all vertices of all triangles in the current range

	int num_triangles = to - from + 1;

	if (num_triangles <= max_leaf_items) {  // e.g. max_leaf_items = 4
		return node; // leaf
	}

	int axis, pivot;
	float cost;
	FindBestSplitSAH(from, to, node->box, &pivot, &axis, &cost);

	float leaf_cost = (float)num_triangles;
	if (cost >= leaf_cost) {
		return node; // leaf
	}

	// sort triangles here – see “How to sort triangles“ slide
	std::vector<Triangle*>::iterator begin = items_.begin();
	std::nth_element(begin + from, begin + pivot , begin + to + 1, TriangleComparator(axis));  // O(n) in avg

	//printf("Split range [%d, %d] using pivot %d\n", from, to, pivot);

	node->children[0] = BuildTree(from, pivot - 1);
	node->children[1] = BuildTree(pivot, to);

	return node;
}

void BVH::FindBestSplitSAH(int from, int to, AABB node_box, int* pivot_, int* axis_, float* cost_) {
	*cost_ = FLT_MAX;
	*axis_ = 0;
	*pivot_ = (from + to) / 2;

	int num_triangles = to - from + 1;

	if (num_triangles <= 1) {
		return;
	}

	const float t_trav = 1.0f;
	const float t_int = 1.0f;
	const int NUM_BINS = 16;

	float parent_area = node_box.SurfaceArea();

	if (parent_area < 0.000001f) {
		return;
	}

	float best_split_pos = 0.0f;
	int best_left_count = 0;

	for (int axis = 0; axis < 3; axis++) {
		float min_val = node_box.bounds[0].data[axis];
		float max_val = node_box.bounds[1].data[axis];
		float range = max_val - min_val;

		if (range < 0.000001f) continue;

		for (int bin = 1; bin < NUM_BINS; bin++) {
			float split_pos = min_val + (bin * range) / NUM_BINS;

			AABB left_box, right_box;
			left_box.bounds[0] = Vector3(FLT_MAX, FLT_MAX, FLT_MAX);
			left_box.bounds[1] = Vector3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
			right_box.bounds[0] = Vector3(FLT_MAX, FLT_MAX, FLT_MAX);
			right_box.bounds[1] = Vector3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

			int left_count = 0, right_count = 0;

			for (int i = from; i <= to; i++) {
				float centroid = items_[i]->Get_centroid().data[axis];

				if (centroid < split_pos) {
					left_box.Expand(items_[i]->getAABB());
					left_count++;
				}
				else {
					right_box.Expand(items_[i]->getAABB());
					right_count++;
				}
			}

			if (left_count == 0 || right_count == 0) {
				continue;
			}

			float left_area = left_box.SurfaceArea();
			float right_area = right_box.SurfaceArea();

			float cost = t_trav +
				(left_area / parent_area) * t_int * left_count +
				(right_area / parent_area) * t_int * right_count;

			if (cost < *cost_) {
				*cost_ = cost;
				*axis_ = axis;
				best_split_pos = split_pos;
				best_left_count = left_count;
			}
		}
	}

	std::sort(items_.begin() + from, items_.begin() + to + 1,
		TriangleComparator(*axis_));

	*pivot_ = from + best_left_count;
}

void BVH::FindHit(RTCRayHit& ray_hit) {
	MyRTCRayHit result;
	result.ray = ray_hit.ray;
	result.hit = ray_hit.hit;
	result.inv_dir = Vector3(1.0f / ray_hit.ray.dir_x, 1.0f / ray_hit.ray.dir_y, 1.0f / ray_hit.ray.dir_z);
	result.sign[0] = (result.inv_dir.x < 0);
	result.sign[1] = (result.inv_dir.y < 0);
	result.sign[2] = (result.inv_dir.z < 0);

	result = Traverse(result, root_);

	if (result.hit.geomID != RTC_INVALID_GEOMETRY_ID) {
		ray_hit.hit = result.hit;
		ray_hit.ray = result.ray;
	}
}

MyRTCRayHit BVH::Traverse(MyRTCRayHit ray_hit, Node* node) {
	MyRTCRayHit closest_hit = ray_hit;
	closest_hit.hit.geomID = RTC_INVALID_GEOMETRY_ID;

	float tmin, tmax;
	if (!RayBoxIntersection(ray_hit, node->box, tmin, tmax)) {

		return closest_hit;
	}

	// EARLY OUT
	if (tmin > ray_hit.ray.tfar) {
		return closest_hit;
	}

	bool is_leaf = (node->children[0] == nullptr && node->children[1] == nullptr);

	if (is_leaf) {
		for (int i = node->span[0]; i <= node->span[1]; ++i) {
			MyRTCRayHit tempRayHit = ray_hit;
			if (RayTriangleIntersection(tempRayHit, items_[i])) {
				if (tempRayHit.ray.tfar < closest_hit.ray.tfar) {
					closest_hit = tempRayHit;
					ray_hit.ray.tfar = tempRayHit.ray.tfar;
				}
			}
		}
	}
	else {
		MyRTCRayHit current_ray = ray_hit;

		float original_tfar = current_ray.ray.tfar;

		MyRTCRayHit hit_left = Traverse(current_ray, node->children[0]);

		if (hit_left.hit.geomID != RTC_INVALID_GEOMETRY_ID && hit_left.ray.tfar < closest_hit.ray.tfar) {
			closest_hit = hit_left;
			current_ray.ray.tfar = hit_left.ray.tfar; // EARLY OUT pro druhe dite
		}
		else {
			current_ray.ray.tfar = original_tfar;
		}

		MyRTCRayHit hit_right = Traverse(current_ray, node->children[1]);

		if (hit_right.hit.geomID != RTC_INVALID_GEOMETRY_ID && hit_right.ray.tfar < closest_hit.ray.tfar) {
			closest_hit = hit_right;
		}
	}

	return closest_hit;
}

bool BVH::RayBoxIntersection(const MyRTCRayHit& ray_hit, const AABB& box, float& t0, float& t1) {
	const RTCRay& ray = ray_hit.ray;

	t0 = ray.tnear;
	t1 = ray.tfar;

	for (int i = 0; i < 3; i++) {
		const float origin[3] = { ray.org_x, ray.org_y, ray.org_z };

		// nasobeni inverznim smerem misto deleni
		float inv_dir = ray_hit.inv_dir.data[i];

		// 0 nebo 1 podle smeru paprsku
		int near_idx = ray_hit.sign[i];
		int far_idx = 1 - ray_hit.sign[i];

		float t_near = (box.bounds[near_idx].data[i] - origin[i]) * inv_dir;
		float t_far = (box.bounds[far_idx].data[i] - origin[i]) * inv_dir;

		t0 = (std::max)(t0, t_near);
		t1 = (std::min)(t1, t_far);

		if (t0 > t1) return false;
	}

	return t1 >= (std::max)(t0, 0.0f) && t1 >= ray_hit.ray.tnear;
}

bool BVH::RayTriangleIntersection(MyRTCRayHit& rayHit, Triangle* triangle) {
	// Vrcholy trojúhelníku
	const Vector3& v0 = triangle->vertex(0).position;
	const Vector3& v1 = triangle->vertex(1).position;
	const Vector3& v2 = triangle->vertex(2).position;

	// Směr paprsku
	Vector3 ray_dir(rayHit.ray.dir_x, rayHit.ray.dir_y, rayHit.ray.dir_z);
	Vector3 ray_origin(rayHit.ray.org_x, rayHit.ray.org_y, rayHit.ray.org_z);

	// Hrany trojúhelníku
	Vector3 edge1 = v1 - v0;
	Vector3 edge2 = v2 - v0;

	// Determinant
	Vector3 h = ray_dir.CrossProduct(edge2);
	float a = edge1.DotProduct(h);

	const float EPSILON = 0.0000001f;
	if (a > -EPSILON && a < EPSILON) {
		return false;  // Paprsek je rovnobezyný s troj...
	}


	float f = 1.0f / a;
	Vector3 s = ray_origin - v0;
	float u = f * s.DotProduct(h);

	// Test barycentrických souřadnic
	if (u < 0.0f || u > 1.0f) {
		return false;
	}

	Vector3 q = s.CrossProduct(edge1);
	float v = f * ray_dir.DotProduct(q);

	if (v < 0.0f || v + u > 1.0f) {
		return false;
	}

	// Vzdálenost k průsečíku
	float t = f * edge2.DotProduct(q);

	// Test, zda je průsečík v rozsahu paprsku
	if (t >= rayHit.ray.tnear && t <= rayHit.ray.tfar) {
		// Vypočítej normálu (pro osvětlení)
		Vector3 normal = edge1.CrossProduct(edge2);

		// Vyplň hit strukturu
		rayHit.ray.tfar = t;
		rayHit.hit.geomID = triangle->GetGeomID();
		rayHit.hit.primID = triangle->GetPrimID();
		rayHit.hit.u = u;
		rayHit.hit.v = v;
		rayHit.hit.Ng_x = normal.x;
		rayHit.hit.Ng_y = normal.y;
		rayHit.hit.Ng_z = normal.z;

		return true;
	}

	return false;
}
