#ifdef __APPLE__

#include "RmlUi_Renderer_Metal.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/Platform.h>
#include <RmlUi/Core/SystemInterface.h>
#include <algorithm>
#include <string.h>
#include <unordered_map>

// Determines the anti-aliasing quality when creating layers.
#ifndef RMLUI_NUM_MSAA_SAMPLES
	#define RMLUI_NUM_MSAA_SAMPLES 2
#endif

#define MAX_NUM_STOPS 16
#define BLUR_SIZE 7
#define BLUR_NUM_WEIGHTS ((BLUR_SIZE + 1) / 2)

// ____________________
// MSL Shader Source
// ____________________

static const char* const g_metalShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

#define MAX_NUM_STOPS 16
#define BLUR_SIZE 7
#define BLUR_NUM_WEIGHTS 4

// -- Vertex structures --

struct RmlVertex {
	float2 position [[attribute(0)]];
	float4 color    [[attribute(1)]];  // UChar4Normalized is auto-converted to float4 by vertex fetch
	float2 texCoord [[attribute(2)]];
};

struct MainVertexOut {
	float4 position [[position]];
	float2 texCoord;
	float4 color;
};

struct PassthroughVertexOut {
	float4 position [[position]];
	float2 texCoord;
};

struct BlurVertexOut {
	float4 position [[position]];
	float2 texCoord0;
	float2 texCoord1;
	float2 texCoord2;
	float2 texCoord3;
	float2 texCoord4;
	float2 texCoord5;
	float2 texCoord6;
};

// -- Uniform structures --

struct MainUniforms {
	float2 translate;
	float4x4 transform;
};

struct GradientUniforms {
	int func;
	float2 p;
	float2 v;
	int num_stops;
	float stop_positions[MAX_NUM_STOPS];
	float4 stop_colors[MAX_NUM_STOPS];
};

struct CreationUniforms {
	float value;
	float2 dimensions;
};

struct ColorMatrixUniforms {
	float4x4 color_matrix;
};

struct BlurVertUniforms {
	float2 texelOffset;
};

struct BlurFragUniforms {
	float weights[BLUR_NUM_WEIGHTS];
	float2 texCoordMin;
	float2 texCoordMax;
};

struct DropShadowUniforms {
	float2 texCoordMin;
	float2 texCoordMax;
	float4 color;
};

// -- Vertex functions --

vertex MainVertexOut vertex_main(
	RmlVertex in [[stage_in]],
	constant MainUniforms& uniforms [[buffer(1)]])
{
	MainVertexOut out;
	out.texCoord = in.texCoord;
	out.color = in.color;

	float2 translatedPos = in.position + uniforms.translate;
	out.position = uniforms.transform * float4(translatedPos, 0.0, 1.0);
	return out;
}

vertex PassthroughVertexOut vertex_passthrough(
	RmlVertex in [[stage_in]])
{
	PassthroughVertexOut out;
	out.texCoord = in.texCoord;
	// Flip Y: Metal textures have origin at top-left (vs GL's bottom-left),
	// so fullscreen quads need Y negated to preserve correct orientation.
	out.position = float4(in.position.x, -in.position.y, 0.0, 1.0);
	return out;
}

vertex BlurVertexOut vertex_blur(
	RmlVertex in [[stage_in]],
	constant BlurVertUniforms& uniforms [[buffer(1)]])
{
	BlurVertexOut out;
	// Flip Y to match Metal's top-left texture origin (same as vertex_passthrough)
	out.position = float4(in.position.x, -in.position.y, 0.0, 1.0);
	out.texCoord0 = in.texCoord - float(0 - BLUR_NUM_WEIGHTS + 1) * uniforms.texelOffset;
	out.texCoord1 = in.texCoord - float(1 - BLUR_NUM_WEIGHTS + 1) * uniforms.texelOffset;
	out.texCoord2 = in.texCoord - float(2 - BLUR_NUM_WEIGHTS + 1) * uniforms.texelOffset;
	out.texCoord3 = in.texCoord - float(3 - BLUR_NUM_WEIGHTS + 1) * uniforms.texelOffset;
	out.texCoord4 = in.texCoord - float(4 - BLUR_NUM_WEIGHTS + 1) * uniforms.texelOffset;
	out.texCoord5 = in.texCoord - float(5 - BLUR_NUM_WEIGHTS + 1) * uniforms.texelOffset;
	out.texCoord6 = in.texCoord - float(6 - BLUR_NUM_WEIGHTS + 1) * uniforms.texelOffset;
	return out;
}

// -- Fragment functions --

fragment float4 fragment_color(
	MainVertexOut in [[stage_in]])
{
	return in.color;
}


fragment float4 fragment_texture(
	MainVertexOut in [[stage_in]],
	texture2d<float> tex [[texture(0)]],
	sampler samp [[sampler(0)]])
{
	float4 texColor = tex.sample(samp, in.texCoord);
	return in.color * texColor;
}

// Gradient definitions must match ShaderGradientFunction enum
#define GRADIENT_LINEAR 0
#define GRADIENT_RADIAL 1
#define GRADIENT_CONIC 2
#define GRADIENT_REPEATING_LINEAR 3
#define GRADIENT_REPEATING_RADIAL 4
#define GRADIENT_REPEATING_CONIC 5
#define PI 3.14159265

float4 mix_stop_colors(float t, constant GradientUniforms& u) {
	float4 color = u.stop_colors[0];

	for (int i = 1; i < u.num_stops; i++)
		color = mix(color, u.stop_colors[i], smoothstep(u.stop_positions[i-1], u.stop_positions[i], t));

	return color;
}

fragment float4 fragment_gradient(
	MainVertexOut in [[stage_in]],
	constant GradientUniforms& uniforms [[buffer(2)]])
{
	float t = 0.0;

	if (uniforms.func == GRADIENT_LINEAR || uniforms.func == GRADIENT_REPEATING_LINEAR)
	{
		float dist_square = dot(uniforms.v, uniforms.v);
		float2 V = in.texCoord - uniforms.p;
		t = dot(uniforms.v, V) / dist_square;
	}
	else if (uniforms.func == GRADIENT_RADIAL || uniforms.func == GRADIENT_REPEATING_RADIAL)
	{
		float2 V = in.texCoord - uniforms.p;
		t = length(uniforms.v * V);
	}
	else if (uniforms.func == GRADIENT_CONIC || uniforms.func == GRADIENT_REPEATING_CONIC)
	{
		float2x2 R = float2x2(uniforms.v.x, uniforms.v.y, -uniforms.v.y, uniforms.v.x);
		float2 V = R * (in.texCoord - uniforms.p);
		t = 0.5 + atan2(-V.x, V.y) / (2.0 * PI);
	}

	if (uniforms.func == GRADIENT_REPEATING_LINEAR || uniforms.func == GRADIENT_REPEATING_RADIAL || uniforms.func == GRADIENT_REPEATING_CONIC)
	{
		float t0 = uniforms.stop_positions[0];
		float t1 = uniforms.stop_positions[uniforms.num_stops - 1];
		t = t0 + fmod(t - t0, t1 - t0);
	}

	return in.color * mix_stop_colors(t, uniforms);
}

// "Creation" by Danilo Guanabara, based on: https://www.shadertoy.com/view/XsXXDn
fragment float4 fragment_creation(
	MainVertexOut in [[stage_in]],
	constant CreationUniforms& uniforms [[buffer(2)]])
{
	float t = uniforms.value;
	float3 c;
	float l;
	for (int i = 0; i < 3; i++) {
		float2 p = in.texCoord;
		float2 uv = p;
		p -= 0.5;
		p.x *= uniforms.dimensions.x / uniforms.dimensions.y;
		float z = t + float(i) * 0.07;
		l = length(p);
		uv += p / l * (sin(z) + 1.0) * abs(sin(l * 9.0 - z - z));
		c[i] = 0.01 / length(fmod(uv, 1.0) - 0.5);
	}
	return float4(c / l, in.color.a);
}

fragment float4 fragment_passthrough(
	PassthroughVertexOut in [[stage_in]],
	texture2d<float> tex [[texture(0)]],
	sampler samp [[sampler(0)]])
{
	return tex.sample(samp, in.texCoord);
}

fragment float4 fragment_color_matrix(
	PassthroughVertexOut in [[stage_in]],
	texture2d<float> tex [[texture(0)]],
	sampler samp [[sampler(0)]],
	constant ColorMatrixUniforms& uniforms [[buffer(2)]])
{
	float4 texColor = tex.sample(samp, in.texCoord);
	float3 transformedColor = float3(uniforms.color_matrix * texColor);
	return float4(transformedColor, texColor.a);
}

fragment float4 fragment_blend_mask(
	PassthroughVertexOut in [[stage_in]],
	texture2d<float> tex [[texture(0)]],
	texture2d<float> texMask [[texture(1)]],
	sampler samp [[sampler(0)]])
{
	float4 texColor = tex.sample(samp, in.texCoord);
	float maskAlpha = texMask.sample(samp, in.texCoord).a;
	return texColor * maskAlpha;
}

fragment float4 fragment_blur(
	BlurVertexOut in [[stage_in]],
	texture2d<float> tex [[texture(0)]],
	sampler samp [[sampler(0)]],
	constant BlurFragUniforms& uniforms [[buffer(2)]])
{
	float2 texCoords[BLUR_SIZE] = {
		in.texCoord0, in.texCoord1, in.texCoord2, in.texCoord3,
		in.texCoord4, in.texCoord5, in.texCoord6
	};

	float4 color = float4(0.0);
	for (int i = 0; i < BLUR_SIZE; i++)
	{
		float2 in_region = step(uniforms.texCoordMin, texCoords[i]) * step(texCoords[i], uniforms.texCoordMax);
		color += tex.sample(samp, texCoords[i]) * in_region.x * in_region.y * uniforms.weights[abs(i - BLUR_NUM_WEIGHTS + 1)];
	}
	return color;
}

fragment float4 fragment_drop_shadow(
	PassthroughVertexOut in [[stage_in]],
	texture2d<float> tex [[texture(0)]],
	sampler samp [[sampler(0)]],
	constant DropShadowUniforms& uniforms [[buffer(2)]])
{
	float2 in_region = step(uniforms.texCoordMin, in.texCoord) * step(in.texCoord, uniforms.texCoordMax);
	return tex.sample(samp, in.texCoord).a * in_region.x * in_region.y * uniforms.color;
}
)";

// ____________________
// Implementation
// ____________________

namespace MetalGfx {

enum class PipelineId {
	None,
	Color,
	Texture,
	Gradient,
	Creation,
	Passthrough,
	ColorMatrix,
	BlendMask,
	Blur,
	DropShadow,
	Count,
};

enum class BlendMode {
	PremultipliedAlpha,
	Replace,
	BlendFactor,
};

enum class StencilMode {
	Disabled,
	TestEqual,
	WriteSet,
	WriteSetInverse,
	WriteIntersect,
};

struct CompiledGeometryData {
	id<MTLBuffer> vertexBuffer;
	id<MTLBuffer> indexBuffer;
	uint32_t indexCount;
};

struct RenderTargetData {
	int width = 0;
	int height = 0;
	id<MTLTexture> msaaTexture = nil;       // MSAA color
	id<MTLTexture> resolveTexture = nil;     // Resolved non-MSAA color (also used as shader input)
	id<MTLTexture> stencilTexture = nil;     // Stencil buffer
	bool ownsStencilTexture = false;
};

struct PostprocessTargetData {
	int width = 0;
	int height = 0;
	id<MTLTexture> colorTexture = nil;
	bool needsInitialClear = true;  // First use must clear to avoid garbage
};

static id<MTLTexture> CreateMSAATexture(id<MTLDevice> device, int width, int height, MTLPixelFormat format, int samples)
{
	MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:width height:height mipmapped:NO];
	desc.textureType = MTLTextureType2DMultisample;
	desc.sampleCount = samples;
	desc.usage = MTLTextureUsageRenderTarget;
	desc.storageMode = MTLStorageModePrivate;
	return [device newTextureWithDescriptor:desc];
}

static id<MTLTexture> CreateTexture2D(id<MTLDevice> device, int width, int height, MTLPixelFormat format, bool renderTarget)
{
	MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:width height:height mipmapped:NO];
	desc.usage = MTLTextureUsageShaderRead | (renderTarget ? MTLTextureUsageRenderTarget : 0);
	desc.storageMode = MTLStorageModePrivate;
	return [device newTextureWithDescriptor:desc];
}

static id<MTLTexture> CreateStencilTexture(id<MTLDevice> device, int width, int height, int samples)
{
	MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8 width:width height:height mipmapped:NO];
	if (samples > 1)
	{
		desc.textureType = MTLTextureType2DMultisample;
		desc.sampleCount = samples;
	}
	desc.usage = MTLTextureUsageRenderTarget;
	desc.storageMode = MTLStorageModePrivate;
	return [device newTextureWithDescriptor:desc];
}

static void CreateRenderTarget(id<MTLDevice> device, RenderTargetData& out, int width, int height, int samples, id<MTLTexture> sharedStencil)
{
	out.width = width;
	out.height = height;
	out.msaaTexture = CreateMSAATexture(device, width, height, MTLPixelFormatRGBA8Unorm, samples);
	out.resolveTexture = CreateTexture2D(device, width, height, MTLPixelFormatRGBA8Unorm, true);

	if (sharedStencil)
	{
		out.stencilTexture = sharedStencil;
		out.ownsStencilTexture = false;
	}
	else
	{
		out.stencilTexture = CreateStencilTexture(device, width, height, samples);
		out.ownsStencilTexture = true;
	}
}

static void DestroyRenderTarget(RenderTargetData& rt)
{
	rt.msaaTexture = nil;
	rt.resolveTexture = nil;
	if (rt.ownsStencilTexture)
		rt.stencilTexture = nil;
	rt = {};
}

