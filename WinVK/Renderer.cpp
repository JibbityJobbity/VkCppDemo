#include "Renderer.hpp"

#include <iostream>
#include <fstream>
#include <sstream>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

vk::UniqueShaderModule Renderer::createShaderFromFile(std::string path, shaderc_shader_kind shaderKind) {
	std::ifstream file;
	file.open(path, std::ios::in);
	if (!file) {
		throw std::runtime_error("Couldn't open file: " + path);
	}
	std::stringstream fileContents;
	fileContents << file.rdbuf();
	file.close();

	shaderc::Compiler compiler;
	shaderc::CompileOptions options;

	options.SetOptimizationLevel(shaderc_optimization_level_performance);

	shaderc::SpvCompilationResult spvCompilationResult = compiler.CompileGlslToSpv(
		fileContents.str(), shaderKind, "shader_src", options
	);

	std::vector<uint32_t> spirv { spvCompilationResult.cbegin(), spvCompilationResult.cend() };

	vk::ShaderModuleCreateInfo createInfo;
	createInfo.setCodeSize(spirv.size() * sizeof(uint32_t));
	createInfo.setPCode(spirv.data());

	vk::UniqueShaderModule shaderModule = device->createShaderModuleUnique(createInfo);
	return shaderModule;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData) {
	std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
	return VK_FALSE;
}

Renderer::Renderer(const RendererConfig& config) {
	swapImageFormat = vk::Format::eUndefined;
	m_window.width = config.width;
	m_window.height = config.height;

	vsync = config.vsync;
	debugMessengerEnabled = config.debugMessenger;

	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	m_window.window = glfwCreateWindow(config.width, config.height, "Renderer Window", nullptr, nullptr);
}

