#include "rmlRenderInterfaceFilters.h"

#include "rmlRenderInterfaceShaders.h"
#include "RmlUi/Core/Dictionary.h"

namespace juceRmlUi::gl2
{
	RenderInterfaceFilters::RenderInterfaceFilters(RenderInterfaceShaders& _shaders) : m_shaders(_shaders)
	{
	}

	CompiledShader* RenderInterfaceFilters::create(const Rml::String& _name, const Rml::Dictionary& _parameters)
	{
		CompiledShader filter = {};

		if (_name == "opacity")
		{
			const auto opacity = Rml::Get(_parameters, "value", 1.0f);

			filter.type = ShaderType::FullscreenColorMatrix;
			filter.params.colorMatrix = Rml::Matrix4f::Identity();
			filter.params.colorMatrix[3][3] = opacity;
		}
		else if (_name == "blur")
		{
			return nullptr;
		}
		else if (_name == "drop-shadow")
		{
			return nullptr;
			/*
			filter.type = FilterType::DropShadow;
			filter.sigma = Rml::Get(parameters, "sigma", 0.f);
			filter.color = Rml::Get(parameters, "color", Rml::Colourb()).ToPremultiplied();
			filter.offset = Rml::Get(parameters, "offset", Rml::Vector2f(0.f));
			*/
		}
		else if (_name == "brightness")
		{
			filter.type = ShaderType::FullscreenColorMatrix;
			const float value = Rml::Get(_parameters, "value", 1.0f);
			filter.params.colorMatrix = Rml::Matrix4f::Diag(value, value, value, 1.f);
		}
		else if (_name == "contrast")
		{
			filter.type = ShaderType::FullscreenColorMatrix;
			const float value = Rml::Get(_parameters, "value", 1.0f);
			const float grayness = 0.5f - 0.5f * value;
			filter.params.colorMatrix = Rml::Matrix4f::Diag(value, value, value, 1.f);
			filter.params.colorMatrix.SetColumn(3, Rml::Vector4f(grayness, grayness, grayness, 1.f));
		}
		else if (_name == "invert")
		{
			filter.type = ShaderType::FullscreenColorMatrix;
			const float value = Rml::Math::Clamp(Rml::Get(_parameters, "value", 1.0f), 0.f, 1.f);
			const float inverted = 1.f - 2.f * value;
			filter.params.colorMatrix = Rml::Matrix4f::Diag(inverted, inverted, inverted, 1.f);
			filter.params.colorMatrix.SetColumn(3, Rml::Vector4f(value, value, value, 1.f));
		}
		else if (_name == "grayscale")
		{
			filter.type = ShaderType::FullscreenColorMatrix;
			const float value = Rml::Get(_parameters, "value", 1.0f);
			const float valueInv = 1.f - value;
			const Rml::Vector3f gray = value * Rml::Vector3f(0.2126f, 0.7152f, 0.0722f);

			filter.params.colorMatrix = Rml::Matrix4f::FromRows(
				{gray.x + valueInv, gray.y,            gray.z,            0.f},
				{gray.x,            gray.y + valueInv, gray.z,            0.f},
				{gray.x,            gray.y,            gray.z + valueInv, 0.f},
				{0.f,               0.f,               0.f,               1.f}
			);
		}
		else if (_name == "sepia")
		{
			filter.type = ShaderType::FullscreenColorMatrix;
			const float value = Rml::Get(_parameters, "value", 1.0f);
			const float valueInv = 1.f - value;
			const Rml::Vector3f r = value * Rml::Vector3f(0.393f, 0.769f, 0.189f);
			const Rml::Vector3f g = value * Rml::Vector3f(0.349f, 0.686f, 0.168f);
			const Rml::Vector3f b = value * Rml::Vector3f(0.272f, 0.534f, 0.131f);

			filter.params.colorMatrix = Rml::Matrix4f::FromRows(
				{r.x + valueInv, r.y,             r.z,             0.f},
				{g.x,            g.y + valueInv,  g.z,             0.f},
				{b.x,            b.y,             b.z + valueInv,  0.f},
				{0.f,            0.f,             0.f,             1.f}
			);
		}
		else if (_name == "hue-rotate")
		{
			// Hue-rotation and saturation values based on: https://www.w3.org/TR/filter-effects-1/#attr-valuedef-type-huerotate
			filter.type = ShaderType::FullscreenColorMatrix;
			const float value = Rml::Get(_parameters, "value", 1.0f);
			const float s = Rml::Math::Sin(value);
			const float c = Rml::Math::Cos(value);

			filter.params.colorMatrix = Rml::Matrix4f::FromRows(
				{0.213f + 0.787f * c - 0.213f * s,  0.715f - 0.715f * c - 0.715f * s,  0.072f - 0.072f * c + 0.928f * s,  0.f},
				{0.213f - 0.213f * c + 0.143f * s,  0.715f + 0.285f * c + 0.140f * s,  0.072f - 0.072f * c - 0.283f * s,  0.f},
				{0.213f - 0.213f * c - 0.787f * s,  0.715f - 0.715f * c + 0.715f * s,  0.072f + 0.928f * c + 0.072f * s,  0.f},
				{0.f,                               0.f,                               0.f,                               1.f}
			);
		}
		else if (_name == "saturate")
		{
			filter.type = ShaderType::FullscreenColorMatrix;
			const float value = Rml::Get(_parameters, "value", 1.0f);

			filter.params.colorMatrix = Rml::Matrix4f::FromRows(
				{0.213f + 0.787f * value,  0.715f - 0.715f * value,  0.072f - 0.072f * value,  0.f},
				{0.213f - 0.213f * value,  0.715f + 0.285f * value,  0.072f - 0.072f * value,  0.f},
				{0.213f - 0.213f * value,  0.715f - 0.715f * value,  0.072f + 0.928f * value,  0.f},
				{0.f,                      0.f,                      0.f,                      1.f}
			);
		}

		if (filter.type != ShaderType::Count)
			return new CompiledShader(filter);

		Rml::Log::Message(Rml::Log::LT_WARNING, "Unsupported filter type '%s'.", _name.c_str());
		return nullptr;
	}
}
