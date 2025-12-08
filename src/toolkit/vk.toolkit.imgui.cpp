module;
#include <array>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <stdexcept>
module vk.toolkit.imgui;
import vk.context;
import vk.toolkit.vulkan;
import vk.toolkit.log;


void vk::toolkit::imgui::create_imgui(context::EngineContext& eng, VkFormat swapchain_format) {
    constexpr std::array pool_sizes{VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, 1000}, VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000}, VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000}, VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000}, VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000}, VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000}, VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000}, VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000}, VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

    const VkDescriptorPoolCreateInfo pool_info{
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets       = 1000u * static_cast<uint32_t>(pool_sizes.size()),
        .poolSizeCount = static_cast<uint32_t>(pool_sizes.size()),
        .pPoolSizes    = pool_sizes.data(),
    };
    log::vk_check(vkCreateDescriptorPool(eng.device, &pool_info, nullptr, &eng.descriptor_allocator.pool), "Failed to create descriptor pool");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    ImGui::StyleColorsDark();

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        auto& style                       = ImGui::GetStyle();
        style.WindowRounding              = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    if (!ImGui_ImplSDL3_InitForVulkan(eng.window)) {
        throw std::runtime_error("Failed to initialize ImGui SDL3 backend");
    }

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.Instance            = eng.instance;
    init_info.PhysicalDevice      = eng.physical;
    init_info.Device              = eng.device;
    init_info.QueueFamily         = eng.graphics_queue_family;
    init_info.Queue               = eng.graphics_queue;
    init_info.DescriptorPool      = eng.descriptor_allocator.pool;
    init_info.MinImageCount       = context::FRAME_OVERLAP;
    init_info.ImageCount          = context::FRAME_OVERLAP;
    init_info.MSAASamples         = VK_SAMPLE_COUNT_1_BIT;
    init_info.UseDynamicRendering = VK_TRUE;
    init_info.CheckVkResultFn     = [](const VkResult res) { toolkit::log::vk_check(res, "ImGui Vulkan operation"); };

    VkPipelineRenderingCreateInfo rendering_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = 1, .pColorAttachmentFormats = &swapchain_format};

    init_info.PipelineRenderingCreateInfo = rendering_info;

    if (!ImGui_ImplVulkan_Init(&init_info)) {
        throw std::runtime_error("Failed to initialize ImGui Vulkan backend");
    }
}
void vk::toolkit::imgui::destroy_imgui() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void vk::toolkit::imgui::begin_imgui_frame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void vk::toolkit::imgui::end_imgui_frame(const VkCommandBuffer& cmd, const context::FrameContext& frm) {
    ImGui::Render();

    VkImage target_image    = VK_NULL_HANDLE;
    VkImageView target_view = VK_NULL_HANDLE;

    if (frm.presentation_mode == context::PresentationMode::DirectToSwapchain) {
        target_image = frm.swapchain_image;
        target_view  = frm.swapchain_image_view;
        vulkan::transition_to_color_attachment(cmd, target_image, VK_IMAGE_LAYOUT_UNDEFINED);
    } else {
        if (!frm.color_attachments.empty()) {
            target_image = frm.color_attachments[0].image;
            target_view  = frm.color_attachments[0].view;
            vulkan::transition_to_color_attachment(cmd, target_image, VK_IMAGE_LAYOUT_GENERAL);
        }
    }

    if (target_image != VK_NULL_HANDLE && target_view != VK_NULL_HANDLE) {
        const VkRenderingAttachmentInfo color_attachment{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = target_view, .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE};

        const VkRenderingInfo rendering_info{.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, frm.extent}, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &color_attachment};

        vkCmdBeginRendering(cmd, &rendering_info);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        vkCmdEndRendering(cmd);

        if (const auto& io = ImGui::GetIO(); io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        if (frm.presentation_mode != context::PresentationMode::DirectToSwapchain) {
            const VkImageMemoryBarrier2 barrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask                          = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask                         = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                .dstStageMask                          = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .dstAccessMask                         = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                .oldLayout                             = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .newLayout                             = VK_IMAGE_LAYOUT_GENERAL,
                .image                                 = target_image,
                .subresourceRange                      = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};

            const VkDependencyInfo dep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};

            vkCmdPipelineBarrier2(cmd, &dep);
        }
    }
}
