module;
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vulkan/vulkan_raii.hpp>
module vk.context;
import std;


namespace vk::context {
    void check_validation_layers_support(const raii::Context& context, const std::span<const char* const> required_layers) {
        auto layer_properties = context.enumerateInstanceLayerProperties();
        for (auto const& required_layer : required_layers) {
            if (std::ranges::none_of(layer_properties, [required_layer](auto const& layerProperty) { return strcmp(layerProperty.layerName, required_layer) == 0; })) {
                throw std::runtime_error("Required layer not supported: " + std::string(required_layer));
            }
        }
    }
    void check_extensions_support(const raii::Context& context, const std::span<const char* const> required_extensions) {
        auto extension_properties = context.enumerateInstanceExtensionProperties();
        for (auto const& required_extension : required_extensions) {
            if (std::ranges::none_of(extension_properties, [required_extension](auto const& extensionProperty) { return strcmp(extensionProperty.extensionName, required_extension) == 0; })) {
                throw std::runtime_error("Required extension not supported: " + std::string(required_extension));
            }
        }
    }
    VKAPI_ATTR Bool32 VKAPI_CALL debug_callback(const DebugUtilsMessageSeverityFlagBitsEXT severity, const DebugUtilsMessageTypeFlagsEXT type, const DebugUtilsMessengerCallbackDataEXT* pCallbackData, void*) {
        if (const auto sev = DebugUtilsMessageSeverityFlagsEXT(severity); sev & (DebugUtilsMessageSeverityFlagBitsEXT::eWarning | DebugUtilsMessageSeverityFlagBitsEXT::eError)) {
            std::cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << std::endl;
        }
        return False;
    }

    [[nodiscard]] bool has_device_extension(const raii::PhysicalDevice& pd, const char* name) {
        const auto exts = pd.enumerateDeviceExtensionProperties();
        return std::ranges::any_of(exts, [&](const auto& e) { return std::strcmp(e.extensionName, name) == 0; });
    }

    [[nodiscard]] std::vector<const char*> build_device_extensions(const raii::PhysicalDevice& pd, const bool want_ext_dynamic_state) {
        std::vector<const char*> exts;
        exts.push_back(KHRSwapchainExtensionName);

        if (want_ext_dynamic_state && has_device_extension(pd, EXTExtendedDynamicStateExtensionName)) {
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
        VkSurfaceKHR _surface;
        if (glfwCreateWindowSurface(*instance, window.get(), nullptr, &_surface) != 0) throw std::runtime_error("failed to create window surface!");
        auto surface = raii::SurfaceKHR(instance, _surface);
        int w = 0, h = 0;
        glfwGetFramebufferSize(window.get(), &w, &h);
        Extent2D extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
        return std::make_tuple(std::move(surface), std::move(window), extent);
    }

    raii::PhysicalDevice pick_physical_device_raii(const raii::Instance& instance) {
        const auto devices = instance.enumeratePhysicalDevices();

        auto has_graphics_queue = [&](const raii::PhysicalDevice& device) {
            const auto families = device.getQueueFamilyProperties();
            return std::ranges::any_of(families, [](const auto& qf) { return static_cast<bool>(qf.queueFlags & QueueFlagBits::eGraphics); });
        };

        auto is_usable = [&](const raii::PhysicalDevice& device) {
            if (const auto props = device.getProperties(); props.apiVersion < VK_API_VERSION_1_4) return false;
            if (!has_graphics_queue(device)) return false;
            if (!has_device_extension(device, KHRSwapchainExtensionName)) return false;
            return true;
        };

        if (const auto it = std::ranges::find_if(devices, is_usable); it != devices.end()) {
            return *it;
        }
        throw std::runtime_error("failed to find a suitable GPU");
    }


    struct DeviceCreatePolicy {
        bool want_ext_dynamic_state   = true;
        bool want_fill_mode_non_solid = true;
        bool want_sampler_anisotropy  = true;
    };

    auto create_logical_device_raii(const raii::PhysicalDevice& physical_device, const raii::SurfaceKHR& surface, const DeviceCreatePolicy& policy) {
        const auto queue_families = physical_device.getQueueFamilyProperties();

        const auto graphics_queue_index = [&]() -> uint32_t {
            for (uint32_t i = 0; i < queue_families.size(); ++i) {
                const bool supports_graphics = static_cast<bool>(queue_families[i].queueFlags & QueueFlagBits::eGraphics);
                const bool supports_present  = physical_device.getSurfaceSupportKHR(i, surface);
                if (supports_graphics && supports_present) return i;
            }
            throw std::runtime_error("No queue family supports both graphics and present");
        }();

        const auto enabled_exts          = build_device_extensions(physical_device, policy.want_ext_dynamic_state);
        const bool ext_dyn_state_enabled = std::ranges::any_of(enabled_exts, [](const char* s) { return std::strcmp(s, EXTExtendedDynamicStateExtensionName) == 0; });

        StructureChain<PhysicalDeviceFeatures2, PhysicalDeviceVulkan11Features, PhysicalDeviceVulkan13Features, PhysicalDeviceExtendedDynamicStateFeaturesEXT> features{
            {},
            {.shaderDrawParameters = true},
            {.synchronization2 = true, .dynamicRendering = true},
            {.extendedDynamicState = ext_dyn_state_enabled ? VK_TRUE : VK_FALSE},
        };

        if (policy.want_fill_mode_non_solid) {
            features.get<PhysicalDeviceFeatures2>().features.fillModeNonSolid = VK_TRUE;
        }
        if (policy.want_sampler_anisotropy) {
            features.get<PhysicalDeviceFeatures2>().features.samplerAnisotropy = VK_TRUE;
        }

        constexpr float queue_priority = 1.0f;

        const DeviceQueueCreateInfo queue_ci{
            .queueFamilyIndex = graphics_queue_index,
            .queueCount       = 1,
            .pQueuePriorities = &queue_priority,
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

    vk_context.device               = std::move(device);
    vk_context.graphics_queue       = std::move(graphics_queue);
    vk_context.graphics_queue_index = graphics_queue_index;
    vk_context.command_pool         = create_command_pool_raii(vk_context.device, vk_context.graphics_queue_index);

    return {std::move(vk_context), std::move(surface_context)};
}
