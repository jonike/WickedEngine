#define _CRT_SECURE_NO_WARNINGS
#include "wiGraphicsDevice_Vulkan.h"
#include "wiHelper.h"
#include "ResourceMapping.h"

#include <sstream>
#include <vector>
#include <cstring>
#include <iostream>
#include <set>



#ifdef WICKEDENGINE_BUILD_VULKAN
#pragma comment(lib,"vulkan-1.lib")

namespace wiGraphicsTypes
{

	// Validation layer helpers:
	const std::vector<const char*> validationLayers = {
		"VK_LAYER_LUNARG_standard_validation"
	};
#ifdef NDEBUG
	const bool enableValidationLayers = false;
#else
	const bool enableValidationLayers = true;
#endif
	bool checkValidationLayerSupport() {
		uint32_t layerCount;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

		std::vector<VkLayerProperties> availableLayers(layerCount);
		vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

		for (const char* layerName : validationLayers) {
			bool layerFound = false;

			for (const auto& layerProperties : availableLayers) {
				if (strcmp(layerName, layerProperties.layerName) == 0) {
					layerFound = true;
					break;
				}
			}

			if (!layerFound) {
				return false;
			}
		}

		return true;
	}
	static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
		VkDebugReportFlagsEXT flags,
		VkDebugReportObjectTypeEXT objType,
		uint64_t obj,
		size_t location,
		int32_t code,
		const char* layerPrefix,
		const char* msg,
		void* userData) {

		std::stringstream ss("");
		ss << "[VULKAN validation layer]: " << msg << std::endl;

		std::cerr << ss.str();
		OutputDebugStringA(ss.str().c_str());

		return VK_FALSE;
	}
	VkResult CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback) {
		auto func = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
		if (func != nullptr) {
			return func(instance, pCreateInfo, pAllocator, pCallback);
		}
		else {
			return VK_ERROR_EXTENSION_NOT_PRESENT;
		}
	}
	void DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) {
		auto func = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
		if (func != nullptr) {
			func(instance, callback, pAllocator);
		}
	}

	// Queue families:
	struct QueueFamilyIndices {
		int graphicsFamily = -1;
		int presentFamily = -1;

		bool isComplete() {
			return graphicsFamily >= 0 && presentFamily >= 0;
		}
	};
	QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
		QueueFamilyIndices indices;

		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

		std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

		int i = 0;
		for (const auto& queueFamily : queueFamilies) {
			VkBool32 presentSupport = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
			if (queueFamily.queueCount > 0 && presentSupport) {
				indices.presentFamily = i;
			}

			if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
				indices.graphicsFamily = i;
			}

			if (indices.isComplete()) {
				break;
			}

			i++;
		}

		return indices;
	}

	// Swapchain helpers:
	const std::vector<const char*> deviceExtensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};
	bool checkDeviceExtensionSupport(VkPhysicalDevice device) {
		uint32_t extensionCount;
		vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

		std::vector<VkExtensionProperties> availableExtensions(extensionCount);
		vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

		std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

		for (const auto& extension : availableExtensions) {
			requiredExtensions.erase(extension.extensionName);
		}

		return requiredExtensions.empty();
	}

	struct SwapChainSupportDetails {
		VkSurfaceCapabilitiesKHR capabilities;
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> presentModes;
	};
	SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
		SwapChainSupportDetails details;

		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

		uint32_t formatCount;
		vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

		if (formatCount != 0) {
			details.formats.resize(formatCount);
			vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
		}

		uint32_t presentModeCount;
		vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

		if (presentModeCount != 0) {
			details.presentModes.resize(presentModeCount);
			vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
		}

		return details;
	}
	VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
		if (availableFormats.size() == 1 && availableFormats[0].format == VK_FORMAT_UNDEFINED) {
			return { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
		}

		for (const auto& availableFormat : availableFormats) {
			if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
				return availableFormat;
			}
		}

		return availableFormats[0];
	}
	VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR> availablePresentModes) {
		VkPresentModeKHR bestMode = VK_PRESENT_MODE_FIFO_KHR;

		for (const auto& availablePresentMode : availablePresentModes) {
			if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
				return availablePresentMode;
			}
			else if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
				bestMode = availablePresentMode;
			}
		}

		return bestMode;
	}

	// Device selection helpers:
	bool isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface) {
		QueueFamilyIndices indices = findQueueFamilies(device, surface);

		bool extensionsSupported = checkDeviceExtensionSupport(device);

		bool swapChainAdequate = false;
		if (extensionsSupported) {
			SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device, surface);
			swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
		}

		return indices.isComplete() && extensionsSupported && swapChainAdequate;
	}



	// Converters:
	inline VkFormat _ConvertFormat(FORMAT value)
	{
		// _TYPELESS is converted to _UINT or _FLOAT or _UNORM in that order depending on availability!
		// X channel is converted to regular missing channel (eg. FORMAT_B8G8R8X8_UNORM -> VK_FORMAT_B8G8R8A8_UNORM)
		switch (value)
		{
		case FORMAT_UNKNOWN:
			return VK_FORMAT_UNDEFINED;
			break;
		case FORMAT_R32G32B32A32_TYPELESS:
			return VK_FORMAT_R32G32B32A32_UINT;
			break;
		case FORMAT_R32G32B32A32_FLOAT:
			return VK_FORMAT_R32G32B32A32_SFLOAT;
			break;
		case FORMAT_R32G32B32A32_UINT:
			return VK_FORMAT_R32G32B32A32_UINT;
			break;
		case FORMAT_R32G32B32A32_SINT:
			return VK_FORMAT_R32G32B32A32_SINT;
			break;
		case FORMAT_R32G32B32_TYPELESS:
			return VK_FORMAT_R32G32B32_UINT;
			break;
		case FORMAT_R32G32B32_FLOAT:
			return VK_FORMAT_R32G32B32_SFLOAT;
			break;
		case FORMAT_R32G32B32_UINT:
			return VK_FORMAT_R32G32B32_UINT;
			break;
		case FORMAT_R32G32B32_SINT:
			return VK_FORMAT_R32G32B32_SINT;
			break;
		case FORMAT_R16G16B16A16_TYPELESS:
			return VK_FORMAT_R16G16B16A16_UINT;
			break;
		case FORMAT_R16G16B16A16_FLOAT:
			return VK_FORMAT_R16G16B16A16_SFLOAT;
			break;
		case FORMAT_R16G16B16A16_UNORM:
			return VK_FORMAT_R16G16B16A16_UNORM;
			break;
		case FORMAT_R16G16B16A16_UINT:
			return VK_FORMAT_R16G16B16A16_UINT;
			break;
		case FORMAT_R16G16B16A16_SNORM:
			return VK_FORMAT_R16G16B16A16_SNORM;
			break;
		case FORMAT_R16G16B16A16_SINT:
			return VK_FORMAT_R16G16B16A16_SINT;
			break;
		case FORMAT_R32G32_TYPELESS:
			return VK_FORMAT_R32G32_UINT;
			break;
		case FORMAT_R32G32_FLOAT:
			return VK_FORMAT_R32G32_SFLOAT;
			break;
		case FORMAT_R32G32_UINT:
			return VK_FORMAT_R32G32_UINT;
			break;
		case FORMAT_R32G32_SINT:
			return VK_FORMAT_R32G32_SINT;
			break;
		case FORMAT_R32G8X24_TYPELESS:
			return VK_FORMAT_R32G32_UINT; // possible mismatch!
			break;
		case FORMAT_D32_FLOAT_S8X24_UINT:
			return VK_FORMAT_D32_SFLOAT_S8_UINT;
			break;
		case FORMAT_R32_FLOAT_X8X24_TYPELESS:
			return VK_FORMAT_R32G32_SFLOAT; // possible mismatch!
			break;
		case FORMAT_X32_TYPELESS_G8X24_UINT:
			return VK_FORMAT_R32G32_UINT; // possible mismatch!
			break;
		case FORMAT_R10G10B10A2_TYPELESS:
			return VK_FORMAT_A2B10G10R10_UINT_PACK32;
			break;
		case FORMAT_R10G10B10A2_UNORM:
			return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
			break;
		case FORMAT_R10G10B10A2_UINT:
			return VK_FORMAT_A2B10G10R10_UINT_PACK32;
			break;
		case FORMAT_R11G11B10_FLOAT:
			return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
			break;
		case FORMAT_R8G8B8A8_TYPELESS:
			return VK_FORMAT_R8G8B8A8_UINT;
			break;
		case FORMAT_R8G8B8A8_UNORM:
			return VK_FORMAT_R8G8B8A8_UNORM;
			break;
		case FORMAT_R8G8B8A8_UNORM_SRGB:
			return VK_FORMAT_R8G8B8A8_SRGB;
			break;
		case FORMAT_R8G8B8A8_UINT:
			return VK_FORMAT_R8G8B8A8_UINT;
			break;
		case FORMAT_R8G8B8A8_SNORM:
			return VK_FORMAT_R8G8B8A8_SNORM;
			break;
		case FORMAT_R8G8B8A8_SINT:
			return VK_FORMAT_R8G8B8A8_SINT;
			break;
		case FORMAT_R16G16_TYPELESS:
			return VK_FORMAT_R16G16_UINT;
			break;
		case FORMAT_R16G16_FLOAT:
			return VK_FORMAT_R16G16_SFLOAT;
			break;
		case FORMAT_R16G16_UNORM:
			return VK_FORMAT_R16G16_UNORM;
			break;
		case FORMAT_R16G16_UINT:
			return VK_FORMAT_R16G16_UINT;
			break;
		case FORMAT_R16G16_SNORM:
			return VK_FORMAT_R16G16_SNORM;
			break;
		case FORMAT_R16G16_SINT:
			return VK_FORMAT_R16G16_SINT;
			break;
		case FORMAT_R32_TYPELESS:
			return VK_FORMAT_R32_UINT;
			break;
		case FORMAT_D32_FLOAT:
			return VK_FORMAT_D32_SFLOAT;
			break;
		case FORMAT_R32_FLOAT:
			return VK_FORMAT_R32_SFLOAT;
			break;
		case FORMAT_R32_UINT:
			return VK_FORMAT_R32_UINT;
			break;
		case FORMAT_R32_SINT:
			return VK_FORMAT_R32_SINT;
			break;
		case FORMAT_R24G8_TYPELESS:
			return VK_FORMAT_R32_UINT;
			break;
		case FORMAT_D24_UNORM_S8_UINT:
			return VK_FORMAT_D24_UNORM_S8_UINT;
			break;
		case FORMAT_R24_UNORM_X8_TYPELESS:
			return VK_FORMAT_D24_UNORM_S8_UINT;
			break;
		case FORMAT_X24_TYPELESS_G8_UINT:
			return VK_FORMAT_D24_UNORM_S8_UINT;
			break;
		case FORMAT_R8G8_TYPELESS:
			return VK_FORMAT_R8G8_UINT;
			break;
		case FORMAT_R8G8_UNORM:
			return VK_FORMAT_R8G8_UNORM;
			break;
		case FORMAT_R8G8_UINT:
			return VK_FORMAT_R8G8_UINT;
			break;
		case FORMAT_R8G8_SNORM:
			return VK_FORMAT_R8G8_SNORM;
			break;
		case FORMAT_R8G8_SINT:
			return VK_FORMAT_R8G8_SINT;
			break;
		case FORMAT_R16_TYPELESS:
			return VK_FORMAT_R16_SFLOAT;
			break;
		case FORMAT_R16_FLOAT:
			return VK_FORMAT_R16_SFLOAT;
			break;
		case FORMAT_D16_UNORM:
			return VK_FORMAT_D16_UNORM;
			break;
		case FORMAT_R16_UNORM:
			return VK_FORMAT_R16_UNORM;
			break;
		case FORMAT_R16_UINT:
			return VK_FORMAT_R16_UINT;
			break;
		case FORMAT_R16_SNORM:
			return VK_FORMAT_R16_SNORM;
			break;
		case FORMAT_R16_SINT:
			return VK_FORMAT_R16_SINT;
			break;
		case FORMAT_R8_TYPELESS:
			return VK_FORMAT_R8_UINT;
			break;
		case FORMAT_R8_UNORM:
			return VK_FORMAT_R8_UNORM;
			break;
		case FORMAT_R8_UINT:
			return VK_FORMAT_R8_UINT;
			break;
		case FORMAT_R8_SNORM:
			return VK_FORMAT_R8_SNORM;
			break;
		case FORMAT_R8_SINT:
			return VK_FORMAT_R8_SINT;
			break;
		case FORMAT_A8_UNORM:
			return VK_FORMAT_R8_UNORM; // mismatch!
			break;
		case FORMAT_R1_UNORM:
			return VK_FORMAT_R8_UNORM; // mismatch!
			break;
		case FORMAT_R9G9B9E5_SHAREDEXP:
			return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32; // maybe ok
			break;
		case FORMAT_R8G8_B8G8_UNORM:
			return VK_FORMAT_R8G8B8A8_UNORM; // mismatch
			break;
		case FORMAT_G8R8_G8B8_UNORM:
			return VK_FORMAT_R8G8B8A8_UNORM; // mismatch
			break;
		case FORMAT_BC1_TYPELESS:
			return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
			break;
		case FORMAT_BC1_UNORM:
			return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
			break;
		case FORMAT_BC1_UNORM_SRGB:
			return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
			break;
		case FORMAT_BC2_TYPELESS:
			return VK_FORMAT_BC2_UNORM_BLOCK;
			break;
		case FORMAT_BC2_UNORM:
			return VK_FORMAT_BC2_UNORM_BLOCK;
			break;
		case FORMAT_BC2_UNORM_SRGB:
			return VK_FORMAT_BC2_SRGB_BLOCK;
			break;
		case FORMAT_BC3_TYPELESS:
			return VK_FORMAT_BC3_UNORM_BLOCK;
			break;
		case FORMAT_BC3_UNORM:
			return VK_FORMAT_BC3_UNORM_BLOCK;
			break;
		case FORMAT_BC3_UNORM_SRGB:
			return VK_FORMAT_BC3_SRGB_BLOCK;
			break;
		case FORMAT_BC4_TYPELESS:
			return VK_FORMAT_BC4_UNORM_BLOCK;
			break;
		case FORMAT_BC4_UNORM:
			return VK_FORMAT_BC4_UNORM_BLOCK;
			break;
		case FORMAT_BC4_SNORM:
			return VK_FORMAT_BC4_SNORM_BLOCK;
			break;
		case FORMAT_BC5_TYPELESS:
			return VK_FORMAT_BC5_UNORM_BLOCK;
			break;
		case FORMAT_BC5_UNORM:
			return VK_FORMAT_BC5_UNORM_BLOCK;
			break;
		case FORMAT_BC5_SNORM:
			return VK_FORMAT_BC5_SNORM_BLOCK;
			break;
		case FORMAT_B5G6R5_UNORM:
			return VK_FORMAT_B5G6R5_UNORM_PACK16;
			break;
		case FORMAT_B5G5R5A1_UNORM:
			return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
			break;
		case FORMAT_B8G8R8A8_UNORM:
			return VK_FORMAT_B8G8R8A8_UNORM;
			break;
		case FORMAT_B8G8R8X8_UNORM:
			return VK_FORMAT_B8G8R8A8_UNORM;
			break;
		case FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
			return VK_FORMAT_B10G11R11_UFLOAT_PACK32; // mismatch
			break;
		case FORMAT_B8G8R8A8_TYPELESS:
			return VK_FORMAT_B8G8R8A8_UINT;
			break;
		case FORMAT_B8G8R8A8_UNORM_SRGB:
			return VK_FORMAT_B8G8R8A8_SRGB;
			break;
		case FORMAT_B8G8R8X8_TYPELESS:
			return VK_FORMAT_B8G8R8A8_UINT;
			break;
		case FORMAT_B8G8R8X8_UNORM_SRGB:
			return VK_FORMAT_B8G8R8A8_SRGB;
			break;
		case FORMAT_BC6H_TYPELESS:
			return VK_FORMAT_BC6H_SFLOAT_BLOCK;
			break;
		case FORMAT_BC6H_UF16:
			return VK_FORMAT_BC6H_UFLOAT_BLOCK;
			break;
		case FORMAT_BC6H_SF16:
			return VK_FORMAT_BC6H_SFLOAT_BLOCK;
			break;
		case FORMAT_BC7_TYPELESS:
			return VK_FORMAT_BC7_UNORM_BLOCK; // maybe mismatch
			break;
		case FORMAT_BC7_UNORM:
			return VK_FORMAT_BC7_UNORM_BLOCK;
			break;
		case FORMAT_BC7_UNORM_SRGB:
			return VK_FORMAT_BC7_SRGB_BLOCK;
			break;
		case FORMAT_B4G4R4A4_UNORM:
			return VK_FORMAT_B4G4R4A4_UNORM_PACK16;
			break;
		default:
			break;
		}
		return VK_FORMAT_UNDEFINED;
	}


	// Engine functions
	GraphicsDevice_Vulkan::GraphicsDevice_Vulkan(wiWindowRegistration::window_type window, bool fullscreen) : GraphicsDevice()
	{
		FULLSCREEN = fullscreen;

		RECT rect = RECT();
		GetClientRect(window, &rect);
		SCREENWIDTH = rect.right - rect.left;
		SCREENHEIGHT = rect.bottom - rect.top;



		// Fill out application info:
		VkApplicationInfo appInfo = {};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "Wicked Engine Application";
		appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.pEngineName = "Wicked Engine";
		appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.apiVersion = VK_API_VERSION_1_0;

		// Enumerate available extensions:
		uint32_t extensionCount = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
		std::vector<VkExtensionProperties> extensions(extensionCount);
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());
		std::vector<const char*> extensionNames;
		for (auto& x : extensions)
		{
			extensionNames.push_back(x.extensionName);
		}

		if (enableValidationLayers && !checkValidationLayerSupport()) {
			throw std::runtime_error("validation layers requested, but not available!");
		}

		// Create instance:
		{
			VkInstanceCreateInfo createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
			createInfo.pApplicationInfo = &appInfo;
			createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
			createInfo.ppEnabledExtensionNames = extensionNames.data();
			createInfo.enabledLayerCount = 0;
			if (enableValidationLayers)
			{
				createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
				createInfo.ppEnabledLayerNames = validationLayers.data();
			}
			if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
				throw std::runtime_error("failed to create instance!");
			}
		}

		// Register validation layer callback:
		if (enableValidationLayers)
		{
			VkDebugReportCallbackCreateInfoEXT createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
			createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
			createInfo.pfnCallback = debugCallback;
			if (CreateDebugReportCallbackEXT(instance, &createInfo, nullptr, &callback) != VK_SUCCESS) {
				throw std::runtime_error("failed to set up debug callback!");
			}
		}


		// Surface creation:
		{
#ifdef _WIN32
			VkWin32SurfaceCreateInfoKHR createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
			createInfo.hwnd = window;
			createInfo.hinstance = GetModuleHandle(nullptr);

			auto CreateWin32SurfaceKHR = (PFN_vkCreateWin32SurfaceKHR)vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR");

			if (!CreateWin32SurfaceKHR || CreateWin32SurfaceKHR(instance, &createInfo, nullptr, &surface) != VK_SUCCESS) {
				throw std::runtime_error("failed to create window surface!");
			}
#else
#error WICKEDENGINE VULKAN DEVICE ERROR: PLATFORM NOT SUPPORTED
#endif // WIN32
		}


		// Enumerating and creating devices:
		{
			uint32_t deviceCount = 0;
			vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

			if (deviceCount == 0) {
				throw std::runtime_error("failed to find GPUs with Vulkan support!");
			}

			std::vector<VkPhysicalDevice> devices(deviceCount);
			vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

			for (const auto& device : devices) {
				if (isDeviceSuitable(device, surface)) {
					physicalDevice = device;
					break;
				}
			}

			if (physicalDevice == VK_NULL_HANDLE) {
				throw std::runtime_error("failed to find a suitable GPU!");
			}

			QueueFamilyIndices indices = findQueueFamilies(physicalDevice, surface);

			std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
			std::set<int> uniqueQueueFamilies = { indices.graphicsFamily, indices.presentFamily };

			float queuePriority = 1.0f;
			for (int queueFamily : uniqueQueueFamilies) {
				VkDeviceQueueCreateInfo queueCreateInfo = {};
				queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queueCreateInfo.queueFamilyIndex = queueFamily;
				queueCreateInfo.queueCount = 1;
				queueCreateInfo.pQueuePriorities = &queuePriority;
				queueCreateInfos.push_back(queueCreateInfo);
			}

			VkPhysicalDeviceFeatures deviceFeatures = {};

			VkDeviceCreateInfo createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

			createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
			createInfo.pQueueCreateInfos = queueCreateInfos.data();

			createInfo.pEnabledFeatures = &deviceFeatures;

			createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
			createInfo.ppEnabledExtensionNames = deviceExtensions.data();

			if (enableValidationLayers) {
				createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
				createInfo.ppEnabledLayerNames = validationLayers.data();
			}
			else {
				createInfo.enabledLayerCount = 0;
			}

			if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
				throw std::runtime_error("failed to create logical device!");
			}

			vkGetDeviceQueue(device, indices.graphicsFamily, 0, &graphicsQueue);
			vkGetDeviceQueue(device, indices.presentFamily, 0, &presentQueue);
		}


		// Create default pipeline:
		{
			VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
			pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutInfo.setLayoutCount = 0;
			pipelineLayoutInfo.pushConstantRangeCount = 0;

			if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &defaultPipelineLayout) != VK_SUCCESS) {
				throw std::runtime_error("failed to create pipeline layout!");
			}
		}


		// Set up swap chain:
		{
			SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice, surface);

			VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
			VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);

			swapChainExtent = { static_cast<uint32_t>(SCREENWIDTH), static_cast<uint32_t>(SCREENHEIGHT) };
			swapChainExtent.width = max(swapChainSupport.capabilities.minImageExtent.width, min(swapChainSupport.capabilities.maxImageExtent.width, swapChainExtent.width));
			swapChainExtent.height = max(swapChainSupport.capabilities.minImageExtent.height, min(swapChainSupport.capabilities.maxImageExtent.height, swapChainExtent.height));

			uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
			if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
				imageCount = swapChainSupport.capabilities.maxImageCount;
			}

			VkSwapchainCreateInfoKHR createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
			createInfo.surface = surface;
			createInfo.minImageCount = imageCount;
			createInfo.imageFormat = surfaceFormat.format;
			createInfo.imageColorSpace = surfaceFormat.colorSpace;
			createInfo.imageExtent = swapChainExtent;
			createInfo.imageArrayLayers = 1;
			createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

			QueueFamilyIndices indices = findQueueFamilies(physicalDevice, surface);
			uint32_t queueFamilyIndices[] = { (uint32_t)indices.graphicsFamily, (uint32_t)indices.presentFamily };

			if (indices.graphicsFamily != indices.presentFamily) {
				createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
				createInfo.queueFamilyIndexCount = 2;
				createInfo.pQueueFamilyIndices = queueFamilyIndices;
			}
			else {
				createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
				createInfo.queueFamilyIndexCount = 0; // Optional
				createInfo.pQueueFamilyIndices = nullptr; // Optional
			}

			createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
			createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
			createInfo.presentMode = presentMode;
			createInfo.clipped = VK_TRUE;
			createInfo.oldSwapchain = VK_NULL_HANDLE;

			if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
				throw std::runtime_error("failed to create swap chain!");
			}

			vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
			swapChainImages.resize(imageCount);
			vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());

			swapChainImageFormat = surfaceFormat.format;


			swapChainImageViews.resize(swapChainImages.size());
			for (size_t i = 0; i < swapChainImages.size(); i++) 
			{
				VkImageViewCreateInfo createInfo = {};
				createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				createInfo.image = swapChainImages[i];
				createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
				createInfo.format = swapChainImageFormat;
				createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
				createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
				createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
				createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
				createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				createInfo.subresourceRange.baseMipLevel = 0;
				createInfo.subresourceRange.levelCount = 1;
				createInfo.subresourceRange.baseArrayLayer = 0;
				createInfo.subresourceRange.layerCount = 1;

				if (vkCreateImageView(device, &createInfo, nullptr, &swapChainImageViews[i]) != VK_SUCCESS) {
					throw std::runtime_error("failed to create image views!");
				}
			}
		}




		// Create default render pass:
		{
			VkAttachmentDescription colorAttachment = {};
			colorAttachment.format = swapChainImageFormat;
			colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

			VkAttachmentReference colorAttachmentRef = {};
			colorAttachmentRef.attachment = 0;
			colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass = {};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &colorAttachmentRef;

			VkRenderPassCreateInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.attachmentCount = 1;
			renderPassInfo.pAttachments = &colorAttachment;
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;

			if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &defaultRenderPass) != VK_SUCCESS) {
				throw std::runtime_error("failed to create render pass!");
			}
		}

		// Create swapchain render targets:
		{
			swapChainFramebuffers.resize(swapChainImageViews.size());
			for (size_t i = 0; i < swapChainImageViews.size(); i++) {
				VkImageView attachments[] = {
					swapChainImageViews[i]
				};

				VkFramebufferCreateInfo framebufferInfo = {};
				framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				framebufferInfo.renderPass = defaultRenderPass;
				framebufferInfo.attachmentCount = 1;
				framebufferInfo.pAttachments = attachments;
				framebufferInfo.width = swapChainExtent.width;
				framebufferInfo.height = swapChainExtent.height;
				framebufferInfo.layers = 1;

				if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
					throw std::runtime_error("failed to create framebuffer!");
				}
			}
		}


	}
	GraphicsDevice_Vulkan::~GraphicsDevice_Vulkan()
	{
		vkDestroyPipelineLayout(device, defaultPipelineLayout, nullptr);
		vkDestroyRenderPass(device, defaultRenderPass, nullptr);	
		for (auto framebuffer : swapChainFramebuffers) {
			vkDestroyFramebuffer(device, framebuffer, nullptr);
		}
		for (auto imageView : swapChainImageViews) {
			vkDestroyImageView(device, imageView, nullptr);
		}
		vkDestroySwapchainKHR(device, swapChain, nullptr);
		vkDestroyDevice(device, nullptr);
		DestroyDebugReportCallbackEXT(instance, callback, nullptr);
		vkDestroyInstance(instance, nullptr);
	}

	void GraphicsDevice_Vulkan::SetResolution(int width, int height)
	{
		if (width != SCREENWIDTH || height != SCREENHEIGHT)
		{
			SCREENWIDTH = width;
			SCREENHEIGHT = height;
			//swapChain->ResizeBuffers(2, width, height, _ConvertFormat(GetBackBufferFormat()), 0);
			RESOLUTIONCHANGED = true;
		}
	}

	Texture2D GraphicsDevice_Vulkan::GetBackBuffer()
	{
		return Texture2D();
	}

	HRESULT GraphicsDevice_Vulkan::CreateBuffer(const GPUBufferDesc *pDesc, const SubresourceData* pInitialData, GPUBuffer *ppBuffer)
	{
		return E_FAIL;
	}
	HRESULT GraphicsDevice_Vulkan::CreateTexture1D(const Texture1DDesc* pDesc, const SubresourceData *pInitialData, Texture1D **ppTexture1D)
	{
		return E_FAIL;
	}
	HRESULT GraphicsDevice_Vulkan::CreateTexture2D(const Texture2DDesc* pDesc, const SubresourceData *pInitialData, Texture2D **ppTexture2D)
	{
		return E_FAIL;
	}
	HRESULT GraphicsDevice_Vulkan::CreateTexture3D(const Texture3DDesc* pDesc, const SubresourceData *pInitialData, Texture3D **ppTexture3D)
	{
		return E_FAIL;
	}
	HRESULT GraphicsDevice_Vulkan::CreateInputLayout(const VertexLayoutDesc *pInputElementDescs, UINT NumElements,
		const void *pShaderBytecodeWithInputSignature, SIZE_T BytecodeLength, VertexLayout *pInputLayout)
	{
		return E_FAIL;
	}
	HRESULT GraphicsDevice_Vulkan::CreateVertexShader(const void *pShaderBytecode, SIZE_T BytecodeLength, VertexShader *pVertexShader)
	{
		return E_FAIL;
	}
	HRESULT GraphicsDevice_Vulkan::CreatePixelShader(const void *pShaderBytecode, SIZE_T BytecodeLength, PixelShader *pPixelShader)
	{
		return E_FAIL;
	}
	HRESULT GraphicsDevice_Vulkan::CreateGeometryShader(const void *pShaderBytecode, SIZE_T BytecodeLength, GeometryShader *pGeometryShader)
	{
		return E_FAIL;
	}
	HRESULT GraphicsDevice_Vulkan::CreateHullShader(const void *pShaderBytecode, SIZE_T BytecodeLength, HullShader *pHullShader)
	{
		return E_FAIL;
	}
	HRESULT GraphicsDevice_Vulkan::CreateDomainShader(const void *pShaderBytecode, SIZE_T BytecodeLength, DomainShader *pDomainShader)
	{
		return E_FAIL;
	}
	HRESULT GraphicsDevice_Vulkan::CreateComputeShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ComputeShader *pComputeShader)
	{
		return E_FAIL;
	}
	HRESULT GraphicsDevice_Vulkan::CreateBlendState(const BlendStateDesc *pBlendStateDesc, BlendState *pBlendState)
	{
		return E_FAIL;
	}
	HRESULT GraphicsDevice_Vulkan::CreateDepthStencilState(const DepthStencilStateDesc *pDepthStencilStateDesc, DepthStencilState *pDepthStencilState)
	{
		return E_FAIL;
	}
	HRESULT GraphicsDevice_Vulkan::CreateRasterizerState(const RasterizerStateDesc *pRasterizerStateDesc, RasterizerState *pRasterizerState)
	{
		return E_FAIL;
	}
	HRESULT GraphicsDevice_Vulkan::CreateSamplerState(const SamplerDesc *pSamplerDesc, Sampler *pSamplerState)
	{
		return E_FAIL;
	}
	HRESULT GraphicsDevice_Vulkan::CreateQuery(const GPUQueryDesc *pDesc, GPUQuery *pQuery)
	{
		return E_FAIL;
	}
	HRESULT GraphicsDevice_Vulkan::CreateGraphicsPSO(const GraphicsPSODesc* pDesc, GraphicsPSO* pso)
	{
		pso->desc = *pDesc;

		pso->renderPass_Vulkan = new VkRenderPass;
		pso->pipeline_Vulkan = new VkPipeline;


		std::vector<VkAttachmentDescription> attachments;
		std::vector<VkAttachmentReference> colorAttachmentRefs;

		attachments.reserve(pDesc->numRTs);
		colorAttachmentRefs.reserve(pDesc->numRTs);

		for (UINT i = 0; i < pDesc->numRTs; ++i)
		{
			VkAttachmentDescription attachment = {};
			attachment.format = _ConvertFormat(pDesc->RTFormats[i]);
			attachment.samples = VK_SAMPLE_COUNT_1_BIT;
			attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
			attachments.push_back(attachment);

			VkAttachmentReference ref = {};
			ref.attachment = i;
			ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			colorAttachmentRefs.push_back(ref);
		}


		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = pDesc->numRTs;
		subpass.pColorAttachments = colorAttachmentRefs.data();

		if (pDesc->DSFormat != FORMAT_UNKNOWN && pDesc->dss != nullptr)
		{
			const bool depth_read_only = pDesc->dss->GetDesc().DepthWriteMask == DEPTH_WRITE_MASK_ZERO;
			const bool stencil_read_only = pDesc->dss->GetDesc().StencilWriteMask == 0;
			const bool all_read_only = depth_read_only && stencil_read_only;

			VkAttachmentDescription attachment = {};
			attachment.format = _ConvertFormat(pDesc->DSFormat);
			attachment.samples = VK_SAMPLE_COUNT_1_BIT;
			attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // hmmm...
			attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // hmmm...
			attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
			attachments.push_back(attachment);

			VkAttachmentReference depthAttachmentRef = {};
			depthAttachmentRef.attachment = static_cast<uint32_t>(attachments.size() - 1);
			depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			//if (all_read_only)
			//{
			//	depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
			//}
			//else
			//{
			//	if (depth_read_only)
			//	{
			//		depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL_KHR;
			//	}
			//	else if (stencil_read_only)
			//	{
			//		depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL_KHR;
			//	}
			//	else
			//	{
			//		depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			//	}
			//}

			subpass.pDepthStencilAttachment = &depthAttachmentRef;
		}

		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;

		if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &defaultRenderPass) != VK_SUCCESS) {
			throw std::runtime_error("failed to create render pass!");
		}



		VkGraphicsPipelineCreateInfo pipelineInfo = {};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.layout = defaultPipelineLayout;
		pipelineInfo.renderPass = *static_cast<VkRenderPass*>(pso->renderPass_Vulkan);
		pipelineInfo.subpass = 0;
		pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

		// TODO...
		//pipelineInfo.stageCount;

		if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, static_cast<VkPipeline*>(pso->pipeline_Vulkan)) != VK_SUCCESS) {
			throw std::runtime_error("failed to create graphics pipeline!");
		}

		return S_OK;
	}
	HRESULT GraphicsDevice_Vulkan::CreateComputePSO(const ComputePSODesc* pDesc, ComputePSO* pso)
	{
		pso->pipeline_Vulkan = new VkPipeline;

		VkComputePipelineCreateInfo pipelineInfo = {};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipelineInfo.layout = defaultPipelineLayout;
		pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

		if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, static_cast<VkPipeline*>(pso->pipeline_Vulkan)) != VK_SUCCESS) {
			throw std::runtime_error("failed to create graphics pipeline!");
		}

		return S_OK;
	}


	void GraphicsDevice_Vulkan::PresentBegin()
	{
	}
	void GraphicsDevice_Vulkan::PresentEnd()
	{
	}

	void GraphicsDevice_Vulkan::ExecuteDeferredContexts()
	{
	}
	void GraphicsDevice_Vulkan::FinishCommandList(GRAPHICSTHREAD threadID)
	{
	}


	void GraphicsDevice_Vulkan::BindViewports(UINT NumViewports, const ViewPort *pViewports, GRAPHICSTHREAD threadID)
	{
		//assert(NumViewports <= 6);
		//VkViewport viewports[6];
		//for (UINT i = 0; i < NumViewports; ++i)
		//{
		//	viewports[i].x = pViewports[i].TopLeftX;
		//	viewports[i].y = pViewports[i].TopLeftY;
		//	viewports[i].width = pViewports[i].Width;
		//	viewports[i].height = pViewports[i].Height;
		//	viewports[i].minDepth = pViewports[i].MinDepth;
		//	viewports[i].maxDepth = pViewports[i].MaxDepth;
		//}
		//vkCmdSetViewport(commandBuffers[threadID], 0, NumViewports, viewports);
	}
	void GraphicsDevice_Vulkan::BindRenderTargetsUAVs(UINT NumViews, Texture* const *ppRenderTargets, Texture2D* depthStencilTexture, GPUResource* const *ppUAVs, int slotUAV, int countUAV,
		GRAPHICSTHREAD threadID, int arrayIndex)
	{
	}
	void GraphicsDevice_Vulkan::BindRenderTargets(UINT NumViews, Texture* const *ppRenderTargets, Texture2D* depthStencilTexture, GRAPHICSTHREAD threadID, int arrayIndex)
	{
	}
	void GraphicsDevice_Vulkan::ClearRenderTarget(Texture* pTexture, const FLOAT ColorRGBA[4], GRAPHICSTHREAD threadID, int arrayIndex)
	{
	}
	void GraphicsDevice_Vulkan::ClearDepthStencil(Texture2D* pTexture, UINT ClearFlags, FLOAT Depth, UINT8 Stencil, GRAPHICSTHREAD threadID, int arrayIndex)
	{
	}
	void GraphicsDevice_Vulkan::BindResource(SHADERSTAGE stage, GPUResource* resource, int slot, GRAPHICSTHREAD threadID, int arrayIndex)
	{
	}
	void GraphicsDevice_Vulkan::BindResources(SHADERSTAGE stage, GPUResource *const* resources, int slot, int count, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::BindUnorderedAccessResource(SHADERSTAGE stage, GPUResource* resource, int slot, GRAPHICSTHREAD threadID, int arrayIndex)
	{
	}
	void GraphicsDevice_Vulkan::BindUnorderedAccessResources(SHADERSTAGE stage, GPUResource *const* resources, int slot, int count, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::BindUnorderedAccessResourceCS(GPUResource* resource, int slot, GRAPHICSTHREAD threadID, int arrayIndex)
	{
	}
	void GraphicsDevice_Vulkan::BindUnorderedAccessResourcesCS(GPUResource *const* resources, int slot, int count, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::UnBindResources(int slot, int num, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::UnBindUnorderedAccessResources(int slot, int num, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::BindSampler(SHADERSTAGE stage, Sampler* sampler, int slot, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::BindConstantBuffer(SHADERSTAGE stage, GPUBuffer* buffer, int slot, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::BindVertexBuffers(GPUBuffer* const *vertexBuffers, int slot, int count, const UINT* strides, const UINT* offsets, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::BindIndexBuffer(GPUBuffer* indexBuffer, const INDEXBUFFER_FORMAT format, UINT offset, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::BindPrimitiveTopology(PRIMITIVETOPOLOGY type, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::BindStencilRef(UINT value, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::BindBlendFactor(XMFLOAT4 value, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::BindGraphicsPSO(GraphicsPSO* pso, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::BindComputePSO(ComputePSO* pso, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::Draw(int vertexCount, UINT startVertexLocation, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::DrawIndexed(int indexCount, UINT startIndexLocation, UINT baseVertexLocation, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::DrawInstanced(int vertexCount, int instanceCount, UINT startVertexLocation, UINT startInstanceLocation, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::DrawIndexedInstanced(int indexCount, int instanceCount, UINT startIndexLocation, UINT baseVertexLocation, UINT startInstanceLocation, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::DrawInstancedIndirect(GPUBuffer* args, UINT args_offset, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::DrawIndexedInstancedIndirect(GPUBuffer* args, UINT args_offset, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::Dispatch(UINT threadGroupCountX, UINT threadGroupCountY, UINT threadGroupCountZ, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::DispatchIndirect(GPUBuffer* args, UINT args_offset, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::GenerateMips(Texture* texture, GRAPHICSTHREAD threadID, int arrayIndex)
	{
	}
	void GraphicsDevice_Vulkan::CopyTexture2D(Texture2D* pDst, Texture2D* pSrc, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::CopyTexture2D_Region(Texture2D* pDst, UINT dstMip, UINT dstX, UINT dstY, Texture2D* pSrc, UINT srcMip, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::MSAAResolve(Texture2D* pDst, Texture2D* pSrc, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::UpdateBuffer(GPUBuffer* buffer, const void* data, GRAPHICSTHREAD threadID, int dataSize)
	{
	}
	void* GraphicsDevice_Vulkan::AllocateFromRingBuffer(GPURingBuffer* buffer, size_t dataSize, UINT& offsetIntoBuffer, GRAPHICSTHREAD threadID)
	{
		offsetIntoBuffer = 0;
		return nullptr;
	}
	void GraphicsDevice_Vulkan::InvalidateBufferAccess(GPUBuffer* buffer, GRAPHICSTHREAD threadID)
	{
	}
	bool GraphicsDevice_Vulkan::DownloadBuffer(GPUBuffer* bufferToDownload, GPUBuffer* bufferDest, void* dataDest, GRAPHICSTHREAD threadID)
	{
		return false;
	}
	void GraphicsDevice_Vulkan::SetScissorRects(UINT numRects, const Rect* rects, GRAPHICSTHREAD threadID)
	{
		//assert(rects != nullptr);
		//assert(numRects <= 8);
		//VkRect2D scissors[8];
		//for (UINT i = 0; i < numRects; ++i)
		//{
		//	scissors[i].extent.width = abs(rects[i].right - rects[i].left);
		//	scissors[i].extent.height = abs(rects[i].bottom - rects[i].top);
		//	scissors[i].offset.x = rects[i].left;
		//	scissors[i].offset.y = rects[i].bottom;
		//}
		//vkCmdSetScissor(commandBuffers[threadID], 0, numRects, scissors);
	}

	void GraphicsDevice_Vulkan::QueryBegin(GPUQuery *query, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::QueryEnd(GPUQuery *query, GRAPHICSTHREAD threadID)
	{
	}
	bool GraphicsDevice_Vulkan::QueryRead(GPUQuery *query, GRAPHICSTHREAD threadID)
	{
		return true;
	}

	void GraphicsDevice_Vulkan::UAVBarrier(GPUResource *const* uavs, UINT NumBarriers, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::TransitionBarrier(GPUResource *const* resources, UINT NumBarriers, RESOURCE_STATES stateBefore, RESOURCE_STATES stateAfter, GRAPHICSTHREAD threadID)
	{
	}


	HRESULT GraphicsDevice_Vulkan::CreateTextureFromFile(const std::string& fileName, Texture2D **ppTexture, bool mipMaps, GRAPHICSTHREAD threadID)
	{
		return E_FAIL;
	}
	HRESULT GraphicsDevice_Vulkan::SaveTexturePNG(const std::string& fileName, Texture2D *pTexture, GRAPHICSTHREAD threadID)
	{
		return E_FAIL;
	}
	HRESULT GraphicsDevice_Vulkan::SaveTextureDDS(const std::string& fileName, Texture *pTexture, GRAPHICSTHREAD threadID)
	{
		return E_FAIL;
	}

	void GraphicsDevice_Vulkan::EventBegin(const std::string& name, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::EventEnd(GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::SetMarker(const std::string& name, GRAPHICSTHREAD threadID)
	{
	}

}


#endif // WICKEDENGINE_BUILD_VULKAN
