#include <vulkan/vulkan.h>
#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VK_CHECK(x) do { VkResult r=(x); if(r!=VK_SUCCESS){printf("error: " #x " = %d\n",r);exit(1);} } while(0)

static uint32_t* readSPV(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("error: cannot open %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END); *size = ftell(f); rewind(f);
    uint32_t* buf = (uint32_t*)malloc(*size);
    fread(buf, 1, *size, f); fclose(f);
    return buf;
}

static unsigned char* readPPM(const char* path, int* w, int* h) {
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    char magic[3];
    fscanf(f, "%2s\n%d %d\n255\n", magic, w, h);
    unsigned char* data = (unsigned char*)malloc((*w) * (*h) * 3);
    fread(data, 3, (*w) * (*h), f);
    fclose(f);
    return data;
}

uint32_t findMemoryType(VkPhysicalDevice pdev, uint32_t filter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pdev, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((filter & (1<<i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    return 0;
}

int main() {
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) { printf("error: no display\n"); return 1; }
    Window win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1280, 720, 0, 0, 0);
    XSelectInput(dpy, win, KeyPressMask | StructureNotifyMask);
    XStoreName(dpy, win, "Prism - Bad Apple");
    XMapWindow(dpy, win); XFlush(dpy);

    const char* instExts[] = {"VK_KHR_surface","VK_KHR_xlib_surface"};
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    ai.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &ai;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = instExts;
    VkInstance instance;
    VK_CHECK(vkCreateInstance(&ici, nullptr, &instance));

    PFN_vkCreateXlibSurfaceKHR createXlibSurface = (PFN_vkCreateXlibSurfaceKHR)
        vkGetInstanceProcAddr(instance, "vkCreateXlibSurfaceKHR");
    VkXlibSurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR};
    sci.dpy = dpy; sci.window = win;
    VkSurfaceKHR surface;
    VK_CHECK(createXlibSurface(instance, &sci, nullptr, &surface));

    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    VkPhysicalDevice pdevs[8];
    vkEnumeratePhysicalDevices(instance, &count, pdevs);
    VkPhysicalDevice pdev = pdevs[0];
    for (uint32_t i = 0; i < count; i++) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(pdevs[i], &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) { pdev = pdevs[i]; break; }
    }

    float qprio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = 0; qci.queueCount = 1; qci.pQueuePriorities = &qprio;
    const char* devExts[] = {"VK_KHR_swapchain"};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1; dci.ppEnabledExtensionNames = devExts;
    VkDevice device;
    VK_CHECK(vkCreateDevice(pdev, &dci, nullptr, &device));
    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pdev, surface, &caps);
    VkSwapchainCreateInfoKHR swci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swci.surface = surface; swci.minImageCount = 2;
    swci.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    swci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swci.imageExtent = {1280, 720}; swci.imageArrayLayers = 1;
    swci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swci.presentMode = VK_PRESENT_MODE_FIFO_KHR; swci.clipped = VK_TRUE;
    swci.preTransform = caps.currentTransform;
    swci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    VkSwapchainKHR swapchain;
    VK_CHECK(vkCreateSwapchainKHR(device, &swci, nullptr, &swapchain));

    uint32_t imgCount = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &imgCount, nullptr);
    VkImage imgs[8];
    vkGetSwapchainImagesKHR(device, swapchain, &imgCount, imgs);

    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.queueFamilyIndex = 0;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool cmdPool;
    vkCreateCommandPool(device, &cpci, nullptr, &cmdPool);

    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = cmdPool; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    vkAllocateCommandBuffers(device, &cbai, &cmd);

    VkSemaphoreCreateInfo semi{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkSemaphore imageAvail, renderDone;
    VkFence fence;
    vkCreateSemaphore(device, &semi, nullptr, &imageAvail);
    vkCreateSemaphore(device, &semi, nullptr, &renderDone);
    vkCreateFence(device, &fenci, nullptr, &fence);

    VkDeviceSize bufSize = 1280 * 720 * 4;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = bufSize; bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer stagingBuf;
    vkCreateBuffer(device, &bci, nullptr, &stagingBuf);
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device, stagingBuf, &memReq);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = memReq.size;
    mai.memoryTypeIndex = findMemoryType(pdev, memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory stagingMem;
    vkAllocateMemory(device, &mai, nullptr, &stagingMem);
    vkBindBufferMemory(device, stagingBuf, stagingMem, 0);

    bool running = true;
    int frameNum = 1;
    const int TOTAL_FRAMES = 13144;

    while (running && frameNum <= TOTAL_FRAMES) {
        while (XPending(dpy)) {
            XEvent e; XNextEvent(dpy, &e);
            if (e.type == KeyPress || e.type == DestroyNotify) running = false;
        }

        char framePath[256];
        snprintf(framePath, sizeof(framePath),
            "/data/data/com.termux/files/home/prism/bad_apple_native/frame_%04d.ppm", frameNum);

        int fw, fh;
        unsigned char* rgb = readPPM(framePath, &fw, &fh);
        if (!rgb) { frameNum++; continue; }

        void* mapped;
        vkMapMemory(device, stagingMem, 0, bufSize, 0, &mapped);
        unsigned char* dst = (unsigned char*)mapped;
        for (int i = 0; i < fw * fh; i++) {
            dst[i*4+0] = rgb[i*3+2];
            dst[i*4+1] = rgb[i*3+1];
            dst[i*4+2] = rgb[i*3+0];
            dst[i*4+3] = 255;
        }
        vkUnmapMemory(device, stagingMem);
        free(rgb);

        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &fence);

        uint32_t imgIdx;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvail, VK_NULL_HANDLE, &imgIdx);

        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);

        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.image = imgs[imgIdx];
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {(uint32_t)fw, (uint32_t)fh, 1};
        vkCmdCopyBufferToImage(cmd, stagingBuf, imgs[imgIdx],
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = 0;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        vkEndCommandBuffer(cmd);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.waitSemaphoreCount = 1;   si.pWaitSemaphores = &imageAvail;
        si.pWaitDstStageMask = &waitStage;
        si.commandBufferCount = 1;   si.pCommandBuffers = &cmd;
        si.signalSemaphoreCount = 1; si.pSignalSemaphores = &renderDone;
        vkQueueSubmit(queue, 1, &si, fence);

        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &renderDone;
        pi.swapchainCount = 1;     pi.pSwapchains = &swapchain;
        pi.pImageIndices = &imgIdx;
        vkQueuePresentKHR(queue, &pi);

        frameNum++;
        usleep(16666);
    }

    vkDeviceWaitIdle(device);
    vkDestroyBuffer(device, stagingBuf, nullptr);
    vkFreeMemory(device, stagingMem, nullptr);
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    XCloseDisplay(dpy);
    return 0;
}
