#pragma once

const auto g_vertexShader = R"(

attribute vec3 aPos;

#ifdef USE_TEXTURE
attribute vec2 aTexCoord;
varying vec2 vTexCoord;
#endif

#ifdef USE_VERTEX_COLOR
attribute vec4 aVertexColor;
varying vec4 vVertexColor;
#endif

void main()
{
#ifdef USE_TRANSFORMATION_MATRIX
    gl_Position = gl_ModelViewProjectionMatrix * vec4(aPos, 1.0);
#else
    gl_Position = vec4(aPos, 1.0);
#endif

#ifdef USE_TEXTURE
    vTexCoord = aTexCoord;
#endif
})";

// ______________________
//
const auto g_fragmentShader = R"(

uniform sampler2D uTexture;

#ifdef USE_TEXTURE
varying vec2 vTexCoord;
#endif

#ifdef USE_VERTEX_COLOR
varying vec4 vVertexColor;
#endif

#ifdef USE_COLOR_MATRIX
uniform mat4 uColorMatrix;
#endif

void main()
{
#ifdef USE_TEXTURE
    vec4 color = texture2D(uTexture, vTexCoord);
#else
    vec4 color = vec4(1,1,1,1);
#endif

#if USE_COLOR_MATRIX
	// The general case uses a 4x5 color matrix for full rgba transformation, plus a constant term with the last column.
	// However, we only consider the case of rgb transformations. Thus, we could in principle use a 3x4 matrix, but we
	// keep the alpha row for simplicity.
	// In the general case we should do the matrix transformation in non-premultiplied space. However, without alpha
	// transformations, we can do it directly in premultiplied space to avoid the extra division and multiplication
	// steps. In this space, the constant term needs to be multiplied by the alpha value, instead of unity.
	vec4 texColor = texture(_tex, fragTexCoord);
	color.rgb = (uColorMatrix * color).rgb;
#endif

#ifdef USE_VERTEX_COLOR
	color *= vVertexColor;
#endif

	float gr = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    color.rgb = vec3(gr, gr, gr);
    gl_FragColor = color;
})";
