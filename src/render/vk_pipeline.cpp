// HellVerdict — vk_pipeline.cpp
// Copyright © 2026 Neofilisoft / Studio Balmung

#include "vk_pipeline.hpp"
#include <fstream>
#include <iostream>
#include <cstring>
#include <sys/stat.h>

namespace HellVerdict {

#define VKP_CHECK(expr, msg) \
    do { VkResult _r=(expr); if(_r!=VK_SUCCESS){ \
        std::cerr<<"[VkPipe] "<<msg<<" failed: "<<_r<<"\n"; return false; } } while(0)

// ── SPIR-V loader ─────────────────────────────────────────────────────────────
VkShaderModule VkPipelineManager::_load_spv(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { std::cerr << "[VkPipe] Missing: " << path << "\n"; return VK_NULL_HANDLE; }
    size_t sz = (size_t)f.tellg(); f.seekg(0);
    std::vector<char> buf(sz);
    f.read(buf.data(), (std::streamsize)sz);

    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = sz;
    ci.pCode    = reinterpret_cast<const uint32_t*>(buf.data());
    VkShaderModule mod = VK_NULL_HANDLE;
    vkCreateShaderModule(_device, &ci, nullptr, &mod);
    _tracked.push_back({path, _file_mtime(path)});
    return mod;
}

int64_t VkPipelineManager::_file_mtime(const std::string& p) {
    struct stat s{}; return (stat(p.c_str(),&s)==0) ? (int64_t)s.st_mtime : 0;
}

// ── Cache I/O ─────────────────────────────────────────────────────────────────
void VkPipelineManager::_load_cache() {
    std::vector<char> data;
    if (!_cache_path.empty()) {
        std::ifstream f(_cache_path, std::ios::binary|std::ios::ate);
        if (f) { size_t n=(size_t)f.tellg(); f.seekg(0); data.resize(n); f.read(data.data(),(std::streamsize)n); }
    }
    VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    ci.initialDataSize = data.size();
    ci.pInitialData    = data.empty() ? nullptr : data.data();
    vkCreatePipelineCache(_device, &ci, nullptr, &_vk_cache);
}

void VkPipelineManager::_save_cache() {
    if (_cache_path.empty() || !_vk_cache) return;
    size_t sz=0; vkGetPipelineCacheData(_device,_vk_cache,&sz,nullptr);
    if (!sz) return;
    std::vector<char> d(sz); vkGetPipelineCacheData(_device,_vk_cache,&sz,d.data());
    std::ofstream f(_cache_path,std::ios::binary); f.write(d.data(),(std::streamsize)sz);
    std::cout << "[VkPipe] Cache saved " << sz/1024 << "KB\n";
}

// ── Init ──────────────────────────────────────────────────────────────────────
bool VkPipelineManager::init(VkDevice device, VkFormat color_fmt,
                               VkFormat depth_fmt, const std::string& shader_dir,
                               const std::string& cache_path) {
    _device      = device;
    _color_fmt   = color_fmt;
    _depth_fmt   = depth_fmt;
    _shader_dir  = shader_dir;
    _cache_path  = cache_path;

    _load_cache();

    // Pipeline layout — one push constant block (128B) for all pipelines
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.size       = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcr;
    VKP_CHECK(vkCreatePipelineLayout(_device,&plci,nullptr,&_layout),"PipelineLayout");

    if (!_build_world_pipe(true,  _pipes[(int)PipelineId::World]))     return false;
    if (!_build_world_pipe(false, _pipes[(int)PipelineId::WorldLOD]))  return false;
    if (!_build_billboard_pipe(_pipes[(int)PipelineId::Billboard]))    return false;
    if (!_build_hud_pipe      (_pipes[(int)PipelineId::HUD]))          return false;
    if (!_build_present_pipe  (_pipes[(int)PipelineId::Present]))      return false;
    if (!_build_sdf_pipe      (_pipes[(int)PipelineId::SdfText]))      return false;

    _save_cache();
    std::cout << "[VkPipe] All " << (int)PipelineId::COUNT << " pipelines ready\n";
    return true;
}

// ── Generic pipeline factory ──────────────────────────────────────────────────
bool VkPipelineManager::_create_pipe(
    VkShaderModule vert, VkShaderModule frag,
    bool depth_write, bool alpha_blend,
    bool cull_back,   bool depth_test,
    int  stride,
    const VkVertexInputAttributeDescription* attrs, int attr_count,
    VkPipeline& out)
{
    if (!vert || !frag) { return false; }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,
                 VK_SHADER_STAGE_VERTEX_BIT,   vert, "main"};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,
                 VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main"};

