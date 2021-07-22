#include "Builder.h"

void Builder::setup(vsg::ref_ptr<vsg::Window> window, vsg::ViewportState* viewport, uint32_t maxNumTextures)
{
    auto device = window->getOrCreateDevice();

    _compile = vsg::CompileTraversal::create(window, viewport);

    // for now just allocated enough room for s
    uint32_t maxSets = maxNumTextures;
    vsg::DescriptorPoolSizes descriptorPoolSizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxNumTextures}};

    _compile->context.descriptorPool = vsg::DescriptorPool::create(device, maxSets, descriptorPoolSizes);

    _allocatedTextureCount = 0;
    _maxNumTextures = maxNumTextures;

}

vsg::ref_ptr<vsg::BindDescriptorSets> Builder::_createTexture(const GeometryInfo& info)
{
    auto textureData = info.image;
    if (!textureData)
    {
        if (auto itr = _colorData.find(info.color); itr != _colorData.end())
        {
            textureData = itr->second;
        }
        else
        {
            auto image = _colorData[info.color] = vsg::vec4Array2D::create(2, 2, info.color, vsg::Data::Layout{VK_FORMAT_R32G32B32A32_SFLOAT});
            image->set(0, 0, {0.0f, 1.0f, 1.0f, 1.0f});
            image->set(1, 1, {0.0f, 0.0f, 1.0f, 1.0f});
            textureData = image;
        }
    }

    auto& bindDescriptorSets = _textureDescriptorSets[textureData];
    if (bindDescriptorSets) return bindDescriptorSets;

    // create texture image and associated DescriptorSets and binding
    auto texture = vsg::DescriptorImage::create(vsg::Sampler::create(), textureData, 0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    auto descriptorSet = vsg::DescriptorSet::create(_descriptorSetLayout, vsg::Descriptors{texture});

    bindDescriptorSets = _textureDescriptorSets[textureData] = vsg::BindDescriptorSets::create(VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 0, vsg::DescriptorSets{descriptorSet});
    return bindDescriptorSets;
}

vsg::ref_ptr<vsg::BindGraphicsPipeline> Builder::_createGraphicsPipeline()
{
    if (_bindGraphicsPipeline) return _bindGraphicsPipeline;

    std::cout<<"Builder::_initGraphicsPipeline()"<<std::endl;

    // set up search paths to SPIRV shaders and textures
    vsg::Paths searchPaths = vsg::getEnvPaths("VSG_FILE_PATH");

    vsg::ref_ptr<vsg::ShaderStage> vertexShader = vsg::ShaderStage::read(VK_SHADER_STAGE_VERTEX_BIT, "main", vsg::findFile("shaders/vert_PushConstants.spv", searchPaths));
    vsg::ref_ptr<vsg::ShaderStage> fragmentShader = vsg::ShaderStage::read(VK_SHADER_STAGE_FRAGMENT_BIT, "main", vsg::findFile("shaders/frag_PushConstants.spv", searchPaths));
    if (!vertexShader || !fragmentShader)
    {
        std::cout << "Could not create shaders." << std::endl;
        return {};
    }

    // set up graphics pipeline
    vsg::DescriptorSetLayoutBindings descriptorBindings{
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr} // { binding, descriptorTpe, descriptorCount, stageFlags, pImmutableSamplers}
    };

    _descriptorSetLayout = vsg::DescriptorSetLayout::create(descriptorBindings);

    vsg::DescriptorSetLayouts descriptorSetLayouts{_descriptorSetLayout};

    vsg::PushConstantRanges pushConstantRanges{
        {VK_SHADER_STAGE_VERTEX_BIT, 0, 128} // projection view, and model matrices, actual push constant calls autoaatically provided by the VSG's DispatchTraversal
    };

    _pipelineLayout = vsg::PipelineLayout::create(descriptorSetLayouts, pushConstantRanges);

    vsg::VertexInputState::Bindings vertexBindingsDescriptions{
        VkVertexInputBindingDescription{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}, // vertex data
        VkVertexInputBindingDescription{1, sizeof(vsg::vec4), VK_VERTEX_INPUT_RATE_VERTEX}, // colour data
        VkVertexInputBindingDescription{2, sizeof(vsg::vec2), VK_VERTEX_INPUT_RATE_VERTEX}  // tex coord data
    };

    vsg::VertexInputState::Attributes vertexAttributeDescriptions{
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},    // vertex data
        VkVertexInputAttributeDescription{1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0}, // colour data
        VkVertexInputAttributeDescription{2, 2, VK_FORMAT_R32G32_SFLOAT, 0},       // tex coord data
    };

    vsg::GraphicsPipelineStates pipelineStates{
        vsg::VertexInputState::create(vertexBindingsDescriptions, vertexAttributeDescriptions),
        vsg::InputAssemblyState::create(),
        vsg::RasterizationState::create(),
        vsg::MultisampleState::create(),
        vsg::ColorBlendState::create(),
        vsg::DepthStencilState::create()};

    auto graphicsPipeline = vsg::GraphicsPipeline::create(_pipelineLayout, vsg::ShaderStages{vertexShader, fragmentShader}, pipelineStates);
    _bindGraphicsPipeline = vsg::BindGraphicsPipeline::create(graphicsPipeline);

    return _bindGraphicsPipeline;
}

