#include <algorithm>

#include "domain.hh"
#include "parameterization.hh"
#include "ribbon.hh"
#include "surface.hh"

Surface::Surface()
  : n_(0), use_gamma_(true) {
}

Surface::~Surface() {
}

void
Surface::setGamma(bool use_gamma) {
  use_gamma_ = use_gamma;
}

void
Surface::setCurve(size_t i, const std::shared_ptr<BSCurve> &curve) {
  if (n_ <= i) {
    ribbons_.resize(i + 1);
    n_ = i + 1;
  }
  ribbons_[i] = newRibbon();
  ribbons_[i]->setCurve(curve);
  domain_->setSide(i, curve);
}

void
Surface::setCurves(const CurveVector &curves) {
  ribbons_.clear();
  ribbons_.reserve(curves.size());
  for (const auto &c : curves) {
    ribbons_.push_back(newRibbon());
    ribbons_.back()->setCurve(c);
  }
  domain_->setSides(curves);
  n_ = curves.size();
}

void
Surface::setupLoop() {
  // Tasks:
  // - propagate adjacency information
  // - normalize curves
  // - reverse curves when needed (and normalize once again, for safety)
  for (size_t i = 0; i < n_; ++i)
    ribbons_[i]->curve()->normalize();
  for (size_t i = 0; i < n_; ++i) {
    std::shared_ptr<Ribbon> rp = ribbons_[prev(i)], rn = ribbons_[next(i)];
    ribbons_[i]->setNeighbors(rp, rn);
    if (i == 0) {
      Point3D r_start = ribbons_[i]->curve()->eval(0.0);
      Point3D r_end = ribbons_[i]->curve()->eval(1.0);
      Point3D rn_start = rn->curve()->eval(0.0);
      Point3D rn_end = rn->curve()->eval(1.0);
      double end_to_start = (r_end - rn_start).norm();
      double end_to_end = (r_end - rn_end).norm();
      double start_to_start = (r_start - rn_start).norm();
      double start_to_end = (r_start - rn_end).norm();
      if (std::min(start_to_start, start_to_end) < std::min(end_to_start, end_to_end)) {
        ribbons_[i]->curve()->reverse();
        ribbons_[i]->curve()->normalize();
      }
    } else {
      Point3D r_start = ribbons_[i]->curve()->eval(0.0);
      Point3D r_end = ribbons_[i]->curve()->eval(1.0);
      Point3D rp_end = rp->curve()->eval(1.0);
      if ((r_end - rp_end).norm() < (r_start - rp_end).norm()) {
        ribbons_[i]->curve()->reverse();
        ribbons_[i]->curve()->normalize();
      }
    }
  }
}

void
Surface::update(size_t i) {
  if (domain_->update())
    param_->update();
  ribbons_[i]->update();
  updateCorner(prev(i));
  updateCorner(i);
}

void
Surface::update() {
  if (domain_->update())
    param_->update();
  for (auto &r : ribbons_)
    r->update();
  updateCorners();
}

std::shared_ptr<const Ribbon>
Surface::ribbon(size_t i) const {
  return ribbons_[i];
}

TriMesh
Surface::eval(size_t resolution) const {
  TriMesh mesh = domain_->meshTopology(resolution);
  Point2DVector uvs = domain_->parameters(resolution);
  PointVector points; points.reserve(uvs.size());
  std::transform(uvs.begin(), uvs.end(), std::back_inserter(points),
                 [this](const Point2D &uv) { return eval(uv); });
  mesh.setPoints(points);
  return mesh;
}

Point3D
Surface::cornerCorrection(size_t i, double si, double si1) const {
  // Assumes that both si and si1 are 0 at the corner
  si = std::min(std::max(si, 0.0), 1.0);
  si1 = std::min(std::max(si1, 0.0), 1.0);
  return corner_data_[i].point
    + corner_data_[i].tangent1 * gamma(si)
    + corner_data_[i].tangent2 * gamma(si1)
    + rationalTwist(si, si1, corner_data_[i].twist1, corner_data_[i].twist2)
      * gamma(si) * gamma(si1);
}

