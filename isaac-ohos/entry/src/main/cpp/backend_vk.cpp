// Vulkan backend: VK-first present path (user decision). Instance + OHOS
// surface + swapchain + clear-color present loop. Full capability logging
// (G0-VK): loader version, instance extensions, GPU name/api/vendor.
#include "backend.h"

#include <vulkan/vulkan.h>
#if defined(__has_include)
#if __has_include(<vulkan/vulkan_ohos.h>)
#include <vulkan/vulkan_ohos.h>
#define HAVE_VK_OHOS_HDR 1
#endif
#endif
#include <hilog/log.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef VK_OHOS_SURFACE_EXTENSION_NAME
#define VK_OHOS_SURFACE_EXTENSION_NAME "VK_OHOS_surface"
#endif

#ifndef VK_STRUCTURE_TYPE_SURFACE_CREATE_INFO_OHOS
#define VK_STRUCTURE_TYPE_SURFACE_CREATE_INFO_OHOS ((VkStructureType)1000685000)
#ifndef HAVE_VK_OHOS_HDR
typedef struct VkSurfaceCreateInfoOHOS {
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
    struct OHNativeWindow* window;
} VkSurfaceCreateInfoOHOS;
typedef VkResult (VKAPI_PTR* PFN_vkCreateSurfaceOHOS)(
    VkInstance instance, const VkSurfaceCreateInfoOHOS* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);
#endif
#endif

#define TAG "isaac.vk"
#define LOGI(...) ((void)OH_LOG_Print(LOG_APP, LOG_INFO, 0, TAG, __VA_ARGS__))
#define LOGE(...) ((void)OH_LOG_Print(LOG_APP, LOG_ERROR, 0, TAG, __VA_ARGS__))

namespace {

struct BackendVK : public Backend {
    // Only the OHOS surface constructor needs dynamic lookup; everything else
    // is core/WSI and comes straight from the linked libvulkan.so.
    PFN_vkCreateSurfaceOHOS pCreateSurfaceOHOS = nullptr;

    VkInstance inst = VK_NULL_HANDLE;
    VkSurfaceKHR surf = VK_NULL_HANDLE;
    VkPhysicalDevice pd = VK_NULL_HANDLE;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t qfam = 0;
    VkSwapchainKHR swap = VK_NULL_HANDLE;
    VkFormat fmt = VK_FORMAT_B8G8R8A8_UNORM;
    VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D ext = {0, 0};
    std::vector<VkImage> imgs;
    std::vector<VkImageView> views;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = nullptr;
    VkSemaphore semAcq = VK_NULL_HANDLE;
    VkSemaphore semRdy = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    OHNativeWindow* win = nullptr;
    int w = 0;
    int h = 0;
    std::string info;

    const char* name() const override { return "vk"; }

    bool init(OHNativeWindow* window, int width, int height) override {
        win = window;
        w = width;
        h = height;

        uint32_t n = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
        std::vector<VkExtensionProperties> exts(n > 0 ? n : 0);
        if (n > 0) {
            vkEnumerateInstanceExtensionProperties(nullptr, &n, exts.data());
        }
        bool hasSurface = false;
        bool hasOhos = false;
        std::string extList;
        for (const auto& e : exts) {
            extList += e.extensionName;
            extList += " ";
            if (strcmp(e.extensionName, "VK_KHR_surface") == 0) {
                hasSurface = true;
            }
            if (strcmp(e.extensionName, VK_OHOS_SURFACE_EXTENSION_NAME) == 0) {
                hasOhos = true;
            }
        }
        info += "instance_ext=" + extList + "\n";
        if (!hasSurface || !hasOhos) {
            LOGE("missing WSI instance ext: KHR_surface=%{public}d OHOS=%{public}d", hasSurface, hasOhos);
            return false;
        }

        uint32_t apiVer = 0;
        PFN_vkEnumerateInstanceVersion pVer =
            reinterpret_cast<PFN_vkEnumerateInstanceVersion>(vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
        if (pVer != nullptr && pVer(&apiVer) == VK_SUCCESS) {
            char buf[64];
            snprintf(buf, sizeof(buf), "loader_api=%u.%u.%u",
                     VK_VERSION_MAJOR(apiVer), VK_VERSION_MINOR(apiVer), VK_VERSION_PATCH(apiVer));
            info += std::string(buf) + "\n";
        }

        VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr,
                                 "isaac-ohos", 1, "isaac-ohos", 1, VK_API_VERSION_1_1};
        const char* instExtNames[] = {"VK_KHR_surface", VK_OHOS_SURFACE_EXTENSION_NAME};
        VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr,
                                    0, &app, 0, nullptr, 2, instExtNames};
        if (vkCreateInstance(&ici, nullptr, &inst) != VK_SUCCESS) {
            LOGE("vkCreateInstance failed");
            return false;
        }