void Builder::compile(vsg::ref_ptr<vsg::Node> subgraph)
{
    if (verbose) std::cout << "Builder::compile(" << subgraph << ") _compile = " << _compile << std::endl;

    if (_compile)
    {
        subgraph->accept(*_compile);
        _compile->context.record();
        _compile->context.waitForCompletion();
    }
}

vsg::ref_ptr<vsg::Node> Builder::createBox(const GeometryInfo& info)
{
    auto& subgraph = _boxes[info];
    if (subgraph)
    {
        std::cout<<"reused createBox()"<<std::endl;
        return subgraph;
    }

    std::cout<<"new createBox()"<<std::endl;

    // create StateGroup as the root of the scene/command graph to hold the GraphicsProgram, and binding of Descriptors to decorate the whole graph
    auto scenegraph = vsg::StateGroup::create();
    scenegraph->add(_createGraphicsPipeline());
    scenegraph->add(_createTexture(info));

    auto dx = info.dx;
    auto dy = info.dy;
    auto dz = info.dz;
    auto origin = info.position - dx * 0.5f - dy * 0.5f - dz * 0.5f;

    vsg::vec3 v000(origin);
    vsg::vec3 v100(origin + dx);
    vsg::vec3 v110(origin + dx + dy);
    vsg::vec3 v010(origin + dy);
    vsg::vec3 v001(origin + dz);
    vsg::vec3 v101(origin + dx + dz);
    vsg::vec3 v111(origin + dx + dy + dz);
    vsg::vec3 v011(origin + dy + dz);

    // set up vertex and index arrays
    auto vertices = vsg::vec3Array::create(
        {v000, v100, v101, v001,
         v100, v110, v111, v101,
         v110, v010, v011, v111,
         v010, v000, v001, v011,
         v010, v110, v100, v000,
         v001, v101, v111, v011}); // VK_FORMAT_R32G32B32_SFLOAT, VK_VERTEX_INPUT_RATE_INSTANCE, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE

    auto colors = vsg::vec3Array::create(vertices->size(), vsg::vec3(1.0f, 1.0f, 1.0f));
    // VK_FORMAT_R32G32B32_SFLOAT, VK_VERTEX_INPUT_RATE_VERTEX, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE

#if 0
    vsg::vec3 n0(0.0f, -1.0f, 0.0f);
    vsg::vec3 n1(1.0f, 0.0f, 0.0f);
    vsg::vec3 n2(0.0f, 1.0f, 0.0f);
    vsg::vec3 n3(0.0f, -1.0f, 0.0f);
    vsg::vec3 n4(0.0f, 0.0f, -1.0f);
    vsg::vec3 n5(0.0f, 0.0f, 1.0f);
    auto normals = vsg::vec3Array::create(
    {
        n0, n0, n0, n0,
        n1, n1, n1, n1,
        n2, n2, n2, n2,
        n3, n3, n3, n3,
        n4, n4, n4, n4,
        n5, n5, n5, n5,
    }); // VK_FORMAT_R32G32B32_SFLOAT, VK_VERTEX_INPUT_RATE_VERTEX, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE
#endif

    vsg::vec2 t00(0.0f, 0.0f);
    vsg::vec2 t01(0.0f, 1.0f);
    vsg::vec2 t10(1.0f, 0.0f);
    vsg::vec2 t11(1.0f, 1.0f);

    auto texcoords = vsg::vec2Array::create(
        {t00, t10, t11, t01,
         t00, t10, t11, t01,
         t00, t10, t11, t01,
         t00, t10, t11, t01,
         t00, t10, t11, t01,
         t00, t10, t11, t01}); // VK_FORMAT_R32G32_SFLOAT, VK_VERTEX_INPUT_RATE_VERTEX, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE

    auto indices = vsg::ushortArray::create(
        {0, 1, 2, 0, 2, 3,
         4, 5, 6, 4, 6, 7,
         8, 9, 10, 8, 10, 11,
         12, 13, 14, 12, 14, 15,
         16, 17, 18, 16, 18, 19,
         20, 21, 22, 20, 22, 23}); // VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE

    // setup geometry
    auto vid = vsg::VertexIndexDraw::create();
    vid->arrays = vsg::DataList{vertices, colors, texcoords};
    vid->indices = indices;
    vid->indexCount = indices->size();
    vid->instanceCount = 1;

    scenegraph->addChild(vid);

    compile(scenegraph);

    subgraph = scenegraph;
    return subgraph;
}

