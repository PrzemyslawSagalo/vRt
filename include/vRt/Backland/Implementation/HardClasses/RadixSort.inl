#pragma once

//#include "../../vRt_subimpl.inl"
#include "../Utils.hpp"

namespace _vt {
    using namespace vrt;

    // planned add hardcodes for radix sorting
    VtResult createRadixSort(std::shared_ptr<Device> _vtDevice, VtArtificalDeviceExtension vtExtension, std::shared_ptr<RadixSort>& vtRadix) {
        //constexpr const auto STEPS = VRT_USE_MORTON_32 ? 4ull : 8ull, WG_COUNT = 64ull, RADICE_AFFINE = 256ull; // 8-bit
        constexpr const auto STEPS = VRT_USE_MORTON_32 ? 8ull : 16ull, WG_COUNT = 64ull, RADICE_AFFINE = 16ull; // QLC
        //constexpr const auto STEPS = VRT_USE_MORTON_32 ? 16ull : 32ull, WG_COUNT = 64ull, RADICE_AFFINE = 4ull; // MLC


        VtResult result = VK_SUCCESS;

        //auto vtRadix = (_vtRadix = std::make_shared<RadixSort>());
        auto vkDevice = _vtDevice->_device;
        auto vkPipelineCache = _vtDevice->_pipelineCache;
        vtRadix = std::make_shared<RadixSort>();
        vtRadix->_device = _vtDevice;

        const auto vendorName = _vtDevice->_vendorName;



        std::shared_ptr<BufferManager> bManager = {};
        createBufferManager(_vtDevice, bManager);

        VtDeviceBufferCreateInfo bfic = {};
        bfic.usageFlag = VkBufferUsageFlags(vk::BufferUsageFlagBits::eStorageBuffer);

        VtBufferRegionCreateInfo bfi = {};
        {
            bfi.format = VK_FORMAT_R32_UINT;
            bfi.bufferSize = vtExtension.maxPrimitives * sizeof(uint64_t);
            createBufferRegion(bManager, bfi, vtRadix->_tmpValuesBuffer);

            bfi.format = VK_FORMAT_R32G32_UINT;
            bfi.bufferSize = vtExtension.maxPrimitives * sizeof(uint64_t);
            createBufferRegion(bManager, bfi, vtRadix->_tmpKeysBuffer);

            bfi.format = VK_FORMAT_UNDEFINED;
            bfi.bufferSize = 16ull * STEPS;
            createBufferRegion(bManager, bfi, vtRadix->_stepsBuffer); // unused

            bfi.format = VK_FORMAT_R32_UINT;
            bfi.bufferSize = RADICE_AFFINE * WG_COUNT * sizeof(uint32_t);
            createBufferRegion(bManager, bfi, vtRadix->_histogramBuffer);
            createBufferRegion(bManager, bfi, vtRadix->_prefixSumBuffer);
        }

        { // build final shared buffer for this class
            createSharedBuffer(bManager, bfic, vtRadix->_sharedBuffer);
        }


        std::vector<vk::PushConstantRange> constRanges = {
            vk::PushConstantRange(vk::ShaderStageFlagBits::eCompute, 0u, strided<uint32_t>(2))
        };

        std::vector<vk::DescriptorSetLayout> dsLayouts = {
            vk::DescriptorSetLayout(_vtDevice->_descriptorLayoutMap["radixSort"]),
            vk::DescriptorSetLayout(_vtDevice->_descriptorLayoutMap["radixSortBind"])
        };

        vtRadix->_pipelineLayout = vk::Device(vkDevice).createPipelineLayout(vk::PipelineLayoutCreateInfo({}, dsLayouts.size(), dsLayouts.data(), constRanges.size(), constRanges.data()));
        vtRadix->_histogramPipeline = createComputeHC(vkDevice, getCorrectPath(qradix::histogram, vendorName, _vtDevice->_shadersPath), vtRadix->_pipelineLayout, vkPipelineCache);
        vtRadix->_workPrefixPipeline = createComputeHC(vkDevice, getCorrectPath(qradix::workPrefix, vendorName, _vtDevice->_shadersPath), vtRadix->_pipelineLayout, vkPipelineCache);
        vtRadix->_permutePipeline = createComputeHC(vkDevice, getCorrectPath(qradix::permute, vendorName, _vtDevice->_shadersPath), vtRadix->_pipelineLayout, vkPipelineCache);
        vtRadix->_copyhackPipeline = createComputeHC(vkDevice, getCorrectPath(qradix::copyhack, vendorName, _vtDevice->_shadersPath), vtRadix->_pipelineLayout, vkPipelineCache);

        const auto&& dsc = vk::Device(vkDevice).allocateDescriptorSets(vk::DescriptorSetAllocateInfo().setDescriptorPool(_vtDevice->_descriptorPool).setPSetLayouts(&dsLayouts[0]).setDescriptorSetCount(1));
        vtRadix->_descriptorSet = std::move(dsc[0]);

        // write radix sort descriptor sets
        const auto writeTmpl = vk::WriteDescriptorSet(vtRadix->_descriptorSet, 0, 0, 1, vk::DescriptorType::eStorageBuffer);
        std::vector<vk::WriteDescriptorSet> writes = {
            vk::WriteDescriptorSet(writeTmpl).setDstBinding(0).setPBufferInfo((vk::DescriptorBufferInfo*)&vtRadix->_tmpKeysBuffer->_descriptorInfo()),
            vk::WriteDescriptorSet(writeTmpl).setDstBinding(1).setPBufferInfo((vk::DescriptorBufferInfo*)&vtRadix->_tmpValuesBuffer->_descriptorInfo()),
            vk::WriteDescriptorSet(writeTmpl).setDstBinding(2).setPBufferInfo((vk::DescriptorBufferInfo*)&vtRadix->_stepsBuffer->_descriptorInfo()), //unused
            vk::WriteDescriptorSet(writeTmpl).setDstBinding(3).setPBufferInfo((vk::DescriptorBufferInfo*)&vtRadix->_histogramBuffer->_descriptorInfo()),
            vk::WriteDescriptorSet(writeTmpl).setDstBinding(4).setPBufferInfo((vk::DescriptorBufferInfo*)&vtRadix->_prefixSumBuffer->_descriptorInfo()),
        };
        vk::Device(vkDevice).updateDescriptorSets(writes, {});

        return result;
    };

};