static void CreatePostprocessTarget(id<MTLDevice> device, PostprocessTargetData& out, int width, int height)
{
	out.width = width;
	out.height = height;
	out.colorTexture = CreateTexture2D(device, width, height, MTLPixelFormatRGBA8Unorm, true);
}

static void DestroyPostprocessTarget(PostprocessTargetData& pp)
{
	pp.colorTexture = nil;
	pp = {};
}

} // namespace MetalGfx

// ____________________
// Compiled filter/shader types (same as GL3)
// ____________________

enum class FilterType { Invalid = 0, Passthrough, Blur, DropShadow, ColorMatrix, MaskImage };
struct CompiledFilter {
	FilterType type;
	float blend_factor;
	float sigma;
	Rml::Vector2f offset;
	Rml::ColourbPremultiplied color;
	Rml::Matrix4f color_matrix;
};

enum class ShaderGradientFunction { Linear, Radial, Conic, RepeatingLinear, RepeatingRadial, RepeatingConic };
enum class CompiledShaderType { Invalid = 0, Gradient, Creation };
struct CompiledShader {
	CompiledShaderType type;
	ShaderGradientFunction gradient_function;
	Rml::Vector2f p;
	Rml::Vector2f v;
	Rml::Vector<float> stop_positions;
	Rml::Vector<Rml::Colourf> stop_colors;
	Rml::Vector2f dimensions;
};

// ____________________
// Impl (PIMPL)
// ____________________

struct RenderInterface_Metal::Impl
{
	Rml::CoreInstance& core_instance;
	id<MTLDevice> device = nil;
	id<MTLCommandQueue> commandQueue = nil;
	id<MTLLibrary> shaderLibrary = nil;

	// Vertex descriptor shared across pipelines
	MTLVertexDescriptor* vertexDescriptor = nil;

	// Pipeline states per (program, blend) combination
	struct PipelineKey {
		MetalGfx::PipelineId pipeline;
		MetalGfx::BlendMode blend;
		bool colorWriteDisabled;

		bool operator==(const PipelineKey& o) const { return pipeline == o.pipeline && blend == o.blend && colorWriteDisabled == o.colorWriteDisabled; }
	};
	struct PipelineKeyHash {
		size_t operator()(const PipelineKey& k) const
		{
			return std::hash<int>()(static_cast<int>(k.pipeline)) ^ (std::hash<int>()(static_cast<int>(k.blend)) << 8) ^ (std::hash<bool>()(k.colorWriteDisabled) << 16);
		}
	};
	std::unordered_map<PipelineKey, id<MTLRenderPipelineState>, PipelineKeyHash> pipelineStates;

	// Depth-stencil states
	id<MTLDepthStencilState> stencilDisabled = nil;  // No stencil test (for postprocess passes)
	id<MTLDepthStencilState> stencilAlwaysKeep = nil; // Stencil test ALWAYS, ops KEEP (default for MSAA layers, matches GL3)
	id<MTLDepthStencilState> stencilTestEqual = nil;
	id<MTLDepthStencilState> stencilWriteReplace = nil;
	id<MTLDepthStencilState> stencilWriteIncrement = nil;

	// Sampler states
	id<MTLSamplerState> samplerLinearClamp = nil;
	id<MTLSamplerState> samplerLinearRepeat = nil;

	// Current frame state
	id<MTLCommandBuffer> commandBuffer = nil;
	id<MTLRenderCommandEncoder> renderEncoder = nil;
	id<CAMetalDrawable> currentDrawable = nil;

	// Active pipeline tracking
	MetalGfx::PipelineId activePipeline = MetalGfx::PipelineId::None;
	MetalGfx::BlendMode activeBlend = MetalGfx::BlendMode::PremultipliedAlpha;
	bool activeColorWriteDisabled = false;
	MetalGfx::StencilMode activeStencilMode = MetalGfx::StencilMode::Disabled;
	int stencilRef = 1;
	bool stencilEnabled = false;
	bool clipMaskEnabled = false;

	// Transform state
	Rml::Matrix4f transform;
	Rml::Matrix4f projection;
	Rml::Rectanglei scissorState;

	// Viewport
	int viewportWidth = 0;
	int viewportHeight = 0;
	int viewportOffsetX = 0;
	int viewportOffsetY = 0;

	// Fullscreen quad
	Rml::CompiledGeometryHandle fullscreenQuadGeometry = {};

	// Active render target texture (for tracking which render pass we're in)
	id<MTLTexture> currentMSAATarget = nil;
	id<MTLTexture> currentResolveTarget = nil;
	id<MTLTexture> currentStencilTarget = nil;
	bool currentTargetNeedsClear = false;

	// Bound texture
	id<MTLTexture> boundTexture = nil;

	// Layer stack
	class RenderLayerStack {
	public:
		RenderLayerStack(Rml::CoreInstance& _coreInstance, id<MTLDevice> _device)
			: core_instance(_coreInstance), device(_device) { fb_postprocess.resize(4); }
		~RenderLayerStack() { DestroyAll(); }

		Rml::LayerHandle PushLayer()
		{
			RMLUI_ASSERT(layers_size <= (int)fb_layers.size());

			if (layers_size == (int)fb_layers.size())
			{
				id<MTLTexture> sharedStencil = fb_layers.empty() ? nil : fb_layers.front().stencilTexture;
				fb_layers.push_back({});
				MetalGfx::CreateRenderTarget(device, fb_layers.back(), width, height, RMLUI_NUM_MSAA_SAMPLES, sharedStencil);
			}

			layers_size += 1;
			return GetTopLayerHandle();
		}

		void PopLayer()
		{
			RMLUI_ASSERT(layers_size > 0);
			layers_size -= 1;
		}

		const MetalGfx::RenderTargetData& GetLayer(Rml::LayerHandle layer) const
		{
			RMLUI_ASSERT((size_t)layer < (size_t)layers_size);
			return fb_layers[layer];
		}

		const MetalGfx::RenderTargetData& GetTopLayer() const { return GetLayer(GetTopLayerHandle()); }

		Rml::LayerHandle GetTopLayerHandle() const
		{
			RMLUI_ASSERT(layers_size > 0);
			return static_cast<Rml::LayerHandle>(layers_size - 1);
		}

		MetalGfx::PostprocessTargetData& GetPostprocessPrimary() { return EnsurePostprocess(0); }
		MetalGfx::PostprocessTargetData& GetPostprocessSecondary() { return EnsurePostprocess(1); }
		MetalGfx::PostprocessTargetData& GetPostprocessTertiary() { return EnsurePostprocess(2); }
		MetalGfx::PostprocessTargetData& GetBlendMask() { return EnsurePostprocess(3); }

		void SwapPostprocessPrimarySecondary() { std::swap(fb_postprocess[0], fb_postprocess[1]); }

		void BeginFrame(int newWidth, int newHeight)
		{
			RMLUI_ASSERT(layers_size == 0);

			if (newWidth != width || newHeight != height)
			{
				width = newWidth;
				height = newHeight;
				DestroyAll();
			}

			PushLayer();
		}

		void EndFrame()
		{
			RMLUI_ASSERT(layers_size == 1);
			PopLayer();
		}

	private:
		void DestroyAll()
		{
			for (auto& rt : fb_layers)
				MetalGfx::DestroyRenderTarget(rt);
			fb_layers.clear();

			for (auto& pp : fb_postprocess)
				MetalGfx::DestroyPostprocessTarget(pp);
		}

		MetalGfx::PostprocessTargetData& EnsurePostprocess(int index)
		{
			RMLUI_ASSERT(index < (int)fb_postprocess.size());
			auto& pp = fb_postprocess[index];
			if (!pp.colorTexture)
				MetalGfx::CreatePostprocessTarget(device, pp, width, height);
			return pp;
		}

		Rml::CoreInstance& core_instance;
		id<MTLDevice> device;
		int width = 0, height = 0;
		int layers_size = 0;
		Rml::Vector<MetalGfx::RenderTargetData> fb_layers;
		Rml::Vector<MetalGfx::PostprocessTargetData> fb_postprocess;
	};

	Rml::UniquePtr<RenderLayerStack> renderLayers;

	// -- Methods --

	explicit Impl(Rml::CoreInstance& _coreInstance, id<MTLDevice> _device)
		: core_instance(_coreInstance), device(_device)
	{
		commandQueue = [device newCommandQueue];
	}

	bool Initialize()
	{
		if (!CreateShaderLibrary())
			return false;
		if (!CreateVertexDescriptor())
			return false;
		if (!CreatePipelineStates())
			return false;
		if (!CreateDepthStencilStates())
			return false;
		if (!CreateSamplerStates())
			return false;

		renderLayers = Rml::MakeUnique<RenderLayerStack>(core_instance, device);

		return true;
	}

	bool CreateShaderLibrary()
	{
		NSError* error = nil;
		NSString* source = [NSString stringWithUTF8String:g_metalShaderSource];
		MTLCompileOptions* options = [[MTLCompileOptions alloc] init];
		options.languageVersion = MTLLanguageVersion2_0;

		shaderLibrary = [device newLibraryWithSource:source options:options error:&error];
		if (!shaderLibrary)
		{
			Rml::Log::Message(core_instance, Rml::Log::LT_ERROR, "Failed to compile Metal shader library: %s",
				error ? [[error localizedDescription] UTF8String] : "unknown error");
			return false;
		}
		return true;
	}

	bool CreateVertexDescriptor()
	{
		vertexDescriptor = [[MTLVertexDescriptor alloc] init];

		// Position: float2 at offset 0
		vertexDescriptor.attributes[0].format = MTLVertexFormatFloat2;
		vertexDescriptor.attributes[0].offset = offsetof(Rml::Vertex, position);
		vertexDescriptor.attributes[0].bufferIndex = 0;

		// Color: uchar4 normalized at offset 8
		vertexDescriptor.attributes[1].format = MTLVertexFormatUChar4Normalized;
		vertexDescriptor.attributes[1].offset = offsetof(Rml::Vertex, colour);
		vertexDescriptor.attributes[1].bufferIndex = 0;

		// TexCoord: float2 at offset 12
		vertexDescriptor.attributes[2].format = MTLVertexFormatFloat2;
		vertexDescriptor.attributes[2].offset = offsetof(Rml::Vertex, tex_coord);
		vertexDescriptor.attributes[2].bufferIndex = 0;

		// Stride
		vertexDescriptor.layouts[0].stride = sizeof(Rml::Vertex);
		vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

		return true;
	}

	struct PipelineDef {
		MetalGfx::PipelineId id;
		const char* vertFunc;
		const char* fragFunc;
		MetalGfx::BlendMode blend;
		bool colorWriteDisabled;
	};

	bool CreatePipelineStates()
	{
		// Define all pipeline variants we need
		const PipelineDef defs[] = {
			// Normal rendering with premultiplied alpha
			{ MetalGfx::PipelineId::Color,       "vertex_main",        "fragment_color",        MetalGfx::BlendMode::PremultipliedAlpha, false },
			{ MetalGfx::PipelineId::Texture,      "vertex_main",        "fragment_texture",      MetalGfx::BlendMode::PremultipliedAlpha, false },
			{ MetalGfx::PipelineId::Gradient,     "vertex_main",        "fragment_gradient",     MetalGfx::BlendMode::PremultipliedAlpha, false },
			{ MetalGfx::PipelineId::Creation,     "vertex_main",        "fragment_creation",     MetalGfx::BlendMode::PremultipliedAlpha, false },
			{ MetalGfx::PipelineId::Passthrough,  "vertex_passthrough", "fragment_passthrough",  MetalGfx::BlendMode::PremultipliedAlpha, false },

			// Stencil-only rendering (for clip masks)
			{ MetalGfx::PipelineId::Color,        "vertex_main",        "fragment_color",        MetalGfx::BlendMode::PremultipliedAlpha, true },

			// Replace blend (no blending, for filter passes)
			{ MetalGfx::PipelineId::Passthrough,  "vertex_passthrough", "fragment_passthrough",  MetalGfx::BlendMode::Replace, false },
			{ MetalGfx::PipelineId::ColorMatrix,  "vertex_passthrough", "fragment_color_matrix", MetalGfx::BlendMode::Replace, false },
			{ MetalGfx::PipelineId::BlendMask,    "vertex_passthrough", "fragment_blend_mask",   MetalGfx::BlendMode::Replace, false },
			{ MetalGfx::PipelineId::Blur,         "vertex_blur",        "fragment_blur",         MetalGfx::BlendMode::Replace, false },
			{ MetalGfx::PipelineId::DropShadow,   "vertex_passthrough", "fragment_drop_shadow",  MetalGfx::BlendMode::Replace, false },

			// Passthrough with blend factor (for opacity filter)
			{ MetalGfx::PipelineId::Passthrough,  "vertex_passthrough", "fragment_passthrough",  MetalGfx::BlendMode::BlendFactor, false },
		};

		for (const auto& def : defs)
		{
			if (!CreateSinglePipeline(def))
				return false;
		}

		return true;
	}

