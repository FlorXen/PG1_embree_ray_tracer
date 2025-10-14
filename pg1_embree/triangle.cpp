#include "stdafx.h"
#include "triangle.h"

Triangle::Triangle( const Vertex & v0, const Vertex & v1, const Vertex & v2, Surface * surface )
{
	vertices_[0] = v0;
	vertices_[1] = v1;
	vertices_[2] = v2;	

	assert( !is_degenerate() );
}

Vertex Triangle::vertex( const int i )
{
	return vertices_[i];
}

Vector3 Triangle::Get_centroid() {
	if (!is_centroid_computed) {
		centroid = (vertices_[0].position + vertices_[1].position + vertices_[2].position) / 3.0f;
		is_centroid_computed = true;
	}
	return centroid;
}

AABB Triangle::getAABB() const {
	AABB box;
	box.bounds[0] = vertices_[0].position.Min(vertices_[1].position.Min(vertices_[2].position));
	box.bounds[1] = vertices_[0].position.Max(vertices_[1].position.Max(vertices_[2].position));
	return box;
}

bool Triangle::is_degenerate() const
{
	return vertices_[0].position == vertices_[1].position ||
		vertices_[0].position == vertices_[2].position || 
		vertices_[1].position == vertices_[2].position;
}
