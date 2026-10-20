// vkview — offscreen Vulkan renderer that runs the XenosRecomp-generated SPIR-V shaders of MGR:R.
//
// It consumes a "scene package" written by tools/export_scene.py (manifest.txt + blobs) and writes a PPM.
// Shader ABI (see XenosRecomp shader_common.h):
//   push constants { u64 vertexConstants, u64 pixelConstants, u64 sharedConstants }  (buffer device addresses)
//   set 0 = Texture2D[], set 1 = Texture3D[], set 2 = TextureCube[], set 3 = SamplerState[]   (bindless heaps)
//   spec constant 0 = g_SpecConstants
// Manifest tokens (whitespace separated): see load_manifest().
#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>

#define VK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "Vulkan error %d at %s:%d (%s)\n", (int)r_, __FILE__, __LINE__, #x); exit(1); } } while (0)

static std::vector<uint8_t> read_file(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) { fprintf(stderr, "cannot open %s\n", p.c_str()); exit(1); }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

struct Ctx {
    VkInstance inst; VkPhysicalDevice pd; VkDevice dev; VkQueue q; uint32_t qf; VkCommandPool pool;
    VkPhysicalDeviceMemoryProperties mp;
} C;

static uint32_t find_mem(uint32_t bits, VkMemoryPropertyFlags want) {
    for (uint32_t i = 0; i < C.mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (C.mp.memoryTypes[i].propertyFlags & want) == want) return i;
    fprintf(stderr, "no memory type\n"); exit(1);
}

struct Buf { VkBuffer b = VK_NULL_HANDLE; VkDeviceMemory m = VK_NULL_HANDLE; void* p = nullptr; VkDeviceSize size = 0; VkDeviceAddress addr = 0; };

static Buf make_buf(VkDeviceSize size, VkBufferUsageFlags usage, bool addr = false) {
    Buf r; r.size = size ? size : 4;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size = r.size; bi.usage = usage | (addr ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT : 0);
    VK(vkCreateBuffer(C.dev, &bi, nullptr, &r.b));
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(C.dev, r.b, &mr);
    VkMemoryAllocateFlagsInfo fi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO}; fi.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = mr.size;
    ai.memoryTypeIndex = find_mem(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (addr) ai.pNext = &fi;
    VK(vkAllocateMemory(C.dev, &ai, nullptr, &r.m)); VK(vkBindBufferMemory(C.dev, r.b, r.m, 0));
    VK(vkMapMemory(C.dev, r.m, 0, VK_WHOLE_SIZE, 0, &r.p));
    if (addr) { VkBufferDeviceAddressInfo di{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; di.buffer = r.b; r.addr = vkGetBufferDeviceAddress(C.dev, &di); }
    return r;
}

static VkCommandBuffer begin_cmd() {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ai.commandPool = C.pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
    VkCommandBuffer cb; VK(vkAllocateCommandBuffers(C.dev, &ai, &cb));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; VK(vkBeginCommandBuffer(cb, &bi));
    return cb;
}
static void end_cmd(VkCommandBuffer cb) {
    VK(vkEndCommandBuffer(cb));
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    VK(vkQueueSubmit(C.q, 1, &si, VK_NULL_HANDLE)); VK(vkQueueWaitIdle(C.q)); vkFreeCommandBuffers(C.dev, C.pool, 1, &cb);
}

static void barrier(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to, VkImageAspectFlags aspect, uint32_t levels,
                    VkPipelineStageFlags2 s0, VkAccessFlags2 a0, VkPipelineStageFlags2 s1, VkAccessFlags2 a1) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = s0; b.srcAccessMask = a0; b.dstStageMask = s1; b.dstAccessMask = a1; b.oldLayout = from; b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.image = img; b.subresourceRange = {aspect, 0, levels, 0, 1};
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO}; di.imageMemoryBarrierCount = 1; di.pImageMemoryBarriers = &b; vkCmdPipelineBarrier2(cb, &di);
}

