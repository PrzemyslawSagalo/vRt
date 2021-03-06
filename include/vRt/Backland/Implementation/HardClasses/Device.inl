#pragma once

//#include "../../vRt_subimpl.inl"
#include "../Utils.hpp"

namespace _vt {
    using namespace vrt;


    Device::~Device() {
        // HANGING ON RX VEGA GPU 
        //if (_device) vk::Device(_device).waitIdle(); // just wait device idle for destructor 
        _device = {};
    };


    VtResult convertDevice(VkDevice device, const std::shared_ptr<PhysicalDevice>& physicalDevice, const VtDeviceAggregationInfo& vtExtension, std::shared_ptr<Device>& vtDevice) {
        //auto vtDevice = (vtDevice = std::make_shared<Device>());
        vtDevice = std::make_shared<Device>();
        vtDevice->_physicalDevice = physicalDevice; // reference for aliasing
        vtDevice->_device = device;
        auto gpu = vk::PhysicalDevice(physicalDevice->_physicalDevice);

        // device vendoring
        vtDevice->_vendorName = getVendorName(gpu.getProperties().vendorID);
        
        { // get device properties 
            auto features = (vtDevice->_features = std::make_shared<DeviceFeatures>());
            features->_device = vtDevice;

            // extensions support list 
            const auto extList = gpu.enumerateDeviceExtensionProperties();
            features->_extensions = std::vector<VkExtensionProperties>(extList.begin(), extList.end());

            // features support list
            auto _vkfeatures = vk::PhysicalDeviceFeatures2{};
            auto _vkproperties = vk::PhysicalDeviceProperties2{};
            

            // get features 
            features->_float16int8 = vk::PhysicalDeviceFloat16Int8FeaturesKHR{};
            features->_storage8 = vk::PhysicalDevice8BitStorageFeaturesKHR{};
            features->_storage16 = vk::PhysicalDevice16BitStorageFeatures{};
            features->_descriptorIndexing = vk::PhysicalDeviceDescriptorIndexingFeaturesEXT{};
            _vkfeatures.pNext = &features->_storage16, 
                features->_storage16.pNext = &features->_storage8, 
                features->_storage8.pNext = &features->_descriptorIndexing, 
                features->_descriptorIndexing.pNext = &features->_float16int8;

            // get properties 
            features->_subgroup = vk::PhysicalDeviceSubgroupProperties{};
            _vkproperties.pNext = &features->_subgroup;

            // call getters
            gpu.getFeatures2(&_vkfeatures), gpu.getProperties2(&_vkproperties);
            features->_features = _vkfeatures, features->_properties = _vkproperties, features->_limits = features->_properties.properties.limits;
        };

        // only next-gen GPU can have native uint16_t support
        if (vtDevice->_features->_features.features.shaderInt16) {
            if (vtDevice->_vendorName == VT_VENDOR_AMD) vtDevice->_vendorName = VT_VENDOR_AMD_VEGA;
            if (vtDevice->_vendorName == VT_VENDOR_NVIDIA) vtDevice->_vendorName = VT_VENDOR_NV_TURING;
        };

        



        VtResult result = VK_SUCCESS;
#ifdef AMD_VULKAN_MEMORY_ALLOCATOR_H
        if (vtExtension.allocator) {
            vtDevice->_allocator = *(const VmaAllocator*)vtExtension.allocator; result = VK_SUCCESS;
        }
        else {
            VmaAllocatorCreateInfo allocatorInfo = {};
            allocatorInfo.physicalDevice = *(vtDevice->_physicalDevice);
            allocatorInfo.device = vtDevice->_device;
            allocatorInfo.preferredLargeHeapBlockSize = 32 * sizeof(uint32_t);
            allocatorInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            allocatorInfo.pAllocationCallbacks = nullptr;
            allocatorInfo.pVulkanFunctions = nullptr;
            allocatorInfo.pHeapSizeLimit = nullptr;

#ifdef VOLK_H_
            // load API calls for mapping
            VolkDeviceTable vktable = {};
            volkLoadDeviceTable(&vktable, vtDevice->_device);

            // mapping volk with VMA functions
            VmaVulkanFunctions vfuncs = {};
            vfuncs.vkAllocateMemory = vktable.vkAllocateMemory;
            vfuncs.vkBindBufferMemory = vktable.vkBindBufferMemory;
            vfuncs.vkBindImageMemory = vktable.vkBindImageMemory;
            vfuncs.vkCreateBuffer = vktable.vkCreateBuffer;
            vfuncs.vkCreateImage = vktable.vkCreateImage;
            vfuncs.vkDestroyBuffer = vktable.vkDestroyBuffer;
            vfuncs.vkDestroyImage = vktable.vkDestroyImage;
            vfuncs.vkFreeMemory = vktable.vkFreeMemory;
            vfuncs.vkGetBufferMemoryRequirements = vktable.vkGetBufferMemoryRequirements;
            vfuncs.vkGetBufferMemoryRequirements2KHR = vktable.vkGetBufferMemoryRequirements2KHR;
            vfuncs.vkGetImageMemoryRequirements = vktable.vkGetImageMemoryRequirements;
            vfuncs.vkGetImageMemoryRequirements2KHR = vktable.vkGetImageMemoryRequirements2KHR;
            vfuncs.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
            vfuncs.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
            vfuncs.vkMapMemory = vktable.vkMapMemory;
            vfuncs.vkUnmapMemory = vktable.vkUnmapMemory;
            allocatorInfo.pVulkanFunctions = &vfuncs;
#endif

            if (vmaCreateAllocator(&allocatorInfo, &vtDevice->_allocator) == VK_SUCCESS) { result = VK_SUCCESS; };
        }
#endif

        // link device with vulkan.hpp
        auto _device = vk::Device(vtDevice->_device);

        // create default pipeline cache
        vtDevice->_pipelineCache = vk::PipelineCache{};//_device.createPipelineCache(vk::PipelineCacheCreateInfo());

        // make descriptor pool
        const auto dmult = 0x800u;
        std::vector<VkDescriptorPoolSize> dps = {
            vk::DescriptorPoolSize().setType(vk::DescriptorType::eSampledImage).setDescriptorCount(0x100u * dmult),
            vk::DescriptorPoolSize().setType(vk::DescriptorType::eStorageImage).setDescriptorCount(0x100u * dmult),
            vk::DescriptorPoolSize().setType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(0x100u * dmult),
            vk::DescriptorPoolSize().setType(vk::DescriptorType::eUniformBuffer).setDescriptorCount(0x100u * dmult),
            vk::DescriptorPoolSize().setType(vk::DescriptorType::eSampler).setDescriptorCount(0x100u * dmult),
            vk::DescriptorPoolSize().setType(vk::DescriptorType::eCombinedImageSampler).setDescriptorCount(0x100u * dmult),
            vk::DescriptorPoolSize().setType(vk::DescriptorType::eStorageTexelBuffer).setDescriptorCount(0x100u * dmult),
            vk::DescriptorPoolSize().setType(vk::DescriptorType::eUniformTexelBuffer).setDescriptorCount(0x100u * dmult)
        };
        vtDevice->_descriptorPoolSizes = dps;
        vtDevice->_shadersPath = vtExtension.shaderPath;

        // if ray tracing NV supported, add additional descriptor pool types
        if (vtExtension.enableAdvancedAcceleration && vtExtension.pAccelerationExtension) {
            vtExtension.pAccelerationExtension->_DeviceInitialization(vtDevice);
        };

        // 
        if (vtExtension.pFamilyIndices) {
            for (uint32_t i = 0; i < vtExtension.familyIndiceCount; i++) {
                vtDevice->_familyIndices.push_back(vtExtension.pFamilyIndices[i]);
            };
        };

        //
         const auto pbindings = vk::DescriptorBindingFlagBitsEXT::ePartiallyBound | vk::DescriptorBindingFlagBitsEXT::eUpdateAfterBind | vk::DescriptorBindingFlagBitsEXT::eVariableDescriptorCount | vk::DescriptorBindingFlagBitsEXT::eUpdateUnusedWhilePending;
         const auto vkfl = vk::DescriptorSetLayoutBindingFlagsCreateInfoEXT().setPBindingFlags(&pbindings);
         const auto vkpi = vk::DescriptorSetLayoutCreateInfo().setPNext(&vkfl);
        vtDevice->_descriptorPool = VkDescriptorPool(_device.createDescriptorPool(vk::DescriptorPoolCreateInfo().setMaxSets(1024).setPPoolSizes((vk::DescriptorPoolSize*)vtDevice->_descriptorPoolSizes.data()).setPoolSizeCount(vtDevice->_descriptorPoolSizes.size()).setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet | vk::DescriptorPoolCreateFlagBits::eUpdateAfterBindEXT)));

