#pragma once

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>
#include <shaderc/shaderc.hpp>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

typedef struct RendererConfig {
	bool vsync;
	bool debugMessenger;
	int width;
	int height;
} RendererConfig;

class Renderer
{
public:
	Renderer(const RendererConfig& config);
	void Initialize();
	void Draw();
	~Renderer();
protected:
	void constructSwapChain();
	vk::UniqueShaderModule createShaderFromFile(std::string path, shaderc_shader_kind shaderKind);

	vk::DynamicLoader dl;
	vk::UniqueInstance instance;
	vk::UniqueDebugUtilsMessengerEXT debugMessenger;
	vk::PhysicalDevice physicalDev;
	vk::UniqueDevice device;
	vk::Queue graphicsQueue;
	vk::Queue transferQueue;
	vk::Queue presentQueue;
	vk::SurfaceKHR surface;
	vk::UniqueSwapchainKHR swapchain;
	vk::Format swapImageFormat;
	vk::Extent2D swapExtent;
	std::vector<vk::Image> swapChainImages;
	std::vector<vk::UniqueFramebuffer> swapChainFrameBuffers;
	std::vector<vk::UniqueImageView> swapImageViews;
	vk::UniqueRenderPass renderPass;
	vk::UniquePipelineLayout pipelineLayout;
	unsigned int queueIndices[3] = {
		0xFFFFFFFF,	// graphics
		0xFFFFFFFF,	// transfer
		0xFFFFFFFF	// present
	};
	vk::UniquePipelineCache pipelineCache;
	vk::UniquePipeline graphicsPipeline;
	vk::UniqueCommandPool commandPool;
	std::vector<vk::UniqueCommandBuffer> commandBuffers;
	vk::UniqueSemaphore imageAvailableSemaphore;
	vk::UniqueSemaphore renderFinishedSemaphore;

	bool vsync;
	bool debugMessengerEnabled;

	struct {
		int width;
		int height;
		GLFWwindow* window;
	} m_window;

	glm::vec3 pos = glm::vec3(0.0, 0.0, 0.0);
	glm::vec3 front = glm::vec3(0.0, 0.0, -1.0);
	glm::vec3 up = glm::vec3(0.0, 1.0, 0.0);
	glm::mat4 view = glm::lookAt(pos, front, up);
};

