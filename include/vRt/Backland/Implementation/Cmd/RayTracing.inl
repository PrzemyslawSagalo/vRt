#pragma once

//#include "../../vRt_subimpl.inl"
#include "../Utils.hpp"

namespace _vt {
    using namespace vrt;


    // planned type validations, also, planned advanced descriptor sets support in vertex assemblies
    VtResult bindDescriptorSets(std::shared_ptr<CommandBuffer> cmdBuf, VtPipelineBindPoint pipelineBindPoint, VtPipelineLayout layout, uint32_t firstSet = 0, const std::vector<VkDescriptorSet>& descriptorSets = {}, const std::vector<VkDescriptorSet>& dynamicOffsets = {}) {
        VtResult result = VK_SUCCESS;
        if (pipelineBindPoint == VT_PIPELINE_BIND_POINT_RAYTRACING) {
            cmdBuf->_boundDescriptorSets = descriptorSets;
        } else {
            cmdBuf->_boundVIDescriptorSets = descriptorSets;
        };
        return result;
    };

    // planned type validations
    VtResult bindPipeline(std::shared_ptr<CommandBuffer> cmdBuf, VtPipelineBindPoint pipelineBindPoint, std::shared_ptr<Pipeline> pipeline) {
        VtResult result = VK_SUCCESS;
        if (pipelineBindPoint == VT_PIPELINE_BIND_POINT_RAYTRACING) {
            cmdBuf->_rayTracingPipeline = pipeline;
        };
        return result;
    };

    // update material data in command
    VtResult bindMaterialSet(std::shared_ptr<CommandBuffer> cmdBuf, VtEntryUsageFlags usageIn, std::shared_ptr<MaterialSet> matrl) {
        VtResult result = VK_SUCCESS;
        cmdBuf->_materialSet = matrl;
        return result;
    };


    using cntr_t = uint64_t;