void Renderer::Initialize() {
	PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
	VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

	// Instance creation
	{
		vk::ApplicationInfo appInfo(
			"Application",
			VK_MAKE_VERSION(0, 1, 0),
			"Engine",
			VK_MAKE_VERSION(0, 1, 0),
			VK_VERSION_1_2
		);

		//auto iExt = vk::enumerateInstanceExtensionProperties();
		std::vector<const char*> iExt{
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME
		};
		uint32_t nGlfwExt = 0;
		const char** glfwExt = glfwGetRequiredInstanceExtensions(&nGlfwExt);
		for (int i = 0; i < nGlfwExt; i++)
			iExt.push_back(glfwExt[i]);

		vk::InstanceCreateInfo instanceInfo;
		instanceInfo.setEnabledExtensionCount(iExt.size());
		instanceInfo.setPpEnabledExtensionNames(iExt.data());
		instanceInfo.setEnabledLayerCount(0);
		instanceInfo.setPApplicationInfo(&appInfo);

		instance = vk::createInstanceUnique(instanceInfo);
		VULKAN_HPP_DEFAULT_DISPATCHER.init(instance.get());
	}

	// Set debug callback
	if (debugMessengerEnabled) {
		debugMessenger = instance->createDebugUtilsMessengerEXTUnique(
			vk::DebugUtilsMessengerCreateInfoEXT{ {},
				vk::DebugUtilsMessageSeverityFlagBitsEXT::eError | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
					vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose,
				vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
					vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
				debugCallback }
		);
	}

	// Device creation
	{
		std::vector<vk::PhysicalDevice> devices = instance->enumeratePhysicalDevices();
		if (devices.size() == 0) {
			std::cerr << "Couldn't find any devices" << std::endl;
		}
		physicalDev = devices[0];
		vk::PhysicalDeviceProperties devProps;
		for (auto& dev : devices) {
			devProps = dev.getProperties();
			if (devProps.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
				physicalDev = dev;
				break;
			}
		}
		std::cout << "Using: " << devProps.deviceName << std::endl;

		// Obtain surface
		VkSurfaceKHR tempSurface;
		glfwCreateWindowSurface(static_cast<VkInstance>(instance.get()), m_window.window, nullptr, &tempSurface);
		surface = vk::SurfaceKHR(tempSurface);

		std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
		auto queueFamilyProps = physicalDev.getQueueFamilyProperties();
		for (int i = 0; i < queueFamilyProps.size(); i++) {
			if (queueFamilyProps[i].queueFlags & vk::QueueFlagBits::eGraphics && queueIndices[0] == 0xFFFFFFFF)
				queueIndices[0] = i;
			if (queueFamilyProps[i].queueFlags & vk::QueueFlagBits::eTransfer && queueIndices[1] == 0xFFFFFFFF)
				queueIndices[1] = i;
			if (physicalDev.getSurfaceSupportKHR(i, surface) && queueIndices[2] == 0xFFFFFFFF)
				queueIndices[2] = i;
		}
		vk::DeviceQueueCreateInfo queueCreateInfo;
		float priority = 1.0f;
		queueCreateInfo.setPQueuePriorities(&priority);
		queueCreateInfo.setQueueCount(1);
		queueCreateInfo.setQueueFamilyIndex(queueIndices[0]);
		queueCreateInfos.push_back(queueCreateInfo);
		queueCreateInfo.setQueueFamilyIndex(queueIndices[1]);
		queueCreateInfos.push_back(queueCreateInfo);
		queueCreateInfo.setQueueFamilyIndex(queueIndices[2]);
		queueCreateInfos.push_back(queueCreateInfo);

		std::vector<const char*> dExt = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		};

		vk::DeviceCreateInfo devCreateInfo;
		devCreateInfo.setPpEnabledExtensionNames(dExt.data());
		devCreateInfo.setEnabledExtensionCount(static_cast<uint32_t>(dExt.size()));
		devCreateInfo.setEnabledLayerCount(0);
		devCreateInfo.setPEnabledFeatures(&physicalDev.getFeatures());
		devCreateInfo.setPQueueCreateInfos(queueCreateInfos.data());
		devCreateInfo.setQueueCreateInfoCount(static_cast<uint32_t>(queueCreateInfos.size()));

		device = physicalDev.createDeviceUnique(devCreateInfo);
		graphicsQueue = device->getQueue(queueIndices[0], 0);
		transferQueue = device->getQueue(queueIndices[1], 0);
		presentQueue = device->getQueue(queueIndices[2], 0);
		//VULKAN_HPP_DEFAULT_DISPATCHER.init(device.get());
	}

	swapchain = {};
	constructSwapChain();

	// Create render pass
	{
		vk::AttachmentDescription colorAttachment;
		colorAttachment.setFormat(swapImageFormat);
		colorAttachment.setSamples(vk::SampleCountFlagBits::e1);
		colorAttachment.setLoadOp(vk::AttachmentLoadOp::eClear);
		colorAttachment.setStoreOp(vk::AttachmentStoreOp::eStore);
		colorAttachment.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare);
		colorAttachment.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare);
		colorAttachment.setInitialLayout(vk::ImageLayout::eUndefined);
		colorAttachment.setFinalLayout(vk::ImageLayout::ePresentSrcKHR);

		vk::AttachmentReference colorAttachmentRef;
		colorAttachmentRef.setAttachment(0);
		colorAttachmentRef.setLayout(vk::ImageLayout::eColorAttachmentOptimal);

		vk::SubpassDescription subpass;
		subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics);
		subpass.setColorAttachmentCount(1);
		subpass.setPColorAttachments(&colorAttachmentRef);

		vk::RenderPassCreateInfo renderPassInfo;
		renderPassInfo.setAttachmentCount(1);
		renderPassInfo.setPAttachments(&colorAttachment);
		renderPassInfo.setSubpassCount(1);
		renderPassInfo.setPSubpasses(&subpass);

		renderPass = device->createRenderPassUnique(renderPassInfo);
	}

	// Create pipeline
	{
		vk::UniqueShaderModule vertexShader = createShaderFromFile("vert.glsl", shaderc_glsl_vertex_shader);
		vk::UniqueShaderModule fragmentShader = createShaderFromFile("frag.glsl", shaderc_glsl_fragment_shader);

		vk::PipelineShaderStageCreateInfo vertShaderInfo;
		vertShaderInfo.setStage(vk::ShaderStageFlagBits::eVertex);
		vertShaderInfo.setModule(*vertexShader);
		vertShaderInfo.setPName("main");

		vk::PipelineShaderStageCreateInfo fragShaderInfo;
		fragShaderInfo.setStage(vk::ShaderStageFlagBits::eFragment);
		fragShaderInfo.setModule(*fragmentShader);
		fragShaderInfo.setPName("main");

		vk::PipelineShaderStageCreateInfo shaderStages[] = {
			vertShaderInfo, fragShaderInfo
		};

		vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.setVertexBindingDescriptionCount(0);
		vertexInputInfo.setVertexAttributeDescriptionCount(0);

		vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
		inputAssembly.setPrimitiveRestartEnable(VK_FALSE);
		inputAssembly.setTopology(vk::PrimitiveTopology::eTriangleList);

		vk::Viewport viewport;
		viewport.setHeight((float)swapExtent.height);
		viewport.setWidth((float)swapExtent.width);
		viewport.setX(0.0f);
		viewport.setY(0.0f);
		viewport.setMinDepth(0.0f);
		viewport.setMaxDepth(1.0f);

		vk::Rect2D scissor;
		scissor.setOffset({ 0, 0 });
		scissor.setExtent(swapExtent);

		vk::PipelineViewportStateCreateInfo viewportState;
		viewportState.setViewportCount(1);
		viewportState.setPViewports(&viewport);

		vk::PipelineRasterizationStateCreateInfo rasterInfo;
		rasterInfo.setCullMode(vk::CullModeFlagBits::eBack);
		rasterInfo.setFrontFace(vk::FrontFace::eClockwise);
		rasterInfo.setDepthClampEnable(VK_FALSE);
		rasterInfo.setRasterizerDiscardEnable(VK_FALSE);
		rasterInfo.setPolygonMode(vk::PolygonMode::eFill);
		rasterInfo.setLineWidth(1.0f);
		rasterInfo.setDepthBiasEnable(VK_FALSE);

		vk::PipelineMultisampleStateCreateInfo multisampling;
		multisampling.setSampleShadingEnable(VK_FALSE);
		multisampling.setRasterizationSamples(vk::SampleCountFlagBits::e1);

		vk::PipelineColorBlendAttachmentState colorBlendAttachment;
		colorBlendAttachment.setColorWriteMask(
			vk::ColorComponentFlagBits::eR |
			vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB |
			vk::ColorComponentFlagBits::eA
		);
		colorBlendAttachment.setBlendEnable(VK_FALSE);

		vk::PipelineColorBlendStateCreateInfo colorBlending;
		colorBlending.setLogicOpEnable(VK_FALSE);
		colorBlending.setAttachmentCount(1);
		colorBlending.setPAttachments(&colorBlendAttachment);

		vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
		pipelineLayout = device->createPipelineLayoutUnique(pipelineLayoutInfo);

		vk::GraphicsPipelineCreateInfo pipelineInfo;
		pipelineInfo.setStageCount(2);
		pipelineInfo.setPStages(shaderStages);
		pipelineInfo.setPVertexInputState(&vertexInputInfo);
		pipelineInfo.setPInputAssemblyState(&inputAssembly);
		pipelineInfo.setPViewportState(&viewportState);
		pipelineInfo.setPRasterizationState(&rasterInfo);
		pipelineInfo.setPMultisampleState(&multisampling);
		pipelineInfo.setPDepthStencilState(nullptr);
		pipelineInfo.setPColorBlendState(&colorBlending);
		pipelineInfo.setPDynamicState(nullptr);
		pipelineInfo.setLayout(*pipelineLayout);
		pipelineInfo.setRenderPass(*renderPass);
		pipelineInfo.setSubpass(0);

		vk::PipelineCacheCreateInfo cacheCreateInfo;
		cacheCreateInfo.setInitialDataSize(0);
		pipelineCache = device->createPipelineCacheUnique(cacheCreateInfo);

		graphicsPipeline = device->createGraphicsPipelineUnique(*pipelineCache, pipelineInfo);
	}
}