vsg::ref_ptr<vsg::Node> Builder::createCapsule(const GeometryInfo& info)
{
    std::cout<<"createCapsule()"<<std::endl;
    return createBox(info);
}

vsg::ref_ptr<vsg::Node> Builder::createCone(const GeometryInfo& info)
{
    std::cout<<"createCone()"<<std::endl;
    return createBox(info);
}

vsg::ref_ptr<vsg::Node> Builder::createCylinder(const GeometryInfo& info)
{
    std::cout<<"createCylinder()"<<std::endl;
    return createBox(info);
}

vsg::ref_ptr<vsg::Node> Builder::createQuad(const GeometryInfo& info)
{
    auto& subgraph = _boxes[info];
    if (subgraph)
    {
        std::cout<<"reused createQuad()"<<std::endl;
        return subgraph;
    }

    std::cout<<"new createQuad()"<<std::endl;

    auto scenegraph = vsg::StateGroup::create();
    scenegraph->add(_createGraphicsPipeline());
    scenegraph->add(_createTexture(info));

    auto dx = info.dx;
    auto dy = info.dy;
    auto origin = info.position - dx * 0.5f - dy * 0.5f;

    // set up vertex and index arrays
    auto vertices = vsg::vec3Array::create(
        {origin,
         origin + dx,
         origin + dx + dy,
         origin + dy}); // VK_FORMAT_R32G32B32_SFLOAT, VK_VERTEX_INPUT_RATE_INSTANCE, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE

    auto colors = vsg::vec3Array::create(
        {{1.0f, 0.0f, 0.0f},
         {0.0f, 1.0f, 0.0f},
         {0.0f, 0.0f, 1.0f},
         {1.0f, 1.0f, 1.0f}}); // VK_FORMAT_R32G32B32_SFLOAT, VK_VERTEX_INPUT_RATE_VERTEX, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE

    auto texcoords = vsg::vec2Array::create(
        {{0.0f, 0.0f},
         {1.0f, 0.0f},
         {1.0f, 1.0f},
         {0.0f, 1.0f}}); // VK_FORMAT_R32G32_SFLOAT, VK_VERTEX_INPUT_RATE_VERTEX, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE

    auto indices = vsg::ushortArray::create(
        {
            0,
            1,
            2,
            2,
            3,
            0,
        }); // VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE

    // setup geometry
    auto vid = vsg::VertexIndexDraw::create();
    vid->arrays = vsg::DataList{vertices, colors, texcoords};
    vid->indices = indices;
    vid->indexCount = indices->size();
    vid->instanceCount = 1;

    scenegraph->addChild(vid);

    compile(scenegraph);

    return scenegraph;
}

vsg::ref_ptr<vsg::Node> Builder::createSphere(const GeometryInfo& info)
{
    std::cout<<"createSphere()"<<std::endl;
    return createBox(info);
}