	bool CreateSinglePipeline(const PipelineDef& def)
	{
		id<MTLFunction> vertFunc = [shaderLibrary newFunctionWithName:[NSString stringWithUTF8String:def.vertFunc]];
		id<MTLFunction> fragFunc = [shaderLibrary newFunctionWithName:[NSString stringWithUTF8String:def.fragFunc]];

		if (!vertFunc || !fragFunc)
		{
			Rml::Log::Message(core_instance, Rml::Log::LT_ERROR, "Failed to find Metal shader functions: %s / %s", def.vertFunc, def.fragFunc);
			return false;
		}

		MTLRenderPipelineDescriptor* pipeDesc = [[MTLRenderPipelineDescriptor alloc] init];
		pipeDesc.vertexFunction = vertFunc;
		pipeDesc.fragmentFunction = fragFunc;
		pipeDesc.vertexDescriptor = vertexDescriptor;

		// MSAA render targets
		pipeDesc.rasterSampleCount = RMLUI_NUM_MSAA_SAMPLES;
		pipeDesc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
		pipeDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
		pipeDesc.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;

		// Color write mask
		if (def.colorWriteDisabled)
			pipeDesc.colorAttachments[0].writeMask = MTLColorWriteMaskNone;
		else
			pipeDesc.colorAttachments[0].writeMask = MTLColorWriteMaskAll;

		// Blend mode
		switch (def.blend)
		{
		case MetalGfx::BlendMode::PremultipliedAlpha:
			pipeDesc.colorAttachments[0].blendingEnabled = YES;
			pipeDesc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
			pipeDesc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
			pipeDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
			pipeDesc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
			pipeDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
			pipeDesc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
			break;
		case MetalGfx::BlendMode::Replace:
			pipeDesc.colorAttachments[0].blendingEnabled = NO;
			break;
		case MetalGfx::BlendMode::BlendFactor:
			pipeDesc.colorAttachments[0].blendingEnabled = YES;
			pipeDesc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
			pipeDesc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
			pipeDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorBlendColor;
			pipeDesc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorZero;
			pipeDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorBlendAlpha;
			pipeDesc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorZero;
			break;
		}

		NSError* error = nil;
		id<MTLRenderPipelineState> pso = [device newRenderPipelineStateWithDescriptor:pipeDesc error:&error];
		if (!pso)
		{
			Rml::Log::Message(core_instance, Rml::Log::LT_ERROR, "Failed to create Metal pipeline state: %s",
				error ? [[error localizedDescription] UTF8String] : "unknown");
			return false;
		}

		PipelineKey key{def.id, def.blend, def.colorWriteDisabled};
		pipelineStates[key] = pso;

		// Also create non-MSAA variant for postprocess passes
		if (def.blend != MetalGfx::BlendMode::PremultipliedAlpha || def.id == MetalGfx::PipelineId::Passthrough ||
			def.id == MetalGfx::PipelineId::ColorMatrix || def.id == MetalGfx::PipelineId::BlendMask ||
			def.id == MetalGfx::PipelineId::Blur || def.id == MetalGfx::PipelineId::DropShadow)
		{
			pipeDesc.rasterSampleCount = 1;
			pipeDesc.stencilAttachmentPixelFormat = MTLPixelFormatInvalid;

			id<MTLRenderPipelineState> ppPso = [device newRenderPipelineStateWithDescriptor:pipeDesc error:&error];
			if (ppPso)
			{
				// Store with a special marker — use negative blend enum to distinguish
				// We'll use a different approach: postprocess pipelines have colorWriteDisabled = false and blend as-is,
				// but we need a way to look them up. Let's use a flag.
				// For simplicity, we store postprocess variants using sampleCount=1 prefix in the key.
				// Actually, let's just create these on the fly or use a separate map.
			}
		}

		return true;
	}

	// For postprocess passes (non-MSAA, no stencil), we create pipelines on demand
	std::unordered_map<PipelineKey, id<MTLRenderPipelineState>, PipelineKeyHash> postprocessPipelineStates;

	id<MTLRenderPipelineState> GetPostprocessPipeline(MetalGfx::PipelineId pipeId, MetalGfx::BlendMode blend)
	{
		PipelineKey key{pipeId, blend, false};
		auto it = postprocessPipelineStates.find(key);
		if (it != postprocessPipelineStates.end())
			return it->second;

		// Create on demand
		const char* vertFuncName = "vertex_passthrough";
		const char* fragFuncName = "fragment_passthrough";

		switch (pipeId)
		{
		case MetalGfx::PipelineId::Color: vertFuncName = "vertex_main"; fragFuncName = "fragment_color"; break;
		case MetalGfx::PipelineId::Texture: vertFuncName = "vertex_main"; fragFuncName = "fragment_texture"; break;
		case MetalGfx::PipelineId::Passthrough: break; // defaults are correct
		case MetalGfx::PipelineId::ColorMatrix: fragFuncName = "fragment_color_matrix"; break;
		case MetalGfx::PipelineId::BlendMask: fragFuncName = "fragment_blend_mask"; break;
		case MetalGfx::PipelineId::Blur: vertFuncName = "vertex_blur"; fragFuncName = "fragment_blur"; break;
		case MetalGfx::PipelineId::DropShadow: fragFuncName = "fragment_drop_shadow"; break;
		default: break;
		}

		id<MTLFunction> vf = [shaderLibrary newFunctionWithName:[NSString stringWithUTF8String:vertFuncName]];
		id<MTLFunction> ff = [shaderLibrary newFunctionWithName:[NSString stringWithUTF8String:fragFuncName]];
		if (!vf || !ff) return nil;

		MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
		desc.vertexFunction = vf;
		desc.fragmentFunction = ff;
		desc.vertexDescriptor = vertexDescriptor;
		desc.rasterSampleCount = 1;
		desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
		desc.stencilAttachmentPixelFormat = MTLPixelFormatInvalid;

		switch (blend)
		{
		case MetalGfx::BlendMode::PremultipliedAlpha:
			desc.colorAttachments[0].blendingEnabled = YES;
			desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
			desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
			desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
			desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
			desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
			desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
			break;
		case MetalGfx::BlendMode::Replace:
			desc.colorAttachments[0].blendingEnabled = NO;
			break;
		case MetalGfx::BlendMode::BlendFactor:
			desc.colorAttachments[0].blendingEnabled = YES;
			desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
			desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
			desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorBlendColor;
			desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorZero;
			desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorBlendAlpha;
			desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorZero;
			break;
		}

		NSError* error = nil;
		id<MTLRenderPipelineState> pso = [device newRenderPipelineStateWithDescriptor:desc error:&error];
		if (pso)
		{
			postprocessPipelineStates[key] = pso;
		}
		else
		{
			Rml::Log::Message(core_instance, Rml::Log::LT_ERROR, "Failed to create postprocess pipeline %d blend %d: %s",
				(int)pipeId, (int)blend, error ? [[error localizedDescription] UTF8String] : "unknown");
		}
		return pso;
	}

	// Also need a pipeline for the final blit to the drawable (which uses BGRAUnorm)
	id<MTLRenderPipelineState> drawablePipeline = nil;

	bool CreateDrawablePipeline(MTLPixelFormat drawableFormat)
	{
		if (drawablePipeline)
			return true;

		id<MTLFunction> vf = [shaderLibrary newFunctionWithName:@"vertex_passthrough"];
		id<MTLFunction> ff = [shaderLibrary newFunctionWithName:@"fragment_passthrough"];
		if (!vf || !ff) return false;

		MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
		desc.vertexFunction = vf;
		desc.fragmentFunction = ff;
		desc.vertexDescriptor = vertexDescriptor;
		desc.rasterSampleCount = 1;
		desc.colorAttachments[0].pixelFormat = drawableFormat;

		// Premultiplied alpha blend for final composite
		desc.colorAttachments[0].blendingEnabled = YES;
		desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
		desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
		desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
		desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
		desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
		desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

		NSError* error = nil;
		drawablePipeline = [device newRenderPipelineStateWithDescriptor:desc error:&error];
		return drawablePipeline != nil;
	}

	bool CreateDepthStencilStates()
	{
		// Disabled (no stencil — for postprocess passes without stencil attachment)
		{
			MTLDepthStencilDescriptor* desc = [[MTLDepthStencilDescriptor alloc] init];
			desc.depthCompareFunction = MTLCompareFunctionAlways;
			desc.depthWriteEnabled = NO;
			stencilDisabled = [device newDepthStencilStateWithDescriptor:desc];
		}

		// Always pass, keep ops (default for MSAA layers — matches GL3's BeginFrame state)
		{
			MTLDepthStencilDescriptor* desc = [[MTLDepthStencilDescriptor alloc] init];
			desc.depthCompareFunction = MTLCompareFunctionAlways;
			desc.depthWriteEnabled = NO;

			MTLStencilDescriptor* stencilDesc = [[MTLStencilDescriptor alloc] init];
			stencilDesc.stencilCompareFunction = MTLCompareFunctionAlways;
			stencilDesc.stencilFailureOperation = MTLStencilOperationKeep;
			stencilDesc.depthFailureOperation = MTLStencilOperationKeep;
			stencilDesc.depthStencilPassOperation = MTLStencilOperationKeep;
			stencilDesc.readMask = 0xFF;
			stencilDesc.writeMask = 0xFF;

			desc.frontFaceStencil = stencilDesc;
			desc.backFaceStencil = stencilDesc;
			stencilAlwaysKeep = [device newDepthStencilStateWithDescriptor:desc];
		}

		// Test equal
		{
			MTLDepthStencilDescriptor* desc = [[MTLDepthStencilDescriptor alloc] init];
			desc.depthCompareFunction = MTLCompareFunctionAlways;
			desc.depthWriteEnabled = NO;

			MTLStencilDescriptor* stencilDesc = [[MTLStencilDescriptor alloc] init];
			stencilDesc.stencilCompareFunction = MTLCompareFunctionEqual;
			stencilDesc.stencilFailureOperation = MTLStencilOperationKeep;
			stencilDesc.depthFailureOperation = MTLStencilOperationKeep;
			stencilDesc.depthStencilPassOperation = MTLStencilOperationKeep;
			stencilDesc.readMask = 0xFF;
			stencilDesc.writeMask = 0xFF;

			desc.frontFaceStencil = stencilDesc;
			desc.backFaceStencil = stencilDesc;
			stencilTestEqual = [device newDepthStencilStateWithDescriptor:desc];
		}

		// Write replace (for Set/SetInverse)
		{
			MTLDepthStencilDescriptor* desc = [[MTLDepthStencilDescriptor alloc] init];
			desc.depthCompareFunction = MTLCompareFunctionAlways;
			desc.depthWriteEnabled = NO;

			MTLStencilDescriptor* stencilDesc = [[MTLStencilDescriptor alloc] init];
			stencilDesc.stencilCompareFunction = MTLCompareFunctionAlways;
			stencilDesc.stencilFailureOperation = MTLStencilOperationKeep;
			stencilDesc.depthFailureOperation = MTLStencilOperationKeep;
			stencilDesc.depthStencilPassOperation = MTLStencilOperationReplace;
			stencilDesc.readMask = 0xFF;
			stencilDesc.writeMask = 0xFF;

			desc.frontFaceStencil = stencilDesc;
			desc.backFaceStencil = stencilDesc;
			stencilWriteReplace = [device newDepthStencilStateWithDescriptor:desc];
		}

		// Write increment (for Intersect)
		{
			MTLDepthStencilDescriptor* desc = [[MTLDepthStencilDescriptor alloc] init];
			desc.depthCompareFunction = MTLCompareFunctionAlways;
			desc.depthWriteEnabled = NO;

			MTLStencilDescriptor* stencilDesc = [[MTLStencilDescriptor alloc] init];
			stencilDesc.stencilCompareFunction = MTLCompareFunctionAlways;
			stencilDesc.stencilFailureOperation = MTLStencilOperationKeep;
			stencilDesc.depthFailureOperation = MTLStencilOperationKeep;
			stencilDesc.depthStencilPassOperation = MTLStencilOperationIncrementClamp;
			stencilDesc.readMask = 0xFF;
			stencilDesc.writeMask = 0xFF;

			desc.frontFaceStencil = stencilDesc;
			desc.backFaceStencil = stencilDesc;
			stencilWriteIncrement = [device newDepthStencilStateWithDescriptor:desc];
		}

		return stencilDisabled && stencilAlwaysKeep && stencilTestEqual && stencilWriteReplace && stencilWriteIncrement;
	}

	bool CreateSamplerStates()
	{
		// Linear clamp (for postprocess FBs)
		{
			MTLSamplerDescriptor* desc = [[MTLSamplerDescriptor alloc] init];
			desc.minFilter = MTLSamplerMinMagFilterLinear;
			desc.magFilter = MTLSamplerMinMagFilterLinear;
			desc.sAddressMode = MTLSamplerAddressModeClampToZero;
			desc.tAddressMode = MTLSamplerAddressModeClampToZero;
			samplerLinearClamp = [device newSamplerStateWithDescriptor:desc];
		}

		// Linear repeat (for UI textures — no mipmap filter for now)
		{
			MTLSamplerDescriptor* desc = [[MTLSamplerDescriptor alloc] init];
			desc.minFilter = MTLSamplerMinMagFilterLinear;
			desc.magFilter = MTLSamplerMinMagFilterLinear;
			desc.mipFilter = MTLSamplerMipFilterNotMipmapped;
			desc.sAddressMode = MTLSamplerAddressModeRepeat;
			desc.tAddressMode = MTLSamplerAddressModeRepeat;
			samplerLinearRepeat = [device newSamplerStateWithDescriptor:desc];
		}

		return samplerLinearClamp && samplerLinearRepeat;
	}

	// -- Render pass management --

	void EndCurrentRenderEncoder()
	{
		if (renderEncoder)
		{
			[renderEncoder endEncoding];
			renderEncoder = nil;
		}
	}

	void BeginRenderPassOnLayer(const MetalGfx::RenderTargetData& layer, bool clear)
	{
		EndCurrentRenderEncoder();

		MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
		passDesc.colorAttachments[0].texture = layer.msaaTexture;
		passDesc.colorAttachments[0].resolveTexture = layer.resolveTexture;
		passDesc.colorAttachments[0].loadAction = clear ? MTLLoadActionClear : MTLLoadActionLoad;
		// Must use StoreAndMultisampleResolve to preserve MSAA data for re-entry
		// (e.g. after CompositeLayers or PopLayer). Plain MultisampleResolve discards
		// the MSAA data, causing magenta garbage on reload.
		passDesc.colorAttachments[0].storeAction = MTLStoreActionStoreAndMultisampleResolve;
		passDesc.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);