struct Tex { VkImage img; VkDeviceMemory mem; VkImageView view; };
static Tex make_tex(uint32_t w, uint32_t h, uint32_t levels, const std::vector<uint8_t>& data) {
    Tex t{};
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ii.imageType = VK_IMAGE_TYPE_2D; ii.format = VK_FORMAT_R8G8B8A8_UNORM; ii.extent = {w, h, 1};
    ii.mipLevels = levels; ii.arrayLayers = 1; ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK(vkCreateImage(C.dev, &ii, nullptr, &t.img));
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(C.dev, t.img, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = mr.size; ai.memoryTypeIndex = find_mem(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK(vkAllocateMemory(C.dev, &ai, nullptr, &t.mem)); VK(vkBindImageMemory(C.dev, t.img, t.mem, 0));
    Buf st = make_buf(data.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT); memcpy(st.p, data.data(), data.size());
    VkCommandBuffer cb = begin_cmd();
    barrier(cb, t.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, levels, VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    std::vector<VkBufferImageCopy> cp; size_t off = 0; uint32_t lw = w, lh = h;
    for (uint32_t l = 0; l < levels; l++) {
        VkBufferImageCopy c{}; c.bufferOffset = off; c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1}; c.imageExtent = {lw, lh, 1}; cp.push_back(c);
        off += (size_t)lw * lh * 4; lw = lw > 1 ? lw / 2 : 1; lh = lh > 1 ? lh / 2 : 1;
    }
    vkCmdCopyBufferToImage(cb, st.b, t.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (uint32_t)cp.size(), cp.data());
    barrier(cb, t.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, levels, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    end_cmd(cb); vkDestroyBuffer(C.dev, st.b, nullptr); vkFreeMemory(C.dev, st.m, nullptr);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = t.img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = ii.format; vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
    VK(vkCreateImageView(C.dev, &vi, nullptr, &t.view));
    return t;
}

struct Attr { uint32_t loc, off, fmt; };
struct Draw {
    std::string vs, ps; uint32_t spec = 0; std::string vb, ib, vsc, psc, shc; uint32_t stride = 0, vcount = 0, icount = 0; int cull = 0; int blend = 0; int depthwrite = 1;
    std::vector<Attr> attrs;
};
struct TexDesc { uint32_t idx, w, h, levels; std::string file; };
struct SmpDesc { uint32_t idx, linear, mip, addr, aniso; };
struct Scene { uint32_t W = 512, H = 512; float clear[4] = {0.08f, 0.08f, 0.1f, 1.0f}; std::vector<TexDesc> tex; std::vector<SmpDesc> smp; std::vector<Draw> draws; };

static Scene load_manifest(const std::string& dir) {
    Scene s; std::ifstream f(dir + "/manifest.txt"); std::string line; Draw* cur = nullptr;
    while (std::getline(f, line)) {
        std::istringstream in(line); std::string k; in >> k; if (k.empty() || k[0] == '#') continue;
        if (k == "size") in >> s.W >> s.H;
        else if (k == "clear") in >> s.clear[0] >> s.clear[1] >> s.clear[2] >> s.clear[3];
        else if (k == "tex") { TexDesc t; in >> t.idx >> t.w >> t.h >> t.levels >> t.file; s.tex.push_back(t); }
        else if (k == "smp") { SmpDesc d; in >> d.idx >> d.linear >> d.mip >> d.addr >> d.aniso; s.smp.push_back(d); }
        else if (k == "draw") { s.draws.emplace_back(); cur = &s.draws.back(); }
        else if (!cur) continue;
        else if (k == "vs") in >> cur->vs; else if (k == "ps") in >> cur->ps; else if (k == "spec") in >> cur->spec;
        else if (k == "vb") in >> cur->vb >> cur->stride >> cur->vcount; else if (k == "ib") in >> cur->ib >> cur->icount;
        else if (k == "attr") { Attr a; in >> a.loc >> a.off >> a.fmt; cur->attrs.push_back(a); }
        else if (k == "vsconst") in >> cur->vsc; else if (k == "psconst") in >> cur->psc; else if (k == "shared") in >> cur->shc;
        else if (k == "cull") in >> cur->cull; else if (k == "blend") in >> cur->blend; else if (k == "depthwrite") in >> cur->depthwrite;
    }
    return s;
}

static VkShaderModule load_shader(const std::string& dir, const std::string& name) {
    auto code = read_file(dir + "/" + name);
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; ci.codeSize = code.size(); ci.pCode = (const uint32_t*)code.data();
    VkShaderModule m; VK(vkCreateShaderModule(C.dev, &ci, nullptr, &m)); return m;
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: vkview <package dir> <out.ppm>\n"); return 2; }
    std::string dir = argv[1]; Scene sc = load_manifest(dir);
    // ---- instance / device
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion = VK_API_VERSION_1_3; app.pApplicationName = "vkview";
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &app;
    VK(vkCreateInstance(&ici, nullptr, &C.inst));
    uint32_t n = 0; vkEnumeratePhysicalDevices(C.inst, &n, nullptr); std::vector<VkPhysicalDevice> pds(n); vkEnumeratePhysicalDevices(C.inst, &n, pds.data());
    C.pd = pds[0];
    for (auto p : pds) { VkPhysicalDeviceProperties pr; vkGetPhysicalDeviceProperties(p, &pr); if (pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { C.pd = p; break; } }
    VkPhysicalDeviceProperties pr; vkGetPhysicalDeviceProperties(C.pd, &pr); fprintf(stderr, "device: %s\n", pr.deviceName);
    vkGetPhysicalDeviceMemoryProperties(C.pd, &C.mp);
    uint32_t nq = 0; vkGetPhysicalDeviceQueueFamilyProperties(C.pd, &nq, nullptr); std::vector<VkQueueFamilyProperties> qp(nq); vkGetPhysicalDeviceQueueFamilyProperties(C.pd, &nq, qp.data());
    C.qf = 0; for (uint32_t i = 0; i < nq; i++) if (qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { C.qf = i; break; }
    float prio = 1.0f; VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qi.queueFamilyIndex = C.qf; qi.queueCount = 1; qi.pQueuePriorities = &prio;
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES}; f13.dynamicRendering = VK_TRUE; f13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES}; f12.pNext = &f13;
    f12.bufferDeviceAddress = VK_TRUE; f12.runtimeDescriptorArray = VK_TRUE; f12.descriptorBindingPartiallyBound = VK_TRUE; f12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    f12.descriptorIndexing = VK_TRUE; f12.scalarBlockLayout = VK_TRUE;
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; f2.pNext = &f12;
    f2.features.shaderInt64 = VK_TRUE; f2.features.samplerAnisotropy = VK_TRUE; f2.features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
    f2.features.shaderClipDistance = VK_TRUE; f2.features.shaderFloat64 = VK_TRUE; f2.features.depthClamp = VK_TRUE;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; di.pNext = &f2; di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi;
    VK(vkCreateDevice(C.pd, &di, nullptr, &C.dev)); vkGetDeviceQueue(C.dev, C.qf, 0, &C.q);
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pci.queueFamilyIndex = C.qf; pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; VK(vkCreateCommandPool(C.dev, &pci, nullptr, &C.pool));

    // ---- descriptor heaps (4 sets, 1 binding each)
    const uint32_t HEAP = 256;
    VkDescriptorType types[4] = {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLER};
    VkDescriptorSetLayout sl[4];
    for (int i = 0; i < 4; i++) {
        VkDescriptorSetLayoutBinding b{}; b.binding = 0; b.descriptorType = types[i]; b.descriptorCount = HEAP; b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorBindingFlags fl = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
        VkDescriptorSetLayoutBindingFlagsCreateInfo bf{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO}; bf.bindingCount = 1; bf.pBindingFlags = &fl;
        VkDescriptorSetLayoutCreateInfo lc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; lc.pNext = &bf; lc.bindingCount = 1; lc.pBindings = &b;
        VK(vkCreateDescriptorSetLayout(C.dev, &lc, nullptr, &sl[i]));
    }
    VkDescriptorPoolSize ps[2] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, HEAP * 3}, {VK_DESCRIPTOR_TYPE_SAMPLER, HEAP}};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dpi.maxSets = 4; dpi.poolSizeCount = 2; dpi.pPoolSizes = ps; VkDescriptorPool dp; VK(vkCreateDescriptorPool(C.dev, &dpi, nullptr, &dp));
    VkDescriptorSet sets[4]; VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; dai.descriptorPool = dp; dai.descriptorSetCount = 4; dai.pSetLayouts = sl; VK(vkAllocateDescriptorSets(C.dev, &dai, sets));

    // ---- textures and samplers
    std::vector<Tex> texs; std::vector<VkDescriptorImageInfo> ii;
    for (auto& t : sc.tex) {
        Tex x = make_tex(t.w, t.h, t.levels, read_file(dir + "/" + t.file)); texs.push_back(x);
        VkDescriptorImageInfo d{}; d.imageView = x.view; d.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; ii.push_back(d);
    }
    std::vector<VkWriteDescriptorSet> wr;
    for (size_t i = 0; i < sc.tex.size(); i++) {
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet = sets[0]; w.dstBinding = 0; w.dstArrayElement = sc.tex[i].idx; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; w.pImageInfo = &ii[i]; wr.push_back(w);
    }
    std::vector<VkSampler> smps; std::vector<VkDescriptorImageInfo> si;
    for (auto& s : sc.smp) {
        VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; ci.magFilter = ci.minFilter = s.linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST; ci.mipmapMode = s.mip ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        VkSamplerAddressMode am = s.addr == 1 ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : (s.addr == 2 ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT : VK_SAMPLER_ADDRESS_MODE_REPEAT);
        ci.addressModeU = ci.addressModeV = ci.addressModeW = am; ci.anisotropyEnable = s.aniso > 1; ci.maxAnisotropy = (float)(s.aniso > 1 ? s.aniso : 1); ci.maxLod = 16.0f;
        VkSampler sm; VK(vkCreateSampler(C.dev, &ci, nullptr, &sm)); smps.push_back(sm); VkDescriptorImageInfo d{}; d.sampler = sm; si.push_back(d);
    }
    for (size_t i = 0; i < sc.smp.size(); i++) {
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet = sets[3]; w.dstBinding = 0; w.dstArrayElement = sc.smp[i].idx; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER; w.pImageInfo = &si[i]; wr.push_back(w);
    }
    if (!wr.empty()) vkUpdateDescriptorSets(C.dev, (uint32_t)wr.size(), wr.data(), 0, nullptr);

    // ---- pipeline layout
    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 24};
    VkPipelineLayoutCreateInfo plc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; plc.setLayoutCount = 4; plc.pSetLayouts = sl; plc.pushConstantRangeCount = 1; plc.pPushConstantRanges = &pcr;
    VkPipelineLayout pl; VK(vkCreatePipelineLayout(C.dev, &plc, nullptr, &pl));

    // ---- render targets
    VkImage colorImg, depthImg; VkDeviceMemory colorMem, depthMem; VkImageView colorView, depthView;
    auto mk2d = [&](VkFormat fmt, VkImageUsageFlags u, VkImage& im, VkDeviceMemory& mem, VkImageView& v, VkImageAspectFlags asp) {
        VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ic.imageType = VK_IMAGE_TYPE_2D; ic.format = fmt; ic.extent = {sc.W, sc.H, 1}; ic.mipLevels = ic.arrayLayers = 1; ic.samples = VK_SAMPLE_COUNT_1_BIT; ic.tiling = VK_IMAGE_TILING_OPTIMAL; ic.usage = u;
        VK(vkCreateImage(C.dev, &ic, nullptr, &im)); VkMemoryRequirements mr; vkGetImageMemoryRequirements(C.dev, im, &mr);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = mr.size; ai.memoryTypeIndex = find_mem(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK(vkAllocateMemory(C.dev, &ai, nullptr, &mem)); VK(vkBindImageMemory(C.dev, im, mem, 0));
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = im; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = fmt; vi.subresourceRange = {asp, 0, 1, 0, 1}; VK(vkCreateImageView(C.dev, &vi, nullptr, &v));
    };
    mk2d(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, colorImg, colorMem, colorView, VK_IMAGE_ASPECT_COLOR_BIT);
    mk2d(VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImg, depthMem, depthView, VK_IMAGE_ASPECT_DEPTH_BIT);

    // ---- per-draw resources
    struct DrawRes { VkPipeline pipe; Buf vb, ib, vsc, psc, shc; };
    std::vector<DrawRes> dr;
    for (auto& d : sc.draws) {
        DrawRes r{};
        auto vb = read_file(dir + "/" + d.vb), ib = read_file(dir + "/" + d.ib);
        r.vb = make_buf(vb.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT); memcpy(r.vb.p, vb.data(), vb.size());
        r.ib = make_buf(ib.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT); memcpy(r.ib.p, ib.data(), ib.size());
        auto upload = [&](const std::string& f, Buf& b) { auto v = read_file(dir + "/" + f); b = make_buf(v.size() < 16 ? 16 : v.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true); memcpy(b.p, v.data(), v.size()); };
        upload(d.vsc, r.vsc); upload(d.psc, r.psc); upload(d.shc, r.shc);
        VkShaderModule vsm = load_shader(dir, d.vs), psm = load_shader(dir, d.ps);
        VkSpecializationMapEntry se{0, 0, 4}; VkSpecializationInfo spi{1, &se, 4, &d.spec};
        VkPipelineShaderStageCreateInfo st[2]{{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
        st[0].stage = VK_SHADER_STAGE_VERTEX_BIT; st[0].module = vsm; st[0].pName = "main"; st[0].pSpecializationInfo = &spi;
        st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = psm; st[1].pName = "main"; st[1].pSpecializationInfo = &spi;
        VkVertexInputBindingDescription bd{0, d.stride, VK_VERTEX_INPUT_RATE_VERTEX};
        std::vector<VkVertexInputAttributeDescription> ad; for (auto& a : d.attrs) ad.push_back({a.loc, 0, (VkFormat)a.fmt, a.off});
        VkPipelineVertexInputStateCreateInfo vis{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO}; vis.vertexBindingDescriptionCount = 1; vis.pVertexBindingDescriptions = &bd; vis.vertexAttributeDescriptionCount = (uint32_t)ad.size(); vis.pVertexAttributeDescriptions = ad.data();
        VkPipelineInputAssemblyStateCreateInfo ias{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vps{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vps.viewportCount = vps.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f;
        rs.cullMode = d.cull == 0 ? VK_CULL_MODE_NONE : (d.cull == 1 ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT); rs.frontFace = VK_FRONT_FACE_CLOCKWISE; rs.depthClampEnable = VK_TRUE;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO}; dss.depthTestEnable = VK_TRUE; dss.depthWriteEnable = d.depthwrite ? VK_TRUE : VK_FALSE; dss.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        VkPipelineColorBlendAttachmentState cba{}; cba.colorWriteMask = 0xF;
        if (d.blend) { cba.blendEnable = VK_TRUE; cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; cba.colorBlendOp = VK_BLEND_OP_ADD; cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; cba.alphaBlendOp = VK_BLEND_OP_ADD; }
        VkPipelineColorBlendStateCreateInfo cbs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; cbs.attachmentCount = 1; cbs.pAttachments = &cba;
        VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}; VkPipelineDynamicStateCreateInfo dys{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dys.dynamicStateCount = 2; dys.pDynamicStates = dyn;
        VkFormat cf = VK_FORMAT_R8G8B8A8_UNORM; VkPipelineRenderingCreateInfo rci{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO}; rci.colorAttachmentCount = 1; rci.pColorAttachmentFormats = &cf; rci.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
        VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; gp.pNext = &rci; gp.stageCount = 2; gp.pStages = st; gp.pVertexInputState = &vis; gp.pInputAssemblyState = &ias; gp.pViewportState = &vps;
        gp.pRasterizationState = &rs; gp.pMultisampleState = &ms; gp.pDepthStencilState = &dss; gp.pColorBlendState = &cbs; gp.pDynamicState = &dys; gp.layout = pl;
        VkResult pr2 = vkCreateGraphicsPipelines(C.dev, VK_NULL_HANDLE, 1, &gp, nullptr, &r.pipe);
        if (pr2 != VK_SUCCESS) { fprintf(stderr, "pipeline creation failed (%d) for %s + %s\n", (int)pr2, d.vs.c_str(), d.ps.c_str()); r.pipe = VK_NULL_HANDLE; }
        dr.push_back(r);
    }

    // ---- record + submit
    VkCommandBuffer cb = begin_cmd();
    barrier(cb, colorImg, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    barrier(cb, depthImg, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT, 1, VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    VkRenderingAttachmentInfo ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}; ca.imageView = colorView; ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    memcpy(ca.clearValue.color.float32, sc.clear, 16);
    VkRenderingAttachmentInfo da{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}; da.imageView = depthView; da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL; da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; da.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; da.clearValue.depthStencil = {1.0f, 0};
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO}; ri.renderArea = {{0, 0}, {sc.W, sc.H}}; ri.layerCount = 1; ri.colorAttachmentCount = 1; ri.pColorAttachments = &ca; ri.pDepthAttachment = &da;
    vkCmdBeginRendering(cb, &ri);
    VkViewport vp{0.0f, (float)sc.H, (float)sc.W, -(float)sc.H, 0.0f, 1.0f};   // negative height: Direct3D-style y-up clip space
    VkRect2D scr{{0, 0}, {sc.W, sc.H}}; vkCmdSetViewport(cb, 0, 1, &vp); vkCmdSetScissor(cb, 0, 1, &scr);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 4, sets, 0, nullptr);
    int drawn = 0;
    for (size_t i = 0; i < sc.draws.size(); i++) {
        if (dr[i].pipe == VK_NULL_HANDLE) continue;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, dr[i].pipe);
        uint64_t pc[3] = {dr[i].vsc.addr, dr[i].psc.addr, dr[i].shc.addr}; vkCmdPushConstants(cb, pl, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 24, pc);
        VkDeviceSize zero = 0; vkCmdBindVertexBuffers(cb, 0, 1, &dr[i].vb.b, &zero); vkCmdBindIndexBuffer(cb, dr[i].ib.b, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, sc.draws[i].icount, 1, 0, 0, 0); drawn++;
    }
    vkCmdEndRendering(cb);
    barrier(cb, colorImg, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    Buf out = make_buf((VkDeviceSize)sc.W * sc.H * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    VkBufferImageCopy cp{}; cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; cp.imageExtent = {sc.W, sc.H, 1}; vkCmdCopyImageToBuffer(cb, colorImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, out.b, 1, &cp);
    end_cmd(cb);
    FILE* fo = fopen(argv[2], "wb"); fprintf(fo, "P6\n%u %u\n255\n", sc.W, sc.H);
    const uint8_t* px = (const uint8_t*)out.p; for (uint32_t i = 0; i < sc.W * sc.H; i++) fwrite(px + 4 * i, 1, 3, fo); fclose(fo);
    fprintf(stderr, "drew %d of %zu draws -> %s\n", drawn, sc.draws.size(), argv[2]);
    return 0;
}
