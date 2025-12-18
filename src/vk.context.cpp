module;
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vulkan/vulkan_raii.hpp>
module vk.context;
import std;


namespace vk::context {

    namespace {

        struct DeviceCreatePolicy {
            bool want_ext_dynamic_state   = true;
            bool want_fill_mode_non_solid = true;
            bool want_sampler_anisotropy  = true;
        };

        [[nodiscard]] bool has_device_extension(const raii::PhysicalDevice& pd, const char* name) {
            const auto exts = pd.enumerateDeviceExtensionProperties();
            return std::ranges::any_of(exts, [name](const auto& e) { return std::strcmp(e.extensionName, name) == 0; });
        }

        template <typename Properties, typename Accessor>
        void ensure_support(const std::string_view label, const std::span<const char* const> required, const Properties& available, Accessor accessor) {
            for (const char* name : required) {
                if (const bool found = std::ranges::any_of(available, [&](const auto& property) { return std::strcmp(accessor(property), name) == 0; }); !found) {
                    std::string message;
                    message.reserve(label.size() + std::strlen(name) + 32);
                    message += "Required ";
                    message += label;
                    message += " not supported: ";
                    message += name;
                    throw std::runtime_error(message);
                }
            }
        }

        [[nodiscard]] bool supports_graphics_queue(const raii::PhysicalDevice& device) {
            const auto families = device.getQueueFamilyProperties();
            return std::ranges::any_of(families, [](const auto& qf) { return static_cast<bool>(qf.queueFlags & QueueFlagBits::eGraphics); });
        }

        [[nodiscard]] bool meets_device_requirements(const raii::PhysicalDevice& device) {
            if (device.getProperties().apiVersion < VK_API_VERSION_1_4) return false;
            if (!supports_graphics_queue(device)) return false;
            if (!has_device_extension(device, KHRSwapchainExtensionName)) return false;
            return true;
        }

        [[nodiscard]] uint32_t find_graphics_present_queue_index(const raii::PhysicalDevice& device, const raii::SurfaceKHR& surface) {
            const auto queue_families = device.getQueueFamilyProperties();
            for (uint32_t i = 0; i < queue_families.size(); ++i) {
                const bool supports_graphics = static_cast<bool>(queue_families[i].queueFlags & QueueFlagBits::eGraphics);
                const bool supports_present  = device.getSurfaceSupportKHR(i, surface);
                if (supports_graphics && supports_present) return i;
            }
            throw std::runtime_error("No queue family supports both graphics and present");
        }

        [[nodiscard]] auto build_feature_chain(const DeviceCreatePolicy& policy, const bool enable_ext_dynamic_state) {
            StructureChain<PhysicalDeviceFeatures2, PhysicalDeviceVulkan11Features, PhysicalDeviceVulkan13Features, PhysicalDeviceExtendedDynamicStateFeaturesEXT> features{
                {},
                {.shaderDrawParameters = VK_TRUE},
                {.synchronization2 = VK_TRUE, .dynamicRendering = VK_TRUE},
                {.extendedDynamicState = enable_ext_dynamic_state ? VK_TRUE : VK_FALSE},
            };

            if (policy.want_fill_mode_non_solid) {
                features.get<PhysicalDeviceFeatures2>().features.fillModeNonSolid = VK_TRUE;
            }
            if (policy.want_sampler_anisotropy) {
                features.get<PhysicalDeviceFeatures2>().features.samplerAnisotropy = VK_TRUE;
            }

            return features;
        }

        [[nodiscard]] Extent2D framebuffer_extent(const std::shared_ptr<GLFWwindow>& window) {
            int w = 0;
            int h = 0;
            glfwGetFramebufferSize(window.get(), &w, &h);
            return {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
        }
    } // namespace

    void check_validation_layers_support(const raii::Context& context, const std::span<const char* const> required_layers) {
        const auto layer_properties = context.enumerateInstanceLayerProperties();
        ensure_support("layer", required_layers, layer_properties, [](const auto& property) { return property.layerName; });
    }

