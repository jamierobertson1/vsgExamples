#include "GraphicsPipelineConfig.h"

using namespace vsg;

GraphicsPipelineConfig::GraphicsPipelineConfig(ref_ptr<ShaderSet> in_shaderSet) :
    shaderSet(in_shaderSet)
{
    vertexInputState = VertexInputState::create();
    inputAssemblyState = vsg::InputAssemblyState::create();
    rasterizationState = vsg::RasterizationState::create();
    colorBlendState = vsg::ColorBlendState::create();
    multisampleState = vsg::MultisampleState::create();
    depthStencilState = vsg::DepthStencilState::create();

    shaderHints = vsg::ShaderCompileSettings::create();
}

bool GraphicsPipelineConfig::assignArray(DataList& arrays, const std::string& name, VkVertexInputRate vertexInputRate, ref_ptr<Data> array)
{
    auto& attributeBinding = shaderSet->getAttributeBinding(name);
    if (attributeBinding)
    {
        if (!attributeBinding.define.empty()) shaderHints->defines.push_back(attributeBinding.define);

        uint32_t bindingIndex = baseAttributeBinding + static_cast<uint32_t>(arrays.size());
        vertexInputState->vertexAttributeDescriptions.push_back(VkVertexInputAttributeDescription{attributeBinding.location, bindingIndex, attributeBinding.format, 0});
        vertexInputState->vertexBindingDescriptions.push_back(VkVertexInputBindingDescription{bindingIndex, array->getLayout().stride, vertexInputRate});
        arrays.push_back(array);
        return true;
    }
    return false;
}

bool GraphicsPipelineConfig::assignTexture(Descriptors& descriptors, const std::string& name, ref_ptr<Data> textureData, ref_ptr<Sampler> sampler)
{
    if (auto& textureBinding = shaderSet->getUniformBinding(name))
    {
        if (!sampler) sampler = Sampler::create();

        if (!textureBinding.define.empty()) shaderHints->defines.push_back(textureBinding.define);

        descriptorBindings.push_back(VkDescriptorSetLayoutBinding{textureBinding.binding, textureBinding.descriptorType, textureBinding.descriptorCount, textureBinding.stageFlags, nullptr});

        // create texture image and associated DescriptorSets and binding
        auto texture = vsg::DescriptorImage::create(sampler, textureData ? textureData : textureBinding.data, textureBinding.binding, 0, textureBinding.descriptorType);
        descriptors.push_back(texture);
        return true;
    }
    return false;
}

bool GraphicsPipelineConfig::assignUniform(Descriptors& descriptors, const std::string& name, ref_ptr<Data> data)
{
    if (auto& uniformBinding = shaderSet->getUniformBinding(name))
    {
        if (!uniformBinding.define.empty()) shaderHints->defines.push_back(uniformBinding.define);

        descriptorBindings.push_back(VkDescriptorSetLayoutBinding{uniformBinding.binding, uniformBinding.descriptorType, uniformBinding.descriptorCount, uniformBinding.stageFlags, nullptr});

        auto uniform = vsg::DescriptorBuffer::create(data ? data : uniformBinding.data, uniformBinding.binding);
        descriptors.push_back(uniform);

        return true;
    }
    return false;
}

void GraphicsPipelineConfig::init()
{
    descriptorSetLayout = vsg::DescriptorSetLayout::create(descriptorBindings);

    vsg::PushConstantRanges pushConstantRanges;
    for(auto& pcb : shaderSet->pushConstantRanges)
    {
        if (pcb.define.empty()) pushConstantRanges.push_back(pcb.range);
    }

    layout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{descriptorSetLayout}, pushConstantRanges);

    GraphicsPipelineStates pipelineStates{vertexInputState, inputAssemblyState, rasterizationState, colorBlendState, multisampleState, depthStencilState};

    graphicsPipeline = GraphicsPipeline::create(layout, shaderSet->getShaderStages(shaderHints), pipelineStates, subpass);
    bindGraphicsPipeline = vsg::BindGraphicsPipeline::create(graphicsPipeline);
}


int GraphicsPipelineConfig::compare(const Object& rhs) const
{
    return Object::compare(rhs);
}