        {
            const std::vector<vk::DescriptorSetLayoutBinding> _bindings = {
                vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // rays
                vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // hit heads
                vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // closest hit indices
                vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // miss hit indices 
                vk::DescriptorSetLayoutBinding(4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // hit payloads
                vk::DescriptorSetLayoutBinding(5, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // ray indices 
                vk::DescriptorSetLayoutBinding(6, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // constant buffer
                vk::DescriptorSetLayoutBinding(7, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // counters 
                vk::DescriptorSetLayoutBinding(8, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)),  // task lists
                vk::DescriptorSetLayoutBinding(9, vk::DescriptorType::eStorageBuffer, 0x10000, vk::ShaderStageFlags(vtDevice->_descriptorAccess)),
                vk::DescriptorSetLayoutBinding(10, vk::DescriptorType::eStorageTexelBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // ray<->hit binding payload 
                vk::DescriptorSetLayoutBinding(11, vk::DescriptorType::eStorageTexelBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)),
                vk::DescriptorSetLayoutBinding(12, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // group counters 
                vk::DescriptorSetLayoutBinding(13, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // group counters for reads
                vk::DescriptorSetLayoutBinding(14, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // ray indices for reads
            };
            //vkfl.setBindingCount(_bindings.size());
            vtDevice->_descriptorLayoutMap["rayTracing"] = _device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo(vkpi).setPBindings(_bindings.data()).setBindingCount(_bindings.size()));
        };

        {
            const std::vector<vk::DescriptorBindingFlagsEXT> _bindingFlags = { vk::DescriptorBindingFlagBitsEXT::ePartiallyBound, vk::DescriptorBindingFlagBitsEXT::ePartiallyBound, {}, {}, };
            const auto vkfl = vk::DescriptorSetLayoutBindingFlagsCreateInfoEXT().setPBindingFlags(_bindingFlags.data()).setBindingCount(_bindingFlags.size());

            const std::vector<vk::DescriptorSetLayoutBinding> _bindings = {
                vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageBuffer, 0x10000, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // bvh main block 
                vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageBuffer, 0x10000, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // bvh nodes 
                vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // bvh instances
                //vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // bvh blocks  
                vk::DescriptorSetLayoutBinding(4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess))  // bvh transform buffer
            };
            vtDevice->_descriptorLayoutMap["hlbvh2"] = _device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo(vkpi).setPNext(&vkfl).setPBindings(_bindings.data()).setBindingCount(_bindings.size()));
        };

        {
            const std::vector<vk::DescriptorSetLayoutBinding> _bindings = {
                vk::DescriptorSetLayoutBinding(0 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // morton codes
                vk::DescriptorSetLayoutBinding(1 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // morton indices
                vk::DescriptorSetLayoutBinding(2 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // const buffer 
                vk::DescriptorSetLayoutBinding(3 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // leafs
                vk::DescriptorSetLayoutBinding(4 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // aabb on work
                vk::DescriptorSetLayoutBinding(5 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // vote flags
                vk::DescriptorSetLayoutBinding(6 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // in-process indices
                vk::DescriptorSetLayoutBinding(7 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // leaf node indices
                vk::DescriptorSetLayoutBinding(8 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // in-process counters
                vk::DescriptorSetLayoutBinding(9 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // box calculation of scene 
            };
            vtDevice->_descriptorLayoutMap["hlbvh2work"] = _device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo(vkpi).setPBindings(_bindings.data()).setBindingCount(_bindings.size()));
        };

        {
            const std::vector<vk::DescriptorSetLayoutBinding> _bindings = {
                  vk::DescriptorSetLayoutBinding(0 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // vertex assembly counters
                  vk::DescriptorSetLayoutBinding(1 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // material buffer (unused)
                  vk::DescriptorSetLayoutBinding(2 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // order buffer (unused)
                  vk::DescriptorSetLayoutBinding(3 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // writable vertices
                  vk::DescriptorSetLayoutBinding(4 , vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // writable attributes (DEPRECATED row)
                //vk::DescriptorSetLayoutBinding(5 , vk::DescriptorType::eStorageTexelBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // readonly vertices
                  vk::DescriptorSetLayoutBinding(5 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)),
                  vk::DescriptorSetLayoutBinding(6 , vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // readonly attributes (DEPRECATED row)
                //vk::DescriptorSetLayoutBinding(7 , vk::DescriptorType::eStorageTexelBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // precomputed normals
                  vk::DescriptorSetLayoutBinding(7 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // precomputed normals
                  vk::DescriptorSetLayoutBinding(8 , vk::DescriptorType::eUniformTexelBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)),
                  vk::DescriptorSetLayoutBinding(9 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // vertex input uniform buffer (TEMPORARY)
            };
            vtDevice->_descriptorLayoutMap["vertexData"] = _device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo(vkpi).setPBindings(_bindings.data()).setBindingCount(_bindings.size()));
        };

        {
            const std::vector<vk::DescriptorSetLayoutBinding> _bindings = {
                vk::DescriptorSetLayoutBinding(0 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // keys in
                vk::DescriptorSetLayoutBinding(1 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // values in
            };
            vtDevice->_descriptorLayoutMap["radixSortBind"] = _device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo(vkpi).setPBindings(_bindings.data()).setBindingCount(_bindings.size()));
        };

        {
            const std::vector<vk::DescriptorSetLayoutBinding> _bindings = {
                vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // keys cache
                vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // values cache
                vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // radice step properties
                vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // histogram of radices (every work group)
                vk::DescriptorSetLayoutBinding(4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // prefix-sum of radices (every work group)
            };
            vtDevice->_descriptorLayoutMap["radixSort"] = _device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo(vkpi).setPBindings(_bindings.data()).setBindingCount(_bindings.size()));
        };

        {
            const std::vector<vk::DescriptorSetLayoutBinding> _bindings = {
                vk::DescriptorSetLayoutBinding(0 , vk::DescriptorType::eSampledImage, VRT_MAX_IMAGES, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // textures
                vk::DescriptorSetLayoutBinding(1 , vk::DescriptorType::eSampler, VRT_MAX_SAMPLERS, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // samplers
                vk::DescriptorSetLayoutBinding(2 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // material buffer
                vk::DescriptorSetLayoutBinding(3 , vk::DescriptorType::eUniformTexelBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // virtual texture and sampler combinations
                vk::DescriptorSetLayoutBinding(4 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // material set uniform 
            };

            const std::vector<vk::DescriptorBindingFlagsEXT> _bindingFlags = { vk::DescriptorBindingFlagBitsEXT::ePartiallyBound, vk::DescriptorBindingFlagBitsEXT::ePartiallyBound, {}, {}, {}, };
            const auto vkfl = vk::DescriptorSetLayoutBindingFlagsCreateInfoEXT().setPBindingFlags(_bindingFlags.data()).setBindingCount(_bindingFlags.size());
            vtDevice->_descriptorLayoutMap["materialSet"] = _device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo(vkpi).setPNext(&vkfl).setPBindings(_bindings.data()).setBindingCount(_bindings.size()));
        };

        {
            const std::vector<vk::DescriptorSetLayoutBinding> _bindings = {
                //vk::DescriptorSetLayoutBinding(0 , vk::DescriptorType::eUniformTexelBuffer, vendorName == VT_VENDOR_INTEL ? 1 : 8, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // vertex raw data
                vk::DescriptorSetLayoutBinding(0 , vk::DescriptorType::eStorageBuffer, 0x10000, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // vertex raw data
                //vk::DescriptorSetLayoutBinding(1 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // virtual regions
                vk::DescriptorSetLayoutBinding(2 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // buffer views
                vk::DescriptorSetLayoutBinding(3 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // accessors
                vk::DescriptorSetLayoutBinding(4 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // attribute bindings 
                //vk::DescriptorSetLayoutBinding(5 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), // vertex input uniform
                vk::DescriptorSetLayoutBinding(6 , vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlags(vtDevice->_descriptorAccess)), 
            };

            const std::vector<vk::DescriptorBindingFlagsEXT> _bindingFlags = { vk::DescriptorBindingFlagBitsEXT::ePartiallyBound, {}, {}, {}, {}, };
            const auto vkfl = vk::DescriptorSetLayoutBindingFlagsCreateInfoEXT().setPBindingFlags(_bindingFlags.data()).setBindingCount(_bindingFlags.size());
            vtDevice->_descriptorLayoutMap["vertexInputSet"] = _device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo(vkpi).setPNext(&vkfl).setPBindings(_bindings.data()).setBindingCount(_bindings.size()));
        };

        { //
            const auto layoutSet = _device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo(vkpi).setPBindings(nullptr).setBindingCount(0));
            vtDevice->_emptyDS = _device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo().setDescriptorPool(vtDevice->_descriptorPool).setPSetLayouts(&layoutSet).setDescriptorSetCount(1))[0];
            vtDevice->_descriptorLayoutMap["empty"] = layoutSet;
        };

        // 
        const auto vendorName = vtDevice->_vendorName;
        auto simfo = VtAttributePipelineCreateInfo{};
        simfo.assemblyModule = makeComputePipelineStageInfo(*vtDevice, _vt::readBinary(getCorrectPath(natives::vertexAssembly, vendorName, vtDevice->_shadersPath)));
        simfo.interpolModule = makeComputePipelineStageInfo(*vtDevice, _vt::readBinary(getCorrectPath(hlbvh2::interpolator, vendorName, vtDevice->_shadersPath)));

        // native vertex input pipeline layout 
        auto vtpl = VtPipelineLayoutCreateInfo{};
        createPipelineLayout(vtDevice, vtpl, simfo.pipelineLayout, VT_PIPELINE_LAYOUT_TYPE_VERTEXINPUT);

        // create native tools 
        for ( uint32_t t = 0; t < vtDevice->_supportedThreadCount; t++ ) {
            vtDevice->_radixSort.push_back({});             createRadixSort(vtDevice, vtExtension, vtDevice->_radixSort[t]);
            vtDevice->_nativeVertexAssembler.push_back({}); createAssemblyPipeline(vtDevice, simfo, vtDevice->_nativeVertexAssembler[t], true);
            vtDevice->_acceleratorBuilder.push_back({});    createAcceleratorHLBVH2(vtDevice, vtExtension, vtDevice->_acceleratorBuilder[t]);
        }

        // create dull barrier pipeline
        //auto rng = vk::PushConstantRange(vk::ShaderStageFlags(vk::ShaderStageFlagBits::eCompute), 0u, strided<uint32_t>(2));
        auto ppl = vk::Device(device).createPipelineLayout(vk::PipelineLayoutCreateInfo({}, 0, nullptr, 0, nullptr));
        //vtDevice->_dullBarrier = createComputeHC(device, natives::dullBarrier.at(vtDevice->_vendorName), ppl, vkPipelineCache);



        // add acceleration extension support
        if (vtExtension.enableAdvancedAcceleration && vtExtension.pAccelerationExtension) {
            vtDevice->_enabledAdvancedAcceleration = vtExtension.enableAdvancedAcceleration;
            if (vtExtension.pAccelerationExtension->_Criteria(vtDevice->_features) == VK_SUCCESS) {
                std::shared_ptr<AcceleratorExtensionBase> hExtensionAccelerator = {};
                vtExtension.pAccelerationExtension->_Initialization(vtDevice, hExtensionAccelerator);
                vtDevice->_hExtensionAccelerator.push_back(hExtensionAccelerator);
            };
        };

        return result;
    };


    inline VtResult createDevice(const std::shared_ptr<PhysicalDevice>& physicalDevice, const VkDeviceCreateInfo& vdvi, std::shared_ptr<Device>& vtDevice) {
        VtResult result = VK_ERROR_INITIALIZATION_FAILED;

        // default structure values
        VtDeviceAggregationInfo vtExtension = {};
        auto vtExtensionPtr = vtSearchStructure(vdvi, VT_STRUCTURE_TYPE_DEVICE_AGGREGATION_INFO);

        // if found, getting some info
        if (vtExtensionPtr) vtExtension = (VtDeviceAggregationInfo&)*vtExtensionPtr;

        // be occurate with "VkDeviceCreateInfo", because after creation device, all "vt" extended structures will destoyed
        VkDevice vkDevice = {};
        if (vkCreateDevice(*physicalDevice, (const VkDeviceCreateInfo*)vtExplodeArtificals(vdvi), nullptr, &vkDevice) == VK_SUCCESS) { result = VK_SUCCESS; };

        // manually convert device
        convertDevice(vkDevice, physicalDevice, vtExtension, vtDevice);
        return result;
    };







    // 
     VtResult AcceleratorExtensionBase::_DoIntersections(const std::shared_ptr<CommandBuffer>& cmdBuf, const std::shared_ptr<AcceleratorSet>& acceleratorSet, const std::shared_ptr<RayTracingSet>& rayTracingSet) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    };
     VtResult AcceleratorExtensionBase::_BuildAccelerator(const std::shared_ptr<CommandBuffer>& cmdBuf, const std::shared_ptr<AcceleratorSet>& acceleratorSet, const VtAcceleratorBuildInfo& buildInfo) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    };
     VtResult AcceleratorExtensionBase::_Init(const std::shared_ptr<Device>& device, const VtDeviceAdvancedAccelerationExtension * extensionBasedInfo) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    };
     VtResult AcceleratorExtensionBase::_Criteria(const std::shared_ptr<DeviceFeatures>& supportedFeatures) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    };

    // connectors with extension classes
     VtResult AcceleratorExtensionBase::_ConstructAcceleratorSet(const std::shared_ptr<AcceleratorSet>& accelSet) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    };
     VtResult AcceleratorExtensionBase::_ConstructVertexAssembly(const std::shared_ptr<VertexAssemblySet>& assemblySet) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    };

};