Point3D
Surface::sideInterpolant(size_t i, double si, double di) const {
  si = std::min(std::max(si, 0.0), 1.0);
  di = std::max(gamma(di), 0.0);
  return ribbons_[i]->eval(Point2D(si, di));
}

DoubleVector
Surface::blendCorner(const Point2DVector &sds) const {
  DoubleVector blf; blf.reserve(n_);

  size_t close_to_boundary = 0;
  for (const auto &sd : sds) {
    if (sd[1] < epsilon)
      ++close_to_boundary;
  }

  if (close_to_boundary > 0) {
    for (size_t i = 0; i < n_; ++i) {
      size_t ip = next(i);
      if (close_to_boundary > 1)
        blf.push_back(sds[i][1] < epsilon && sds[ip][1] < epsilon ? 1.0 : 0.0);
      else if (sds[i][1] < epsilon) {
        double tmp = std::pow(sds[ip][1], -2);
        blf.push_back(tmp / (tmp + std::pow(sds[prev(i)][1], -2)));
      } else if (sds[ip][1] < epsilon) {
        double tmp = std::pow(sds[i][1], -2);
        blf.push_back(tmp / (tmp + std::pow(sds[next(ip)][1], -2)));
      } else
        blf.push_back(0.0);
    }
  } else {
    double denominator = 0.0;
    for (size_t i = 0; i < n_; ++i) {
      blf.push_back(std::pow(sds[i][1] * sds[next(i)][1], -2));
      denominator += blf.back();
    }
    std::transform(blf.begin(), blf.end(), blf.begin(),
                   [denominator](double x) { return x / denominator; });
  }

  return blf;
}

DoubleVector
Surface::blendSideSingular(const Point2DVector &sds) const {
  DoubleVector blf; blf.reserve(n_);

  size_t close_to_boundary = 0;
  for (const auto &sd : sds) {
    if (sd[1] < epsilon)
      ++close_to_boundary;
  }

  if (close_to_boundary > 0) {
    double blend_val = 1.0 / close_to_boundary;
    for (const auto &sd : sds)
      blf.push_back(sd[1] < epsilon ? blend_val : 0.0);
  } else {
    double denominator = 0.0;
    for (const auto &sd : sds) {
      blf.push_back(std::pow(sd[1], -2));
      denominator += blf.back();
    }
    std::transform(blf.begin(), blf.end(), blf.begin(),
                   [denominator](double x) { return x / denominator; });
  }

  return blf;
}

double
Surface::blendHermite(double x) {
  double x2 = x * x;
  return 2.0 * x * x2 - 3.0 * x2 + 1.0;
}

void
Surface::updateCorner(size_t i) {
  static const double step = 1.0e-4;
  size_t ip = next(i);

  VectorVector der;
  Vector3D d1, d2;
  ribbons_[i]->curve()->eval(1.0, 1, der);
  corner_data_[i].point = der[0];
  corner_data_[i].tangent1 = -der[1];
  ribbons_[ip]->curve()->eval(0.0, 1, der);
  corner_data_[i].tangent2 = der[1];
  d1 = ribbons_[i]->eval(Point2D(1.0, 1.0));
  d2 = ribbons_[i]->eval(Point2D(1.0 - step, 1.0));
  corner_data_[i].twist1 = (d2 - d1) / step;
  d1 = ribbons_[ip]->eval(Point2D(0.0, 1.0));
  d2 = ribbons_[ip]->eval(Point2D(step, 1.0));
  corner_data_[i].twist1 = (d2 - d1) / step;
}

void
Surface::updateCorners() {
  corner_data_.resize(n_);
  for (size_t i = 0; i < n_; ++i)
    updateCorner(i);
}

double
Surface::gamma(double d) const {
  if (use_gamma_)
    return d / (2.0 * d + 1.0);
  return d;
}

Vector3D
Surface::rationalTwist(double u, double v, const Vector3D &f, const Vector3D &g) {
  if (std::abs(u + v) < epsilon)
    return Vector3D(0,0,0);
  return (f * u + g * v) / (u + v);
}
