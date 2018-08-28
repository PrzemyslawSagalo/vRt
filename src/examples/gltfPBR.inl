#pragma once
#include "renderer.inl"

namespace rnd {

    using namespace vrt;

    inline void Renderer::Arguments(int argc, char** argv) {
        args::ArgumentParser parser("This is a test rendering program.", "");
        args::HelpFlag help(parser, "help", "Available flags", { 'h', "help" });
        args::ValueFlag<int> computeflag(parser, "compute-device-id", "Vulkan compute device (UNDER CONSIDERATION)", { 'c' });
        args::ValueFlag<int> deviceflag(parser, "graphics-device-id", "Vulkan graphics device to use (also should support compute)", { 'g' });
        args::ValueFlag<float> scaleflag(parser, "scale", "Scaling of model object", { 's' });
        args::ValueFlag<std::string> directoryflag(parser, "directory", "Directory of resources", { 'd' });
        args::ValueFlag<std::string> shaderflag(parser, "shaders", "Used SPIR-V shader pack", { 'p' });
        args::ValueFlag<std::string> bgflag(parser, "background", "Environment background", { 'b' });
        args::ValueFlag<std::string> modelflag(parser, "model", "Model to view (planned multiple models support)", { 'm' });

        try {
            parser.ParseCLI(argc, argv);
        }
        catch (args::Help)
        {
            std::cout << parser; glfwTerminate();
        }

        // read arguments
        if (deviceflag) gpuID = args::get(deviceflag);
        if (shaderflag) shaderPrefix = args::get(shaderflag);
        if (bgflag) bgTexName = args::get(bgflag);
        if (modelflag) modelInput = args::get(modelflag);
        if (scaleflag) modelScale = args::get(scaleflag);
        if (directoryflag) directory = args::get(directoryflag);
        if (help) { std::cout << parser; glfwTerminate(); } // required help or no arguments
        if (modelInput == "") std::cerr << "No model found :(" << std::endl;
    };

