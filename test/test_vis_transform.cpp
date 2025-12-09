#include <numbers>
#include <print>
#include <vector>
import vk.engine;
import vk.plugins.transform;
import vk.toolkit.math;
import vk.toolkit.geometry;
import vk.toolkit.camera;

int main() {
    vk::engine::VulkanEngine engine;

    constexpr std::size_t count = 28;
    std::vector<vk::toolkit::math::Mat4> poses;
    poses.reserve(count);

    constexpr vk::toolkit::math::Vec3 target{0.0f, 0.0f, 0.0f};
    constexpr float two_pi = 2.0f * std::numbers::pi_v<float>;

    for (std::size_t i = 0; i < count; ++i) {
        constexpr float height_variation = 0.6f;
        constexpr float radius           = 4.0f;
        const float t                    = static_cast<float>(i) / static_cast<float>(count);
        const float theta                = t * two_pi;
        const float wobble               = std::sin(theta * 3.0f) * 0.25f;
        const float distance             = radius * (0.8f + 0.15f * std::cos(theta * 0.5f));
        const float height               = height_variation * std::cos(theta * 1.5f) + wobble * 0.35f;

        const vk::toolkit::math::Vec3 position{distance * std::cos(theta), height, distance * std::sin(theta)};
        poses.push_back(vk::toolkit::geometry::build_pose(position, target, vk::toolkit::math::Vec3{0.0f, 1.0f, 0.0f}));
    }

    vk::plugins::TransformViewer viewer(std::make_shared<vk::toolkit::camera::Camera>(), poses);

    engine.init(viewer);
    engine.run(viewer);
    engine.cleanup();
    return 0;
}