        pCreateSurfaceOHOS = reinterpret_cast<PFN_vkCreateSurfaceOHOS>(
            vkGetInstanceProcAddr(inst, "vkCreateSurfaceOHOS"));
        if (pCreateSurfaceOHOS == nullptr) {
            LOGE("vkCreateSurfaceOHOS not found in loader");
            return false;
        }
        VkSurfaceCreateInfoOHOS sci = {};
        sci.sType = VK_STRUCTURE_TYPE_SURFACE_CREATE_INFO_OHOS;
        sci.window = win;
        if (pCreateSurfaceOHOS(inst, &sci, nullptr, &surf) != VK_SUCCESS) {
            LOGE("vkCreateSurfaceOHOS failed");
            return false;
        }

        uint32_t npd = 0;
        vkEnumeratePhysicalDevices(inst, &npd, nullptr);
        if (npd == 0) {
            LOGE("no physical device");
            return false;
        }
        std::vector<VkPhysicalDevice> pds(npd);
        vkEnumeratePhysicalDevices(inst, &npd, pds.data());
        pd = pds[0];
        VkPhysicalDeviceProperties props = {};
        vkGetPhysicalDeviceProperties(pd, &props);
        char buf[256];
        snprintf(buf, sizeof(buf), "gpu=%s api=%u.%u.%u vendor=0x%04x",
                 props.deviceName,
                 VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion),
                 VK_VERSION_PATCH(props.apiVersion), props.vendorID);
        info += std::string(buf) + "\n";
        LOGI("%{public}s", buf);

