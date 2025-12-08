module;
#include "vk_mem_alloc.h"
#include <SDL3/SDL.h>
#include <array>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <vulkan/vulkan.h>
export module vk.context;

namespace vk::context {
    constexpr auto reset   = "\033[0m";
    constexpr auto bold    = "\033[1m";
    constexpr auto red     = "\033[31m";
    constexpr auto green   = "\033[32m";
    constexpr auto yellow  = "\033[33m";
    constexpr auto blue    = "\033[34m";
    constexpr auto magenta = "\033[35m";
    constexpr auto cyan    = "\033[36m";

    export void vk_check(const VkResult result, const char* operation = "") {
        if (result != VK_SUCCESS) {
            throw std::runtime_error(std::format("{}{}Vulkan Error{}: {} (code: {})", bold, red, reset, operation, static_cast<int>(result)));
        }
    }

    export struct DescriptorAllocator {
        struct PoolSizeRatio {
            VkDescriptorType type;
            float ratio;
        };
        VkDescriptorPool pool{};
        void init_pool(VkDevice device, uint32_t maxSets, std::span<const PoolSizeRatio> ratios);
        void clear_descriptors(VkDevice device) const;
        void destroy_pool(VkDevice device) const;
        VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout) const;
    };

    export struct EngineContext {
        SDL_Window* window{};
        VkInstance instance{};
        VkDebugUtilsMessengerEXT debug_messenger{};
        VkSurfaceKHR surface{};
        VkPhysicalDevice physical{};
        VkDevice device{};
        VkQueue graphics_queue{};
        VkQueue compute_queue{};
        VkQueue transfer_queue{};
        VkQueue present_queue{};
        uint32_t graphics_queue_family{};
        uint32_t compute_queue_family{};
        uint32_t transfer_queue_family{};
        uint32_t present_queue_family{};
        VmaAllocator allocator{};
        DescriptorAllocator descriptor_allocator{};
        // void* services{};
    };

    export inline constexpr unsigned int FRAME_OVERLAP = 2;
    export enum class PresentationMode : uint8_t { EngineBlit, RendererComposite, DirectToSwapchain };


    export struct AttachmentRequest {
        std::string name;
        VkFormat format{VK_FORMAT_R16G16B16A16_SFLOAT};
        VkImageUsageFlags usage{VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
        VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
        VkImageAspectFlags aspect{VK_IMAGE_ASPECT_COLOR_BIT};
        VkImageLayout initial_layout{VK_IMAGE_LAYOUT_GENERAL};
    };

    export struct AttachmentResource {
        struct AllocatedImage {
            VkImage image{};
            VkImageView imageView{};
            VmaAllocation allocation{};
            VkExtent3D imageExtent{};
            VkFormat imageFormat{};
        };
        std::string name;
        VkImageUsageFlags usage{};
        VkImageAspectFlags aspect{};
        VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
        VkImageLayout initial_layout{VK_IMAGE_LAYOUT_GENERAL};
        AllocatedImage image;
    };

    export struct AttachmentView {
        std::string_view name{};
        VkImage image{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkExtent3D extent{};
        VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
        VkImageUsageFlags usage{0};
        VkImageAspectFlags aspect{VK_IMAGE_ASPECT_COLOR_BIT};
        VkImageLayout current_layout{VK_IMAGE_LAYOUT_UNDEFINED};
    };

    export struct SwapchainSystem {
        VkSwapchainKHR swapchain{};
        VkFormat swapchain_image_format{};
        VkExtent2D swapchain_extent{};
        std::vector<VkImage> swapchain_images{};
        std::vector<VkImageView> swapchain_image_views{};
        std::vector<AttachmentResource> color_attachments{};
        std::optional<AttachmentResource> depth_attachment;
    };

    export struct RendererCaps {
        uint32_t api_version{};
        uint32_t frames_in_flight{FRAME_OVERLAP};
        VkBool32 dynamic_rendering{VK_TRUE};
        VkBool32 timeline_semaphore{VK_TRUE};
        VkBool32 descriptor_indexing{VK_TRUE};
        VkBool32 buffer_device_address{VK_TRUE};
        VkBool32 uses_depth{VK_FALSE};
        VkBool32 uses_offscreen{VK_TRUE};
        VkSampleCountFlagBits color_samples{VK_SAMPLE_COUNT_1_BIT};
        PresentationMode presentation_mode{PresentationMode::EngineBlit};
        std::string presentation_attachment{"hdr_color"};
        std::vector<AttachmentRequest> color_attachments{AttachmentRequest{.name = "hdr_color"}};
        std::optional<AttachmentRequest> depth_attachment{};
        VkFormat preferred_swapchain_format{VK_FORMAT_B8G8R8A8_UNORM};
        VkFormat preferred_depth_format{VK_FORMAT_D32_SFLOAT};
        VkImageUsageFlags swapchain_usage{VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
        VkPresentModeKHR present_mode{VK_PRESENT_MODE_FIFO_KHR};
        bool enable_imgui{true};
        bool allow_async_compute{false};
        bool allow_async_transfer{false};
        bool need_ray_tracing_pipeline{false};
        bool need_acceleration_structure{false};
        bool need_ray_query{false};
        bool need_mesh_shader{false};
        bool need_shader_int64{false};
        bool need_shader_float16{false};
        std::vector<const char*> extra_instance_extensions{};
        std::vector<const char*> extra_device_extensions{};
    };

    export struct FrameContext {
        uint64_t frame_index{};
        uint32_t image_index{};
        VkExtent2D extent{};
        VkFormat swapchain_format{};
        double dt_sec{};
        double time_sec{};
        VkImage swapchain_image{};
        VkImageView swapchain_image_view{};
        VkImage offscreen_image{};
        VkImageView offscreen_image_view{};
        VkImage depth_image{VK_NULL_HANDLE};
        VkImageView depth_image_view{VK_NULL_HANDLE};
        std::span<const AttachmentView> color_attachments{};
        const AttachmentView* depth_attachment{nullptr};
        PresentationMode presentation_mode{PresentationMode::EngineBlit};
    };

    export struct FrameData {
        VkCommandPool commandPool{};
        VkCommandBuffer mainCommandBuffer{};
        VkSemaphore imageAcquired{};
        VkSemaphore renderComplete{};
        uint64_t submitted_timeline_value{0};
        std::vector<std::function<void()>> dq{};
        VkCommandBuffer asyncComputeCommandBuffer{};
        VkSemaphore asyncComputeFinished{};
        bool asyncComputeSubmitted{false};
        VkCommandPool computeCommandPool{};
    };

    export enum class PluginPhase : uint32_t {
        None       = 0,
        Setup      = 1 << 0,
        Initialize = 1 << 1,
        PreRender  = 1 << 2,
        Render     = 1 << 3,
        PostRender = 1 << 4,
        ImGUI      = 1 << 5,
        Present    = 1 << 6,
        Cleanup    = 1 << 7,
        All        = 0xFFFFFFFF,
    };

    export constexpr PluginPhase operator|(PluginPhase a, PluginPhase b) {
        return static_cast<PluginPhase>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    export constexpr PluginPhase& operator|=(PluginPhase& a, PluginPhase b) {
        a = static_cast<PluginPhase>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
        return a;
    }

    export constexpr bool operator&(PluginPhase a, PluginPhase b) {
        return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
    }

    export constexpr PluginPhase& operator&=(PluginPhase& a, PluginPhase b) {
        a = static_cast<PluginPhase>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
        return a;
    }

    export struct PluginContext {
        EngineContext* engine{nullptr};
        FrameContext* frame{nullptr};
        RendererCaps* caps{nullptr};
        VkCommandBuffer* cmd{nullptr};
        const SDL_Event* event{nullptr};
        uint64_t frame_number{0};
        float delta_time{0.0f};
    };

    export void transition_image_layout(const VkCommandBuffer& cmd, const AttachmentView& target, VkImageLayout old_layout, VkImageLayout new_layout);
    export void transition_to_color_attachment(const VkCommandBuffer& cmd, VkImage image, VkImageLayout old_layout);


    export struct Vec3 {
        float x{}, y{}, z{};

        constexpr Vec3() = default;
        constexpr Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

        constexpr Vec3 operator+(const Vec3& o) const {
            return {x + o.x, y + o.y, z + o.z};
        }
        constexpr Vec3 operator-(const Vec3& o) const {
            return {x - o.x, y - o.y, z - o.z};
        }
        constexpr Vec3 operator*(float s) const {
            return {x * s, y * s, z * s};
        }
        constexpr Vec3 operator/(float s) const {
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
        constexpr Vec3& operator*=(float s) {
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
        [[nodiscard]] static Mat4 perspective(float fov_y_rad, float aspect, float znear, float zfar) {
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


    enum class CameraMode : uint8_t { Orbit, Fly };
    enum class ProjectionMode : uint8_t { Perspective, Orthographic };

    export struct CameraState {
        CameraMode mode{CameraMode::Orbit};
        ProjectionMode projection{ProjectionMode::Perspective};

        // Orbit mode parameters
        Vec3 target{0, 0, 0};
        float distance{5.0f};
        float yaw_deg{-45.0f};
        float pitch_deg{25.0f};

        // Fly mode parameters
        Vec3 eye{0, 0, 5};
        float fly_yaw_deg{-90.0f};
        float fly_pitch_deg{0.0f};

        // Projection parameters
        float fov_y_deg{50.0f};
        float ortho_height{5.0f};
        float znear{0.01f};
        float zfar{1000.0f};
    };


    export class Camera {
    public:
        void update(float dt_sec, int viewport_w, int viewport_h);
        void handle_event(const SDL_Event& event);

        [[nodiscard]] const Mat4& view_matrix() const {
            return view_;
        }
        [[nodiscard]] const Mat4& proj_matrix() const {
            return proj_;
        }
        [[nodiscard]] const CameraState& state() const {
            return state_;
        }
        [[nodiscard]] Vec3 eye_position() const;

        void set_state(const CameraState& s);
        void set_mode(CameraMode m);
        void set_projection(ProjectionMode p);
        void home_view();
        void draw_imgui_panel();
        void draw_mini_axis_gizmo();

    private:
        void recompute_matrices();
        void apply_inertia(float dt);
        bool show_camera_panel_{true};

        CameraState state_{};
        Mat4 view_{};
        Mat4 proj_{};
        int viewport_width_{1};
        int viewport_height_{1};

        // Mouse state
        bool lmb_{false}, mmb_{false}, rmb_{false};
        int last_mx_{0}, last_my_{0};
        bool fly_capturing_{false};

        // Keyboard state
        bool key_w_{false}, key_a_{false}, key_s_{false}, key_d_{false};
        bool key_q_{false}, key_e_{false};
        bool key_shift_{false}, key_ctrl_{false}, key_space_{false}, key_alt_{false};

        // Inertia
        float yaw_vel_{0}, pitch_vel_{0};
        float pan_x_vel_{0}, pan_y_vel_{0};
        float zoom_vel_{0};
    };
} // namespace vk::context
