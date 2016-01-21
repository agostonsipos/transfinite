#pragma once

#include <algorithm>
#include <fstream>

#include "geometry.hh"

#include "Triangulation.hh"

namespace Geometry {

class TriMesh::TriMeshImpl {
public:
  void resizePoints(size_t n) { m_.Points().resize(n); }
  void setPoints(const PointVector &pv) {
    m_.Points().clear(); m_.Points().reserve(pv.size());
    std::transform(pv.begin(), pv.end(), std::back_inserter(m_.Points()),
                   [](const Point3D &p) { return Point<3, double>(p[0], p[1], p[2]); });
    m_.UpdateOctree();
  }
  void addTriangle(size_t a, size_t b, size_t c) { m_.AddTriangle(a, b, c, false); }
  PointVector points() const {
    PointVector points; points.reserve(m_.Points().size());
    std::transform(m_.Points().begin(), m_.Points().end(), std::back_inserter(points),
                   [](const Point<3, double> &p) { return Point3D(p[0], p[1], p[2]); });
    return points;
  }
  std::list<Triangle> triangles() const {
    std::list<Triangle> tris;
    for (size_t f = 0, fe = m_.NrFaces(); f != fe; ++f) {
      Triangle tri;
      size_t index = 0;
      for (auto i = m_.VoF_Begin(f), ie = m_.VoF_End(f); i != ie; ++i)
        tri[index++] = m_.VertexToPoint(*i);
      tris.push_back(tri);
    }
    return tris;
  }
  Triangle closestTriangle(const Point3D &p) const {
    size_t f = m_.ClosestTriangleToPoint(Point<3, double>(p[0], p[1], p[2]));
    Triangle tri;
    size_t index = 0;
    for (auto i = m_.VoF_Begin(f), ie = m_.VoF_End(f); i != ie; ++i)
      tri[index++] = m_.VertexToPoint(*i);
    return tri;
  }
  void writeOBJ(std::string filename) const {
    std::ofstream f(filename);
    if (!f.is_open()) {
      std::cerr << "Unable to open file: " << filename << std::endl;
      return;
    }
    int vertex_offs = 0;
    m_.TriangulationToOBJ(f, 0, vertex_offs);
    f.close();
  }
private:
  Triangulation<Point<3, double>> m_;
};

} // namespace Geometry
