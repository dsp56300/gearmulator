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

#ifdef USE_VERTEX_COLOR
	vVertexColor = aVertexColor;
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

#ifdef USE_COLOR_MATRIX
	color = uColorMatrix * color;
#endif

#ifdef USE_VERTEX_COLOR
	color *= vVertexColor;
#endif

//	float gr = dot(color.rgb, vec3(0.299, 0.587, 0.114));
//  color.rgb = vec3(gr, gr, gr);
    gl_FragColor = color;
})";