		// Combined depth-stencil texture (Depth32Float_Stencil8)
		passDesc.depthAttachment.texture = layer.stencilTexture;
		passDesc.depthAttachment.loadAction = clear ? MTLLoadActionClear : MTLLoadActionLoad;
		passDesc.depthAttachment.storeAction = MTLStoreActionStore;
		passDesc.depthAttachment.clearDepth = 1.0;

		passDesc.stencilAttachment.texture = layer.stencilTexture;
		passDesc.stencilAttachment.loadAction = clear ? MTLLoadActionClear : MTLLoadActionLoad;
		passDesc.stencilAttachment.storeAction = MTLStoreActionStore;
		passDesc.stencilAttachment.clearStencil = 0;

		renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:passDesc];

		// Set default state
		MTLViewport vp = {0, 0, (double)viewportWidth, (double)viewportHeight, 0, 1};
		[renderEncoder setViewport:vp];

		if (clipMaskEnabled)
		{
			[renderEncoder setDepthStencilState:stencilTestEqual];
			[renderEncoder setStencilReferenceValue:stencilRef];
		}
		else
		{
			// Use ALWAYS/KEEP instead of disabled — matches GL3's default stencil state.
			// Stencil must remain enabled on MSAA layers so that stencil clear/write
			// operations in RenderToClipMask work correctly.
			[renderEncoder setDepthStencilState:stencilAlwaysKeep];
		}

		if (scissorState.Valid())
		{
			const int x = Rml::Math::Clamp(scissorState.Left(), 0, viewportWidth);
			const int y = Rml::Math::Clamp(scissorState.Top(), 0, viewportHeight);
			const int w = Rml::Math::Clamp(scissorState.Width(), 0, viewportWidth - x);
			const int h = Rml::Math::Clamp(scissorState.Height(), 0, viewportHeight - y);
			MTLScissorRect sr = {(NSUInteger)x, (NSUInteger)y, (NSUInteger)w, (NSUInteger)h};
			[renderEncoder setScissorRect:sr];
		}

		currentMSAATarget = layer.msaaTexture;
		currentResolveTarget = layer.resolveTexture;
		currentStencilTarget = layer.stencilTexture;
		activePipeline = MetalGfx::PipelineId::None; // Force re-bind
	}

	void BeginRenderPassOnPostprocess(MetalGfx::PostprocessTargetData& target, bool clear)
	{
		EndCurrentRenderEncoder();

		// Force clear on first use to avoid reading uninitialized texture data (shows as magenta)
		if (target.needsInitialClear)
		{
			clear = true;
			target.needsInitialClear = false;
		}

		MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
		passDesc.colorAttachments[0].texture = target.colorTexture;
		passDesc.colorAttachments[0].loadAction = clear ? MTLLoadActionClear : MTLLoadActionLoad;
		passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
		passDesc.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);

		renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:passDesc];

		MTLViewport vp = {0, 0, (double)target.width, (double)target.height, 0, 1};
		[renderEncoder setViewport:vp];
		[renderEncoder setDepthStencilState:stencilDisabled];

		currentMSAATarget = nil;
		currentResolveTarget = nil;
		currentStencilTarget = nil;
		activePipeline = MetalGfx::PipelineId::None;
	}

	void BeginRenderPassOnDrawable(id<CAMetalDrawable> drawable, MTLPixelFormat format)
	{
		EndCurrentRenderEncoder();

		MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
		passDesc.colorAttachments[0].texture = drawable.texture;
		passDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
		passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
		passDesc.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);

		renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:passDesc];

		MTLViewport vp = {(double)viewportOffsetX, (double)viewportOffsetY, (double)viewportWidth, (double)viewportHeight, 0, 1};
		[renderEncoder setViewport:vp];
		[renderEncoder setDepthStencilState:stencilDisabled];

		currentMSAATarget = nil;
		currentResolveTarget = nil;
		currentStencilTarget = nil;
		activePipeline = MetalGfx::PipelineId::None;
	}

	// MSAA resolve from layer to postprocess target (via render pass store action)
	void ResolveLayerToPostprocess(const MetalGfx::RenderTargetData& layer, MetalGfx::PostprocessTargetData& target)
	{
		EndCurrentRenderEncoder();

		// The layer's resolve texture already has the resolved data if the render pass used
		// MTLStoreActionMultisampleResolve. We just need to copy/blit it to the postprocess target.
		id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];

		const NSUInteger w = Rml::Math::Min(layer.width, target.width);
		const NSUInteger h = Rml::Math::Min(layer.height, target.height);

		[blit copyFromTexture:layer.resolveTexture
				  sourceSlice:0
				  sourceLevel:0
				 sourceOrigin:MTLOriginMake(0, 0, 0)
				   sourceSize:MTLSizeMake(w, h, 1)
					toTexture:target.colorTexture
			 destinationSlice:0
			 destinationLevel:0
			destinationOrigin:MTLOriginMake(0, 0, 0)];

		[blit endEncoding];
		target.needsInitialClear = false;
	}

	void UsePipeline(MetalGfx::PipelineId pipeId, MetalGfx::BlendMode blend = MetalGfx::BlendMode::PremultipliedAlpha, bool colorWriteDisabled = false)
	{
		if (!renderEncoder) return;

		PipelineKey key{pipeId, blend, colorWriteDisabled};
		auto it = pipelineStates.find(key);
		if (it == pipelineStates.end())
		{
			Rml::Log::Message(core_instance, Rml::Log::LT_ERROR, "UsePipeline FAILED: pipeline=%d blend=%d cwd=%d not found! (have %d pipelines)",
				(int)pipeId, (int)blend, (int)colorWriteDisabled, (int)pipelineStates.size());
			return;
		}

		[renderEncoder setRenderPipelineState:it->second];
		activePipeline = pipeId;
		activeBlend = blend;
		activeColorWriteDisabled = colorWriteDisabled;
	}

	void UsePostprocessPipeline(MetalGfx::PipelineId pipeId, MetalGfx::BlendMode blend = MetalGfx::BlendMode::Replace)
	{
		if (!renderEncoder) return;

		auto pso = GetPostprocessPipeline(pipeId, blend);
		if (!pso) return;

		[renderEncoder setRenderPipelineState:pso];
		activePipeline = pipeId;
		activeBlend = blend;
	}

	void SetScissorOnEncoder(Rml::Rectanglei region)
	{
		if (!renderEncoder) return;

		if (region.Valid())
		{
			const int x = Rml::Math::Clamp(region.Left(), 0, viewportWidth);
			const int y = Rml::Math::Clamp(region.Top(), 0, viewportHeight);
			const int w = Rml::Math::Max(Rml::Math::Clamp(region.Width(), 0, viewportWidth - x), 1);
			const int h = Rml::Math::Max(Rml::Math::Clamp(region.Height(), 0, viewportHeight - y), 1);
			MTLScissorRect sr = {(NSUInteger)x, (NSUInteger)y, (NSUInteger)w, (NSUInteger)h};
			[renderEncoder setScissorRect:sr];
		}
		else
		{
			// Full viewport
			MTLScissorRect sr = {0, 0, (NSUInteger)viewportWidth, (NSUInteger)viewportHeight};
			[renderEncoder setScissorRect:sr];
		}
	}
};

// ____________________
// RenderInterface_Metal implementation
// ____________________

RenderInterface_Metal::RenderInterface_Metal(Rml::CoreInstance& in_core_instance, void* _device)
	: RenderInterface(in_core_instance)
{
	m_impl = Rml::MakeUnique<Impl>(in_core_instance, (__bridge id<MTLDevice>)_device);
	if (m_impl->Initialize())
	{
		m_initialized = true;

		// Create fullscreen quad geometry
		Rml::Mesh mesh;
		Rml::MeshUtilities::GenerateQuad(mesh, Rml::Vector2f(-1), Rml::Vector2f(2), {});
		m_impl->fullscreenQuadGeometry = CompileGeometry(mesh.vertices, mesh.indices);
	}
}

RenderInterface_Metal::~RenderInterface_Metal()
{
	if (m_impl && m_impl->fullscreenQuadGeometry)
	{
		ReleaseGeometry(m_impl->fullscreenQuadGeometry);
		m_impl->fullscreenQuadGeometry = {};
	}
}

void RenderInterface_Metal::SetViewport(int _width, int _height, int _offsetX, int _offsetY)
{
	m_impl->viewportWidth = Rml::Math::Max(_width, 1);
	m_impl->viewportHeight = Rml::Math::Max(_height, 1);
	m_impl->viewportOffsetX = _offsetX;
	m_impl->viewportOffsetY = _offsetY;
	m_impl->projection = Rml::Matrix4f::ProjectOrtho(0, (float)m_impl->viewportWidth, (float)m_impl->viewportHeight, 0, -10000, 10000);
}

void RenderInterface_Metal::BeginFrame(void* _drawable)
{
	RMLUI_ASSERT(m_impl->viewportWidth >= 1 && m_impl->viewportHeight >= 1);

	id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)_drawable;
	m_impl->currentDrawable = drawable;
	m_impl->commandBuffer = [m_impl->commandQueue commandBuffer];

	// Create drawable pipeline if needed
	m_impl->CreateDrawablePipeline(drawable.texture.pixelFormat);

	SetTransform(nullptr);

	m_impl->renderLayers->BeginFrame(m_impl->viewportWidth, m_impl->viewportHeight);

	const auto& topLayer = m_impl->renderLayers->GetTopLayer();
	m_impl->BeginRenderPassOnLayer(topLayer, true);

	m_impl->scissorState = Rml::Rectanglei::MakeInvalid();
	m_impl->clipMaskEnabled = false;
	m_impl->stencilRef = 1;
}

void RenderInterface_Metal::EndFrame()
{
	// Resolve MSAA top layer to postprocess primary
	const auto& topLayer = m_impl->renderLayers->GetTopLayer();
	auto& ppPrimary = m_impl->renderLayers->GetPostprocessPrimary();

	m_impl->EndCurrentRenderEncoder();

	// The resolve already happened via MTLStoreActionMultisampleResolve when we ended the render pass.
	// Now copy the resolved texture to postprocess primary.
	m_impl->ResolveLayerToPostprocess(topLayer, ppPrimary);

	// Draw to drawable
	m_impl->BeginRenderPassOnDrawable(m_impl->currentDrawable, m_impl->currentDrawable.texture.pixelFormat);

	[m_impl->renderEncoder setRenderPipelineState:m_impl->drawablePipeline];
	[m_impl->renderEncoder setFragmentTexture:ppPrimary.colorTexture atIndex:0];
	[m_impl->renderEncoder setFragmentSamplerState:m_impl->samplerLinearClamp atIndex:0];

	// Draw fullscreen quad
	auto* geometry = reinterpret_cast<MetalGfx::CompiledGeometryData*>(m_impl->fullscreenQuadGeometry);
	[m_impl->renderEncoder setVertexBuffer:geometry->vertexBuffer offset:0 atIndex:0];
	[m_impl->renderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
									  indexCount:geometry->indexCount
									   indexType:MTLIndexTypeUInt32
									 indexBuffer:geometry->indexBuffer
								   indexBufferOffset:0];

	m_impl->EndCurrentRenderEncoder();

	m_impl->renderLayers->EndFrame();

	[m_impl->commandBuffer presentDrawable:m_impl->currentDrawable];
	[m_impl->commandBuffer commit];

	m_impl->commandBuffer = nil;
	m_impl->currentDrawable = nil;
}

void RenderInterface_Metal::Clear()
{
	// Clear is handled by render pass load action
}

Rml::CompiledGeometryHandle RenderInterface_Metal::CompileGeometry(Rml::Span<const Rml::Vertex> _vertices, Rml::Span<const int> _indices)
{
	auto* geometry = new MetalGfx::CompiledGeometryData;

	geometry->vertexBuffer = [m_impl->device newBufferWithBytes:_vertices.data()
														 length:sizeof(Rml::Vertex) * _vertices.size()
														options:MTLResourceStorageModeShared];

	geometry->indexBuffer = [m_impl->device newBufferWithBytes:_indices.data()
														length:sizeof(int) * _indices.size()
													   options:MTLResourceStorageModeShared];

	geometry->indexCount = static_cast<uint32_t>(_indices.size());

	return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry);
}

