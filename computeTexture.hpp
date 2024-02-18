#include <vulkan/vulkan.h>

#include "computeDeviceUtils.hpp"

namespace sourav
{
    namespace Texture
    {
        class computeTexture
        {
            public:
                VkImage               image;
                VkImageLayout         imageLayout;
                VkDeviceMemory        deviceMemory;
                VkImageView           view;
                uint32_t              width, height;
                uint32_t              mipLevels;
                uint32_t              layerCount;
                VkDescriptorImageInfo descriptor;
                VkSampler             sampler;

                void fromBuffer(
                    VkPhysicalDevice   physicalDevice,
                    VkDevice           logicalDevice,
                    VkCommandPool      pool,
                    void *             buffer,
                    VkDeviceSize       bufferSize,
                    VkFormat           format,
                    uint32_t           texWidth,
                    uint32_t           texHeight,
                    VkQueue            copyQueue,
                    VkFilter           filter          = VK_FILTER_LINEAR,
                    VkImageUsageFlags  imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT,
                    VkImageLayout      imageLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        };

        void computeTexture::fromBuffer(
                VkPhysicalDevice   physicalDevice,
                VkDevice           logicalDevice,
                VkCommandPool      pool,
                void *             buffer,
                VkDeviceSize       bufferSize,
                VkFormat           format,
                uint32_t           texWidth,
                uint32_t           texHeight,
                VkQueue            copyQueue,
                VkFilter           filter,
                VkImageUsageFlags  imageUsageFlags,
                VkImageLayout      imageLayout)
        {
            assert(buffer);

            width = texWidth;
            height = texHeight;
            mipLevels = 1;

            VkMemoryAllocateInfo memAllocInfo = sourav::initializers::memoryAllocateInfo();
		    VkMemoryRequirements memReqs;

            // Use a separate command buffer for texture loading
		    VkCommandBuffer copyCmd = sourav::utils::createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, pool, logicalDevice);

            // Create a host-visible staging buffer that contains the raw image data
            VkBuffer stagingBuffer;
            VkDeviceMemory stagingMemory;

            VkBufferCreateInfo bufferCreateInfo = sourav::initializers::bufferCreateInfo();
            bufferCreateInfo.size = bufferSize;
            
            // This buffer is used as a transfer source for the buffer copy
            bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            ST_CHECK_RESULT(vkCreateBuffer(logicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));

            // Get memory requirements for the staging buffer (alignment, memory type bits)
		    vkGetBufferMemoryRequirements(logicalDevice, stagingBuffer, &memReqs);

            memAllocInfo.allocationSize = memReqs.size;

            // Get memory type index for a host visible buffer
		    memAllocInfo.memoryTypeIndex = sourav::utils::getMemoryType(physicalDevice, memReqs.memoryTypeBits,         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, nullptr);

            ST_CHECK_RESULT(vkAllocateMemory(logicalDevice, &memAllocInfo, nullptr, &stagingMemory));
		    ST_CHECK_RESULT(vkBindBufferMemory(logicalDevice, stagingBuffer, stagingMemory, 0));

            // Copy texture data into staging buffer
            uint8_t *data;
            ST_CHECK_RESULT(vkMapMemory(logicalDevice, stagingMemory, 0, memReqs.size, 0, (void **)&data));
            memcpy(data, buffer, bufferSize);
            vkUnmapMemory(logicalDevice, stagingMemory);

            VkBufferImageCopy bufferCopyRegion = {};
            bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            bufferCopyRegion.imageSubresource.mipLevel = 0;
            bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
            bufferCopyRegion.imageSubresource.layerCount = 1;
            bufferCopyRegion.imageExtent.width = width;
            bufferCopyRegion.imageExtent.height = height;
            bufferCopyRegion.imageExtent.depth = 1;
            bufferCopyRegion.bufferOffset = 0;

            // Create optimal tiled target image
            VkImageCreateInfo imageCreateInfo = sourav::initializers::imageCreateInfo();
            imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
            imageCreateInfo.format = format;
            imageCreateInfo.mipLevels = mipLevels;
            imageCreateInfo.arrayLayers = 1;
            imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageCreateInfo.extent = { width, height, 1 };
            imageCreateInfo.usage = imageUsageFlags;

            // Ensure that the TRANSFER_DST bit is set for staging
            if (!(imageCreateInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
            {
                imageCreateInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            }

            ST_CHECK_RESULT(vkCreateImage(logicalDevice, &imageCreateInfo, nullptr, &image));

            vkGetImageMemoryRequirements(logicalDevice, image, &memReqs);

            memAllocInfo.allocationSize = memReqs.size;

            memAllocInfo.memoryTypeIndex = sourav::utils::getMemoryType(physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, nullptr);

            ST_CHECK_RESULT(vkAllocateMemory(logicalDevice, &memAllocInfo, nullptr, &deviceMemory));
		    ST_CHECK_RESULT(vkBindImageMemory(logicalDevice, image, deviceMemory, 0));

            VkImageSubresourceRange subresourceRange = {};
            subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            subresourceRange.baseMipLevel = 0;
            subresourceRange.levelCount = mipLevels;
            subresourceRange.layerCount = 1;

            // Image barrier for optimal image (target)
		    // Optimal image will be used as destination for the copy
            sourav::utils::setImageLayout(
                copyCmd,
                image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                subresourceRange);

            // Copy mip levels from staging buffer
            vkCmdCopyBufferToImage(
                copyCmd,
                stagingBuffer,
                image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &bufferCopyRegion
            );

            // Change texture image layout to shader read after all mip levels have been copied
		    this->imageLayout = imageLayout;

            sourav::utils::setImageLayout(
                copyCmd,
                image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                imageLayout,
                subresourceRange);

            sourav::utils::flushCommandBuffer(logicalDevice, copyCmd, copyQueue, pool);

            // Clean up staging resources
            vkDestroyBuffer(logicalDevice, stagingBuffer, nullptr);
            vkFreeMemory(logicalDevice, stagingMemory, nullptr);

            // Create sampler
            VkSamplerCreateInfo samplerCreateInfo = {};
            samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerCreateInfo.magFilter = filter;
            samplerCreateInfo.minFilter = filter;
            samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerCreateInfo.mipLodBias = 0.0f;
            samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
            samplerCreateInfo.minLod = 0.0f;
            samplerCreateInfo.maxLod = 0.0f;
            samplerCreateInfo.maxAnisotropy = 1.0f;
            ST_CHECK_RESULT(vkCreateSampler(logicalDevice, &samplerCreateInfo, nullptr, &sampler));

            // Create image view
            VkImageViewCreateInfo viewCreateInfo = {};
            viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewCreateInfo.pNext = NULL;
            viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewCreateInfo.format = format;
            viewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            viewCreateInfo.subresourceRange.levelCount = 1;
            viewCreateInfo.image = image;
            ST_CHECK_RESULT(vkCreateImageView(logicalDevice, &viewCreateInfo, nullptr, &view));

            descriptor.sampler = sampler;
            descriptor.imageView = view;
            descriptor.imageLayout = imageLayout;
        }
    }
}