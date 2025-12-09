module;
#include <cmath>
#include <numbers>
#include <span>
#include <stdexcept>
#include <vector>
export module vk.toolkit.geometry;
import vk.toolkit.math;

namespace vk::toolkit::geometry {
    export struct Vertex {
        math::Vec3 pos{};
        math::Vec3 color{};
    };

    export struct ColoredLine {
        math::Vec3 a{};
        math::Vec3 b{};
        math::Vec3 color{};
    };

    export std::vector<ColoredLine> make_axis_lines(const std::vector<math::Mat4>& poses, float axis_length) {
        std::vector<ColoredLine> lines;
        lines.reserve(poses.size() * 3);

        for (const auto& pose : poses) {
            const math::Vec3 origin  = math::extract_position(pose);
            const math::Vec3 right   = {pose.m[0], pose.m[1], pose.m[2]};
            const math::Vec3 up      = {pose.m[4], pose.m[5], pose.m[6]};
            const math::Vec3 forward = {pose.m[8], pose.m[9], pose.m[10]};

            lines.push_back({origin, origin + right * axis_length, math::Vec3{0.94f, 0.33f, 0.31f}});
            lines.push_back({origin, origin + up * axis_length, math::Vec3{0.37f, 0.82f, 0.36f}});
            lines.push_back({origin, origin + forward * axis_length, math::Vec3{0.32f, 0.60f, 1.0f}});
        }

        return lines;
    }

    export std::vector<ColoredLine> make_frustum_lines(const std::vector<math::Mat4>& poses, float near_d, float far_d, float fov_deg) {
        std::vector<ColoredLine> lines;
        lines.reserve(poses.size() * 12);

        const float fov_rad = fov_deg * std::numbers::pi_v<float> / 180.0f;
        const float near_h  = std::tan(fov_rad * 0.5f) * near_d;
        const float near_w  = near_h;
        const float far_h   = std::tan(fov_rad * 0.5f) * far_d;
        const float far_w   = far_h;
        constexpr math::Vec3 edge_color{0.95f, 0.76f, 0.32f};

        auto add_line = [&](const math::Vec3& a, const math::Vec3& b) { lines.push_back(ColoredLine{a, b, edge_color}); };

        for (const auto& pose : poses) {
            const math::Vec3 origin  = extract_position(pose);
            const math::Vec3 right   = {pose.m[0], pose.m[1], pose.m[2]};
            const math::Vec3 up      = {pose.m[4], pose.m[5], pose.m[6]};
            const math::Vec3 forward = {pose.m[8], pose.m[9], pose.m[10]};

            const auto corner = [&](float w, float h, float d) { return origin + forward * d + right * w + up * h; };

            const math::Vec3 nlt = corner(-near_w, near_h, near_d);
            const math::Vec3 nrt = corner(near_w, near_h, near_d);
            const math::Vec3 nlb = corner(-near_w, -near_h, near_d);
            const math::Vec3 nrb = corner(near_w, -near_h, near_d);

            const math::Vec3 flt = corner(-far_w, far_h, far_d);
            const math::Vec3 frt = corner(far_w, far_h, far_d);
            const math::Vec3 flb = corner(-far_w, -far_h, far_d);
            const math::Vec3 frb = corner(far_w, -far_h, far_d);

            add_line(origin, nlt);
            add_line(origin, nrt);
            add_line(origin, nlb);
            add_line(origin, nrb);

            add_line(nlt, nrt);
            add_line(nrt, nrb);
            add_line(nrb, nlb);
            add_line(nlb, nlt);

            add_line(flt, frt);
            add_line(frt, frb);
            add_line(frb, flb);
            add_line(flb, flt);

            add_line(nlt, flt);
            add_line(nrt, frt);
            add_line(nlb, flb);
            add_line(nrb, frb);
        }

        return lines;
    }

    export math::Mat4 build_pose(const math::Vec3& position, const math::Vec3& target, const math::Vec3& world_up) {
        const math::Vec3 forward = (target - position).normalized();
        const math::Vec3 right   = forward.cross(world_up).normalized();
        const math::Vec3 up      = right.cross(forward).normalized();

        math::Mat4 m{};
        m.m = {right.x, right.y, right.z, 0.0f, up.x, up.y, up.z, 0.0f, forward.x, forward.y, forward.z, 0.0f, position.x, position.y, position.z, 1.0f};
        return m;
    }


    export std::tuple<math::Vec3, float> compute_center_and_radius(const std::vector<math::Mat4>& poses) {
        math::Vec3 center{0.0f, 0.0f, 0.0f};
        for (const auto& p : poses) {
            center += extract_position(p);
        }
        if (!poses.empty()) {
            center = center / static_cast<float>(poses.size());
        }

        float average_radius = 0.0f;
        for (const auto& p : poses) {
            average_radius += (extract_position(p) - center).length();
        }
        if (!poses.empty()) average_radius /= static_cast<float>(poses.size());

        return {center, average_radius};
    }