    // ray-tracing pipeline 
    VtResult dispatchRayTracing(std::shared_ptr<CommandBuffer> cmdBuf, uint32_t x = 1, uint32_t y = 1, uint32_t B = 1) {

        VtResult result = VK_SUCCESS;
        auto device = cmdBuf->_parent();
        auto acclb = device->_acceleratorBuilder[0];
        auto accel = cmdBuf->_acceleratorSet.lock();
        auto vertx = cmdBuf->_vertexSet.lock();
        auto matrl = cmdBuf->_materialSet.lock();
        auto rtppl = cmdBuf->_rayTracingPipeline.lock();
        auto rtset = cmdBuf->_rayTracingSet.lock();
        auto vasmp = vertx && vertx->_vertexAssembly ? vertx->_vertexAssembly : device->_nativeVertexAssembler[0];

        const auto TPC = 1ull, TMC = DUAL_COMPUTE;
        rtset->_cuniform.width  = x;
        rtset->_cuniform.height = y;
        rtset->_cuniform.iteration = 0;
        rtset->_cuniform.closestHitOffset = 0;
        rtset->_cuniform.currentGroup = 0;
        rtset->_cuniform.maxRayCount = rtset->_cuniform.width * rtset->_cuniform.height; // try to
        rtset->_cuniform.lastIteration = B-1;

        // form descriptor sets
        std::vector<VkDescriptorSet> _rtSets = { rtset->_descriptorSet };
        if (matrl) {
            _rtSets.push_back(matrl->_descriptorSet); // make material set not necesssary
            cmdUpdateBuffer(*cmdBuf, matrl->_constBuffer, 0, sizeof(uint32_t) * 2, &matrl->_materialCount);
        };
        for (auto s : cmdBuf->_boundDescriptorSets) { _rtSets.push_back(s); }

        // cleaning indices and counters
        auto cmdClean = [&](){
            cmdFillBuffer<0u>(*cmdBuf, rtset->_countersBuffer);
            cmdFillBuffer<0u>(*cmdBuf, rtset->_groupCountersBuffer);
        };

        // run rays generation (if have)
        if (rtppl->_generationPipeline.size() > 0 && rtppl->_generationPipeline[0]) {
            cmdFillBuffer<0u>(*cmdBuf, rtset->_groupCountersBufferRead);
            cmdClean(), cmdUpdateBuffer(*cmdBuf, rtset->_constBuffer, 0, sizeof(rtset->_cuniform), &rtset->_cuniform);
            vkCmdBindDescriptorSets(*cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, rtppl->_pipelineLayout->_rtLayout, 0, _rtSets.size(), _rtSets.data(), 0, nullptr);
            cmdDispatch(*cmdBuf, rtppl->_generationPipeline[0], tiled(x, rtppl->_tiling.width), tiled(y, rtppl->_tiling.height), TMC, false);
        };

        bool hasGroupShaders = false;
        for (int i = 0; i < std::min(std::size_t(4ull), rtppl->_groupPipeline.size()); i++) {
            if (rtppl->_groupPipeline[i]) { hasGroupShaders = true; break; };
        };

        // ray trace command
        for (auto it = 0u; it < B; it++) {

            // reload to caches and reset counters (if has group shaders)
            {
                rtset->_cuniform.iteration = it; commandBarrier(*cmdBuf);
                cmdUpdateBuffer(*cmdBuf, rtset->_constBuffer, 0, sizeof(rtset->_cuniform), &rtset->_cuniform);
                cmdCopyBuffer(*cmdBuf, rtset->_groupCountersBuffer, rtset->_groupCountersBufferRead, { vk::BufferCopy(0, 0, 64ull * sizeof(cntr_t)) });
                cmdCopyBuffer(*cmdBuf, rtset->_groupIndicesBuffer, rtset->_groupIndicesBufferRead, { vk::BufferCopy(0, 0, (rtset->_cuniform.maxRayCount) * MAX_RAY_GROUPS * sizeof(uint32_t)) });
                cmdClean();
            };

            // run traverse processing (single accelerator supported at now)
            if (vertx && vertx->_calculatedPrimitiveCount > 0) {
                if (vertx->_descriptorSetGenerator) vertx->_descriptorSetGenerator();
                if (accel->_descriptorSetGenerator) accel->_descriptorSetGenerator();

                // reset hit counter before new intersections
                const cntr_t zero = 0ull; cmdUpdateBuffer(*cmdBuf, rtset->_countersBuffer, strided<cntr_t>(3), sizeof(cntr_t), &zero);
                cmdFillBuffer<-1>(*cmdBuf, rtset->_traverseCache);

                if (device->_hExtensionAccelerator.size() > 0 && device->_hExtensionAccelerator[0] && device->_enabledAdvancedAcceleration)
                { result = device->_hExtensionAccelerator[0]->_DoIntersections(cmdBuf, accel, rtset); } else // extension-based
                { // stock software BVH traverse (i.e. shader-based)
                    std::vector<VkDescriptorSet> _tvSets = { rtset->_descriptorSet, accel->_descriptorSet, vertx->_descriptorSet };
                    vkCmdBindDescriptorSets(*cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, acclb->_traversePipelineLayout, 0, _tvSets.size(), _tvSets.data(), 0, nullptr);
                    cmdDispatch(*cmdBuf, acclb->_intersectionPipeline, tiled(RV_INTENSIVITY, TPC), 1u, TMC);
                };

                // interpolation hits
                if (vasmp->_intrpPipeline) {
                    std::vector<VkDescriptorSet> _tvSets = { rtset->_descriptorSet, accel->_descriptorSet, vertx->_descriptorSet };
                    vkCmdBindDescriptorSets(*cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, vasmp->_pipelineLayout->_rtLayout, 0, _tvSets.size(), _tvSets.data(), 0, nullptr);
                    cmdDispatch(*cmdBuf, vasmp->_intrpPipeline, tiled(IV_INTENSIVITY, TPC), 1u, TMC); // interpolate intersections
                };
            };

            // use RT pipeline layout
            vkCmdBindDescriptorSets(*cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, rtppl->_pipelineLayout->_rtLayout, 0, _rtSets.size(), _rtSets.data(), 0, nullptr);

            // handling misses in groups
            if (rtppl->_missHitPipeline[0]) { cmdDispatch(*cmdBuf, rtppl->_missHitPipeline[0], tiled(INTENSIVITY, TPC), 1u, TMC, false); };

            // handling hits in groups
            for (int i = 0; i < std::min(std::size_t(4ull), rtppl->_closestHitPipeline.size()); i++) {
                if (rtppl->_closestHitPipeline[i]) { cmdDispatch(*cmdBuf, rtppl->_closestHitPipeline[i], tiled(INTENSIVITY, TPC), 1u, TMC, false); };
            };

            // pre-group shader barrier 
            commandBarrier(*cmdBuf);

            // use resolve shader for resolve ray output or pushing secondaries
            for (auto i = 0u; i < std::min(std::size_t(4ull), rtppl->_groupPipeline.size()); i++) {
                if (rtppl->_groupPipeline[i]) { cmdDispatch(*cmdBuf, rtppl->_groupPipeline[i], tiled(INTENSIVITY, TPC), 1u, TMC, false); };
            };
        };

        // in-end barrier
        //commandBarrier(*cmdBuf);

        return result;
    };

    // create wrapped command buffer
    VtResult queryCommandBuffer(std::shared_ptr<Device> _vtDevice, VkCommandBuffer cmdBuf, std::shared_ptr<CommandBuffer>& vtCmdBuf) {
        vtCmdBuf = std::make_shared<CommandBuffer>();
        vtCmdBuf->_device = _vtDevice;
        vtCmdBuf->_commandBuffer = cmdBuf;
        return VK_SUCCESS;
    };

};