void RenderInterface_Metal::RenderGeometry(Rml::CompiledGeometryHandle _handle, Rml::Vector2f _translation, Rml::TextureHandle _texture)
{
	auto* geometry = reinterpret_cast<MetalGfx::CompiledGeometryData*>(_handle);
	if (!geometry || !m_impl->renderEncoder) return;

	if (_texture == TexturePostprocess)
	{
		// Do nothing (pipeline/texture already set)
	}
	else if (_texture)
	{
		m_impl->UsePipeline(MetalGfx::PipelineId::Texture);

		// Set transform uniforms
		struct { float x, y; float padding[2]; Rml::Matrix4f transform; } uniforms;
		uniforms.x = _translation.x;
		uniforms.y = _translation.y;
		uniforms.transform = m_impl->transform;
		[m_impl->renderEncoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];

		if (_texture != TextureEnableWithoutBinding)
		{
			auto mtlTexture = (__bridge id<MTLTexture>)(void*)_texture;
			[m_impl->renderEncoder setFragmentTexture:mtlTexture atIndex:0];
			[m_impl->renderEncoder setFragmentSamplerState:m_impl->samplerLinearRepeat atIndex:0];
		}
	}
	else
	{
		m_impl->UsePipeline(MetalGfx::PipelineId::Color);

		struct { float x, y; float padding[2]; Rml::Matrix4f transform; } uniforms;
		uniforms.x = _translation.x;
		uniforms.y = _translation.y;
		uniforms.transform = m_impl->transform;
		[m_impl->renderEncoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
	}

	[m_impl->renderEncoder setVertexBuffer:geometry->vertexBuffer offset:0 atIndex:0];
	[m_impl->renderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
									  indexCount:geometry->indexCount
									   indexType:MTLIndexTypeUInt32
									 indexBuffer:geometry->indexBuffer
								   indexBufferOffset:0];
}

void RenderInterface_Metal::ReleaseGeometry(Rml::CompiledGeometryHandle _handle)
{
	auto* geometry = reinterpret_cast<MetalGfx::CompiledGeometryData*>(_handle);
	geometry->vertexBuffer = nil;
	geometry->indexBuffer = nil;
	delete geometry;
}

Rml::TextureHandle RenderInterface_Metal::LoadTexture(Rml::Vector2i& _textureDimensions, const Rml::String& _source)
{
	Rml::FileInterface* file_interface = Rml::GetFileInterface(core_instance);
	Rml::FileHandle file_handle = file_interface->Open(_source);
	if (!file_handle)
		return false;

	file_interface->Seek(file_handle, 0, SEEK_END);
	size_t buffer_size = file_interface->Tell(file_handle);
	file_interface->Seek(file_handle, 0, SEEK_SET);

	if (buffer_size <= sizeof(char) * 18) // TGA header size
	{
		file_interface->Close(file_handle);
		return false;
	}

	using Rml::byte;
	Rml::UniquePtr<byte[]> buffer(new byte[buffer_size]);
	file_interface->Read(buffer.get(), buffer_size, file_handle);
	file_interface->Close(file_handle);

	// Parse TGA header (same as GL3)
#pragma pack(1)
	struct TGAHeader {
		char idLength;
		char colourMapType;
		char dataType;
		short int colourMapOrigin;
		short int colourMapLength;
		char colourMapDepth;
		short int xOrigin;
		short int yOrigin;
		short int width;
		short int height;
		char bitsPerPixel;
		char imageDescriptor;
	};
#pragma pack()

	TGAHeader header;
	memcpy(&header, buffer.get(), sizeof(TGAHeader));

	int color_mode = header.bitsPerPixel / 8;
	const size_t image_size = header.width * header.height * 4;

	if (header.dataType != 2 || color_mode < 3)
		return false;

	const byte* image_src = buffer.get() + sizeof(TGAHeader);
	Rml::UniquePtr<byte[]> image_dest_buffer(new byte[image_size]);
	byte* image_dest = image_dest_buffer.get();

	// Targa is BGR, swap to RGB, flip Y axis, convert to premultiplied alpha
	for (long y = 0; y < header.height; y++)
	{
		long read_index = y * header.width * color_mode;
		long write_index = ((header.imageDescriptor & 32) != 0) ? read_index : (header.height - y - 1) * header.width * 4;
		for (long x = 0; x < header.width; x++)
		{
			image_dest[write_index] = image_src[read_index + 2];
			image_dest[write_index + 1] = image_src[read_index + 1];
			image_dest[write_index + 2] = image_src[read_index];
			if (color_mode == 4)
			{
				const byte alpha = image_src[read_index + 3];
				for (size_t j = 0; j < 3; j++)
					image_dest[write_index + j] = byte((image_dest[write_index + j] * alpha) / 255);
				image_dest[write_index + 3] = alpha;
			}
			else
				image_dest[write_index + 3] = 255;

			write_index += 4;
			read_index += color_mode;
		}
	}

	_textureDimensions.x = header.width;
	_textureDimensions.y = header.height;

	return GenerateTexture({image_dest, image_size}, _textureDimensions);
}

Rml::TextureHandle RenderInterface_Metal::GenerateTexture(Rml::Span<const Rml::byte> _sourceData, Rml::Vector2i _sourceDimensions)
{
	// Use Private storage + staging buffer blit for reliable GPU texture upload.
	// replaceRegion on Shared/Managed textures can produce stale data on Apple Silicon.
	MTLTextureDescriptor* texDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
																					  width:_sourceDimensions.x
																					 height:_sourceDimensions.y
																				  mipmapped:NO];
	texDesc.usage = MTLTextureUsageShaderRead;
	texDesc.storageMode = MTLStorageModePrivate;

	id<MTLTexture> texture = [m_impl->device newTextureWithDescriptor:texDesc];
	if (!texture) return {};

	// Upload via staging buffer → blit encoder (GPU-side copy)
	const NSUInteger bytesPerRow = _sourceDimensions.x * 4;
	const NSUInteger totalBytes = bytesPerRow * _sourceDimensions.y;
	id<MTLBuffer> staging = [m_impl->device newBufferWithBytes:_sourceData.data()
														length:totalBytes
													   options:MTLResourceStorageModeShared];

	id<MTLCommandBuffer> cmdBuf = [m_impl->commandQueue commandBuffer];
	id<MTLBlitCommandEncoder> blit = [cmdBuf blitCommandEncoder];
	[blit copyFromBuffer:staging
			sourceOffset:0
	   sourceBytesPerRow:bytesPerRow
	 sourceBytesPerImage:totalBytes
			  sourceSize:MTLSizeMake(_sourceDimensions.x, _sourceDimensions.y, 1)
			   toTexture:texture
		destinationSlice:0
		destinationLevel:0
	   destinationOrigin:MTLOriginMake(0, 0, 0)];
	[blit endEncoding];
	[cmdBuf commit];
	[cmdBuf waitUntilCompleted];

	// Bridge to handle — caller must release via ReleaseTexture
	return reinterpret_cast<Rml::TextureHandle>((__bridge_retained void*)texture);
}

void RenderInterface_Metal::ReleaseTexture(Rml::TextureHandle _textureHandle)
{
	if (_textureHandle)
	{
		// Release the bridged reference
		id<MTLTexture> tex = (__bridge_transfer id<MTLTexture>)(void*)_textureHandle;
		(void)tex; // ARC releases it
	}
}

void RenderInterface_Metal::EnableScissorRegion(bool _enable)
{
	if (!_enable)
	{
		m_impl->scissorState = Rml::Rectanglei::MakeInvalid();
		m_impl->SetScissorOnEncoder(m_impl->scissorState);
	}
}

void RenderInterface_Metal::SetScissorRegion(Rml::Rectanglei _region)
{
	m_impl->scissorState = _region;
	m_impl->SetScissorOnEncoder(_region);
}

void RenderInterface_Metal::EnableClipMask(bool _enable)
{
	m_impl->clipMaskEnabled = _enable;

	if (!m_impl->renderEncoder) return;

	if (_enable)
	{
		[m_impl->renderEncoder setDepthStencilState:m_impl->stencilTestEqual];
		[m_impl->renderEncoder setStencilReferenceValue:m_impl->stencilRef];
	}
	else
	{
		// Use ALWAYS/KEEP to match GL3's glDisable(GL_STENCIL_TEST) behavior
		// while keeping stencil operations functional on the MSAA layer.
		[m_impl->renderEncoder setDepthStencilState:m_impl->stencilAlwaysKeep];
	}
}

void RenderInterface_Metal::RenderToClipMask(Rml::ClipMaskOperation _maskOperation, Rml::CompiledGeometryHandle _geometry, Rml::Vector2f _translation)
{
	using Rml::ClipMaskOperation;

	if (!m_impl->renderEncoder) return;

	const bool clearStencil = (_maskOperation == ClipMaskOperation::Set || _maskOperation == ClipMaskOperation::SetInverse);

	if (clearStencil)
	{
		// Metal can't clear stencil mid-pass like GL's glClear(GL_STENCIL_BUFFER_BIT).
		// Draw a viewport-covering quad with stencil replace=0 and color write disabled.
		// The scissor rect limits this to the current element area (matching GL3 behavior).
		[m_impl->renderEncoder setDepthStencilState:m_impl->stencilWriteReplace];
		[m_impl->renderEncoder setStencilReferenceValue:0];

		m_impl->UsePipeline(MetalGfx::PipelineId::Color, MetalGfx::BlendMode::PremultipliedAlpha, true);

		// Generate a quad in viewport coordinates [0,0]-[W,H] (not NDC [-1,1])
		// because vertex_main applies the projection matrix.
		Rml::Mesh clearMesh;
		Rml::MeshUtilities::GenerateQuad(clearMesh,
			Rml::Vector2f(0, 0),
			Rml::Vector2f((float)m_impl->viewportWidth, (float)m_impl->viewportHeight), {});
		const auto clearGeo = CompileGeometry(clearMesh.vertices, clearMesh.indices);

		struct { float x, y; float padding[2]; Rml::Matrix4f transform; } uniforms;
		uniforms.x = 0;
		uniforms.y = 0;
		uniforms.transform = m_impl->transform;

		[m_impl->renderEncoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];

		auto* geom = reinterpret_cast<MetalGfx::CompiledGeometryData*>(clearGeo);
		[m_impl->renderEncoder setVertexBuffer:geom->vertexBuffer offset:0 atIndex:0];
		[m_impl->renderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
										  indexCount:geom->indexCount
										   indexType:MTLIndexTypeUInt32
										 indexBuffer:geom->indexBuffer
									   indexBufferOffset:0];
		ReleaseGeometry(clearGeo);
	}

	int stencilTestValue = m_impl->stencilRef;

	switch (_maskOperation)
	{
	case ClipMaskOperation::Set:
		[m_impl->renderEncoder setDepthStencilState:m_impl->stencilWriteReplace];
		[m_impl->renderEncoder setStencilReferenceValue:1];
		stencilTestValue = 1;
		break;
	case ClipMaskOperation::SetInverse:
		[m_impl->renderEncoder setDepthStencilState:m_impl->stencilWriteReplace];
		[m_impl->renderEncoder setStencilReferenceValue:1];
		stencilTestValue = 0;
		break;
	case ClipMaskOperation::Intersect:
		[m_impl->renderEncoder setDepthStencilState:m_impl->stencilWriteIncrement];
		[m_impl->renderEncoder setStencilReferenceValue:0];
		stencilTestValue = m_impl->stencilRef + 1;
		break;
	}

	// Draw the mask geometry with colorWriteDisabled pipeline.
	// MUST NOT call RenderGeometry() here because it would override our
	// colorWriteDisabled pipeline with the normal Color pipeline.
	// In GL3, glColorMask(false) is global state that persists across
	// RenderGeometry, but in Metal the color write mask is baked into
	// the pipeline state.
	{
		m_impl->UsePipeline(MetalGfx::PipelineId::Color, MetalGfx::BlendMode::PremultipliedAlpha, true);

		auto* geometry = reinterpret_cast<MetalGfx::CompiledGeometryData*>(_geometry);

		struct { float x, y; float padding[2]; Rml::Matrix4f transform; } uniforms;
		uniforms.x = _translation.x;
		uniforms.y = _translation.y;
		uniforms.transform = m_impl->transform;
		[m_impl->renderEncoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];

		[m_impl->renderEncoder setVertexBuffer:geometry->vertexBuffer offset:0 atIndex:0];
		[m_impl->renderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
										  indexCount:geometry->indexCount
										   indexType:MTLIndexTypeUInt32
										 indexBuffer:geometry->indexBuffer
									   indexBufferOffset:0];
	}

	// Restore state
	m_impl->stencilRef = stencilTestValue;
	[m_impl->renderEncoder setDepthStencilState:m_impl->stencilTestEqual];
	[m_impl->renderEncoder setStencilReferenceValue:stencilTestValue];
}

void RenderInterface_Metal::SetTransform(const Rml::Matrix4f* _transform)
{
	m_impl->transform = (_transform ? (m_impl->projection * (*_transform)) : m_impl->projection);
}

Rml::LayerHandle RenderInterface_Metal::PushLayer()
{
	m_impl->EndCurrentRenderEncoder();

	const Rml::LayerHandle layerHandle = m_impl->renderLayers->PushLayer();
	const auto& layer = m_impl->renderLayers->GetLayer(layerHandle);
	m_impl->BeginRenderPassOnLayer(layer, true);

	return layerHandle;
}

void RenderInterface_Metal::CompositeLayers(Rml::LayerHandle _source, Rml::LayerHandle _destination, Rml::BlendMode _blendMode,
	Rml::Span<const Rml::CompiledFilterHandle> _filters)
{
	// Resolve source layer to postprocess primary
	const auto& sourceLayer = m_impl->renderLayers->GetLayer(_source);
	auto& ppPrimary = m_impl->renderLayers->GetPostprocessPrimary();

	m_impl->EndCurrentRenderEncoder();
	m_impl->ResolveLayerToPostprocess(sourceLayer, ppPrimary);

	// Apply filters
	RenderFiltersInternal(_filters);

	// Render to destination layer
	const auto& destLayer = m_impl->renderLayers->GetLayer(_destination);
	m_impl->BeginRenderPassOnLayer(destLayer, false);

	auto& ppResult = m_impl->renderLayers->GetPostprocessPrimary();
	[m_impl->renderEncoder setFragmentTexture:ppResult.colorTexture atIndex:0];
	[m_impl->renderEncoder setFragmentSamplerState:m_impl->samplerLinearClamp atIndex:0];

	if (_blendMode == Rml::BlendMode::Replace)
		m_impl->UsePipeline(MetalGfx::PipelineId::Passthrough, MetalGfx::BlendMode::Replace);
	else
		m_impl->UsePipeline(MetalGfx::PipelineId::Passthrough, MetalGfx::BlendMode::PremultipliedAlpha);

	auto* fsQuad = reinterpret_cast<MetalGfx::CompiledGeometryData*>(m_impl->fullscreenQuadGeometry);
	[m_impl->renderEncoder setVertexBuffer:fsQuad->vertexBuffer offset:0 atIndex:0];
	[m_impl->renderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
									  indexCount:fsQuad->indexCount
									   indexType:MTLIndexTypeUInt32
									 indexBuffer:fsQuad->indexBuffer
								   indexBufferOffset:0];

	// If destination isn't the top layer, switch back to top layer
	if (_destination != m_impl->renderLayers->GetTopLayerHandle())
	{
		const auto& topLayer = m_impl->renderLayers->GetTopLayer();
		m_impl->BeginRenderPassOnLayer(topLayer, false);
	}
}