void Renderer::constructSwapChain() {
	auto capabilities = physicalDev.getSurfaceCapabilitiesKHR(surface);
	auto surfaceFormats = physicalDev.getSurfaceFormatsKHR(surface);
	auto presentModes = physicalDev.getSurfacePresentModesKHR(surface);


	vk::SurfaceFormatKHR format = surfaceFormats[0];
	for (const auto& f: surfaceFormats) {
		if (f.format == vk::Format::eB8G8R8A8Srgb && f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
			format = f;
			swapImageFormat = f.format;
			break;
		}
	}

	vk::PresentModeKHR presentMode = vsync ?
		vk::PresentModeKHR::eFifo :
		vk::PresentModeKHR::eImmediate;

	// Extent
	if (capabilities.currentExtent.width != 0xFFFFFFFF) {
		swapExtent = capabilities.currentExtent;
	}
	else {
		swapExtent = ( 
			std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, static_cast<uint32_t>(m_window.width))),
			std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, static_cast<uint32_t>(m_window.height)))
		);
	}

	uint32_t imageCount = capabilities.minImageCount + 1;
	if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
		imageCount = std::min(imageCount, capabilities.maxImageCount);
	}

	vk::SwapchainCreateInfoKHR swapCreateInfo{};
	swapCreateInfo.setSurface(surface);
	swapCreateInfo.setMinImageCount(imageCount);
	swapCreateInfo.setImageFormat(format.format);
	swapCreateInfo.setImageColorSpace(format.colorSpace);
	swapCreateInfo.setImageExtent(swapExtent);
	swapCreateInfo.setImageArrayLayers(1);
	swapCreateInfo.setImageUsage(vk::ImageUsageFlagBits::eColorAttachment);

	if (queueIndices[0] != queueIndices[2]) {
		swapCreateInfo.setImageSharingMode(vk::SharingMode::eConcurrent);
		swapCreateInfo.setQueueFamilyIndexCount(3);
		swapCreateInfo.setPQueueFamilyIndices(queueIndices);
	}
	else {
		swapCreateInfo.setImageSharingMode(vk::SharingMode::eExclusive);
	}
	swapCreateInfo.setPreTransform(capabilities.currentTransform);
	swapCreateInfo.setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque);
	swapCreateInfo.setPresentMode(presentMode);
	swapCreateInfo.setClipped(VK_TRUE);

	swapCreateInfo.setOldSwapchain(swapchain.get());

	swapchain = device->createSwapchainKHRUnique(swapCreateInfo);

	swapChainImages = device->getSwapchainImagesKHR(swapchain.get());
	swapImageViews.reserve(swapChainImages.size());
	for (int i = 0; i < swapChainImages.size(); i++) {
		vk::ImageViewCreateInfo ci{};
		ci.setComponents({
			vk::ComponentSwizzle::eIdentity,
			vk::ComponentSwizzle::eIdentity,
			vk::ComponentSwizzle::eIdentity,
			vk::ComponentSwizzle::eIdentity
			});
		ci.setFormat(swapImageFormat);
		ci.setImage(swapChainImages[i]);
		vk::ImageSubresourceRange isr{};
		isr.setAspectMask(vk::ImageAspectFlagBits::eColor);
		isr.setBaseMipLevel(0);
		isr.setLevelCount(1);
		isr.setBaseArrayLayer(0);
		isr.setLayerCount(1);
		ci.setSubresourceRange(isr);
		swapImageViews.push_back(device->createImageViewUnique(ci));
	}
}

void Renderer::Draw() {
	while (!glfwWindowShouldClose(m_window.window)) {
		glfwPollEvents();
	}
}

Renderer::~Renderer() {
	//vkDestroySurfaceKHR(instance.get(), surface, nullptr);
	glfwDestroyWindow(m_window.window);
	glfwTerminate();
}