#include <algorithm>
#include <random>

#include "../spherical_volume_rendering_util.h"
#include "gtest/gtest.h"

// A set of tests to be run by continuous integration. These verify basic
// traversal properties such as ordering and bounds.
namespace {
void printRayData(const Ray& ray) {
  printf("\nRay origin: {%f, %f, %f}", ray.origin().x(), ray.origin().y(),
         ray.origin().z());
  printf("\nRay direction: {%f, %f, %f}", ray.direction().x(),
         ray.direction().y(), ray.direction().z());
}

void printVoxelInformation(const svr::SphericalVoxel& v,
                           const std::string& info = "") {
  if (info.empty()) {
    printf("{%d, %d, %d} ", v.radial, v.polar, v.azimuthal);
  } else {
    printf("\nAbout: %s\n Voxel: {%d, %d, %d}\n", info.data(), v.radial,
           v.polar, v.azimuthal);
  }
}

template <class It>
void printVoxelGroupingInformation(const std::vector<svr::SphericalVoxel>& v,
                                   It it, const std::string& info) {
  printf("\n%s\n", info.data());
  if (it - 1 == v.cbegin()) {
    printf("\n[Voxel is the first]");
  } else {
    printVoxelInformation(*(it - 1));
  }
  printVoxelInformation(*it);
  if (it + 1 == v.cend()) {
    printf("\n[Voxel is the last]");
  } else {
    printVoxelInformation(*(it + 1));
  }
}

// Verifies each voxel is within bounds.
// For radial voxel i, 0 < i <= num_radial_sections.
// For angular voxel j, 0 <= i < num_angular_sections.
// Returns false if this property does not hold.
bool checkVoxelBounds(const Ray& ray,
                      const std::vector<svr::SphericalVoxel>& actual_voxels,
                      int number_of_radial_voxels, int number_of_polar_sections,
                      int number_of_azimuthal_sections) {
  const auto it2 = std::find_if_not(
      actual_voxels.cbegin(), actual_voxels.cend(),
      [&](const svr::SphericalVoxel& i) {
        return 0 < i.radial && i.radial <= number_of_radial_voxels &&
               0 <= i.azimuthal && i.azimuthal < number_of_azimuthal_sections &&
               0 <= i.polar && i.polar < number_of_polar_sections;
      });
  const bool voxels_within_bounds = it2 == actual_voxels.cend();
  EXPECT_TRUE(voxels_within_bounds);
  if (!voxels_within_bounds) {
    printRayData(ray);
    printVoxelGroupingInformation(
        actual_voxels, it2,
        "There exists a voxel i or a voxel j such that:"
        "\n   0 < i <= number_of_radial_voxels"
        "\n   0 <= j < num_angular_sections"
        "\ndoes not hold.");
  }
  return voxels_within_bounds;
}

// Verifies the entrance and radial voxel is 1 for all rays. Verifies each
// radial voxel's transition order. If its solely a radial hit,
// the next radial voxel should be current+1 or current-1. If it is not, then
// the next radial voxel is current+1, current-1, or current+0.
// Returns false if the checks did not pass.
bool checkRadialVoxelOrdering(
    const Ray& ray, const std::vector<svr::SphericalVoxel>& actual_voxels,
    bool traverses_entire_sphere) {
  const auto it = std::adjacent_find(
      actual_voxels.cbegin(), actual_voxels.cend(),
      [](const svr::SphericalVoxel& v1, const svr::SphericalVoxel& v2) {
        const bool radial_hit_only =
            v1.polar == v2.polar && v1.azimuthal == v2.azimuthal;
        if (radial_hit_only) {
          return !(v1.radial - 1 == v2.radial || v1.radial + 1 == v2.radial);
        }
        const bool within_one =
            (v1.radial == v2.radial || v1.radial - 1 == v2.radial ||
             v1.radial + 1 == v2.radial);
        return !within_one;
      });
  const bool radial_voxel_ordering_properties_are_correct =
      it == actual_voxels.cend();
  EXPECT_TRUE(radial_voxel_ordering_properties_are_correct);
  if (!radial_voxel_ordering_properties_are_correct) {
    printVoxelGroupingInformation(
        actual_voxels, it,
        "The current radial voxel is not within +- 1 of the next voxel.");
    printRayData(ray);
    return false;
  }
  if (traverses_entire_sphere) {
    EXPECT_FALSE(actual_voxels.empty());
    if (actual_voxels.empty()) {
      printf("\nNo intersection with sphere at all.");
      printRayData(ray);
      return false;
    }
    const std::size_t last = actual_voxels.size() - 1;
    EXPECT_TRUE(actual_voxels[0].radial == 1);
    EXPECT_TRUE(actual_voxels[last].radial == 1);
    if (actual_voxels[0].radial != 1 || actual_voxels[last].radial != 1) {
      printf("\nDid not complete entire traversal.");
      const auto first_voxel = actual_voxels[0];
      const auto last_voxel = actual_voxels[last];
      printRayData(ray);
      printVoxelInformation(first_voxel, "Entrance Voxel.");
      printVoxelInformation(last_voxel, "Exit Voxel");
      return false;
    }
  }
  return true;
}

// It should hold true in orthographic projects that each angular voxel should
// be within +- 1 of the last angular voxel except for at most in one case. This
// case occurs when traversing the line x = 0. Returns false if the ordering
// is incorrect.
bool checkAngularVoxelOrdering(const Ray& ray,
                               const std::vector<svr::SphericalVoxel>& v) {
  const auto polar_not_within_one = [](const svr::SphericalVoxel& v1,
                                       const svr::SphericalVoxel& v2) {
    const bool polar_within_one =
        (v1.polar == v2.polar || v1.polar - 1 == v2.polar ||
         v1.polar + 1 == v2.polar);
    return !polar_within_one;
  };
  const auto it_polar =
      std::adjacent_find(v.cbegin(), v.cend(), polar_not_within_one);
  if (it_polar != v.cend()) {
    const auto it2_polar =
        std::adjacent_find(it_polar + 1, v.cend(), polar_not_within_one);
    const bool polar_voxel_ordering_properties_are_correct =
        it2_polar == v.cend();
    EXPECT_TRUE(polar_voxel_ordering_properties_are_correct);
    if (!polar_voxel_ordering_properties_are_correct) {
      printRayData(ray);
      printVoxelGroupingInformation(
          v, it2_polar,
          "A polar voxel makes two jumps greater than +-1 "
          "voxel. This should only occur once per ray when the ray passes "
          "the line X = 0.");
      printVoxelGroupingInformation(v, it_polar, "Previous Jump:");
      return false;
    }
  }
  const auto azimuthal_not_within_one = [](const svr::SphericalVoxel& v1,
                                           const svr::SphericalVoxel& v2) {
    const bool azimuthal_within_one =
        (v1.azimuthal == v2.azimuthal || v1.azimuthal - 1 == v2.azimuthal ||
         v1.azimuthal + 1 == v2.azimuthal);
    return !azimuthal_within_one;
  };
  const auto it_azimuthal =
      std::adjacent_find(v.cbegin(), v.cend(), azimuthal_not_within_one);
  if (it_azimuthal != v.cend()) {
    const auto it2_azimuthal = std::adjacent_find(it_azimuthal + 1, v.cend(),
                                                  azimuthal_not_within_one);
    const bool azimuthal_voxel_ordering_properties_are_correct =
        it2_azimuthal == v.cend();
    EXPECT_TRUE(azimuthal_voxel_ordering_properties_are_correct);
    if (!azimuthal_voxel_ordering_properties_are_correct) {
      printRayData(ray);
      printVoxelGroupingInformation(
          v, it2_azimuthal,
          "An azimuthal voxel makes two jumps greater than +-1 "
          "voxel. This should only occur once per ray when the ray passes "
          "the line X = 0.");
      printVoxelGroupingInformation(v, it_azimuthal, "Previous Jump:");
      return false;
    }
  }
  return true;
}

// Sends X^2 rays through a Y^3 spherical voxel grid orthographically. All rays
// are perpendicular to the Z plane.
void inline orthographicTraverseXSquaredRaysinYCubedVoxels(
    const std::size_t X, const std::size_t Y) noexcept {
  const BoundVec3 sphere_center(0.0, 0.0, 0.0);
  const double sphere_max_radius = 10e4;
  const std::size_t num_radial_sections = Y;
  const std::size_t num_polar_sections = Y;
  const std::size_t num_azimuthal_sections = Y;
  const svr::SphereBound min_bound = {
      .radial = 0.0, .polar = 0.0, .azimuthal = 0.0};
  const svr::SphereBound max_bound = {
      .radial = sphere_max_radius, .polar = 2 * M_PI, .azimuthal = 2 * M_PI};
  const svr::SphericalVoxelGrid grid(min_bound, max_bound, num_radial_sections,
                                     num_polar_sections, num_azimuthal_sections,
                                     sphere_center);
  const UnitVec3 ray_direction(0.0, 0.0, 1.0);
  double ray_origin_x = -1000.0;
  double ray_origin_y = -1000.0;
  const double ray_origin_z = -(sphere_max_radius + 1.0);

  const double ray_origin_plane_movement = 2000.0 / X;
  for (std::size_t i = 0; i < X; ++i) {
    for (std::size_t j = 0; j < X; ++j) {
      const BoundVec3 ray_origin(ray_origin_x, ray_origin_y, ray_origin_z);
      const Ray ray(ray_origin, ray_direction);
      const auto actual_voxels = walkSphericalVolume(ray, grid, /*max_t=*/1.0);
      ASSERT_TRUE(checkVoxelBounds(ray, actual_voxels, Y, Y, Y) &&
                  checkRadialVoxelOrdering(ray, actual_voxels,
                                           /*traverses_entire_sphere=*/true) &&
                  checkAngularVoxelOrdering(ray, actual_voxels));
      ray_origin_y =
          (j == X - 1) ? -1000.0 : ray_origin_y + ray_origin_plane_movement;
    }
    ray_origin_x += ray_origin_plane_movement;
  }
}

// Similar to orthographicTraverseXSquaredRaysinYCubedVoxels, but uses a
// seeded random direction within bounds [1.0, 3.0], A randomly chosen axis is
// then used, and the other two origin values are randomly chosen from a value
// within bounds [-10,000.0, 10,000.0]. The number of sections for each voxel
// type is bounded by [16, Y];
void inline randomRayPlacementOutsideSphere(const std::size_t X,
                                            const std::size_t Y) noexcept {
  std::default_random_engine rd(time(nullptr));
  std::mt19937 mt(rd());
  EXPECT_GT(Y, 24);
  std::uniform_int_distribution<int> num_sections(16, Y);
  const BoundVec3 sphere_center(0.0, 0.0, 0.0);
  const double sphere_max_radius = 10e6;
  const std::size_t num_radial_sections = num_sections(mt);
  const std::size_t num_polar_sections = num_sections(mt);
  const std::size_t num_azimuthal_sections = num_sections(mt);
  const svr::SphereBound min_bound = {
      .radial = 0.0, .polar = 0.0, .azimuthal = 0.0};
  const svr::SphereBound max_bound = {
      .radial = sphere_max_radius, .polar = 2 * M_PI, .azimuthal = 2 * M_PI};
  const svr::SphericalVoxelGrid grid(min_bound, max_bound, num_radial_sections,
                                     num_polar_sections, num_azimuthal_sections,
                                     sphere_center);
  std::uniform_int_distribution<int> ray_major_axis_distribution(1, 3);
  BoundVec3 ray_origin;
  const double chosen_axis = ray_major_axis_distribution(mt);
  if (chosen_axis == 1) {
    ray_origin.x() = -(sphere_max_radius + 1.0);
  } else if (chosen_axis == 2) {
    ray_origin.y() = -(sphere_max_radius + 1.0);
  } else {
    ray_origin.z() = -(sphere_max_radius + 1.0);
  }
  for (int i = 0; i < X * X; ++i) {
    std::uniform_real_distribution<double> dist1(-10000.0, 10000.0);
    if (chosen_axis == 1) {
      ray_origin.y() = dist1(mt);
      ray_origin.z() = dist1(mt);
    } else if (chosen_axis == 2) {
      ray_origin.x() = dist1(mt);
      ray_origin.z() = dist1(mt);
    } else {
      ray_origin.x() = dist1(mt);
      ray_origin.y() = dist1(mt);
    }
    std::uniform_real_distribution<double> dist2(1.0, 3.0);
    const Ray ray(ray_origin, UnitVec3(dist2(mt), dist2(mt), dist2(mt)));
    const auto actual_voxels = walkSphericalVolume(ray, grid, /*max_t=*/1.0);
    ASSERT_TRUE(checkVoxelBounds(ray, actual_voxels, num_radial_sections,
                                 num_polar_sections, num_azimuthal_sections) &&
                checkRadialVoxelOrdering(ray, actual_voxels,
                                         /*traverses_entire_sphere=*/true) &&
                checkAngularVoxelOrdering(ray, actual_voxels));
  }
}

// Similar to randomRayPlacementOutsideSphere, but
// the ray origin is within the sphere. The ray origin is placed within bounds
// [10,000.0, 10,000.0) and the ray direction is within bounds [10.0, 10.0).
void inline randomRayPlacementWithinSphere(const std::size_t X,
                                           const std::size_t Y) noexcept {
  std::default_random_engine rd(time(nullptr));
  std::mt19937 mt(rd());
  EXPECT_GT(Y, 24);
  std::uniform_int_distribution<int> num_sections(16, Y);
  const BoundVec3 sphere_center(0.0, 0.0, 0.0);
  const double sphere_max_radius = 10e6;
  const std::size_t num_radial_sections = num_sections(mt);
  const std::size_t num_polar_sections = num_sections(mt);
  const std::size_t num_azimuthal_sections = num_sections(mt);
  const svr::SphereBound min_bound = {
      .radial = 0.0, .polar = 0.0, .azimuthal = 0.0};
  const svr::SphereBound max_bound = {
      .radial = sphere_max_radius, .polar = 2 * M_PI, .azimuthal = 2 * M_PI};
  const svr::SphericalVoxelGrid grid(min_bound, max_bound, num_radial_sections,
                                     num_polar_sections, num_azimuthal_sections,
                                     sphere_center);

  std::uniform_real_distribution<double> dist1(-10000.0, 10000.0);
  std::uniform_real_distribution<double> dist2(-10.0, 10.0);
  std::uniform_real_distribution<double> dist3(-0.1, 1.1);
  for (int i = 0; i < X * X; ++i) {
    BoundVec3 ray_origin(dist1(mt), dist1(mt), dist1(mt));
    const Ray ray(ray_origin, UnitVec3(dist2(mt), dist2(mt), dist2(mt)));
    const double max_t = dist3(mt);
    const auto actual_voxels = walkSphericalVolume(ray, grid, max_t);
    ASSERT_TRUE(checkVoxelBounds(ray, actual_voxels, num_radial_sections,
                                 num_polar_sections, num_azimuthal_sections) &&
                checkRadialVoxelOrdering(ray, actual_voxels,
                                         /*traverses_entire_sphere=*/false) &&
                checkAngularVoxelOrdering(ray, actual_voxels));
  }
}

// Represents the parameters for the CI tests.
// For example, if ray_squared_count = 64, then 64^2 rays will traverse.
// Similarly, if voxel_cubed_count = 32, then the grid will be divided into
// 32^3 voxels. The exception to this is with RandomizedInputs, which uses
// a random number of sections within bounds [16, Y].
struct TestParameters {
  std::size_t ray_squared_count;
  std::size_t voxel_cubed_count;
};

const std::vector<TestParameters> random_test_parameters = {
    {.ray_squared_count = 32, .voxel_cubed_count = 32},
    {.ray_squared_count = 64, .voxel_cubed_count = 32},
    {.ray_squared_count = 64, .voxel_cubed_count = 64},
    {.ray_squared_count = 128, .voxel_cubed_count = 64},
    {.ray_squared_count = 64, .voxel_cubed_count = 128},
    {.ray_squared_count = 128, .voxel_cubed_count = 128},
};

const std::vector<TestParameters> orthographic_test_parameters = {
    {.ray_squared_count = 64, .voxel_cubed_count = 64},
    {.ray_squared_count = 128, .voxel_cubed_count = 64},
    {.ray_squared_count = 256, .voxel_cubed_count = 64},
    {.ray_squared_count = 64, .voxel_cubed_count = 128},
    {.ray_squared_count = 128, .voxel_cubed_count = 128},
    {.ray_squared_count = 64, .voxel_cubed_count = 512},
    {.ray_squared_count = 64, .voxel_cubed_count = 1024},
    {.ray_squared_count = 512, .voxel_cubed_count = 32},
    {.ray_squared_count = 1024, .voxel_cubed_count = 32},
};

TEST(ContinuousIntegration, RayInsideSphereRandomizedInputs) {
  for (const auto param : random_test_parameters) {
    printf("   [ RUN      ] %lu^2 Rays in [16, %lu]^3 Voxels\n",
           param.ray_squared_count, param.voxel_cubed_count);
    randomRayPlacementWithinSphere(param.ray_squared_count,
                                   param.voxel_cubed_count);
    printf("   [       OK ] %lu^2 Rays in [16, %lu]^3 Voxels\n",
           param.ray_squared_count, param.voxel_cubed_count);
  }
}

TEST(ContinuousIntegration, RayOutsideSphereRandomizedInputs) {
  for (const auto param : random_test_parameters) {
    printf("   [ RUN      ] %lu^2 Rays in [16, %lu]^3 Voxels\n",
           param.ray_squared_count, param.voxel_cubed_count);
    randomRayPlacementOutsideSphere(param.ray_squared_count,
                                    param.voxel_cubed_count);
    printf("   [       OK ] %lu^2 Rays in [16, %lu]^3 Voxels\n",
           param.ray_squared_count, param.voxel_cubed_count);
  }
}

TEST(ContinuousIntegration, OrthographicProjection) {
  for (const auto param : orthographic_test_parameters) {
    printf("   [ RUN      ] %lu^2 Rays in %lu^3 Voxels\n",
           param.ray_squared_count, param.voxel_cubed_count);
    orthographicTraverseXSquaredRaysinYCubedVoxels(param.ray_squared_count,
                                                   param.voxel_cubed_count);
    printf("   [       OK ] %lu^2 Rays in %lu^3 Voxels\n",
           param.ray_squared_count, param.voxel_cubed_count);
  }
}

}  // namespace