void RenderInterface_Metal::PopLayer()
{
	m_impl->EndCurrentRenderEncoder();

	m_impl->renderLayers->PopLayer();

	const auto& topLayer = m_impl->renderLayers->GetTopLayer();
	m_impl->BeginRenderPassOnLayer(topLayer, false);
}

Rml::TextureHandle RenderInterface_Metal::SaveLayerAsTexture()
{
	RMLUI_ASSERT(m_impl->scissorState.Valid());
	const Rml::Rectanglei bounds = m_impl->scissorState;

	// Resolve top layer
	const auto& topLayer = m_impl->renderLayers->GetTopLayer();
	auto& ppPrimary = m_impl->renderLayers->GetPostprocessPrimary();

	m_impl->EndCurrentRenderEncoder();
	m_impl->ResolveLayerToPostprocess(topLayer, ppPrimary);

	// Create destination texture
	MTLTextureDescriptor* texDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
																					  width:bounds.Width()
																					 height:bounds.Height()
																				  mipmapped:YES];
	texDesc.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
	texDesc.storageMode = MTLStorageModePrivate;

	id<MTLTexture> renderTexture = [m_impl->device newTextureWithDescriptor:texDesc];
	if (!renderTexture) return {};

	// Blit the region from postprocess primary to the new texture
	id<MTLBlitCommandEncoder> blit = [m_impl->commandBuffer blitCommandEncoder];
	[blit copyFromTexture:ppPrimary.colorTexture
			  sourceSlice:0
			  sourceLevel:0
			 sourceOrigin:MTLOriginMake(bounds.Left(), bounds.Top(), 0)
			   sourceSize:MTLSizeMake(bounds.Width(), bounds.Height(), 1)
				toTexture:renderTexture
		 destinationSlice:0
		 destinationLevel:0
		destinationOrigin:MTLOriginMake(0, 0, 0)];
	[blit generateMipmapsForTexture:renderTexture];
	[blit endEncoding];

	// Resume rendering on top layer
	m_impl->BeginRenderPassOnLayer(topLayer, false);

	return reinterpret_cast<Rml::TextureHandle>((__bridge_retained void*)renderTexture);
}

Rml::CompiledFilterHandle RenderInterface_Metal::SaveLayerAsMaskImage()
{
	const auto& topLayer = m_impl->renderLayers->GetTopLayer();
	auto& ppPrimary = m_impl->renderLayers->GetPostprocessPrimary();
	auto& blendMask = m_impl->renderLayers->GetBlendMask();

	m_impl->EndCurrentRenderEncoder();
	m_impl->ResolveLayerToPostprocess(topLayer, ppPrimary);

	// Copy primary to blend mask using passthrough
	m_impl->BeginRenderPassOnPostprocess(blendMask, false);
	m_impl->UsePostprocessPipeline(MetalGfx::PipelineId::Passthrough, MetalGfx::BlendMode::Replace);

	[m_impl->renderEncoder setFragmentTexture:ppPrimary.colorTexture atIndex:0];
	[m_impl->renderEncoder setFragmentSamplerState:m_impl->samplerLinearClamp atIndex:0];

	auto* fsQuad = reinterpret_cast<MetalGfx::CompiledGeometryData*>(m_impl->fullscreenQuadGeometry);
	[m_impl->renderEncoder setVertexBuffer:fsQuad->vertexBuffer offset:0 atIndex:0];
	[m_impl->renderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
									  indexCount:fsQuad->indexCount
									   indexType:MTLIndexTypeUInt32
									 indexBuffer:fsQuad->indexBuffer
								   indexBufferOffset:0];

	// Resume on top layer
	m_impl->BeginRenderPassOnLayer(topLayer, false);

	auto* filter = new CompiledFilter{};
	filter->type = FilterType::MaskImage;
	return reinterpret_cast<Rml::CompiledFilterHandle>(filter);
}

static Rml::Colourf ConvertToColorf(Rml::ColourbPremultiplied c0)
{
	Rml::Colourf result;
	for (int i = 0; i < 4; i++)
		result[i] = (1.f / 255.f) * float(c0[i]);
	return result;
}

Rml::CompiledFilterHandle RenderInterface_Metal::CompileFilter(const Rml::String& _name, const Rml::Dictionary& _parameters)
{
	CompiledFilter filter = {};

	if (_name == "opacity")
	{
		filter.type = FilterType::Passthrough;
		filter.blend_factor = Rml::Get(core_instance, _parameters, "value", 1.0f);
	}
	else if (_name == "blur")
	{
		filter.type = FilterType::Blur;
		filter.sigma = Rml::Get(core_instance, _parameters, "sigma", 1.0f);
	}
	else if (_name == "drop-shadow")
	{
		filter.type = FilterType::DropShadow;
		filter.sigma = Rml::Get(core_instance, _parameters, "sigma", 0.f);
		filter.color = Rml::Get(core_instance, _parameters, "color", Rml::Colourb()).ToPremultiplied();
		filter.offset = Rml::Get(core_instance, _parameters, "offset", Rml::Vector2f(0.f));
	}
	else if (_name == "brightness")
	{
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Get(core_instance, _parameters, "value", 1.0f);
		filter.color_matrix = Rml::Matrix4f::Diag(value, value, value, 1.f);
	}
	else if (_name == "contrast")
	{
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Get(core_instance, _parameters, "value", 1.0f);
		const float grayness = 0.5f - 0.5f * value;
		filter.color_matrix = Rml::Matrix4f::Diag(value, value, value, 1.f);
		filter.color_matrix.SetColumn(3, Rml::Vector4f(grayness, grayness, grayness, 1.f));
	}
	else if (_name == "invert")
	{
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Math::Clamp(Rml::Get(core_instance, _parameters, "value", 1.0f), 0.f, 1.f);
		const float inverted = 1.f - 2.f * value;
		filter.color_matrix = Rml::Matrix4f::Diag(inverted, inverted, inverted, 1.f);
		filter.color_matrix.SetColumn(3, Rml::Vector4f(value, value, value, 1.f));
	}
	else if (_name == "grayscale")
	{
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Get(core_instance, _parameters, "value", 1.0f);
		const float rev_value = 1.f - value;
		const Rml::Vector3f gray = value * Rml::Vector3f(0.2126f, 0.7152f, 0.0722f);
		filter.color_matrix = Rml::Matrix4f::FromRows(
			{gray.x + rev_value, gray.y,             gray.z,             0.f},
			{gray.x,             gray.y + rev_value, gray.z,             0.f},
			{gray.x,             gray.y,             gray.z + rev_value, 0.f},
			{0.f,                0.f,                0.f,                1.f}
		);
	}
	else if (_name == "sepia")
	{
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Get(core_instance, _parameters, "value", 1.0f);
		const float rev_value = 1.f - value;
		const Rml::Vector3f r_mix = value * Rml::Vector3f(0.393f, 0.769f, 0.189f);
		const Rml::Vector3f g_mix = value * Rml::Vector3f(0.349f, 0.686f, 0.168f);
		const Rml::Vector3f b_mix = value * Rml::Vector3f(0.272f, 0.534f, 0.131f);
		filter.color_matrix = Rml::Matrix4f::FromRows(
			{r_mix.x + rev_value, r_mix.y,             r_mix.z,             0.f},
			{g_mix.x,             g_mix.y + rev_value, g_mix.z,             0.f},
			{b_mix.x,             b_mix.y,             b_mix.z + rev_value, 0.f},
			{0.f,                 0.f,                 0.f,                 1.f}
		);
	}
	else if (_name == "hue-rotate")
	{
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Get(core_instance, _parameters, "value", 1.0f);
		const float s = Rml::Math::Sin(value);
		const float c = Rml::Math::Cos(value);
		filter.color_matrix = Rml::Matrix4f::FromRows(
			{0.213f + 0.787f * c - 0.213f * s,  0.715f - 0.715f * c - 0.715f * s,  0.072f - 0.072f * c + 0.928f * s,  0.f},
			{0.213f - 0.213f * c + 0.143f * s,  0.715f + 0.285f * c + 0.140f * s,  0.072f - 0.072f * c - 0.283f * s,  0.f},
			{0.213f - 0.213f * c - 0.787f * s,  0.715f - 0.715f * c + 0.715f * s,  0.072f + 0.928f * c + 0.072f * s,  0.f},
			{0.f,                               0.f,                               0.f,                               1.f}
		);
	}
	else if (_name == "saturate")
	{
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Get(core_instance, _parameters, "value", 1.0f);
		filter.color_matrix = Rml::Matrix4f::FromRows(
			{0.213f + 0.787f * value,  0.715f - 0.715f * value,  0.072f - 0.072f * value,  0.f},
			{0.213f - 0.213f * value,  0.715f + 0.285f * value,  0.072f - 0.072f * value,  0.f},
			{0.213f - 0.213f * value,  0.715f - 0.715f * value,  0.072f + 0.928f * value,  0.f},
			{0.f,                      0.f,                      0.f,                      1.f}
		);
	}

	if (filter.type != FilterType::Invalid)
		return reinterpret_cast<Rml::CompiledFilterHandle>(new CompiledFilter(std::move(filter)));

	Rml::Log::Message(core_instance, Rml::Log::LT_WARNING, "Unsupported filter type '%s'.", _name.c_str());
	return {};
}

void RenderInterface_Metal::ReleaseFilter(Rml::CompiledFilterHandle _filter)
{
	delete reinterpret_cast<CompiledFilter*>(_filter);
}

Rml::CompiledShaderHandle RenderInterface_Metal::CompileShader(const Rml::String& _name, const Rml::Dictionary& _parameters)
{
	auto ApplyColorStopList = [this](CompiledShader& shader, const Rml::Dictionary& params) {
		auto it = params.find("color_stop_list");
		RMLUI_ASSERT(it != params.end() && it->second.GetType() == Rml::Variant::COLORSTOPLIST);
		const Rml::ColorStopList& colorStopList = it->second.GetReference<Rml::ColorStopList>();
		const int numStops = Rml::Math::Min((int)colorStopList.size(), MAX_NUM_STOPS);

		shader.stop_positions.resize(numStops);
		shader.stop_colors.resize(numStops);
		for (int i = 0; i < numStops; i++)
		{
			const Rml::ColorStop& stop = colorStopList[i];
			RMLUI_ASSERT(stop.position.unit == Rml::Unit::NUMBER);
			shader.stop_positions[i] = stop.position.number;
			shader.stop_colors[i] = ConvertToColorf(stop.color);
		}
	};

	CompiledShader shader = {};

	if (_name == "linear-gradient")
	{
		shader.type = CompiledShaderType::Gradient;
		const bool repeating = Rml::Get(core_instance, _parameters, "repeating", false);
		shader.gradient_function = repeating ? ShaderGradientFunction::RepeatingLinear : ShaderGradientFunction::Linear;
		shader.p = Rml::Get(core_instance, _parameters, "p0", Rml::Vector2f(0.f));
		shader.v = Rml::Get(core_instance, _parameters, "p1", Rml::Vector2f(0.f)) - shader.p;
		ApplyColorStopList(shader, _parameters);
	}
	else if (_name == "radial-gradient")
	{
		shader.type = CompiledShaderType::Gradient;
		const bool repeating = Rml::Get(core_instance, _parameters, "repeating", false);
		shader.gradient_function = repeating ? ShaderGradientFunction::RepeatingRadial : ShaderGradientFunction::Radial;
		shader.p = Rml::Get(core_instance, _parameters, "center", Rml::Vector2f(0.f));
		shader.v = Rml::Vector2f(1.f) / Rml::Get(core_instance, _parameters, "radius", Rml::Vector2f(1.f));
		ApplyColorStopList(shader, _parameters);
	}
	else if (_name == "conic-gradient")
	{
		shader.type = CompiledShaderType::Gradient;
		const bool repeating = Rml::Get(core_instance, _parameters, "repeating", false);
		shader.gradient_function = repeating ? ShaderGradientFunction::RepeatingConic : ShaderGradientFunction::Conic;
		shader.p = Rml::Get(core_instance, _parameters, "center", Rml::Vector2f(0.f));
		const float angle = Rml::Get(core_instance, _parameters, "angle", 0.f);
		shader.v = {Rml::Math::Cos(angle), Rml::Math::Sin(angle)};
		ApplyColorStopList(shader, _parameters);
	}
	else if (_name == "shader")
	{
		const Rml::String value = Rml::Get(core_instance, _parameters, "value", Rml::String());
		if (value == "creation")
		{
			shader.type = CompiledShaderType::Creation;
			shader.dimensions = Rml::Get(core_instance, _parameters, "dimensions", Rml::Vector2f(0.f));
		}
	}

	if (shader.type != CompiledShaderType::Invalid)
		return reinterpret_cast<Rml::CompiledShaderHandle>(new CompiledShader(std::move(shader)));

	Rml::Log::Message(core_instance, Rml::Log::LT_WARNING, "Unsupported shader type '%s'.", _name.c_str());
	return {};
}

