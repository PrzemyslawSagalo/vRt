#pragma once

#include "../../vRt_subimpl.inl"

namespace _vt {
    using namespace vt;



    inline VtResult createVertexAssemblyPipeline(std::shared_ptr<Device> _vtDevice, const VtVertexAssemblyPipelineCreateInfo& info, std::shared_ptr<VertexAssemblyPipeline>& _vtVertexAssembly) {
        VtResult result = VK_SUCCESS;
        auto& vtVertexAssembly = (_vtVertexAssembly = std::make_shared<VertexAssemblyPipeline>());
        vtVertexAssembly->_device = _vtDevice;
        vtVertexAssembly->_pipelineLayout = info.pipelineLayout;
        vtVertexAssembly->_vertexAssemblyPipeline = createCompute(VkDevice(*_vtDevice), info.vertexAssemblyModule, *vtVertexAssembly->_pipelineLayout, VkPipelineCache(*_vtDevice));
        return result;
    };



    inline VtResult createVertexAssemblySet(std::shared_ptr<Device> _vtDevice, const VtVertexAssemblySetCreateInfo &info, std::shared_ptr<VertexAssemblySet>& _vtVertexAssembly) {
        VtResult result = VK_SUCCESS;
        auto& vtVertexAssembly = (_vtVertexAssembly = std::make_shared<VertexAssemblySet>());
        vtVertexAssembly->_device = _vtDevice;

        //constexpr auto maxPrimitives = 1024u * 1024u; // planned import from descriptor
        const auto& maxPrimitives = info.maxPrimitives;

        // build vertex input assembly program
        {
            constexpr auto ATTRIB_EXTENT = 8ull; // no way to set more than it now

            VtDeviceBufferCreateInfo bfi;
            bfi.familyIndex = _vtDevice->_mainFamilyIndex;
            bfi.usageFlag = VkBufferUsageFlags(vk::BufferUsageFlagBits::eStorageBuffer);

            // vertex data buffers
            bfi.bufferSize = maxPrimitives * sizeof(uint32_t);
            bfi.format = VK_FORMAT_R32_UINT;
            createDeviceBuffer(_vtDevice, bfi, vtVertexAssembly->_bitfieldBuffer);

            bfi.bufferSize = maxPrimitives * sizeof(uint32_t);
            bfi.format = VK_FORMAT_UNDEFINED;
            createDeviceBuffer(_vtDevice, bfi, vtVertexAssembly->_materialBuffer);

            bfi.bufferSize = maxPrimitives * sizeof(float) * 4ull;
            bfi.format = VK_FORMAT_R32G32B32A32_SFLOAT;
            createDeviceBuffer(_vtDevice, bfi, vtVertexAssembly->_verticeBuffer);

            bfi.bufferSize = maxPrimitives * sizeof(float) * 4ull;
            bfi.format = VK_FORMAT_R32G32B32A32_SFLOAT;
            createDeviceBuffer(_vtDevice, bfi, vtVertexAssembly->_verticeBufferSide);

            bfi.bufferSize = sizeof(uint32_t) * 4ull;
            bfi.format = VK_FORMAT_R32_UINT;
            createDeviceBuffer(_vtDevice, bfi, vtVertexAssembly->_countersBuffer);


            constexpr auto aWidth = 4096ull * 3ull;

            // create vertex attribute buffer
            VtDeviceImageCreateInfo tfi;
            tfi.familyIndex = _vtDevice->_mainFamilyIndex;
            tfi.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            tfi.format = VK_FORMAT_R32G32B32A32_SFLOAT;
            tfi.imageViewType = VK_IMAGE_VIEW_TYPE_2D;
            tfi.layout = VK_IMAGE_LAYOUT_GENERAL;
            tfi.mipLevels = 1;
            tfi.size = { uint32_t(aWidth), uint32_t(tiled(maxPrimitives * 3ull * ATTRIB_EXTENT, aWidth)+1ull), 1u };
            createDeviceImage(_vtDevice, tfi, vtVertexAssembly->_attributeTexelBuffer);
        };

        {
            std::vector<vk::PushConstantRange> constRanges = {
                vk::PushConstantRange(vk::ShaderStageFlagBits::eCompute, 0u, strided<uint32_t>(12))
            };
            std::vector<vk::DescriptorSetLayout> dsLayouts = {
                vk::DescriptorSetLayout(_vtDevice->_descriptorLayoutMap["vertexData"]),
                vk::DescriptorSetLayout(_vtDevice->_descriptorLayoutMap["vertexInputSet"]),
            };
            auto dsc = vk::Device(*_vtDevice).allocateDescriptorSets(vk::DescriptorSetAllocateInfo().setDescriptorPool(_vtDevice->_descriptorPool).setPSetLayouts(&dsLayouts[0]).setDescriptorSetCount(1));
            vtVertexAssembly->_descriptorSet = dsc[0];


            vk::Sampler attributeSampler = vk::Device(*_vtDevice).createSampler(vk::SamplerCreateInfo()
                .setAddressModeU(vk::SamplerAddressMode::eRepeat)
                .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
                .setMagFilter(vk::Filter::eNearest)
                .setMinFilter(vk::Filter::eNearest)
            );
            auto _write_tmpl = vk::WriteDescriptorSet(vtVertexAssembly->_descriptorSet, 0, 0, 1, vk::DescriptorType::eStorageBuffer);
            std::vector<vk::WriteDescriptorSet> writes = {
                vk::WriteDescriptorSet(_write_tmpl).setDstBinding(0).setDescriptorType(vk::DescriptorType::eStorageBuffer).setPBufferInfo(&vk::DescriptorBufferInfo(vtVertexAssembly->_countersBuffer->_descriptorInfo())),
                vk::WriteDescriptorSet(_write_tmpl).setDstBinding(1).setPBufferInfo(&vk::DescriptorBufferInfo(vtVertexAssembly->_materialBuffer->_descriptorInfo())),
                vk::WriteDescriptorSet(_write_tmpl).setDstBinding(2).setPBufferInfo(&vk::DescriptorBufferInfo(vtVertexAssembly->_bitfieldBuffer->_descriptorInfo())),
                vk::WriteDescriptorSet(_write_tmpl).setDstBinding(3).setDescriptorType(vk::DescriptorType::eStorageTexelBuffer).setPTexelBufferView(&vk::BufferView(vtVertexAssembly->_verticeBuffer->_bufferView)),
                vk::WriteDescriptorSet(_write_tmpl).setDstBinding(4).setDescriptorType(vk::DescriptorType::eStorageImage).setPImageInfo(&vk::DescriptorImageInfo(vtVertexAssembly->_attributeTexelBuffer->_descriptorInfo())),
                vk::WriteDescriptorSet(_write_tmpl).setDstBinding(5).setDescriptorType(vk::DescriptorType::eStorageTexelBuffer).setPTexelBufferView(&vk::BufferView(vtVertexAssembly->_verticeBufferSide->_bufferView)), // planned to replace
                vk::WriteDescriptorSet(_write_tmpl).setDstBinding(6).setDescriptorType(vk::DescriptorType::eCombinedImageSampler).setPImageInfo(&vk::DescriptorImageInfo(vtVertexAssembly->_attributeTexelBuffer->_descriptorInfo()).setSampler(attributeSampler)),
            };
            vk::Device(*_vtDevice).updateDescriptorSets(writes, {});
        };

        return result;
    };



};