    export std::vector<ColoredLine> make_path_lines(const std::vector<math::Mat4>& poses, const math::Vec3 color) {
        std::vector<ColoredLine> lines;
        lines.reserve(poses.size());
        if (poses.size() < 2) return lines;

        math::Vec3 prev = extract_position(poses.front());
        for (std::size_t i = 1; i < poses.size(); ++i) {
            const math::Vec3 curr = extract_position(poses[i]);
            lines.push_back(ColoredLine{prev, curr, color});
            prev = curr;
        }
        lines.push_back(ColoredLine{prev, extract_position(poses.front()), color});
        return lines;
    }

    export class BitmapView {
    public:
        constexpr BitmapView() = default;
        constexpr BitmapView(const int res_x, const int res_y, const int res_z, const std::span<const std::byte> bytes) noexcept : res_x_(res_x), res_y_(res_y), res_z_(res_z), bytes_(bytes) {}

        [[nodiscard]] constexpr bool get(int x, int y, int z) const noexcept {
            const int idx = x + y * res_x_ + z * res_x_ * res_y_;
            const auto b  = std::to_integer<unsigned>(bytes_[idx >> 3]);
            return (b >> (idx & 0x7)) & 1u;
        }

        [[nodiscard]] constexpr int res_x() const noexcept {
            return res_x_;
        }
        [[nodiscard]] constexpr int res_y() const noexcept {
            return res_y_;
        }
        [[nodiscard]] constexpr int res_z() const noexcept {
            return res_z_;
        }

        [[nodiscard]] constexpr std::span<const std::byte> bytes() const noexcept {
            return bytes_;
        }

    private:
        int res_x_{};
        int res_y_{};
        int res_z_{};
        std::span<const std::byte> bytes_{};
    };

    export template <int RES_X, int RES_Y, int RES_Z>
    class Bitmap {
    public:
        static_assert(RES_X > 0 && RES_Y > 0 && RES_Z > 0);

        static constexpr int total_voxels = RES_X * RES_Y * RES_Z;
        static constexpr int byte_count   = (total_voxels + 7) / 8;

        constexpr Bitmap() = default;

        constexpr explicit Bitmap(std::span<const unsigned char> src) {
            // runtime check (size is not a constant expression)
            if (src.size() != byte_count) {
                throw std::runtime_error("Bitmap: size mismatch");
            }

            for (int i = 0; i < byte_count; ++i) {
                data_[i] = std::byte{src[i]};
            }
        }

        [[nodiscard]] constexpr std::span<std::byte> bytes() noexcept {
            return data_;
        }

        [[nodiscard]] constexpr std::span<const std::byte> bytes() const noexcept {
            return data_;
        }

        [[nodiscard]] constexpr bool get(int x, int y, int z) const noexcept {
            const int idx = linear_index(x, y, z);
            const auto b  = std::to_integer<unsigned>(data_[idx >> 3]);
            return (b >> (idx & 0x7)) & 1u;
        }

        constexpr void set(int x, int y, int z) noexcept {
            const int idx = linear_index(x, y, z);
            data_[idx >> 3] |= bit_mask(idx);
        }

        [[nodiscard]] constexpr BitmapView view() const noexcept {
            return BitmapView{RES_X, RES_Y, RES_Z, bytes()};
        }

    private:
        std::array<std::byte, byte_count> data_{};

        [[nodiscard]] static constexpr int linear_index(int x, int y, int z) noexcept {
            return x + y * RES_X + z * RES_X * RES_Y;
        }

        [[nodiscard]] static constexpr std::byte bit_mask(int idx) noexcept {
            return std::byte{static_cast<unsigned char>(1u << (idx & 0x7))};
        }
    };


    template <int RES_X, int RES_Y, int RES_Z>
    constexpr Bitmap<RES_X, RES_Y, RES_Z> make_centered_sphere(float radius_ratio) {
        Bitmap<RES_X, RES_Y, RES_Z> bmp{};

        const float cx = (RES_X - 1) * 0.5f;
        const float cy = (RES_Y - 1) * 0.5f;
        const float cz = (RES_Z - 1) * 0.5f;

        const float r = radius_ratio * std::min({cx, cy, cz});

        const float r2 = r * r;

        for (int z = 0; z < RES_Z; ++z) {
            const float dz = static_cast<float>(z) - cz;
            for (int y = 0; y < RES_Y; ++y) {
                const float dy = static_cast<float>(y) - cy;
                for (int x = 0; x < RES_X; ++x) {
                    const float dx = static_cast<float>(x) - cx;

                    if (dx * dx + dy * dy + dz * dz <= r2) {
                        bmp.set(x, y, z);
                    }
                }
            }
        }

        return bmp;
    }
} // namespace vk::toolkit::geometry
