#include <vulkan/vulkan.h>

#include "computeInitializers.hpp"

namespace sourav
{
    namespace utils
    {
        std::string errorString(VkResult errorCode)
		{
			switch (errorCode)
			{
                #define STR(r) case VK_ ##r: return #r
                    STR(NOT_READY);
                    STR(TIMEOUT);
                    STR(EVENT_SET);
                    STR(EVENT_RESET);
                    STR(INCOMPLETE);
                    STR(ERROR_OUT_OF_HOST_MEMORY);
                    STR(ERROR_OUT_OF_DEVICE_MEMORY);
                    STR(ERROR_INITIALIZATION_FAILED);
                    STR(ERROR_DEVICE_LOST);
                    STR(ERROR_MEMORY_MAP_FAILED);
                    STR(ERROR_LAYER_NOT_PRESENT);
                    STR(ERROR_EXTENSION_NOT_PRESENT);
                    STR(ERROR_FEATURE_NOT_PRESENT);
                    STR(ERROR_INCOMPATIBLE_DRIVER);
                    STR(ERROR_TOO_MANY_OBJECTS);
                    STR(ERROR_FORMAT_NOT_SUPPORTED);
                    STR(ERROR_SURFACE_LOST_KHR);
                    STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
                    STR(SUBOPTIMAL_KHR);
                    STR(ERROR_OUT_OF_DATE_KHR);
                    STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
                    STR(ERROR_VALIDATION_FAILED_EXT);
                    STR(ERROR_INVALID_SHADER_NV);
                #undef STR
                default:
                    return "UNKNOWN_ERROR";
			}
		}

        
        VkCommandBuffer createCommandBuffer(VkCommandBufferLevel level, VkCommandPool pool, VkDevice logicalDevice)
        {
            VkCommandBufferAllocateInfo cmdBufAllocateInfo = sourav::initializers::commandBufferAllocateInfo(pool, level, 1);
            VkCommandBuffer cmdBuffer;

            ST_CHECK_RESULT(vkAllocateCommandBuffers(logicalDevice, &cmdBufAllocateInfo, &cmdBuffer));

            VkCommandBufferBeginInfo cmdBufInfo = sourav::initializers::commandBufferBeginInfo();
			ST_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));

            return cmdBuffer;
        }


        /******************************************************************************************************************************
        * Get the index of a memory type that has all the requested property bits set
        *
        * @param typeBits Bit mask with bits set for each memory type supported by the resource to request for (from VkMemoryRequirements)
        * @param properties Bit mask of properties for the memory type to request
        * @param (Optional) memTypeFound Pointer to a bool that is set to true if a matching memory type has been found
        * 
        * @return Index of the requested memory type
        *
        * @throw Throws an exception if memTypeFound is null and no memory type could be found that supports the requested properties
        ********************************************************************************************************************************/
        uint32_t getMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeBits, VkMemoryPropertyFlags properties, VkBool32 *memTypeFound)
        {
            /** @brief Memory types and heaps of the physical device */
            VkPhysicalDeviceMemoryProperties memoryProperties;
            vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

            for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
            {
                if ((typeBits & 1) == 1)
                {
                    if ((memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
                    {
                        if (memTypeFound)
                        {
                            *memTypeFound = true;
                        }
                        return i;
                    }
                }
                typeBits >>= 1;
            }

            if (memTypeFound)
            {
                *memTypeFound = false;
                return 0;
            }
            else
            {
                throw std::runtime_error("Could not find a matching memory type");
            }
        }

        
        // Create an image memory barrier for changing the layout of
		// an image and put it into an active command buffer
		// See chapter 11.4 "Image Layout" for details

		void setImageLayout(
			VkCommandBuffer cmdbuffer,
			VkImage image,
			VkImageLayout oldImageLayout,
			VkImageLayout newImageLayout,
			VkImageSubresourceRange subresourceRange,
			VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)
		{
			// Create an image barrier object
			VkImageMemoryBarrier imageMemoryBarrier = sourav::initializers::imageMemoryBarrier();
			imageMemoryBarrier.oldLayout = oldImageLayout;
			imageMemoryBarrier.newLayout = newImageLayout;
			imageMemoryBarrier.image = image;
			imageMemoryBarrier.subresourceRange = subresourceRange;

			// Source layouts (old)
			// Source access mask controls actions that have to be finished on the old layout
			// before it will be transitioned to the new layout
			switch (oldImageLayout)
			{
			case VK_IMAGE_LAYOUT_UNDEFINED:
				// Image layout is undefined (or does not matter)
				// Only valid as initial layout
				// No flags required, listed only for completeness
				imageMemoryBarrier.srcAccessMask = 0;
				break;

			case VK_IMAGE_LAYOUT_PREINITIALIZED:
				// Image is preinitialized
				// Only valid as initial layout for linear images, preserves memory contents
				// Make sure host writes have been finished
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
				break;

			case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
				// Image is a color attachment
				// Make sure any writes to the color buffer have been finished
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				break;

			case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
				// Image is a depth/stencil attachment
				// Make sure any writes to the depth/stencil buffer have been finished
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				break;

			case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
				// Image is a transfer source
				// Make sure any reads from the image have been finished
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				break;

			case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
				// Image is a transfer destination
				// Make sure any writes to the image have been finished
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				break;

			case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
				// Image is read by a shader
				// Make sure any shader reads from the image have been finished
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				break;
			default:
				// Other source layouts aren't handled (yet)
				break;
			}

			// Target layouts (new)
			// Destination access mask controls the dependency for the new image layout
			switch (newImageLayout)
			{
			case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
				// Image will be used as a transfer destination
				// Make sure any writes to the image have been finished
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				break;

			case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
				// Image will be used as a transfer source
				// Make sure any reads from the image have been finished
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				break;

			case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
				// Image will be used as a color attachment
				// Make sure any writes to the color buffer have been finished
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				break;

			case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
				// Image layout will be used as a depth/stencil attachment
				// Make sure any writes to depth/stencil buffer have been finished
				imageMemoryBarrier.dstAccessMask = imageMemoryBarrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				break;

			case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
				// Image will be read in a shader (sampler, input attachment)
				// Make sure any writes to the image have been finished
				if (imageMemoryBarrier.srcAccessMask == 0)
				{
					imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
				}
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				break;
			default:
				// Other source layouts aren't handled (yet)
				break;
			}

			// Put barrier inside setup command buffer
			vkCmdPipelineBarrier(
				cmdbuffer,
				srcStageMask,
				dstStageMask,
				0,
				0, nullptr,
				0, nullptr,
				1, &imageMemoryBarrier);
		}


        void flushCommandBuffer(VkDevice logicalDevice, VkCommandBuffer commandBuffer, VkQueue queue, VkCommandPool pool)
        {
            if (commandBuffer == VK_NULL_HANDLE)
                return;
            
            ST_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

            VkSubmitInfo submitInfo = sourav::initializers::submitInfo();
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;

            // Create fence to ensure that the command buffer has finished executing
            VkFenceCreateInfo fenceInfo = sourav::initializers::fenceCreateInfo(VK_FLAGS_NONE);
            VkFence fence;

            ST_CHECK_RESULT(vkCreateFence(logicalDevice, &fenceInfo, nullptr, &fence));
            
            // Submit to the queue
            ST_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));
            
            // Wait for the fence to signal that command buffer has finished executing
            ST_CHECK_RESULT(vkWaitForFences(logicalDevice, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));
            vkDestroyFence(logicalDevice, fence, nullptr);

            vkFreeCommandBuffers(logicalDevice, pool, 1, &commandBuffer);
        }

    }
}