    void check_extensions_support(const raii::Context& context, const std::span<const char* const> required_extensions) {
        const auto extension_properties = context.enumerateInstanceExtensionProperties();
        ensure_support("extension", required_extensions, extension_properties, [](const auto& property) { return property.extensionName; });
    }

    VKAPI_ATTR Bool32 VKAPI_CALL debug_callback(const DebugUtilsMessageSeverityFlagBitsEXT severity, const DebugUtilsMessageTypeFlagsEXT type, const DebugUtilsMessengerCallbackDataEXT* pCallbackData, void*) {
        if (const auto sev = DebugUtilsMessageSeverityFlagsEXT(severity); sev & (DebugUtilsMessageSeverityFlagBitsEXT::eWarning | DebugUtilsMessageSeverityFlagBitsEXT::eError)) {
            std::cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << std::endl;
        }
        return False;
    }

    [[nodiscard]] std::vector<const char*> build_device_extensions(const raii::PhysicalDevice& pd, const bool enable_ext_dynamic_state) {
        std::vector exts{KHRSwapchainExtensionName};
        if (enable_ext_dynamic_state) {
            if (!has_device_extension(pd, EXTExtendedDynamicStateExtensionName)) {
                throw std::runtime_error("Requested VK_EXT_extended_dynamic_state but device does not support it");
            }
            exts.push_back(EXTExtendedDynamicStateExtensionName);
        }
        return exts;
    }

    raii::Instance create_instance_raii(const raii::Context& context, const std::string& app_name, const std::string& engine_name, const std::span<const char* const> required_layers, const std::span<const char* const> required_extensions) {
        check_validation_layers_support(context, required_layers);
        check_extensions_support(context, required_extensions);
        const ApplicationInfo app_info{
            .pApplicationName   = app_name.c_str(),
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName        = engine_name.c_str(),
            .engineVersion      = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion         = ApiVersion14,
        };
        const InstanceCreateInfo createInfo{
            .pApplicationInfo        = &app_info,
            .enabledLayerCount       = static_cast<uint32_t>(required_layers.size()),
            .ppEnabledLayerNames     = required_layers.data(),
            .enabledExtensionCount   = static_cast<uint32_t>(required_extensions.size()),
            .ppEnabledExtensionNames = required_extensions.data(),
        };
        return {context, createInfo};
    }

    raii::DebugUtilsMessengerEXT create_debug_messenger_raii(const raii::Instance& instance) {
        constexpr DebugUtilsMessageSeverityFlagsEXT severity_flags(DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | DebugUtilsMessageSeverityFlagBitsEXT::eWarning | DebugUtilsMessageSeverityFlagBitsEXT::eError);
        constexpr DebugUtilsMessageTypeFlagsEXT message_type_flags(DebugUtilsMessageTypeFlagBitsEXT::eGeneral | DebugUtilsMessageTypeFlagBitsEXT::ePerformance | DebugUtilsMessageTypeFlagBitsEXT::eValidation);
        constexpr DebugUtilsMessengerCreateInfoEXT debug_utils_messenger_create_info_EXT{
            .messageSeverity = severity_flags,
            .messageType     = message_type_flags,
            .pfnUserCallback = &debug_callback,
        };
        return instance.createDebugUtilsMessengerEXT(debug_utils_messenger_create_info_EXT);
    }

    auto create_surface_raii(const raii::Instance& instance) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        auto window = std::shared_ptr<GLFWwindow>(glfwCreateWindow(1920, 1080, "Vulkan Engine Window", nullptr, nullptr), [](GLFWwindow* ptr) { glfwDestroyWindow(ptr); });
        if (!window) throw std::runtime_error("failed to create GLFW window");

        VkSurfaceKHR raw_surface = VK_NULL_HANDLE;
        if (glfwCreateWindowSurface(*instance, window.get(), nullptr, &raw_surface) != VK_SUCCESS) {
            throw std::runtime_error("failed to create window surface");
        }

        auto surface = raii::SurfaceKHR(instance, raw_surface);
        return std::make_tuple(std::move(surface), std::move(window), framebuffer_extent(window));
    }