void RenderInterface_Metal::RenderShader(Rml::CompiledShaderHandle _shaderHandle, Rml::CompiledGeometryHandle _geometryHandle,
	Rml::Vector2f _translation, Rml::TextureHandle /*_texture*/)
{
	RMLUI_ASSERT(_shaderHandle && _geometryHandle);

	const CompiledShader& shader = *reinterpret_cast<CompiledShader*>(_shaderHandle);
	auto* geometry = reinterpret_cast<MetalGfx::CompiledGeometryData*>(_geometryHandle);

	if (!m_impl->renderEncoder) return;

	switch (shader.type)
	{
	case CompiledShaderType::Gradient:
	{
		RMLUI_ASSERT(shader.stop_positions.size() == shader.stop_colors.size());
		const int numStops = (int)shader.stop_positions.size();

		m_impl->UsePipeline(MetalGfx::PipelineId::Gradient);

		// Vertex uniforms
		struct { float x, y; float padding[2]; Rml::Matrix4f transform; } vertUniforms;
		vertUniforms.x = _translation.x;
		vertUniforms.y = _translation.y;
		vertUniforms.transform = m_impl->transform;
		[m_impl->renderEncoder setVertexBytes:&vertUniforms length:sizeof(vertUniforms) atIndex:1];

		// Fragment uniforms — pack gradient data
		struct {
			int func;
			int padding1[3];
			float px, py;
			float vx, vy;
			int num_stops;
			int padding2[3];
			float stop_positions[MAX_NUM_STOPS];
			float stop_colors[MAX_NUM_STOPS * 4]; // float4 per stop
		} gradUniforms = {};

		gradUniforms.func = static_cast<int>(shader.gradient_function);
		gradUniforms.px = shader.p.x;
		gradUniforms.py = shader.p.y;
		gradUniforms.vx = shader.v.x;
		gradUniforms.vy = shader.v.y;
		gradUniforms.num_stops = numStops;

		for (int i = 0; i < numStops; i++)
		{
			gradUniforms.stop_positions[i] = shader.stop_positions[i];
			gradUniforms.stop_colors[i * 4 + 0] = shader.stop_colors[i].red;
			gradUniforms.stop_colors[i * 4 + 1] = shader.stop_colors[i].green;
			gradUniforms.stop_colors[i * 4 + 2] = shader.stop_colors[i].blue;
			gradUniforms.stop_colors[i * 4 + 3] = shader.stop_colors[i].alpha;
		}

		[m_impl->renderEncoder setFragmentBytes:&gradUniforms length:sizeof(gradUniforms) atIndex:2];

		[m_impl->renderEncoder setVertexBuffer:geometry->vertexBuffer offset:0 atIndex:0];
		[m_impl->renderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
										  indexCount:geometry->indexCount
										   indexType:MTLIndexTypeUInt32
										 indexBuffer:geometry->indexBuffer
									   indexBufferOffset:0];
	}
	break;
	case CompiledShaderType::Creation:
	{
		const double time = Rml::GetSystemInterface(core_instance)->GetElapsedTime();

		m_impl->UsePipeline(MetalGfx::PipelineId::Creation);

		struct { float x, y; float padding[2]; Rml::Matrix4f transform; } vertUniforms;
		vertUniforms.x = _translation.x;
		vertUniforms.y = _translation.y;
		vertUniforms.transform = m_impl->transform;
		[m_impl->renderEncoder setVertexBytes:&vertUniforms length:sizeof(vertUniforms) atIndex:1];

		struct { float value; float padding; float dimX, dimY; } creationUniforms;
		creationUniforms.value = static_cast<float>(time);
		creationUniforms.dimX = shader.dimensions.x;
		creationUniforms.dimY = shader.dimensions.y;
		[m_impl->renderEncoder setFragmentBytes:&creationUniforms length:sizeof(creationUniforms) atIndex:2];

		[m_impl->renderEncoder setVertexBuffer:geometry->vertexBuffer offset:0 atIndex:0];
		[m_impl->renderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
										  indexCount:geometry->indexCount
										   indexType:MTLIndexTypeUInt32
										 indexBuffer:geometry->indexBuffer
									   indexBufferOffset:0];
	}
	break;
	case CompiledShaderType::Invalid:
		Rml::Log::Message(core_instance, Rml::Log::LT_WARNING, "Unhandled render shader %d.", (int)shader.type);
		break;
	}
}

void RenderInterface_Metal::ReleaseShader(Rml::CompiledShaderHandle _shaderHandle)
{
	delete reinterpret_cast<CompiledShader*>(_shaderHandle);
}

// Private method for rendering filter chain
void RenderInterface_Metal::RenderFiltersInternal(Rml::Span<const Rml::CompiledFilterHandle> _filterHandles)
{
	for (const Rml::CompiledFilterHandle filterHandle : _filterHandles)
	{
		const CompiledFilter& filter = *reinterpret_cast<const CompiledFilter*>(filterHandle);

		switch (filter.type)
		{
		case FilterType::Passthrough:
		{
			auto& source = m_impl->renderLayers->GetPostprocessPrimary();
			auto& dest = m_impl->renderLayers->GetPostprocessSecondary();

			m_impl->BeginRenderPassOnPostprocess(dest, false);
			m_impl->UsePostprocessPipeline(MetalGfx::PipelineId::Passthrough, MetalGfx::BlendMode::BlendFactor);

			[m_impl->renderEncoder setBlendColorRed:filter.blend_factor green:filter.blend_factor blue:filter.blend_factor alpha:filter.blend_factor];
			[m_impl->renderEncoder setFragmentTexture:source.colorTexture atIndex:0];
			[m_impl->renderEncoder setFragmentSamplerState:m_impl->samplerLinearClamp atIndex:0];

			auto* fsQuad = reinterpret_cast<MetalGfx::CompiledGeometryData*>(m_impl->fullscreenQuadGeometry);
			[m_impl->renderEncoder setVertexBuffer:fsQuad->vertexBuffer offset:0 atIndex:0];
			[m_impl->renderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
											  indexCount:fsQuad->indexCount
											   indexType:MTLIndexTypeUInt32
											 indexBuffer:fsQuad->indexBuffer
										   indexBufferOffset:0];

			m_impl->renderLayers->SwapPostprocessPrimarySecondary();
		}
		break;
		case FilterType::Blur:
		{
			RenderBlurInternal(filter.sigma, &m_impl->renderLayers->GetPostprocessPrimary(),
				&m_impl->renderLayers->GetPostprocessSecondary());
		}
		break;
		case FilterType::DropShadow:
		{
			auto& primary = m_impl->renderLayers->GetPostprocessPrimary();
			auto& secondary = m_impl->renderLayers->GetPostprocessSecondary();

			m_impl->BeginRenderPassOnPostprocess(secondary, true);
			m_impl->UsePostprocessPipeline(MetalGfx::PipelineId::DropShadow, MetalGfx::BlendMode::Replace);

			Rml::Colourf color = ConvertToColorf(filter.color);
			// UV offset for Metal's Y-flipped passthrough: negate both X and Y.
			// GL3 negates X only because GL's UV origin is bottom-left.
			// With our Y-flipped vertex shader, UV origin is top-left, so we
			// negate Y too to match the shadow direction convention.
			const Rml::Vector2f uvOffset = filter.offset / Rml::Vector2f(-(float)m_impl->viewportWidth, -(float)m_impl->viewportHeight);

			struct { float tcMinX, tcMinY; float tcMaxX, tcMaxY; float color[4]; } dsUniforms;
			// Compute tex coord limits from scissor
			if (m_impl->scissorState.Valid())
			{
				const Rml::Vector2f fmin = (Rml::Vector2f(m_impl->scissorState.p0) + Rml::Vector2f(0.5f)) / Rml::Vector2f(primary.width, primary.height);
				const Rml::Vector2f fmax = (Rml::Vector2f(m_impl->scissorState.p1) - Rml::Vector2f(0.5f)) / Rml::Vector2f(primary.width, primary.height);
				dsUniforms.tcMinX = fmin.x;
				dsUniforms.tcMinY = fmin.y;
				dsUniforms.tcMaxX = fmax.x;
				dsUniforms.tcMaxY = fmax.y;
			}
			else
			{
				dsUniforms.tcMinX = 0.f;
				dsUniforms.tcMinY = 0.f;
				dsUniforms.tcMaxX = 1.f;
				dsUniforms.tcMaxY = 1.f;
			}
			// Copy color directly from Colourf data (same layout as GL3's glUniform4fv)
			for (int c = 0; c < 4; c++)
				dsUniforms.color[c] = color[c];

			[m_impl->renderEncoder setFragmentBytes:&dsUniforms length:sizeof(dsUniforms) atIndex:2];
			[m_impl->renderEncoder setFragmentTexture:primary.colorTexture atIndex:0];
			[m_impl->renderEncoder setFragmentSamplerState:m_impl->samplerLinearClamp atIndex:0];

			// Draw fullscreen quad with UV offset
			Rml::Mesh mesh;
			Rml::MeshUtilities::GenerateQuad(mesh, Rml::Vector2f(-1), Rml::Vector2f(2), {});
			for (Rml::Vertex& v : mesh.vertices)
				v.tex_coord = v.tex_coord + uvOffset;

			const auto tempGeo = CompileGeometry(mesh.vertices, mesh.indices);
			auto* tempGeom = reinterpret_cast<MetalGfx::CompiledGeometryData*>(tempGeo);
			[m_impl->renderEncoder setVertexBuffer:tempGeom->vertexBuffer offset:0 atIndex:0];
			[m_impl->renderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
											  indexCount:tempGeom->indexCount
											   indexType:MTLIndexTypeUInt32
											 indexBuffer:tempGeom->indexBuffer
										   indexBufferOffset:0];
			ReleaseGeometry(tempGeo);

			if (filter.sigma >= 0.5f)
			{
				auto& tertiary = m_impl->renderLayers->GetPostprocessTertiary();
				RenderBlurInternal(filter.sigma, &secondary, &tertiary);
			}

			// Overlay the original on top
			m_impl->BeginRenderPassOnPostprocess(secondary, false);
			m_impl->UsePostprocessPipeline(MetalGfx::PipelineId::Passthrough, MetalGfx::BlendMode::PremultipliedAlpha);

			[m_impl->renderEncoder setFragmentTexture:primary.colorTexture atIndex:0];
			[m_impl->renderEncoder setFragmentSamplerState:m_impl->samplerLinearClamp atIndex:0];

			auto* fsQuad = reinterpret_cast<MetalGfx::CompiledGeometryData*>(m_impl->fullscreenQuadGeometry);
			[m_impl->renderEncoder setVertexBuffer:fsQuad->vertexBuffer offset:0 atIndex:0];
			[m_impl->renderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
											  indexCount:fsQuad->indexCount
											   indexType:MTLIndexTypeUInt32
											 indexBuffer:fsQuad->indexBuffer
										   indexBufferOffset:0];

			m_impl->renderLayers->SwapPostprocessPrimarySecondary();
		}
		break;
		case FilterType::ColorMatrix:
		{
			auto& source = m_impl->renderLayers->GetPostprocessPrimary();
			auto& dest = m_impl->renderLayers->GetPostprocessSecondary();

			m_impl->BeginRenderPassOnPostprocess(dest, false);
			m_impl->UsePostprocessPipeline(MetalGfx::PipelineId::ColorMatrix, MetalGfx::BlendMode::Replace);

			struct { Rml::Matrix4f color_matrix; } cmUniforms;
			cmUniforms.color_matrix = filter.color_matrix;
			[m_impl->renderEncoder setFragmentBytes:&cmUniforms length:sizeof(cmUniforms) atIndex:2];

			[m_impl->renderEncoder setFragmentTexture:source.colorTexture atIndex:0];
			[m_impl->renderEncoder setFragmentSamplerState:m_impl->samplerLinearClamp atIndex:0];

			auto* fsQuad = reinterpret_cast<MetalGfx::CompiledGeometryData*>(m_impl->fullscreenQuadGeometry);
			[m_impl->renderEncoder setVertexBuffer:fsQuad->vertexBuffer offset:0 atIndex:0];
			[m_impl->renderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
											  indexCount:fsQuad->indexCount
											   indexType:MTLIndexTypeUInt32
											 indexBuffer:fsQuad->indexBuffer
										   indexBufferOffset:0];

			m_impl->renderLayers->SwapPostprocessPrimarySecondary();
		}
		break;
		case FilterType::MaskImage:
		{
			auto& source = m_impl->renderLayers->GetPostprocessPrimary();
			auto& blendMask = m_impl->renderLayers->GetBlendMask();
			auto& dest = m_impl->renderLayers->GetPostprocessSecondary();

			m_impl->BeginRenderPassOnPostprocess(dest, false);
			m_impl->UsePostprocessPipeline(MetalGfx::PipelineId::BlendMask, MetalGfx::BlendMode::Replace);

			[m_impl->renderEncoder setFragmentTexture:source.colorTexture atIndex:0];
			[m_impl->renderEncoder setFragmentTexture:blendMask.colorTexture atIndex:1];
			[m_impl->renderEncoder setFragmentSamplerState:m_impl->samplerLinearClamp atIndex:0];

			auto* fsQuad = reinterpret_cast<MetalGfx::CompiledGeometryData*>(m_impl->fullscreenQuadGeometry);
			[m_impl->renderEncoder setVertexBuffer:fsQuad->vertexBuffer offset:0 atIndex:0];
			[m_impl->renderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
											  indexCount:fsQuad->indexCount
											   indexType:MTLIndexTypeUInt32
											 indexBuffer:fsQuad->indexBuffer
										   indexBufferOffset:0];

			m_impl->renderLayers->SwapPostprocessPrimarySecondary();
		}
		break;
		case FilterType::Invalid:
			Rml::Log::Message(core_instance, Rml::Log::LT_WARNING, "Unhandled render filter %d.", (int)filter.type);
			break;
		}
	}
}