        uint32_t nq = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, nullptr);
        std::vector<VkQueueFamilyProperties> qs(nq);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, qs.data());
        qfam = UINT32_MAX;
        for (uint32_t i = 0; i < nq; i++) {
            if ((qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
                continue;
            }
            VkBool32 supp = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surf, &supp);
            if (supp) {
                qfam = i;
                break;
            }
        }
        if (qfam == UINT32_MAX) {
            LOGE("no graphics+present queue family");
            return false;
        }

        const float prio = 1.0f;
        VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                       nullptr, 0, qfam, 1, &prio};
        const char* swapchainExt = "VK_KHR_swapchain";
        VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, nullptr, 0,
                                  1, &qci, 0, nullptr, 1, &swapchainExt, nullptr};
        if (vkCreateDevice(pd, &dci, nullptr, &dev) != VK_SUCCESS) {
            LOGE("vkCreateDevice failed");
            return false;
        }
        vkGetDeviceQueue(dev, qfam, 0, &queue);

        if (!CreateSwapchain()) {
            return false;
        }

        VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr,
                                       VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, qfam};
        if (vkCreateCommandPool(dev, &pci, nullptr, &pool) != VK_SUCCESS) {
            LOGE("vkCreateCommandPool failed");
            return false;
        }
        VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                           nullptr, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
        if (vkAllocateCommandBuffers(dev, &cai, &cmd) != VK_SUCCESS) {
            LOGE("vkAllocateCommandBuffers failed");
            return false;
        }
        VkSemaphoreCreateInfo semi = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0};
        vkCreateSemaphore(dev, &semi, nullptr, &semAcq);
        vkCreateSemaphore(dev, &semi, nullptr, &semRdy);
        VkFenceCreateInfo fi = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr,
                                VK_FENCE_CREATE_SIGNALED_BIT};
        vkCreateFence(dev, &fi, nullptr, &fence);

        char sbuf[96];
        snprintf(sbuf, sizeof(sbuf), "swap=%ux%u images=%zu fmt=%d", ext.width, ext.height, imgs.size(), (int)fmt);
        info += std::string(sbuf) + "\n";
        LOGI("VK backend init OK: %{public}s", sbuf);
        return true;
    }

    bool CreateSwapchain() {
        VkSurfaceCapabilitiesKHR caps = {};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, surf, &caps);

        uint32_t nf = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surf, &nf, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(nf > 0 ? nf : 0);
        if (nf > 0) {
            vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surf, &nf, formats.data());
        }
        if (!formats.empty()) {
            fmt = formats[0].format;
            colorSpace = formats[0].colorSpace;
            for (const auto& f : formats) {
                if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) {
                    fmt = f.format;
                    colorSpace = f.colorSpace;
                    break;
                }
            }
        }

        uint32_t iw = caps.currentExtent.width;
        uint32_t ih = caps.currentExtent.height;
        if (iw == 0xFFFFFFFFu || iw == 0 || ih == 0) {
            iw = static_cast<uint32_t>(w > 0 ? w : 1280);
            ih = static_cast<uint32_t>(h > 0 ? h : 720);
            if (caps.minImageExtent.width != 0 && iw < caps.minImageExtent.width) iw = caps.minImageExtent.width;
            if (caps.minImageExtent.height != 0 && ih < caps.minImageExtent.height) ih = caps.minImageExtent.height;
            if (caps.maxImageExtent.width != 0 && caps.maxImageExtent.width != 0xFFFFFFFFu && iw > caps.maxImageExtent.width) iw = caps.maxImageExtent.width;
            if (caps.maxImageExtent.height != 0 && caps.maxImageExtent.height != 0xFFFFFFFFu && ih > caps.maxImageExtent.height) ih = caps.maxImageExtent.height;
        }
        ext.width = iw;
        ext.height = ih;

        if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
            LOGE("swapchain TRANSFER_DST usage unsupported, clear-path unusable");
            return false;
        }

        uint32_t imgCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount) {
            imgCount = caps.maxImageCount;
        }

        VkSwapchainCreateInfoKHR s = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, nullptr};
        s.surface = surf;
        s.minImageCount = imgCount;
        s.imageFormat = fmt;
        s.imageColorSpace = colorSpace;
        s.imageExtent = ext;
        s.imageArrayLayers = 1;
        s.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        s.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        s.preTransform = caps.currentTransform;
        s.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                               ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                               : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        s.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        s.clipped = VK_TRUE;
        s.oldSwapchain = VK_NULL_HANDLE;
        if (vkCreateSwapchainKHR(dev, &s, nullptr, &swap) != VK_SUCCESS) {
            LOGE("vkCreateSwapchainKHR failed");
            return false;
        }

        uint32_t ni = 0;
        vkGetSwapchainImagesKHR(dev, swap, &ni, nullptr);
        imgs.resize(ni);
        vkGetSwapchainImagesKHR(dev, swap, &ni, imgs.data());
        views.resize(ni);
        for (uint32_t i = 0; i < ni; i++) {
            VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr};
            vi.image = imgs[i];
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = fmt;
            vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            if (vkCreateImageView(dev, &vi, nullptr, &views[i]) != VK_SUCCESS) {
                LOGE("vkCreateImageView failed");
                return false;
            }
        }
        return true;
    }

    void DestroySwapchain() {
        for (auto v : views) {
            if (v != VK_NULL_HANDLE) {
                vkDestroyImageView(dev, v, nullptr);
            }
        }
        views.clear();
        imgs.clear();
        if (swap != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(dev, swap, nullptr);
            swap = VK_NULL_HANDLE;
        }
    }

    void Recreate() {
        vkDeviceWaitIdle(dev);
        DestroySwapchain();
        CreateSwapchain();
    }

    std::string probe() override { return info; }

    void frame(float t) override {
        if (dev == VK_NULL_HANDLE || swap == VK_NULL_HANDLE) {
            return;
        }
        vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);

        uint32_t idx = 0;
        VkResult r = vkAcquireNextImageKHR(dev, swap, 500000000ULL, semAcq, VK_NULL_HANDLE, &idx);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) {
            Recreate();
            return;
        }
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
            LOGE("acquire failed %d", r);
            return;
        }
        vkResetFences(dev, 1, &fence);
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
                                       VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
        vkBeginCommandBuffer(cmd, &bi);

        VkImageMemoryBarrier bar = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr};
        bar.srcAccessMask = 0;
        bar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image = imgs[idx];
        bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &bar);

        VkClearColorValue col = {};
        col.float32[0] = 0.05f + 0.12f * (0.5f + 0.5f * sinf(t * 0.9f));
        col.float32[1] = 0.07f + 0.12f * (0.5f + 0.5f * sinf(t * 0.7f + 2.1f));
        col.float32[2] = 0.10f + 0.15f * (0.5f + 0.5f * sinf(t * 0.5f + 4.2f));
        col.float32[3] = 1.0f;
        VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, imgs[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &col, 1, &range);

        bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bar.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        bar.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &bar);
        vkEndCommandBuffer(cmd);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &semAcq;
        si.pWaitDstStageMask = &waitStage;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &semRdy;
        vkQueueSubmit(queue, 1, &si, fence);

        VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, nullptr};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &semRdy;
        pi.swapchainCount = 1;
        pi.pSwapchains = &swap;
        pi.pImageIndices = &idx;
        r = vkQueuePresentKHR(queue, &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
            Recreate();
        }
    }

    void shutdown() override {
        if (dev != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(dev);
        }
        DestroySwapchain();
        if (pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(dev, pool, nullptr);
            pool = VK_NULL_HANDLE;
            cmd = nullptr;
        }
        if (semAcq != VK_NULL_HANDLE) {
            vkDestroySemaphore(dev, semAcq, nullptr);
            semAcq = VK_NULL_HANDLE;
        }
        if (semRdy != VK_NULL_HANDLE) {
            vkDestroySemaphore(dev, semRdy, nullptr);
            semRdy = VK_NULL_HANDLE;
        }
        if (fence != VK_NULL_HANDLE) {
            vkDestroyFence(dev, fence, nullptr);
            fence = VK_NULL_HANDLE;
        }
        if (dev != VK_NULL_HANDLE) {
            vkDestroyDevice(dev, nullptr);
            dev = VK_NULL_HANDLE;
        }
        if (surf != VK_NULL_HANDLE && inst != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(inst, surf, nullptr);
            surf = VK_NULL_HANDLE;
        }
        if (inst != VK_NULL_HANDLE) {
            vkDestroyInstance(inst, nullptr);
            inst = VK_NULL_HANDLE;
        }
    }
};

} // namespace

Backend* CreateBackendVK() {
    return new BackendVK();
}
