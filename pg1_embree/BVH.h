#pragma once
#include "triangle.h"
#include "structs.h"

struct TriangleComparator
{
	int axis_;

	TriangleComparator(const int axis)
	{
		axis_ = axis;
	}

	bool operator() (Triangle* a, Triangle* b)
	{
		return a->centroid.data[axis_] < b->centroid.data[axis_];
	}

};

class BVH {

public:
	BVH();
	~BVH();
	void AddItem(Triangle* item);
	void BuildTree();
	void FindHit(RTCRayHit& ray_hit);
	int max_leaf_items = 4;

private:
	Node* BuildTree(int from, int to);
	MyRTCRayHit Traverse(MyRTCRayHit ray_hit, Node* node);
	AABB GetNodeAABB(int from, int to);
	void FindBestSplitSAH(int from, int to, AABB node_box, int* pivot_, int* axis_, float* cost);
	bool RayBoxIntersection(const MyRTCRayHit& ray_hit, const AABB& box, float& t0, float& t1);
	bool RayTriangleIntersection(MyRTCRayHit& ray, Triangle* triangle);

	Node* root_;
	std::vector<Triangle*> items_;
};