    inline void Renderer::Init(uint32_t windowWidth, uint32_t windowHeight) {
        // create GLFW window
        this->windowWidth = windowWidth, this->windowHeight = windowHeight;
        this->window = glfwCreateWindow(windowWidth, windowHeight, "vRt early test", NULL, NULL);
        if (!this->window) { glfwTerminate(); exit(EXIT_FAILURE); }

        // get DPI
        glfwGetWindowContentScale(this->window, &guiScale, nullptr);
        this->realWidth = windowWidth * guiScale, this->realHeight = windowHeight * guiScale;
        glfwSetWindowSize(this->window, this->realWidth, this->realHeight); // set real size of window

        // create vulkan and ray tracing instance
        appBase = std::make_shared<vte::ApplicationBase>(); auto& appfw = appBase;
        cameraController = std::make_shared<CameraController>();
        cameraController->canvasSize = (glm::uvec2*)&this->windowWidth;
        cameraController->eyePos = &this->eyePos;
        cameraController->upVector = &this->upVector;
        cameraController->viewVector = &this->viewVector;

        instance = appfw->createInstance();
        if (!instance) { glfwTerminate(); exit(EXIT_FAILURE); }

        // get physical devices
        auto physicalDevices = instance.enumeratePhysicalDevices();
        if (physicalDevices.size() < 0) { glfwTerminate(); std::cerr << "Vulkan does not supported, or driver broken." << std::endl; exit(0); }

        // choice device
        if (gpuID >= physicalDevices.size()) { gpuID = physicalDevices.size() - 1; }
        if (gpuID < 0 || gpuID == -1) gpuID = 0;

        // create surface and get format by physical device
        appfw->createWindowSurface(this->window, this->realWidth, this->realHeight, title);
        appfw->format(appfw->getSurfaceFormat(gpu = physicalDevices[gpuID]));

        // set GLFW callbacks
        glfwSetMouseButtonCallback(this->window, &Shared::MouseButtonCallback);
        glfwSetCursorPosCallback(this->window, &Shared::MouseMoveCallback);
        glfwSetKeyCallback(this->window, &Shared::KeyCallback);

        // write physical device name
        vk::PhysicalDeviceProperties devProperties = gpu.getProperties();
        std::cout << "Current Device: ";
        std::cout << devProperties.deviceName << std::endl;
        std::cout << devProperties.vendorID << std::endl;

        // create combined device object
        shaderPack = shaderPrefix + getShaderDir(devProperties.vendorID);
        deviceQueue = appfw->createDeviceQueue(gpu, false, shaderPack); // create default graphical device
        renderpass = appfw->createRenderpass(deviceQueue);
        
        // create image output
        const double SuperSampling = 2.0; // super sampling image
        this->canvasWidth  = this->windowWidth  * SuperSampling;
        this->canvasHeight = this->windowHeight * SuperSampling;

        // super sampled output image
        VtDeviceImageCreateInfo dii = {};
        dii.familyIndex = deviceQueue->familyIndex;
        dii.imageViewType = VK_IMAGE_VIEW_TYPE_2D;
        dii.layout = VK_IMAGE_LAYOUT_GENERAL;
        dii.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        dii.size = { this->canvasWidth, this->canvasHeight, 1 };
        
        // 
        dii.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        vtCreateDeviceImage(deviceQueue->device->rtDev, &dii, &outputImage);
        vtCreateDeviceImage(deviceQueue->device->rtDev, &dii, &specularPass);

        dii.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        vtCreateDeviceImage(deviceQueue->device->rtDev, &dii, &normalPass);
        vtCreateDeviceImage(deviceQueue->device->rtDev, &dii, &originPass);
        
        // dispatch image barrier
        vte::submitOnce(deviceQueue->device->rtDev, deviceQueue->queue, deviceQueue->commandPool, [&](VkCommandBuffer cmdBuf) {
            vtCmdImageBarrier(cmdBuf, outputImage);
            vtCmdImageBarrier(cmdBuf, normalPass);
            vtCmdImageBarrier(cmdBuf, originPass);
            vtCmdImageBarrier(cmdBuf, specularPass);
        });

        {
            // create dull sampler
            vk::SamplerCreateInfo samplerInfo;
            samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.minFilter = vk::Filter::eLinear;
            samplerInfo.magFilter = vk::Filter::eLinear;
            samplerInfo.compareEnable = false;
            dullSampler = deviceQueue->device->logical.createSampler(samplerInfo); // create sampler
        }

        {
            // create env image
            VtDeviceImageCreateInfo dii;
            dii.format = VK_FORMAT_R32G32B32A32_SFLOAT;
            dii.familyIndex = deviceQueue->familyIndex;
            dii.imageViewType = VK_IMAGE_VIEW_TYPE_2D;
            dii.layout = VK_IMAGE_LAYOUT_GENERAL;
            dii.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            dii.size = { 2, 2, 1 };
            vtCreateDeviceImage(deviceQueue->device->rtDev, &dii, &envImage);

            // dispatch image barrier
            vte::submitOnce(deviceQueue->device->rtDev, deviceQueue->queue, deviceQueue->commandPool, [&](VkCommandBuffer cmdBuf) {
                vtCmdImageBarrier(cmdBuf, envImage);
            });
        }

        {
            scale = 10.0f * glm::vec3(1.f, 1.f, 1.f);
            auto atMatrix = glm::lookAt(eyePos*scale, (eyePos+viewVector)*scale, upVector);
            auto pjMatrix = glm::perspective(float(M_PI) / 3.f, 16.f / 9.f, 0.0001f, 1000.f);

            // set first uniform buffer data
            cameraUniformData.projInv = glm::transpose(glm::inverse(pjMatrix));
            cameraUniformData.camInv = glm::transpose(glm::inverse(atMatrix));
            cameraUniformData.sceneRes = glm::vec4(canvasWidth, canvasHeight, 1.f, 1.f);
            cameraUniformData.variant = 1;
        }

        {
            // create uniform buffer
            createBufferFast(deviceQueue, rtUniformBuffer, vte::strided<VtCameraUniform>(1));
            writeIntoBuffer<VtCameraUniform>(deviceQueue, { cameraUniformData }, rtUniformBuffer, 0);
        }

        {
            // custom bindings for ray tracing systems
            auto _bindings = std::vector<vk::DescriptorSetLayoutBinding>{
                vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute), // constants for generation shader
                vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute), // env map for miss shader
                vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eStorageImage, 4, vk::ShaderStageFlagBits::eCompute)
            };
            customedLayouts.push_back(deviceQueue->device->logical.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo().setPBindings(_bindings.data()).setBindingCount(_bindings.size())));

            // create descriptor set, based on layout
            auto dsc = deviceQueue->device->logical.allocateDescriptorSets(vk::DescriptorSetAllocateInfo().setDescriptorPool(deviceQueue->device->descriptorPool).setPSetLayouts(customedLayouts.data()).setDescriptorSetCount(customedLayouts.size()));
            usrDescSet = dsc[0];
        }

        {
            // 
            std::vector<vk::DescriptorImageInfo> passImages = { outputImage->_descriptorInfo(), normalPass->_descriptorInfo(), originPass->_descriptorInfo(), specularPass->_descriptorInfo() };

            // write ray tracing user defined descriptor set
            auto writeTmpl = vk::WriteDescriptorSet(usrDescSet, 0, 0, 1, vk::DescriptorType::eStorageBuffer);
            auto imgdi = vk::DescriptorImageInfo(envImage->_descriptorInfo()).setSampler(dullSampler);
            std::vector<vk::WriteDescriptorSet> writes = {
                vk::WriteDescriptorSet(writeTmpl).setDstBinding(0).setDescriptorType(vk::DescriptorType::eStorageBuffer).setPBufferInfo((vk::DescriptorBufferInfo*)(&rtUniformBuffer->_descriptorInfo())),
                vk::WriteDescriptorSet(writeTmpl).setDstBinding(1).setDescriptorType(vk::DescriptorType::eCombinedImageSampler).setPImageInfo(&imgdi),
                vk::WriteDescriptorSet(writeTmpl).setDstBinding(2).setDescriptorType(vk::DescriptorType::eStorageImage).setPImageInfo(passImages.data()).setDescriptorCount(passImages.size()),
            };
            vk::Device(deviceQueue->device->rtDev).updateDescriptorSets(writes, {});
        }

        {
            // begin timing
            this->tStart = std::chrono::high_resolution_clock::now();
            Shared::active = Active{};
            Shared::TimeCallback(this->tPast = 1e-5);
            Shared::window = this->window; // set GLFW window
            Shared::active.tDiff = 0.0; // reset diff to near-zero (avoid critical math errors)
            Shared::active.keys.resize(1024, uint8_t(0u));
            Shared::active.mouse.resize(128, uint8_t(0u));
            
        }
    };

    inline void Renderer::InitCommands() {
        {
            // make accelerator and vertex builder command
            vxCmdBuf = vte::createCommandBuffer(deviceQueue->device->rtDev, deviceQueue->commandPool, false, false);
            VtCommandBuffer qVxCmdBuf; vtQueryCommandInterface(deviceQueue->device->rtDev, vxCmdBuf, &qVxCmdBuf);
            vtCmdBindDescriptorSets(qVxCmdBuf, VT_PIPELINE_BIND_POINT_VERTEXASSEMBLY, rtVPipelineLayout, 0, 1, &vtxDescSet, 0, nullptr);
            vtCmdBindVertexAssembly(qVxCmdBuf, vertexAssembly);
            vtCmdBindVertexInputSets(qVxCmdBuf, inputs.size(), inputs.data());
            vtCmdBuildVertexAssembly(qVxCmdBuf);
            vkEndCommandBuffer(qVxCmdBuf);
        }

        {
            // make accelerator and vertex builder command
            vxuCmdBuf = vte::createCommandBuffer(deviceQueue->device->rtDev, deviceQueue->commandPool, false, false);
            VtCommandBuffer qVxuCmdBuf; vtQueryCommandInterface(deviceQueue->device->rtDev, vxuCmdBuf, &qVxuCmdBuf);
            vtCmdBindDescriptorSets(qVxuCmdBuf, VT_PIPELINE_BIND_POINT_VERTEXASSEMBLY, rtVPipelineLayout, 0, 1, &vtxDescSet, 0, nullptr);
            vtCmdBindVertexAssembly(qVxuCmdBuf, vertexAssembly);
            vtCmdBindVertexInputSets(qVxuCmdBuf, inputs.size(), inputs.data());
            vtCmdUpdateVertexAssembly(qVxuCmdBuf, 0, true);
            vkEndCommandBuffer(qVxuCmdBuf);
        }

        {
            // make accelerator and vertex builder command
            bCmdBuf = vte::createCommandBuffer(deviceQueue->device->rtDev, deviceQueue->commandPool, false, false);
            VtCommandBuffer qBCmdBuf; vtQueryCommandInterface(deviceQueue->device->rtDev, bCmdBuf, &qBCmdBuf);
            vtCmdBindAccelerator(qBCmdBuf, accelerator);
            vtCmdBindVertexAssembly(qBCmdBuf, vertexAssembly);
            vtCmdBindVertexInputSets(qBCmdBuf, inputs.size(), inputs.data());
            vtCmdBuildAccelerator(qBCmdBuf);
            vkEndCommandBuffer(qBCmdBuf);
        }

        {
            // use single layer only (for experimenting, can changed)
            const uint32_t transparencyOrders = 1;

            // make ray tracing command buffer
            rtCmdBuf = vte::createCommandBuffer(deviceQueue->device->rtDev, deviceQueue->commandPool, false, false);
            VtCommandBuffer qRtCmdBuf; vtQueryCommandInterface(deviceQueue->device->rtDev, rtCmdBuf, &qRtCmdBuf);
            
            vtCmdBindMaterialSet(qRtCmdBuf, VtEntryUsageFlags(VT_ENTRY_USAGE_CLOSEST | VT_ENTRY_USAGE_MISS), materialSet);
            vtCmdBindDescriptorSets(qRtCmdBuf, VT_PIPELINE_BIND_POINT_RAYTRACING, rtPipelineLayout, 0, 1, &usrDescSet, 0, nullptr);
            vtCmdBindRayTracingSet(qRtCmdBuf, raytracingSet);
            vtCmdBindAccelerator(qRtCmdBuf, accelerator);
            vtCmdBindVertexAssembly(qRtCmdBuf, vertexAssembly);

            // primary rays generation
            vtCmdBindPipeline(qRtCmdBuf, VT_PIPELINE_BIND_POINT_RAYTRACING, rtPipeline);
            vtCmdDispatchRayTracing(qRtCmdBuf, canvasWidth, canvasHeight, transparencyOrders);

            // deferred reflection phase
            vtCmdBindPipeline(qRtCmdBuf, VT_PIPELINE_BIND_POINT_RAYTRACING, rfPipeline);
            vtCmdDispatchRayTracing(qRtCmdBuf, canvasWidth, canvasHeight, transparencyOrders);

            // (do you want second step of reflections?)
            //vtCmdBindPipeline(qRtCmdBuf, VT_PIPELINE_BIND_POINT_RAYTRACING, rfPipeline);
            //vtCmdDispatchRayTracing(qRtCmdBuf, canvasWidth, canvasHeight, transparencyOrders);

            vkEndCommandBuffer(qRtCmdBuf);
        }
    };


    inline void Renderer::InitRayTracing() {
        {
            // create accelerator set
            VtAcceleratorSetCreateInfo acci;
            acci.maxPrimitives = 1024 * 2048;
            acci.entryID = 0;
            vtCreateAccelerator(deviceQueue->device->rtDev, &acci, &accelerator);

            // create vertex assembly
            VtVertexAssemblySetCreateInfo vtsi;
            vtsi.maxPrimitives = 1024 * 2048;
            vtCreateVertexAssembly(deviceQueue->device->rtDev, &vtsi, &vertexAssembly);

            // dispatch image barrier for vertex assembly
            vte::submitOnce(deviceQueue->device->rtDev, deviceQueue->queue, deviceQueue->commandPool, [&](VkCommandBuffer cmdBuf) {
                vtCmdImageBarrier(cmdBuf, vrt::VtDeviceImage{ vertexAssembly->_attributeTexelBuffer });
            });
        }

        {
            // create pipeline layout for ray tracing
            VkPipelineLayoutCreateInfo vpi = vk::PipelineLayoutCreateInfo{};
            vpi.pSetLayouts = (VkDescriptorSetLayout*)customedLayouts.data();
            vpi.setLayoutCount = customedLayouts.size();

            VtPipelineLayoutCreateInfo vpti;
            vpti.pGeneralPipelineLayout = (VkPipelineLayoutCreateInfo*)&vpi;

            vtCreateRayTracingPipelineLayout(deviceQueue->device->rtDev, &vpti, &rtPipelineLayout);
        }

        {
            // custom bindings for ray tracing systems
            auto _bindings = std::vector<vk::DescriptorSetLayoutBinding>{
                vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute},
            };
            customedLayouts.push_back(deviceQueue->device->logical.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo().setPBindings(_bindings.data()).setBindingCount(_bindings.size())));

            // create descriptor set, based on layout
            auto dsc = deviceQueue->device->logical.allocateDescriptorSets(vk::DescriptorSetAllocateInfo().setDescriptorPool(deviceQueue->device->descriptorPool).setPSetLayouts(customedLayouts.data()).setDescriptorSetCount(customedLayouts.size()));
            vtxDescSet = dsc[0];
        }

        {
            // create pipeline layout for ray tracing
            VkPipelineLayoutCreateInfo vpi = vk::PipelineLayoutCreateInfo{};
            vpi.pSetLayouts = (VkDescriptorSetLayout*)customedLayouts.data();
            vpi.setLayoutCount = customedLayouts.size();

            VtPipelineLayoutCreateInfo vpti;
            vpti.pGeneralPipelineLayout = &vpi;

            vtCreateVertexAssemblyPipelineLayout(deviceQueue->device->rtDev, &vpti, &rtVPipelineLayout);
        }

        {
            // create ray tracing set
            VtRayTracingSetCreateInfo rtsi;
            rtsi.maxRays = canvasWidth * canvasHeight; // prefer that limit
            vtCreateRayTracingSet(deviceQueue->device->rtDev, &rtsi, &raytracingSet);
        }
    };


    inline void Renderer::InitPipeline() {
        // descriptor set bindings
        std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBindings = { vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, nullptr) };
        std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = { deviceQueue->device->logical.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo().setPBindings(descriptorSetLayoutBindings.data()).setBindingCount(1)) };

        // pipeline layout and cache
        auto pipelineLayout = deviceQueue->device->logical.createPipelineLayout(vk::PipelineLayoutCreateInfo().setPSetLayouts(descriptorSetLayouts.data()).setSetLayoutCount(descriptorSetLayouts.size()));
        auto descriptorSets = deviceQueue->device->logical.allocateDescriptorSets(vk::DescriptorSetAllocateInfo().setDescriptorPool(deviceQueue->device->descriptorPool).setDescriptorSetCount(descriptorSetLayouts.size()).setPSetLayouts(descriptorSetLayouts.data()));
        drawDescriptorSets = {descriptorSets[0]};

        // create pipeline
        vk::Pipeline trianglePipeline = {};
        {
            // pipeline stages
            std::vector<vk::PipelineShaderStageCreateInfo> pipelineShaderStages = {
                vk::PipelineShaderStageCreateInfo().setModule(vte::createShaderModule(deviceQueue->device->logical, vte::readBinary(shaderPack + "/output/render.vert.spv"))).setPName("main").setStage(vk::ShaderStageFlagBits::eVertex),
                vk::PipelineShaderStageCreateInfo().setModule(vte::createShaderModule(deviceQueue->device->logical, vte::readBinary(shaderPack + "/output/render.frag.spv"))).setPName("main").setStage(vk::ShaderStageFlagBits::eFragment)
            };

            // blend modes per framebuffer targets
            std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = {
                vk::PipelineColorBlendAttachmentState()
                .setBlendEnable(true)
                .setSrcColorBlendFactor(vk::BlendFactor::eOne).setDstColorBlendFactor(vk::BlendFactor::eZero).setColorBlendOp(vk::BlendOp::eAdd)
                .setSrcAlphaBlendFactor(vk::BlendFactor::eOne).setDstAlphaBlendFactor(vk::BlendFactor::eZero).setAlphaBlendOp(vk::BlendOp::eAdd)
                .setColorWriteMask(vk::ColorComponentFlags(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA))
            };

            // dynamic states
            std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };

            // create graphics pipeline
            auto tesselationState = vk::PipelineTessellationStateCreateInfo();
            auto vertexInputState = vk::PipelineVertexInputStateCreateInfo();
            auto rasterizartionState = vk::PipelineRasterizationStateCreateInfo();

            trianglePipeline = deviceQueue->device->logical.createGraphicsPipeline(deviceQueue->device->pipelineCache, vk::GraphicsPipelineCreateInfo()
                .setPStages(pipelineShaderStages.data()).setStageCount(pipelineShaderStages.size())
                .setFlags(vk::PipelineCreateFlagBits::eAllowDerivatives)
                .setPVertexInputState(&vertexInputState)
                .setPInputAssemblyState(&vk::PipelineInputAssemblyStateCreateInfo().setTopology(vk::PrimitiveTopology::eTriangleStrip))
                .setPViewportState(&vk::PipelineViewportStateCreateInfo().setViewportCount(1).setScissorCount(1))
                .setPRasterizationState(&vk::PipelineRasterizationStateCreateInfo()
                    .setDepthClampEnable(false)
                    .setRasterizerDiscardEnable(false)
                    .setPolygonMode(vk::PolygonMode::eFill)
                    .setCullMode(vk::CullModeFlagBits::eBack)
                    .setFrontFace(vk::FrontFace::eCounterClockwise)
                    .setDepthBiasEnable(false)
                    .setDepthBiasConstantFactor(0)
                    .setDepthBiasClamp(0)
                    .setDepthBiasSlopeFactor(0)
                    .setLineWidth(1.f))
                .setPDepthStencilState(&vk::PipelineDepthStencilStateCreateInfo()
                    .setDepthTestEnable(false)
                    .setDepthWriteEnable(false)
                    .setDepthCompareOp(vk::CompareOp::eLessOrEqual)
                    .setDepthBoundsTestEnable(false)
                    .setStencilTestEnable(false))
                .setPColorBlendState(&vk::PipelineColorBlendStateCreateInfo()
                    .setLogicOpEnable(false)
                    .setLogicOp(vk::LogicOp::eClear)
                    .setPAttachments(colorBlendAttachments.data())
                    .setAttachmentCount(colorBlendAttachments.size()))
                .setLayout(pipelineLayout)
                .setRenderPass(renderpass)
                .setBasePipelineIndex(0)
                .setPMultisampleState(&vk::PipelineMultisampleStateCreateInfo().setRasterizationSamples(vk::SampleCountFlagBits::e1))
                .setPDynamicState(&vk::PipelineDynamicStateCreateInfo().setPDynamicStates(dynamicStates.data()).setDynamicStateCount(dynamicStates.size()))
                .setPTessellationState(&tesselationState));
        };

        { // write descriptors for showing texture
            vk::SamplerCreateInfo samplerInfo = {};
            samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.minFilter = vk::Filter::eLinear;
            samplerInfo.magFilter = vk::Filter::eLinear;
            samplerInfo.compareEnable = false;
            auto sampler = deviceQueue->device->logical.createSampler(samplerInfo); // create sampler
            auto image = outputImage;

            // desc texture texture
            vk::DescriptorImageInfo imageDesc = {};
            imageDesc.imageLayout = vk::ImageLayout(image->_layout);
            imageDesc.imageView = vk::ImageView(image->_imageView);
            imageDesc.sampler = sampler;

            // update descriptors
            deviceQueue->device->logical.updateDescriptorSets(std::vector<vk::WriteDescriptorSet>{
                vk::WriteDescriptorSet().setDstSet(descriptorSets[0]).setDstBinding(0).setDstArrayElement(0).setDescriptorCount(1).setDescriptorType(vk::DescriptorType::eCombinedImageSampler).setPImageInfo(&imageDesc),
            }, nullptr);
        };

        currentContext = std::make_shared<vte::GraphicsContext>();
        { // create graphic context
            auto context = currentContext;

            // create graphics context
            context->queue = deviceQueue;
            context->pipeline = trianglePipeline;
            context->descriptorPool = deviceQueue->device->descriptorPool;
            context->descriptorSets = drawDescriptorSets;
            context->pipelineLayout = pipelineLayout;

            // create framebuffers by size
            context->renderpass = renderpass;
            context->swapchain = appBase->createSwapchain(deviceQueue);
            context->framebuffers = appBase->createSwapchainFramebuffer(deviceQueue, context->swapchain, context->renderpass);
        };
    };

    // 
    inline void Renderer::InitRayTracingPipeline(){
        {
            // create ray tracing pipeline
            VtVertexAssemblyPipelineCreateInfo vtpi = {};
            vtpi.vertexAssemblyModule = vte::makeComputePipelineStageInfo(deviceQueue->device->rtDev, vte::readBinary(shaderPack + "vertex/vtransformed.comp.spv"));
            vtpi.pipelineLayout = rtVPipelineLayout;
            vtCreateVertexAssemblyPipeline(deviceQueue->device->rtDev, &vtpi, &vtxPipeline);
        }
        
        auto closestShader = vte::makeComputePipelineStageInfo(deviceQueue->device->rtDev, vte::readBinary(shaderPack + "ray-tracing/closest-hit-shader.comp.spv"));
        auto missShader = vte::makeComputePipelineStageInfo(deviceQueue->device->rtDev, vte::readBinary(shaderPack + "ray-tracing/miss-hit-shader.comp.spv"));
        auto groupShader = vte::makeComputePipelineStageInfo(deviceQueue->device->rtDev, vte::readBinary(shaderPack + "ray-tracing/group-shader.comp.spv"));

        {
            auto genShader = vte::makeComputePipelineStageInfo(deviceQueue->device->rtDev, vte::readBinary(shaderPack + "ray-tracing/generation-shader.comp.spv"));

            // create ray tracing pipeline
            VtRayTracingPipelineCreateInfo rtpi = {};
            rtpi.pGenerationModule = &genShader;
            rtpi.pClosestModules = &closestShader;
            rtpi.pMissModules = &missShader;
            rtpi.pGroupModules = &groupShader;

            rtpi.closestModuleCount = 1;
            rtpi.missModuleCount = 1;
            rtpi.groupModuleCount = 1;

            rtpi.pipelineLayout = rtPipelineLayout;
            vtCreateRayTracingPipeline(deviceQueue->device->rtDev, &rtpi, &rtPipeline);
        }

        {
            auto genShader = vte::makeComputePipelineStageInfo(deviceQueue->device->rtDev, vte::readBinary(shaderPack + "ray-tracing/rfgen-shader.comp.spv"));

            // create ray tracing pipeline
            VtRayTracingPipelineCreateInfo rtpi = {};
            rtpi.pGenerationModule = &genShader;
            rtpi.pClosestModules = &closestShader;
            rtpi.pMissModules = &missShader;
            rtpi.pGroupModules = &groupShader;

            rtpi.closestModuleCount = 1;
            rtpi.missModuleCount = 1;
            rtpi.groupModuleCount = 1;

            rtpi.pipelineLayout = rtPipelineLayout;
            vtCreateRayTracingPipeline(deviceQueue->device->rtDev, &rtpi, &rfPipeline);
        }
    };

    // loading mesh (support only one)
    inline void Renderer::Preload(const std::string& modelInput){
        tinygltf::Model model = {};
        tinygltf::TinyGLTF loader = {};
        std::string err, input_filename = modelInput;
        bool ret = loader.LoadASCIIFromFile(&model, &err, input_filename);
        

        //model
        for (auto&b : model.buffers) {
            VtDeviceBuffer buf;
            createBufferFast(deviceQueue, buf, b.data.size());
            if (b.data.size() > 0) writeIntoBuffer(deviceQueue, b.data, buf);
            VDataSpace.push_back(buf);
        }
        for (auto b : VDataSpace) { bviews.push_back(b); };

        {
            createBufferFast(deviceQueue, VBufferRegions, sizeof(VtVertexRegionBinding));
            createBufferFast(deviceQueue, VAccessorSet, sizeof(VtVertexAccessor) * model.accessors.size());
            createBufferFast(deviceQueue, VBufferView, sizeof(VtVertexBufferView) * model.bufferViews.size());
            createBufferFast(deviceQueue, VAttributes, sizeof(VtVertexAttributeBinding) * 1024 * 1024);
            createBufferFast(deviceQueue, VTransforms, sizeof(glm::mat4) * 1024 * 1024);
            createBufferFast(deviceQueue, materialDescs, vte::strided<VtAppMaterial>(128));
            createBufferFast(deviceQueue, materialCombImages, vte::strided<VtVirtualCombinedImage>(64));
        };

        {
            // write ray tracing user defined descriptor set
            auto writeTmpl = vk::WriteDescriptorSet(vtxDescSet, 0, 0, 1, vk::DescriptorType::eStorageBuffer);
            std::vector<vk::WriteDescriptorSet> writes = {
                vk::WriteDescriptorSet(writeTmpl).setDstBinding(0).setDescriptorType(vk::DescriptorType::eStorageBuffer).setPBufferInfo((vk::DescriptorBufferInfo*)&VTransforms->_descriptorInfo()),
            };
            vk::Device(deviceQueue->device->rtDev).updateDescriptorSets(writes, {});
        };

        // create images
        for (auto& I: model.images) 
        {
            mImages.push_back(VtDeviceImage{});
            auto& image = mImages[mImages.size()-1];

            VtDeviceImageCreateInfo dii = {};
            //dii.format = VK_FORMAT_R32G32B32A32_SFLOAT;
            dii.format = VK_FORMAT_R8G8B8A8_UNORM;
            dii.familyIndex = deviceQueue->familyIndex;
            dii.imageViewType = VK_IMAGE_VIEW_TYPE_2D;
            dii.layout = VK_IMAGE_LAYOUT_GENERAL;
            dii.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            dii.size = { uint32_t(I.width), uint32_t(I.height), 1 };
            vtCreateDeviceImage(deviceQueue->device->rtDev, &dii, &image);

            // dispatch image barrier
            vte::submitOnce(deviceQueue->device->rtDev, deviceQueue->queue, deviceQueue->commandPool, [&](VkCommandBuffer cmdBuf) {
                for (auto& mI : mImages) { vtCmdImageBarrier(cmdBuf, mI); }
            });

            writeIntoImage<uint8_t>(deviceQueue, I.image, image, 0);
        }

        if (model.samplers.size() > 0) {
            for (auto& S : model.samplers)
            {
                mSamplers.push_back(VkSampler{});
                auto& sampler = mSamplers[mSamplers.size() - 1];

                vk::SamplerCreateInfo samplerInfo = {};
                samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
                samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
                samplerInfo.minFilter = vk::Filter::eLinear;
                samplerInfo.magFilter = vk::Filter::eLinear;
                samplerInfo.compareEnable = false;

                // set filter and sampling modes
                if (S.magFilter == VK_FILTER_NEAREST) samplerInfo.magFilter = vk::Filter::eNearest;
                if (S.minFilter == VK_FILTER_NEAREST) samplerInfo.minFilter = vk::Filter::eNearest;
                if (S.wrapS == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE) samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
                if (S.wrapT == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE) samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
                if (S.wrapS == VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT) samplerInfo.addressModeU = vk::SamplerAddressMode::eMirroredRepeat;
                if (S.wrapT == VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT) samplerInfo.addressModeV = vk::SamplerAddressMode::eMirroredRepeat;
                if (S.wrapS == VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE) samplerInfo.addressModeU = vk::SamplerAddressMode::eMirrorClampToEdge;
                if (S.wrapT == VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE) samplerInfo.addressModeV = vk::SamplerAddressMode::eMirrorClampToEdge;
                if (S.wrapS == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER) samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToBorder;
                if (S.wrapT == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER) samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToBorder;

                // create sampler
                sampler = deviceQueue->device->logical.createSampler(samplerInfo);
            }
        }
        else {
            mSamplers.push_back(VkSampler{});
            auto& sampler = mSamplers[mSamplers.size() - 1];

            vk::SamplerCreateInfo samplerInfo = {};
            samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
            samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
            samplerInfo.minFilter = vk::Filter::eLinear;
            samplerInfo.magFilter = vk::Filter::eLinear;
            samplerInfo.compareEnable = false;
            sampler = deviceQueue->device->logical.createSampler(samplerInfo);
        }

        {
            std::vector<VtAppMaterial> materials;
            for (auto& M : model.materials)
            {
                materials.push_back(VtAppMaterial{});
                auto& material = materials[materials.size() - 1];
                material.diffuse = glm::vec4(1.f);
                material.diffuseTexture = 0;

                if (M.values.find("baseColorTexture") != M.values.end()) material.diffuseTexture = M.values.at("baseColorTexture").TextureIndex() + 1;
                if (M.values.find("metallicRoughnessTexture") != M.values.end()) material.specularTexture = M.values.at("metallicRoughnessTexture").TextureIndex() + 1;
                if (M.additionalValues.find("emissiveTexture") != M.additionalValues.end()) material.emissiveTexture = M.additionalValues.at("emissiveTexture").TextureIndex() + 1;
                if (M.additionalValues.find("normalTexture") != M.additionalValues.end()) material.bumpTexture = M.additionalValues.at("normalTexture").TextureIndex() + 1;

                if (M.values.find("metallicFactor") != M.values.end()) material.specular.z = M.values.at("metallicFactor").number_value;
                if (M.values.find("roughnessFactor") != M.values.end()) material.specular.y = M.values.at("roughnessFactor").number_value;
                if (M.values.find("baseColorFactor") != M.values.end()) material.diffuse = glm::vec4(glm::make_vec3(M.values.at("baseColorFactor").number_array.data()), 1.f);
            }
            writeIntoBuffer<VtAppMaterial>(deviceQueue, materials, materialDescs, 0);
        }

        {
            std::vector<VtVirtualCombinedImage> textures;
            for (auto& T : model.textures)
            {
                textures.push_back(VtVirtualCombinedImage{});
                textures[textures.size() - 1].setTextureID(T.source).setSamplerID(T.sampler != -1 ? T.sampler : 0);
            }
            writeIntoBuffer<VtVirtualCombinedImage>(deviceQueue, textures, materialCombImages, 0);
        }
        
        for (auto &acs: model.accessors) { accessors.push_back(VtVertexAccessor{ uint32_t(acs.bufferView), uint32_t(acs.byteOffset), uint32_t(_getFormat(acs)) }); }
        for (auto &bv : model.bufferViews) { bufferViews.push_back(VtVertexBufferView{ uint32_t(bv.buffer), uint32_t(bv.byteOffset), uint32_t(bv.byteStride), uint32_t(bv.byteLength) }); }
        
        {
            std::vector<VkDescriptorImageInfo> dsi;
            for (auto& Tr: mImages) { dsi.push_back(Tr->_descriptorInfo()); }

            // create material set
            VtMaterialSetCreateInfo mtsi;
            mtsi.imageCount = dsi.size(); mtsi.pImages = dsi.data();
            mtsi.samplerCount = mSamplers.size(); mtsi.pSamplers = (VkSampler *)mSamplers.data();
            mtsi.bMaterialDescriptionsBuffer = materialDescs;
            mtsi.bImageSamplerCombinations = materialCombImages;
            mtsi.materialCount = model.materials.size();
            vtCreateMaterialSet(deviceQueue->device->rtDev, &mtsi, &materialSet);
        }

        std::vector<VtVertexAttributeBinding> attributes = {};
        for (auto& msh: model.meshes) {
            std::vector<VtVertexInputSet> primitives;
            for (auto& prim : msh.primitives) {
                VtVertexInputSet primitive;

                uint32_t attribOffset = attributes.size();
                VtVertexInputCreateInfo vtii = {};
                vtii.verticeAccessor = 0;
                vtii.indiceAccessor = -1;
                vtii.topology = VT_TOPOLOGY_TYPE_TRIANGLES_LIST;

                for (auto& attr: prim.attributes) { //attr
                    if (attr.first.compare("POSITION") == 0) {
                        vtii.verticeAccessor = attr.second;
                    }

                    if (attr.first.compare("NORMAL") == 0) {
                        attributes.push_back({ NORMAL_TID, uint32_t(attr.second) });
                    }

                    if (attr.first.compare("TEXCOORD_0") == 0) {
                        attributes.push_back({ TEXCOORD_TID, uint32_t(attr.second) });
                    }

                    if (attr.first.compare("TANGENT") == 0) {
                        attributes.push_back({ TANGENT_TID, uint32_t(attr.second) });
                    }
                }

                {
                    tinygltf::Accessor &idcAccessor = model.accessors[prim.indices];
                    vtii.indiceAccessor = prim.indices;
                    vtii.attributeCount = attributes.size() - attribOffset;
                    vtii.primitiveCount = idcAccessor.count / 3;
                    vtii.materialID = prim.material;
                    vtii.pSourceBuffers = bviews.data();
                    vtii.sourceBufferCount = bviews.size();
                    vtii.bBufferAccessors = VAccessorSet;
                    vtii.bBufferAttributeBindings = VAttributes;
                    vtii.attributeOffset = attribOffset;
                    vtii.bBufferRegionBindings = VBufferRegions;
                    vtii.bBufferViews = VBufferView;
                    vtii.vertexAssemblyPipeline = vtxPipeline;
                    vtCreateVertexInputSet(deviceQueue->device->rtDev, &vtii, &primitive);
                    primitives.push_back(primitive);
                }
            }
            vertexInputs.push_back(primitives);
        }

        {
            std::shared_ptr<std::function<void(const tinygltf::Node &, glm::dmat4, int)>> vertexLoader = {};
            vertexLoader = std::make_shared<std::function<void(const tinygltf::Node &, glm::dmat4, int)>>([&](const tinygltf::Node & node, glm::dmat4 inTransform, int recursive)->void {
                glm::dmat4 localTransform(1.0);
                localTransform *= (node.matrix.size() >= 16 ? glm::make_mat4(node.matrix.data()) : glm::dmat4(1.0));
                localTransform *= (node.translation.size() >= 3 ? glm::translate(glm::make_vec3(node.translation.data())) : glm::dmat4(1.0));
                localTransform *= (node.scale.size() >= 3 ? glm::scale(glm::make_vec3(node.scale.data())) : glm::dmat4(1.0));
                localTransform *= (node.rotation.size() >= 4 ? glm::mat4_cast(glm::make_quat(node.rotation.data())) : glm::dmat4(1.0));

                glm::dmat4 transform = inTransform * localTransform;
                if (node.mesh >= 0) {
                    auto mesh = vertexInputs[node.mesh]; // load mesh object (it just vector of primitives)
                    for (auto geom : mesh) {
                        //geom->getUniformSet()->getStructure(p).setTransform(transform); // here is bottleneck with host-GPU exchange
                        inputs.push_back(geom);
                        transforms.push_back(glm::transpose(transform));
                        transforms.push_back(glm::inverse(transform));
                    }
                }
                if (node.children.size() > 0 && node.mesh < 0) {
                    for (int n = 0; n < node.children.size(); n++) {
                        if (recursive >= 0) (*vertexLoader)(model.nodes[node.children[n]], transform, recursive - 1);
                    }
                }
            });

            // matrix with scaling
            auto matrix = glm::dmat4(1.0) * glm::scale(glm::dvec3(modelScale, modelScale, modelScale));

            // load scene
            uint32_t sceneID = 0;
            if (model.scenes.size() > 0) {
                for (int n = 0; n < model.scenes[sceneID].nodes.size(); n++) {
                    tinygltf::Node & node = model.nodes[model.scenes[sceneID].nodes[n]];
                    (*vertexLoader)(node, glm::dmat4(matrix), 16);
                }
            }
        }

        // write to buffers
        writeIntoBuffer(deviceQueue, transforms, VTransforms, 0);
        writeIntoBuffer(deviceQueue, accessors, VAccessorSet, 0);
        writeIntoBuffer(deviceQueue, bufferViews, VBufferView, 0);
        writeIntoBuffer(deviceQueue, attributes, VAttributes, 0);
    };


    inline void Renderer::Precompute(){
        // get time difference
        auto tNow = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - tStart).count();
        auto tDiff = tNow - tPast; tPast = tNow; Shared::TimeCallback(tNow);

        // handle camera moving
        this->cameraController->handle();
        Shared::active.dX = 0.0, Shared::active.dY = 0.0;

        // update camera position
        auto atMatrix = glm::lookAt(eyePos*scale, (eyePos + viewVector)*scale, upVector);
        cameraUniformData.camInv = glm::transpose(glm::inverse(atMatrix));

        // update start position
        vte::submitOnce(deviceQueue->device->rtDev, deviceQueue->queue, deviceQueue->commandPool, [&](VkCommandBuffer cmdBuf) {
            vkCmdUpdateBuffer(cmdBuf, rtUniformBuffer, 0, sizeof(VtCameraUniform), &cameraUniformData);
            _vt::updateCommandBarrier(cmdBuf);
        });
    };


    inline void Renderer::ComputeRayTracing(){
        vte::submitCmd(deviceQueue->device->rtDev, deviceQueue->queue, { vxuCmdBuf });
        vte::submitCmd(deviceQueue->device->rtDev, deviceQueue->queue, { bCmdBuf });
        vte::submitCmd(deviceQueue->device->rtDev, deviceQueue->queue, { rtCmdBuf });
    };


    inline void Renderer::Draw(){
        auto n_semaphore = currSemaphore;
        auto c_semaphore = (currSemaphore + 1) % currentContext->framebuffers.size();
        currSemaphore = c_semaphore;

        // acquire next image where will rendered (and get semaphore when will presented finally)
        n_semaphore = (n_semaphore >= 0 ? n_semaphore : (currentContext->framebuffers.size() - 1));
        currentContext->queue->device->logical.acquireNextImageKHR(currentContext->swapchain, std::numeric_limits<uint64_t>::max(), currentContext->framebuffers[n_semaphore].semaphore, nullptr, &currentBuffer);

        { // submit rendering (and wait presentation in device)
            // prepare viewport and clear info
            std::vector<vk::ClearValue> clearValues = { vk::ClearColorValue(std::array<float,4>{0.2f, 0.2f, 0.2f, 1.0f}), vk::ClearDepthStencilValue(1.0f, 0) };
            auto renderArea = vk::Rect2D(vk::Offset2D(0, 0), appBase->size());
            auto viewport = vk::Viewport(0.0f, 0.0f, appBase->size().width, appBase->size().height, 0, 1.0f);

            // create command buffer (with rewrite)
            auto commandBuffer = (currentContext->framebuffers[n_semaphore].commandBuffer = vte::createCommandBuffer(currentContext->queue->device->logical, currentContext->queue->commandPool, false)); // do reference of cmd buffer
            commandBuffer.beginRenderPass(vk::RenderPassBeginInfo(currentContext->renderpass, currentContext->framebuffers[currentBuffer].frameBuffer, renderArea, clearValues.size(), clearValues.data()), vk::SubpassContents::eInline);
            commandBuffer.setViewport(0, std::vector<vk::Viewport> { viewport });
            commandBuffer.setScissor(0, std::vector<vk::Rect2D> { renderArea });
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, currentContext->pipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, currentContext->pipelineLayout, 0, currentContext->descriptorSets, nullptr);
            commandBuffer.draw(4, 1, 0, 0);
            commandBuffer.endRenderPass();
            commandBuffer.end();

            // create render submission 
            std::vector<vk::Semaphore>
                waitSemaphores = { currentContext->framebuffers[n_semaphore].semaphore },
                signalSemaphores = { currentContext->framebuffers[c_semaphore].semaphore };
            std::vector<vk::PipelineStageFlags> waitStages = { vk::PipelineStageFlagBits::eColorAttachmentOutput };

            auto smbi = vk::SubmitInfo()
                .setPWaitDstStageMask(waitStages.data()).setPWaitSemaphores(waitSemaphores.data()).setWaitSemaphoreCount(waitSemaphores.size())
                .setPCommandBuffers(&commandBuffer).setCommandBufferCount(1)
                .setPSignalSemaphores(signalSemaphores.data()).setSignalSemaphoreCount(signalSemaphores.size());

            // submit command once
            vte::submitCmd(currentContext->queue->device->logical, currentContext->queue->queue, { commandBuffer }, smbi);
            currentContext->queue->device->logical.freeCommandBuffers(currentContext->queue->commandPool, { commandBuffer });
        }

        // present for displaying of this image
        currentContext->queue->queue.presentKHR(vk::PresentInfoKHR(
            1, &currentContext->framebuffers[c_semaphore].semaphore,
            1, &currentContext->swapchain,
            &currentBuffer, nullptr
        ));
    };

    inline void Renderer::HandleData(){
        auto& tDiff = Shared::active.tDiff; // get computed time difference

        std::stringstream tDiffStream;
        tDiffStream << std::fixed << std::setprecision(2) << (tDiff);

        std::stringstream tFramerateStream;
        auto tFramerateStreamF = 1e3 / tDiff;
        tPastFramerateStreamF = tPastFramerateStreamF * 0.5 + tFramerateStreamF * 0.5;
        tFramerateStream << std::fixed << std::setprecision(0) << tPastFramerateStreamF;

        auto wTitle = "vRt : " + tDiffStream.str() + "ms / " + tFramerateStream.str() + "Hz";
        glfwSetWindowTitle(window, wTitle.c_str());
    };

};