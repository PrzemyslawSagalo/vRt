#pragma once

//#include "../../vRt_subimpl.inl"
#include "../Utils.hpp"

namespace _vt {
    using namespace vrt;

    // radix sorting command (qRadix)
    VtResult radixSort(const std::shared_ptr<CommandBuffer>& cmdBuf, VkDescriptorSet inputSet, uint32_t primCount = 2) {
        //constexpr const auto STEPS = VRT_USE_MORTON_32 ? 4ull : 8ull, WG_COUNT = 128ull, RADICE_AFFINE = 1ull; // 8-bit (NOT effective in RX Vega)
        //constexpr const auto STEPS = VRT_USE_MORTON_32 ? 8ull : 16ull, WG_COUNT = 128ull, RADICE_AFFINE = 1ull; // QLC
        //constexpr const auto STEPS = VRT_USE_MORTON_32 ? 16ull : 32ull, WG_COUNT = 128ull, RADICE_AFFINE = 1ull; // MLC

        auto device = cmdBuf->_parent();
        auto STEPS = VRT_USE_MORTON_32 ? 8ull : 16ull, WG_COUNT = 64ull, RADICE_AFFINE = 1ull;
        if (device->_vendorName == VT_VENDOR_NV_TURING) { STEPS = VRT_USE_MORTON_32 ? 4ull : 8ull, WG_COUNT = 64ull, RADICE_AFFINE = 1ull; };

        VtResult result = VK_SUCCESS;
        auto radix = device->_radixSort[0];
        std::vector<VkDescriptorSet> _sets = { radix->_descriptorSet, inputSet };
        vkCmdBindDescriptorSets(*cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, radix->_pipelineLayout, 0, _sets.size(), _sets.data(), 0, nullptr);
        //updateCommandBarrier(*cmdBuf);
        for (auto i = 0u; i < STEPS; i++) {
            std::vector<uint32_t> _values = { primCount, i };
            vkCmdPushConstants(*cmdBuf, radix->_pipelineLayout, VkShaderStageFlagBits::VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) * _values.size(), _values.data());

            cmdFillBuffer<0u>(*cmdBuf, radix->_histogramBuffer);
            cmdFillBuffer<0u>(*cmdBuf, radix->_prefixSumBuffer);
            commandBarrier(*cmdBuf);
            cmdDispatch(*cmdBuf, radix->_histogramPipeline, WG_COUNT, RADICE_AFFINE, 1u, true);
            cmdDispatch(*cmdBuf, radix->_workPrefixPipeline, 1u, 1u, 1u, true);
            cmdDispatch(*cmdBuf, radix->_permutePipeline, WG_COUNT, RADICE_AFFINE, 1u, true);
            cmdDispatch(*cmdBuf, radix->_copyhackPipeline, INTENSIVITY, 1u, 1u, true);
        };
        return result;
    };

};