static void SigmaToParameters(const float desired_sigma, int& out_pass_level, float& out_sigma)
{
	constexpr int max_num_passes = 10;
	constexpr float max_single_pass_sigma = 3.0f;
	out_pass_level = Rml::Math::Clamp(Rml::Math::Log2(int(desired_sigma * (2.f / max_single_pass_sigma))), 0, max_num_passes);
	out_sigma = Rml::Math::Clamp(desired_sigma / float(1 << out_pass_level), 0.0f, max_single_pass_sigma);
}

void RenderInterface_Metal::RenderBlurInternal(float sigma, const void* _sourceDestination, const void* _temp)
{
	auto& sourceDestination = *const_cast<MetalGfx::PostprocessTargetData*>(static_cast<const MetalGfx::PostprocessTargetData*>(_sourceDestination));
	auto& temp = *const_cast<MetalGfx::PostprocessTargetData*>(static_cast<const MetalGfx::PostprocessTargetData*>(_temp));
	int passLevel = 0;
	SigmaToParameters(sigma, passLevel, sigma);

	// Compute blur weights
	float weights[BLUR_NUM_WEIGHTS];
	float normalization = 0.0f;
	for (int i = 0; i < BLUR_NUM_WEIGHTS; i++)
	{
		if (Rml::Math::Absolute(sigma) < 0.1f)
			weights[i] = float(i == 0);
		else
			weights[i] = Rml::Math::Exp(-float(i * i) / (2.0f * sigma * sigma)) / (Rml::Math::SquareRoot(2.f * Rml::Math::RMLUI_PI) * sigma);
		normalization += (i == 0 ? 1.f : 2.0f) * weights[i];
	}
	for (int i = 0; i < BLUR_NUM_WEIGHTS; i++)
		weights[i] /= normalization;

	// Track the downscaled scissor region through the downscale passes
	Rml::Rectanglei scissor = Rml::Rectanglei::FromPositionSize(
		Rml::Vector2i(0), Rml::Vector2i(sourceDestination.width, sourceDestination.height));
	if (m_impl->scissorState.Valid())
		scissor = m_impl->scissorState;

	// Downscale passes
	for (int i = 0; i < passLevel; i++)
	{
		scissor.p0 = (scissor.p0 + Rml::Vector2i(1)) / 2;
		scissor.p1 = Rml::Math::Max(scissor.p1 / 2, scissor.p0);

		const bool fromSource = (i % 2 == 0);
		auto& src = fromSource ? sourceDestination : temp;
		auto& dst = fromSource ? temp : sourceDestination;

		m_impl->BeginRenderPassOnPostprocess(dst, false);

		// Set viewport to half size
		MTLViewport vp = {0, 0, (double)(sourceDestination.width / 2), (double)(sourceDestination.height / 2), 0, 1};
		[m_impl->renderEncoder setViewport:vp];

		m_impl->UsePostprocessPipeline(MetalGfx::PipelineId::Passthrough, MetalGfx::BlendMode::Replace);
		[m_impl->renderEncoder setFragmentTexture:src.colorTexture atIndex:0];
		[m_impl->renderEncoder setFragmentSamplerState:m_impl->samplerLinearClamp atIndex:0];

		auto* fsQuad = reinterpret_cast<MetalGfx::CompiledGeometryData*>(m_impl->fullscreenQuadGeometry);
		[m_impl->renderEncoder setVertexBuffer:fsQuad->vertexBuffer offset:0 atIndex:0];
		[m_impl->renderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
										  indexCount:fsQuad->indexCount
										   indexType:MTLIndexTypeUInt32
										 indexBuffer:fsQuad->indexBuffer
									   indexBufferOffset:0];
	}

	// Ensure data is in temp buffer
	const bool transferToTemp = (passLevel % 2 == 0);
	if (transferToTemp)
	{
		m_impl->BeginRenderPassOnPostprocess(temp, false);
		m_impl->UsePostprocessPipeline(MetalGfx::PipelineId::Passthrough, MetalGfx::BlendMode::Replace);
		[m_impl->renderEncoder setFragmentTexture:sourceDestination.colorTexture atIndex:0];
		[m_impl->renderEncoder setFragmentSamplerState:m_impl->samplerLinearClamp atIndex:0];

		auto* fsQuad = reinterpret_cast<MetalGfx::CompiledGeometryData*>(m_impl->fullscreenQuadGeometry);
		[m_impl->renderEncoder setVertexBuffer:fsQuad->vertexBuffer offset:0 atIndex:0];
		[m_impl->renderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
										  indexCount:fsQuad->indexCount
										   indexType:MTLIndexTypeUInt32
										 indexBuffer:fsQuad->indexBuffer
									   indexBufferOffset:0];
	}

	// Compute tex coord limits from the DOWNSCALED scissor (not original).
	// The blur operates on content that's been downscaled, so the limits
	// must match where that content actually sits in the texture.
	float tcMin[2] = {0, 0};
	float tcMax[2] = {1, 1};
	if (scissor.Valid())
	{
		tcMin[0] = (scissor.p0.x + 0.5f) / sourceDestination.width;
		tcMin[1] = (scissor.p0.y + 0.5f) / sourceDestination.height;
		tcMax[0] = (scissor.p1.x - 0.5f) / sourceDestination.width;
		tcMax[1] = (scissor.p1.y - 0.5f) / sourceDestination.height;
	}

	// Vertical blur: temp -> sourceDestination
	{
		m_impl->BeginRenderPassOnPostprocess(sourceDestination, false);
		m_impl->UsePostprocessPipeline(MetalGfx::PipelineId::Blur, MetalGfx::BlendMode::Replace);

		struct { float texelOffsetX, texelOffsetY; } blurVertUniforms;
		blurVertUniforms.texelOffsetX = 0.f;
		blurVertUniforms.texelOffsetY = 1.0f / temp.height;
		[m_impl->renderEncoder setVertexBytes:&blurVertUniforms length:sizeof(blurVertUniforms) atIndex:1];

		struct { float w[BLUR_NUM_WEIGHTS]; float tcMinX, tcMinY; float tcMaxX, tcMaxY; } blurFragUniforms;
		memcpy(blurFragUniforms.w, weights, sizeof(weights));
		blurFragUniforms.tcMinX = tcMin[0];
		blurFragUniforms.tcMinY = tcMin[1];
		blurFragUniforms.tcMaxX = tcMax[0];
		blurFragUniforms.tcMaxY = tcMax[1];
		[m_impl->renderEncoder setFragmentBytes:&blurFragUniforms length:sizeof(blurFragUniforms) atIndex:2];

		[m_impl->renderEncoder setFragmentTexture:temp.colorTexture atIndex:0];
		[m_impl->renderEncoder setFragmentSamplerState:m_impl->samplerLinearClamp atIndex:0];

		auto* fsQuad = reinterpret_cast<MetalGfx::CompiledGeometryData*>(m_impl->fullscreenQuadGeometry);
		[m_impl->renderEncoder setVertexBuffer:fsQuad->vertexBuffer offset:0 atIndex:0];
		[m_impl->renderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
										  indexCount:fsQuad->indexCount
										   indexType:MTLIndexTypeUInt32
										 indexBuffer:fsQuad->indexBuffer
									   indexBufferOffset:0];
	}

	// Horizontal blur: sourceDestination -> temp
	{
		m_impl->BeginRenderPassOnPostprocess(temp, true); // Clear first for transparent border
		m_impl->UsePostprocessPipeline(MetalGfx::PipelineId::Blur, MetalGfx::BlendMode::Replace);

		struct { float texelOffsetX, texelOffsetY; } blurVertUniforms;
		blurVertUniforms.texelOffsetX = 1.0f / sourceDestination.width;
		blurVertUniforms.texelOffsetY = 0.f;
		[m_impl->renderEncoder setVertexBytes:&blurVertUniforms length:sizeof(blurVertUniforms) atIndex:1];

		struct { float w[BLUR_NUM_WEIGHTS]; float tcMinX, tcMinY; float tcMaxX, tcMaxY; } blurFragUniforms;
		memcpy(blurFragUniforms.w, weights, sizeof(weights));
		blurFragUniforms.tcMinX = tcMin[0];
		blurFragUniforms.tcMinY = tcMin[1];
		blurFragUniforms.tcMaxX = tcMax[0];
		blurFragUniforms.tcMaxY = tcMax[1];
		[m_impl->renderEncoder setFragmentBytes:&blurFragUniforms length:sizeof(blurFragUniforms) atIndex:2];

		[m_impl->renderEncoder setFragmentTexture:sourceDestination.colorTexture atIndex:0];
		[m_impl->renderEncoder setFragmentSamplerState:m_impl->samplerLinearClamp atIndex:0];

		auto* fsQuad = reinterpret_cast<MetalGfx::CompiledGeometryData*>(m_impl->fullscreenQuadGeometry);
		[m_impl->renderEncoder setVertexBuffer:fsQuad->vertexBuffer offset:0 atIndex:0];
		[m_impl->renderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
										  indexCount:fsQuad->indexCount
										   indexType:MTLIndexTypeUInt32
										 indexBuffer:fsQuad->indexBuffer
									   indexBufferOffset:0];
	}

	// Upscale: draw from the downscaled region of temp to the full window region of
	// sourceDestination. GL3 uses glBlitFramebuffer(src_rect, dst_rect, GL_LINEAR);
	// Metal has no scaled blit, so we use a passthrough draw with a viewport set to
	// the destination rectangle and UVs mapping to the source rectangle.
	{
		// The downscaled scissor tells us where the blurred content sits in the temp texture.
		// 'scissor' was iteratively halved from the original window during the downscale passes.
		// We need to stretch that region back to the full window area.
		const Rml::Vector2i srcMin = scissor.p0;
		const Rml::Vector2i srcMax = scissor.p1;

		// The original (full-size) window region for the destination.
		Rml::Rectanglei window = Rml::Rectanglei::FromPositionSize(
			Rml::Vector2i(0), Rml::Vector2i(sourceDestination.width, sourceDestination.height));
		if (m_impl->scissorState.Valid())
			window = m_impl->scissorState;

		const Rml::Vector2i dstMin = window.p0;
		const Rml::Vector2i dstMax = window.p1;

		m_impl->BeginRenderPassOnPostprocess(sourceDestination, false);
		m_impl->UsePostprocessPipeline(MetalGfx::PipelineId::Passthrough, MetalGfx::BlendMode::Replace);

		// Set viewport to destination rectangle — the fullscreen quad [-1,1] fills this area.
		MTLViewport vp = {
			(double)dstMin.x, (double)dstMin.y,
			(double)(dstMax.x - dstMin.x), (double)(dstMax.y - dstMin.y),
			0, 1
		};
		[m_impl->renderEncoder setViewport:vp];

		// Build a quad with UVs that sample from the source rectangle in temp.
		const Rml::Vector2f srcUVMin = Rml::Vector2f(srcMin) / Rml::Vector2f(temp.width, temp.height);
		const Rml::Vector2f srcUVMax = Rml::Vector2f(srcMax) / Rml::Vector2f(temp.width, temp.height);

		Rml::Mesh mesh;
		Rml::MeshUtilities::GenerateQuad(mesh, Rml::Vector2f(-1), Rml::Vector2f(2), {});
		for (Rml::Vertex& v : mesh.vertices)
			v.tex_coord = srcUVMin + v.tex_coord * (srcUVMax - srcUVMin);

		const auto tempGeo = CompileGeometry(mesh.vertices, mesh.indices);

		[m_impl->renderEncoder setFragmentTexture:temp.colorTexture atIndex:0];
		[m_impl->renderEncoder setFragmentSamplerState:m_impl->samplerLinearClamp atIndex:0];

		auto* geom = reinterpret_cast<MetalGfx::CompiledGeometryData*>(tempGeo);
		[m_impl->renderEncoder setVertexBuffer:geom->vertexBuffer offset:0 atIndex:0];
		[m_impl->renderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
										  indexCount:geom->indexCount
										   indexType:MTLIndexTypeUInt32
										 indexBuffer:geom->indexBuffer
									   indexBufferOffset:0];
		ReleaseGeometry(tempGeo);

		// Also do the power-of-two upscale pass if the downscaled region doesn't exactly
		// match a power-of-two division of the destination (matches GL3 second blit pass).
		const Rml::Vector2i targetMin = srcMin * (1 << passLevel);
		const Rml::Vector2i targetMax = srcMax * (1 << passLevel);
		if (targetMin != dstMin || targetMax != dstMax)
		{
			MTLViewport vp2 = {
				(double)targetMin.x, (double)targetMin.y,
				(double)(targetMax.x - targetMin.x), (double)(targetMax.y - targetMin.y),
				0, 1
			};
			[m_impl->renderEncoder setViewport:vp2];

			// Reuse same UVs (still reading from source rect in temp)
			const auto tempGeo2 = CompileGeometry(mesh.vertices, mesh.indices);
			auto* geom2 = reinterpret_cast<MetalGfx::CompiledGeometryData*>(tempGeo2);
			[m_impl->renderEncoder setVertexBuffer:geom2->vertexBuffer offset:0 atIndex:0];
			[m_impl->renderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
											  indexCount:geom2->indexCount
											   indexType:MTLIndexTypeUInt32
											 indexBuffer:geom2->indexBuffer
										   indexBufferOffset:0];
			ReleaseGeometry(tempGeo2);
		}
	}
}

const Rml::Matrix4f& RenderInterface_Metal::GetTransform() const
{
	return m_impl->transform;
}

void RenderInterface_Metal::ResetPipeline()
{
	m_impl->activePipeline = MetalGfx::PipelineId::None;
}

bool RmlMetal::IsSupported()
{
	id<MTLDevice> device = MTLCreateSystemDefaultDevice();
	return device != nil;
}

#endif // __APPLE__