    raii::PhysicalDevice pick_physical_device_raii(const raii::Instance& instance) {
        const auto devices = instance.enumeratePhysicalDevices();

        if (const auto it = std::ranges::find_if(devices, meets_device_requirements); it != devices.end()) {
            return *it;
        }
        throw std::runtime_error("failed to find a suitable GPU");
    }


    auto create_logical_device_raii(const raii::PhysicalDevice& physical_device, const raii::SurfaceKHR& surface, const DeviceCreatePolicy& policy) {
        const uint32_t graphics_queue_index = find_graphics_present_queue_index(physical_device, surface);

        const bool ext_dyn_state_enabled = policy.want_ext_dynamic_state && has_device_extension(physical_device, EXTExtendedDynamicStateExtensionName);
        const auto enabled_exts          = build_device_extensions(physical_device, ext_dyn_state_enabled);
        auto features                    = build_feature_chain(policy, ext_dyn_state_enabled);

        constexpr std::array queue_priority{1.0f};

        const DeviceQueueCreateInfo queue_ci{
            .queueFamilyIndex = graphics_queue_index,
            .queueCount       = 1,
            .pQueuePriorities = queue_priority.data(),
        };

        const DeviceCreateInfo device_ci{
            .pNext                   = &features.get<PhysicalDeviceFeatures2>(),
            .queueCreateInfoCount    = 1,
            .pQueueCreateInfos       = &queue_ci,
            .enabledExtensionCount   = static_cast<uint32_t>(enabled_exts.size()),
            .ppEnabledExtensionNames = enabled_exts.data(),
        };

        auto device         = raii::Device{physical_device, device_ci};
        auto graphics_queue = raii::Queue{device, graphics_queue_index, 0};

        return std::make_tuple(std::move(device), std::move(graphics_queue), graphics_queue_index, enabled_exts);
    }

    raii::CommandPool create_command_pool_raii(const raii::Device& device, const uint32_t graphics_queue_index) {
        const CommandPoolCreateInfo ci{
            .flags            = CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = graphics_queue_index,
        };
        return {device, ci};
    }
} // namespace vk::context

std::pair<vk::context::VulkanContext, vk::context::SurfaceContext> vk::context::setup_vk_context_glfw() {
    if (!glfwInit()) throw std::runtime_error("Failed to initialize GLFW");

    uint32_t glfw_extension_count = 0;
    const auto glfw_extensions    = glfwGetRequiredInstanceExtensions(&glfw_extension_count);

    std::vector required_extensions(glfw_extensions, glfw_extensions + glfw_extension_count);
    required_extensions.push_back(EXTDebugUtilsExtensionName);

    std::vector required_layers = {"VK_LAYER_KHRONOS_validation"};

    VulkanContext vk_context;
    SurfaceContext surface_context;

    vk_context.instance        = create_instance_raii(vk_context.context, "App", "Engine", required_layers, required_extensions);
    vk_context.debug_messenger = create_debug_messenger_raii(vk_context.instance);

    auto [surface, window, extent] = create_surface_raii(vk_context.instance);
    surface_context.surface        = std::move(surface);
    surface_context.window         = std::move(window);
    surface_context.extent         = extent;

    vk_context.physical_device = pick_physical_device_raii(vk_context.instance);

    constexpr DeviceCreatePolicy policy{
        .want_ext_dynamic_state   = true,
        .want_fill_mode_non_solid = true,
        .want_sampler_anisotropy  = true,
    };

    auto [device, graphics_queue, graphics_queue_index, enabled_device_exts] = create_logical_device_raii(vk_context.physical_device, surface_context.surface, policy);
    (void) enabled_device_exts;

    vk_context.device               = std::move(device);
    vk_context.graphics_queue       = std::move(graphics_queue);
    vk_context.graphics_queue_index = graphics_queue_index;
    vk_context.command_pool         = create_command_pool_raii(vk_context.device, vk_context.graphics_queue_index);

    return {std::move(vk_context), std::move(surface_context)};
}
