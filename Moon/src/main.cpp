#include "mnpch.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

const std::vector<const char*> validationLayers = 
{
    "VK_LAYER_KHRONOS_validation"
};
const std::vector<const char*> deviceExtensions = 
{
    "VK_KHR_swapchain"
};

#ifdef MN_DEBUG
const bool enableValidationLayers = true;
#else
const bool enableValidationLayers = false;
#endif

#define VK_CHECK(x)                                                    \
	do                                                                 \
	{                                                                  \
		VkResult err = x;                                              \
		if (err)                                                       \
		{                                                              \
			std::cout <<"Detected Vulkan error: " << err << std::endl; \
			abort();                                                   \
		}                                                              \
	} while (0)

VkResult CreateInstance(VkInstance& instance)
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Moon Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Moon Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    VkInstanceCreateInfo instanceCI = {};
    instanceCI.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCI.pApplicationInfo = &appInfo;
    instanceCI.enabledLayerCount = u32(validationLayers.size());
    instanceCI.ppEnabledLayerNames = validationLayers.data();
    instanceCI.enabledExtensionCount = glfwExtensionCount;
    instanceCI.ppEnabledExtensionNames = glfwExtensions;
    return vkCreateInstance(&instanceCI, nullptr, &instance);
}

struct QueueFamilyIndices
{
    u32 graphicsFamily;
};

QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) 
    {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) 
        {
            indices.graphicsFamily = i;
        }
        i++;
    }

    return indices;
}

VkResult CreateDevice(VkInstance& instance, VkDevice& device)
{
    VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) throw std::exception("No Physical Device");
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    
    VkPhysicalDeviceFeatures mDeviceFeatures = {};
    for (const auto& device : devices)
    {
        VkPhysicalDeviceProperties deviceProperties;
        VkPhysicalDeviceFeatures deviceFeatures;
        vkGetPhysicalDeviceProperties(device, &deviceProperties);
        vkGetPhysicalDeviceFeatures(device, &deviceFeatures);
        if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            mPhysicalDevice = device;
            mDeviceFeatures = deviceFeatures;
            break;
        }
    }

    float queuePriority = 1.0f;
    QueueFamilyIndices indices = findQueueFamilies(mPhysicalDevice);
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = indices.graphicsFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceCI = {};
    deviceCI.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCI.pEnabledFeatures = &mDeviceFeatures;
    deviceCI.enabledExtensionCount = u32(deviceExtensions.size());
    deviceCI.ppEnabledExtensionNames = deviceExtensions.data();
    deviceCI.enabledLayerCount = 0;
    deviceCI.ppEnabledLayerNames = nullptr;
    deviceCI.queueCreateInfoCount = 1;
    deviceCI.pQueueCreateInfos = &queueCreateInfo;
    return vkCreateDevice(mPhysicalDevice, &deviceCI, nullptr, &device);
}

int main()
{
    glfwInit();
    auto* window = glfwCreateWindow(1920, 1080, "Moon", NULL, NULL);

    VkInstance mInstance = VK_NULL_HANDLE;
    VK_CHECK(CreateInstance(mInstance));

    VkDevice mDevice = VK_NULL_HANDLE;
    VK_CHECK(CreateDevice(mInstance, mDevice));

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
    }

    vkDestroyDevice(mDevice, nullptr);
    vkDestroyInstance(mInstance, nullptr);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}