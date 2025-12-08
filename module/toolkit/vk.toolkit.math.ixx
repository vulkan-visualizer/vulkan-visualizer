module;
#include <array>
#include <cmath>
export module vk.toolkit.math;

namespace vk::toolkit::math {
    export struct Vec3 {
        float x{}, y{}, z{};

        constexpr Vec3() = default;
        constexpr Vec3(const float x, const float y, const float z) : x(x), y(y), z(z) {}

        constexpr Vec3 operator+(const Vec3& o) const {
            return {x + o.x, y + o.y, z + o.z};
        }
        constexpr Vec3 operator-(const Vec3& o) const {
            return {x - o.x, y - o.y, z - o.z};
        }
        constexpr Vec3 operator*(const float s) const {
            return {x * s, y * s, z * s};
        }
        constexpr Vec3 operator/(const float s) const {
            return {x / s, y / s, z / s};
        }
        constexpr Vec3& operator+=(const Vec3& o) {
            x += o.x;
            y += o.y;
            z += o.z;
            return *this;
        }
        constexpr Vec3& operator-=(const Vec3& o) {
            x -= o.x;
            y -= o.y;
            z -= o.z;
            return *this;
        }
        constexpr Vec3& operator*=(const float s) {
            x *= s;
            y *= s;
            z *= s;
            return *this;
        }

        [[nodiscard]] constexpr float dot(const Vec3& o) const {
            return x * o.x + y * o.y + z * o.z;
        }
        [[nodiscard]] constexpr Vec3 cross(const Vec3& o) const {
            return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
        }
        [[nodiscard]] float length() const {
            return std::sqrt(dot(*this));
        }
        [[nodiscard]] Vec3 normalized() const {
            const float len = length();
            return len > 0.0f ? *this / len : Vec3{0, 0, 0};
        }
    };

    export struct Mat4 {
        std::array<float, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

        [[nodiscard]] static constexpr Mat4 identity() {
            constexpr Mat4 result;
            return result;
        }

        [[nodiscard]] static Mat4 look_at(const Vec3& eye, const Vec3& center, const Vec3& up) {
            const Vec3 f = (center - eye).normalized();
            const Vec3 s = f.cross(up).normalized();
            const Vec3 u = s.cross(f);

            Mat4 result;
            result.m = {s.x, u.x, -f.x, 0, s.y, u.y, -f.y, 0, s.z, u.z, -f.z, 0, -s.dot(eye), -u.dot(eye), f.dot(eye), 1};
            return result;
        }
        [[nodiscard]] static Mat4 perspective(const float fov_y_rad, const float aspect, const float znear, const float zfar) {
            const float tan_half_fov = std::tan(fov_y_rad * 0.5f);
            Mat4 result{};
            result.m = {1.0f / (aspect * tan_half_fov), 0, 0, 0, 0, -1.0f / tan_half_fov, 0, 0, 0, 0, (zfar + znear) / (znear - zfar), -1, 0, 0, (2.0f * zfar * znear) / (znear - zfar), 0};
            return result;
        }
        [[nodiscard]] Mat4 operator*(const Mat4& o) const {
            Mat4 result{};
            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 4; ++r) {
                    result.m[c * 4 + r] = m[0 * 4 + r] * o.m[c * 4 + 0] + m[1 * 4 + r] * o.m[c * 4 + 1] + m[2 * 4 + r] * o.m[c * 4 + 2] + m[3 * 4 + r] * o.m[c * 4 + 3];
                }
            }
            return result;
        }
    };

    export Vec3 extract_position(const Mat4& m) {
        return Vec3{m.m[12], m.m[13], m.m[14]};
    }
} // namespace vk::toolkit::math
