#include <chrono>
#include <iostream>

#include <lsAdvect.hpp>
#include <lsBooleanOperation.hpp>
#include <lsExpand.hpp>
#include <lsFastAdvect.hpp>
#include <lsMakeGeometry.hpp>
#include <lsToDiskMesh.hpp>
#include <lsToMesh.hpp>
#include <lsToSurfaceMesh.hpp>
#include <lsVTKWriter.hpp>
#include <lsVelocityField.hpp>

class velocityField : public lsVelocityField<double> {
  double getScalarVelocity(const std::array<double, 3> & /*coordinate*/,
                           int /*material*/,
                           const std::array<double, 3> & /*normalVector*/) {
    return 1;
  }
};

int main() {
  constexpr int D = 3;
  typedef double NumericType;

  double extent = 30;
  double gridDelta = 0.25;
  double bounds[2 * D] = {-extent, extent, -extent, extent};
  if (D == 3) {
    bounds[4] = -10;
    bounds[5] = 10;
  }

  typename lsDomain<NumericType, D>::BoundaryType boundaryCons[D];
  for (unsigned i = 0; i < D - 1; ++i) {
    boundaryCons[i] =
        lsDomain<NumericType, D>::BoundaryType::REFLECTIVE_BOUNDARY;
  }
  boundaryCons[D - 1] =
      lsDomain<NumericType, D>::BoundaryType::INFINITE_BOUNDARY;

  lsDomain<double, D> substrate(bounds, boundaryCons, gridDelta);

  double origin[3] = {0., 0., 0.};
  double planeNormal[3] = {0., D == 2, D == 3};

  lsMakeGeometry<double, D>(substrate, lsPlane<double, D>(origin, planeNormal))
      .apply();

  {
    std::cout << "Extracting..." << std::endl;
    lsMesh mesh;
    lsToSurfaceMesh<double, D>(substrate, mesh).apply();
    lsVTKWriter(mesh, "plane.vtk").apply();
  }

  {
    // create layer used for booling
    std::cout << "Creating box..." << std::endl;
    lsDomain<double, D> trench(bounds, boundaryCons, gridDelta);
    double minCorner[3] = {-extent - 1, -extent / 4., -15.};
    double maxCorner[3] = {extent + 1, extent / 4., 1.0};
    if (D == 2) {
      minCorner[0] = minCorner[1];
      minCorner[1] = minCorner[2];
      maxCorner[0] = maxCorner[1];
      maxCorner[1] = maxCorner[2];
    }
    lsMakeGeometry<double, D>(trench, lsBox<double, D>(minCorner, maxCorner))
        .apply();

    {
      std::cout << "Extracting..." << std::endl;
      lsMesh mesh;
      lsToMesh<double, D>(trench, mesh).apply();
      lsVTKWriter(mesh, "box.vtk").apply();
    }

    // Create trench geometry
    std::cout << "Booling trench..." << std::endl;
    lsBooleanOperation<double, D>(substrate, trench,
                                  lsBooleanOperationEnum::RELATIVE_COMPLEMENT)
        .apply();
  }

  lsMesh mesh;

  lsToMesh<NumericType, D>(substrate, mesh).apply();
  lsVTKWriter(mesh, "points.vtk").apply();
  lsToSurfaceMesh<NumericType, D>(substrate, mesh).apply();
  lsVTKWriter(mesh, "surface.vtk").apply();

  // Distance to advect to
  double depositionDistance = 4.0;

  // set up spherical advection dist
  lsSphereDistribution<NumericType, D> dist(depositionDistance);

  lsDomain<double, D> newLayer(substrate);

  std::cout << "FastAdvecting" << std::endl;
  lsFastAdvect<NumericType, D> fastAdvectKernel(newLayer, dist);

  {
    auto start = std::chrono::high_resolution_clock::now();
    fastAdvectKernel.apply();
    auto end = std::chrono::high_resolution_clock::now();
    auto diff =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
            .count();
    std::cout << "Fast Advect: " << diff << "ms" << std::endl;
  }

  lsToSurfaceMesh<double, D>(newLayer, mesh).apply();
  lsVTKWriter(mesh, "FastAdvect.vtk").apply();
  // lsToSurfaceMesh<double, D>(newLayer, mesh).apply();
  // lsVTKWriter(mesh, "finalSurface.vtk").apply();

  // now rund lsAdvect for all other advection schemes
  // last scheme is SLLFS with i == 9
  for (unsigned i = 0; i < 10; ++i) {
    if (i == 4) {
      continue;
    }
    lsAdvect<double, D> advectionKernel;
    lsDomain<double, D> nextLayer(substrate);
    advectionKernel.insertNextLevelSet(nextLayer);

    velocityField velocities;
    advectionKernel.setVelocityField(velocities);
    advectionKernel.setAdvectionTime(depositionDistance);
    advectionKernel.setIntegrationScheme(
        static_cast<lsIntegrationSchemeEnum>(i));
    {
      auto start = std::chrono::high_resolution_clock::now();
      advectionKernel.apply();
      auto end = std::chrono::high_resolution_clock::now();
      auto diff =
          std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
              .count();
      std::cout << "Advect " << i << ": " << diff << "ms" << std::endl;
    }

    lsToSurfaceMesh<double, D>(nextLayer, mesh).apply();
    lsVTKWriter(mesh, "Advect-" + std::to_string(i) + ".vtk").apply();
  }

  return 0;
}