    VkVertexInputBindingDescription vbd{0,(uint32_t)stride,VK_VERTEX_INPUT_RATE_VERTEX};
    VkPipelineVertexInputStateCreateInfo vis{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (attr_count > 0) {
        vis.vertexBindingDescriptionCount   = 1;
        vis.pVertexBindingDescriptions      = &vbd;
        vis.vertexAttributeDescriptionCount = (uint32_t)attr_count;
        vis.pVertexAttributeDescriptions    = attrs;
    }

    VkPipelineInputAssemblyStateCreateInfo ias{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vps{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vps.viewportCount=1; vps.scissorCount=1;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = cull_back ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable  = depth_test  ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = depth_write ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = 0xF;
    if (alpha_blend) {
        cba.blendEnable         = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp        = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        cba.alphaBlendOp        = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo cbs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cbs.attachmentCount=1; cbs.pAttachments=&cba;

    VkDynamicState dyn_s[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount=2; dyn.pDynamicStates=dyn_s;

    // VK 1.3 dynamic rendering — no render pass object needed
    VkPipelineRenderingCreateInfoKHR prc{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    prc.colorAttachmentCount    = 1;
    prc.pColorAttachmentFormats = &_color_fmt;
    prc.depthAttachmentFormat   = depth_test ? _depth_fmt : VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo gci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gci.pNext               = &prc;
    gci.stageCount          = 2;   gci.pStages             = stages;
    gci.pVertexInputState   = &vis; gci.pInputAssemblyState = &ias;
    gci.pViewportState      = &vps; gci.pRasterizationState = &rs;
    gci.pMultisampleState   = &ms;  gci.pDepthStencilState  = &ds;
    gci.pColorBlendState    = &cbs; gci.pDynamicState       = &dyn;
    gci.layout              = _layout;

    VkResult r = vkCreateGraphicsPipelines(_device, _vk_cache, 1, &gci, nullptr, &out);
    vkDestroyShaderModule(_device, vert, nullptr);
    vkDestroyShaderModule(_device, frag, nullptr);
    if (r != VK_SUCCESS) std::cerr << "[VkPipe] vkCreateGraphicsPipelines: " << r << "\n";
    return r == VK_SUCCESS;
}

// ── Per-pipeline builds ───────────────────────────────────────────────────────
// WorldVertex: pos(3f) normal(3f) uv(2f) tex_slot(1u)  = 36 bytes
bool VkPipelineManager::_build_world_pipe(bool cull_back, VkPipeline& out) {
    VkVertexInputAttributeDescription a[] = {
        {0,0,VK_FORMAT_R32G32B32_SFLOAT,  0},  // pos
        {1,0,VK_FORMAT_R32G32B32_SFLOAT, 12},  // normal
        {2,0,VK_FORMAT_R32G32_SFLOAT,    24},  // uv
        {3,0,VK_FORMAT_R32_UINT,         32},  // tex_slot
    };
    return _create_pipe(
        _load_spv(_shader_dir+"/world.vert.spv"),
        _load_spv(_shader_dir+"/world.frag.spv"),
        true, false, cull_back, true, 36, a, 4, out);
}

// BillVert: pos(3f) color(3f) uv(2f) tex_slot(1u) = 36 bytes
bool VkPipelineManager::_build_billboard_pipe(VkPipeline& out) {
    VkVertexInputAttributeDescription a[] = {
        {0,0,VK_FORMAT_R32G32B32_SFLOAT,  0},
        {1,0,VK_FORMAT_R32G32B32_SFLOAT, 12},
        {2,0,VK_FORMAT_R32G32_SFLOAT,    24},
        {3,0,VK_FORMAT_R32_UINT,         32},
    };
    return _create_pipe(
        _load_spv(_shader_dir+"/bill.vert.spv"),
        _load_spv(_shader_dir+"/bill.frag.spv"),
        true, false, false, true, 36, a, 4, out);
}

// HudVert: pos(2f) color(3f) = 20 bytes
bool VkPipelineManager::_build_hud_pipe(VkPipeline& out) {
    VkVertexInputAttributeDescription a[] = {
        {0,0,VK_FORMAT_R32G32_SFLOAT,       0},
        {1,0,VK_FORMAT_R32G32B32_SFLOAT,    8},
    };
    return _create_pipe(
        _load_spv(_shader_dir+"/hud.vert.spv"),
        _load_spv(_shader_dir+"/hud.frag.spv"),
        false, true, false, false, 20, a, 2, out);
}

// Present: no vertex input (fullscreen triangle generated in vert shader)
bool VkPipelineManager::_build_present_pipe(VkPipeline& out) {
    return _create_pipe(
        _load_spv(_shader_dir+"/present.vert.spv"),
        _load_spv(_shader_dir+"/present.frag.spv"),
        false, false, false, false, 0, nullptr, 0, out);
}

// SdfVert: pos(2f) uv(2f) color(4f) = 32 bytes
bool VkPipelineManager::_build_sdf_pipe(VkPipeline& out) {
    VkVertexInputAttributeDescription a[] = {
        {0,0,VK_FORMAT_R32G32_SFLOAT,       0},
        {1,0,VK_FORMAT_R32G32_SFLOAT,       8},
        {2,0,VK_FORMAT_R32G32B32A32_SFLOAT,16},
    };
    return _create_pipe(
        _load_spv(_shader_dir+"/sdf_text.vert.spv"),
        _load_spv(_shader_dir+"/sdf_text.frag.spv"),
        false, true, false, false, 32, a, 3, out);
}

// ── Public accessor ───────────────────────────────────────────────────────────
VkPipeline VkPipelineManager::get(PipelineId id) const {
    return _pipes[(int)id];
}

// ── Hot reload (Debug) ────────────────────────────────────────────────────────
void VkPipelineManager::hot_reload_if_changed() {
#ifndef NDEBUG
    bool changed = false;
    for (auto& sf : _tracked) {
        int64_t mt = _file_mtime(sf.path);
        if (mt != sf.mtime) { sf.mtime = mt; changed = true; }
    }
    if (!changed) return;
    std::cout << "[VkPipe] Shader change — rebuilding\n";
    vkDeviceWaitIdle(_device);
    for (auto& p : _pipes) if (p) { vkDestroyPipeline(_device,p,nullptr); p=VK_NULL_HANDLE; }
    _tracked.clear();
    _build_world_pipe(true,  _pipes[(int)PipelineId::World]);
    _build_world_pipe(false, _pipes[(int)PipelineId::WorldLOD]);
    _build_billboard_pipe(_pipes[(int)PipelineId::Billboard]);
    _build_hud_pipe      (_pipes[(int)PipelineId::HUD]);
    _build_present_pipe  (_pipes[(int)PipelineId::Present]);
    _build_sdf_pipe      (_pipes[(int)PipelineId::SdfText]);
    _save_cache();
    std::cout << "[VkPipe] Hot reload done\n";
#endif
}

// ── Shutdown ──────────────────────────────────────────────────────────────────
void VkPipelineManager::shutdown() {
    if (!_device) return;
    _save_cache();
    for (auto& p : _pipes) if (p) { vkDestroyPipeline(_device,p,nullptr); p=VK_NULL_HANDLE; }
    if (_layout)   vkDestroyPipelineLayout(_device,_layout,nullptr);
    if (_vk_cache) vkDestroyPipelineCache (_device,_vk_cache,nullptr);
    _device=VK_NULL_HANDLE;
}

} // namespace HellVerdict
