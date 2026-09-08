#include <sagas/Render.hpp>

#include <SDL3/SDL.h>
#include <png.h>

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <SDL3/SDL_opengl.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace sagas {
namespace {

[[noreturn]] void fail(std::string message) {
    if (const char* detail=SDL_GetError();detail&&*detail) message += ": "+std::string(detail);
    throw std::runtime_error(std::move(message));
}

GLuint shader(GLenum kind,const char* source) {
    const GLuint handle=glCreateShader(kind);
    glShaderSource(handle,1,&source,nullptr);
    glCompileShader(handle);
    GLint okay{};
    glGetShaderiv(handle,GL_COMPILE_STATUS,&okay);
    if (!okay) {
        GLint size{};
        glGetShaderiv(handle,GL_INFO_LOG_LENGTH,&size);
        std::string log(static_cast<std::size_t>(std::max(size,1)),0);
        glGetShaderInfoLog(handle,size,nullptr,log.data());
        glDeleteShader(handle);
        throw std::runtime_error("OpenGL shader compilation failed: "+log);
    }
    return handle;
}

GLuint program(const char* vertex_source,const char* fragment_source) {
    const GLuint vertex=shader(GL_VERTEX_SHADER,vertex_source);
    const GLuint fragment=shader(GL_FRAGMENT_SHADER,fragment_source);
    const GLuint handle=glCreateProgram();
    glAttachShader(handle,vertex);
    glAttachShader(handle,fragment);
    glLinkProgram(handle);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint okay{};
    glGetProgramiv(handle,GL_LINK_STATUS,&okay);
    if (!okay) {
        GLint size{};
        glGetProgramiv(handle,GL_INFO_LOG_LENGTH,&size);
        std::string log(static_cast<std::size_t>(std::max(size,1)),0);
        glGetProgramInfoLog(handle,size,nullptr,log.data());
        glDeleteProgram(handle);
        throw std::runtime_error("OpenGL program link failed: "+log);
    }
    return handle;
}

constexpr const char* vertex_2d=R"GLSL(#version 410 core
layout(location=0) in vec2 inPosition;
layout(location=1) in vec4 inColor;
layout(location=2) in vec2 inUV;
out vec4 vertexColor;
out vec2 textureUV;
void main() {
    gl_Position=vec4(inPosition.x/160.0-1.0,1.0-inPosition.y/120.0,0.0,1.0);
    vertexColor=inColor;
    textureUV=inUV;
}
)GLSL";

constexpr const char* fragment_2d=R"GLSL(#version 410 core
uniform sampler2D colorTexture;
uniform bool useTexture;
in vec4 vertexColor;
in vec2 textureUV;
out vec4 fragmentColor;
void main() {
    vec4 texel=useTexture ? texture(colorTexture,textureUV) : vec4(1.0);
    vec3 tintLinear=pow(max(vertexColor.rgb,vec3(0.0)),vec3(2.2));
    fragmentColor=vec4(texel.rgb*tintLinear,texel.a*vertexColor.a);
}
)GLSL";

constexpr const char* vertex_forward=R"GLSL(#version 410 core
layout(location=0) in vec3 inPosition;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec4 inColor;
layout(location=3) in vec2 inUV;
uniform float focal;
uniform vec2 viewportScale;
uniform vec2 viewportOffset;
uniform float nearPlane;
uniform float farPlane;
uniform vec3 shadowRight;
uniform vec3 shadowUp;
uniform vec3 shadowForward;
uniform vec3 shadowMinimum;
uniform vec3 shadowMaximum;
uniform bool rdpEnabled;
uniform bool useLighting;
uniform vec3 keyDirection;
uniform vec3 n64Diffuse;
uniform vec3 n64Ambient;
uniform bool textureGen;
uniform bool textureGenLinear;
uniform vec2 generatedScale;
noperspective out vec4 nativeShade;
out vec3 normal;
out vec3 viewDirection;
out vec3 viewPosition;
out vec4 vertexColor;
out vec2 textureUV;
out vec3 shadowCoordinate;
void main() {
    float z=inPosition.z;
    float clipZ=((farPlane+nearPlane)/(farPlane-nearPlane))*z
               -(2.0*farPlane*nearPlane)/(farPlane-nearPlane);
    gl_Position=vec4(inPosition.xy*focal*viewportScale+viewportOffset*z,clipZ,z);
    normal=inNormal;
    viewDirection=normalize(-inPosition);
    viewPosition=inPosition;
    vertexColor=inColor;
    textureUV=inUV;
    nativeShade=inColor;
    if (rdpEnabled && useLighting) {
        vec3 N=normalize(inNormal);
        nativeShade.rgb=clamp(n64Ambient+n64Diffuse*max(dot(N,normalize(keyDirection)),0.0),0.0,1.0);
    }
    if (rdpEnabled && textureGen) {
        vec3 N=normalize(inNormal);
        vec2 generated=textureGenLinear ? acos(clamp(-N.xy,-1.0,1.0))/3.14159265 : (N.xy+1.0)*0.5;
        textureUV=generated*generatedScale;
    }
    vec3 lightPosition=vec3(dot(inPosition,shadowRight),dot(inPosition,shadowUp),
                            dot(inPosition,shadowForward));
    shadowCoordinate=(lightPosition-shadowMinimum)/max(shadowMaximum-shadowMinimum,vec3(0.0001));
}
)GLSL";

constexpr const char* fragment_forward=R"GLSL(#version 410 core
uniform sampler2D colorTexture;
uniform sampler2DShadow shadowMap;
uniform bool rdpEnabled;
uniform uvec2 combineWords;
uniform int combineCycles;
uniform vec4 primitiveColor;
uniform vec4 environmentColor;
uniform vec4 drawTint;
uniform float alphaThreshold;
uniform bool alphaTest;
noperspective in vec4 nativeShade;
uniform bool useTexture;
uniform bool useLighting;
uniform bool translucent;
uniform bool shadowsReady;
uniform ivec2 textureMode;
uniform ivec2 textureMask;
uniform vec2 textureWindow;
uniform vec3 ambientColor;
uniform float ambientIntensity;
uniform vec3 keyDirection;
uniform vec3 keyColor;
uniform float keyIntensity;
uniform bool useSpotLight;
uniform vec3 spotPosition;
uniform vec3 spotDirection;
uniform vec3 spotColor;
uniform float spotIntensity;
uniform float spotRange;
uniform float spotInnerCone;
uniform float spotOuterCone;
uniform vec3 environmentUp;
uniform vec3 reflectionColor;
uniform float reflectionIntensity;
uniform float shininess;
uniform vec3 materialDiffuse;
uniform vec3 materialAmbient;
in vec3 normal;
in vec3 viewDirection;
in vec3 viewPosition;
in vec4 vertexColor;
in vec2 textureUV;
in vec3 shadowCoordinate;
out vec4 fragmentColor;

float n64Coordinate(float normalized,float extent,int mode,int maskBits,float windowSize) {
    float value=normalized*extent;
    if ((mode&2)!=0) value=clamp(value,0.0,max(windowSize-1.0,0.0));
    float period=maskBits>0 ? exp2(float(maskBits)) : extent;
    float section=floor(value/period);
    float sampled=mod(value,period);
    if (sampled<0.0) sampled+=period;
    if ((mode&1)!=0 && (int(section)&1)!=0) sampled=period-sampled;
    return sampled/extent;
}

float filteredShadow(vec3 coordinate,vec3 N,vec3 L) {
    if (!shadowsReady || coordinate.x<=0.0 || coordinate.x>=1.0 ||
        coordinate.y<=0.0 || coordinate.y>=1.0 || coordinate.z<=0.0 || coordinate.z>=1.0) return 1.0;
    float bias=max(0.00035*(1.0-max(dot(N,L),0.0)),0.00008);
    vec2 texel=1.0/vec2(textureSize(shadowMap,0));
    float visibility=0.0;
    for (int y=-2;y<=2;++y) for (int x=-2;x<=2;++x)
        visibility+=texture(shadowMap,vec3(coordinate.xy+vec2(x,y)*texel,coordinate.z-bias));
    return visibility/25.0;
}

// Nintendo RDP color-combiner muxes: each component evaluates (A-B)*C+D.
vec3 rgbSource(uint selector,int inputSlot,vec4 combined,vec4 texel,vec4 shade) {
    if (selector==0u) return combined.rgb;
    if (selector==1u || selector==2u) return texel.rgb;
    if (selector==3u) return primitiveColor.rgb;
    if (selector==4u) return shade.rgb;
    if (selector==5u) return environmentColor.rgb;
    if (inputSlot==2) {
        if (selector==7u) return vec3(combined.a);
        if (selector==8u || selector==9u) return vec3(texel.a);
        if (selector==10u) return vec3(primitiveColor.a);
        if (selector==11u) return vec3(shade.a);
        if (selector==12u) return vec3(environmentColor.a);
        return vec3(0.0);
    }
    if (selector==6u && inputSlot!=1) return vec3(1.0);
    return vec3(0.0);
}
float alphaSource(uint selector,bool multiplier,vec4 combined,vec4 texel,vec4 shade) {
    if (selector==0u) return multiplier ? 0.0 : combined.a;
    if (selector==1u || selector==2u) return texel.a;
    if (selector==3u) return primitiveColor.a;
    if (selector==4u) return shade.a;
    if (selector==5u) return environmentColor.a;
    if (selector==6u) return multiplier ? 0.0 : 1.0;
    return 0.0;
}
vec4 combineCycle(int cycle,vec4 previous,vec4 texel,vec4 shade) {
    uint h=combineWords.x, l=combineWords.y;
    uvec4 rgb=cycle==0 ? uvec4((h>>20)&15u,(l>>28)&15u,(h>>15)&31u,(l>>15)&7u)
                      : uvec4((h>>5)&15u,(l>>24)&15u,h&31u,(l>>6)&7u);
    uvec4 a=cycle==0 ? uvec4((h>>12)&7u,(l>>12)&7u,(h>>9)&7u,(l>>9)&7u)
                    : uvec4((l>>21)&7u,(l>>3)&7u,(l>>18)&7u,l&7u);
    vec3 color=(rgbSource(rgb.x,0,previous,texel,shade)-rgbSource(rgb.y,1,previous,texel,shade))
        *rgbSource(rgb.z,2,previous,texel,shade)+rgbSource(rgb.w,3,previous,texel,shade);
    float alpha=(alphaSource(a.x,false,previous,texel,shade)-alphaSource(a.y,false,previous,texel,shade))
        *alphaSource(a.z,true,previous,texel,shade)+alphaSource(a.w,false,previous,texel,shade);
    return clamp(vec4(color,alpha),0.0,1.0);
}

void main() {
    vec4 texel=vec4(1.0);
    if (useTexture) {
        vec2 extent=vec2(textureSize(colorTexture,0));
        vec2 uv=vec2(n64Coordinate(textureUV.x,extent.x,textureMode.x,textureMask.x,textureWindow.x),
                     n64Coordinate(textureUV.y,extent.y,textureMode.y,textureMask.y,textureWindow.y));
        texel=texture(colorTexture,uv);
    }
    if (rdpEnabled) {
        // Textures are uploaded as sRGB; the RDP combines their encoded
        // channels, then the framebuffer performs the display conversion.
        vec4 encoded=vec4(pow(max(texel.rgb,vec3(0.0)),vec3(1.0/2.2)),texel.a);
        vec4 combined=vec4(0.0);
        if (combineCycles==2) combined=combineCycle(0,combined,encoded,nativeShade);
        combined=combineCycle(1,combined,encoded,nativeShade)*drawTint;
        if (alphaTest && combined.a<=alphaThreshold) discard;
        fragmentColor=vec4(pow(max(combined.rgb,vec3(0.0)),vec3(2.2)),translucent ? combined.a : 1.0);
        return;
    }
    vec3 vertexLinear=pow(max(vertexColor.rgb,vec3(0.0)),vec3(2.2));
    vec3 albedo=texel.rgb*vertexLinear*materialDiffuse;
    float alpha=texel.a*vertexColor.a;
    if (alpha<0.004) discard;
    vec3 color=albedo;
    if (useLighting) {
        vec3 V=normalize(viewDirection);
        vec3 sourceNormal=normalize(normal);
        vec3 L=normalize(keyDirection);
        // Room display lists mix winding on the same visual face.  Orienting
        // toward the camera made the crate's shade follow the view and
        // flicker.  Two-sided lighting toward the key light is stable and
        // still shades the box relative to the room rig.
        vec3 N=faceforward(sourceNormal,-L,sourceNormal);
        vec3 H=normalize(L+V);
        // N64 lights are Gouraud + a strong ambient term, not a binary
        // shadow map. Keep a soft floor so unlit sides stay readable, and
        // mask the orthographic map with a disc so the desk does not get a
        // hard square.
        float radial=length(shadowCoordinate.xy-vec2(0.5))*2.0;
        float disc=1.0-smoothstep(0.62,1.05,radial);
        float visibility=mix(0.62,1.0,filteredShadow(shadowCoordinate,N,L)*disc);
        vec3 reference=abs(L.y)<0.92 ? vec3(0.0,1.0,0.0) : vec3(1.0,0.0,0.0);
        vec3 lightTangent=normalize(cross(reference,L));
        vec3 lightBitangent=normalize(cross(L,lightTangent));
        // Integrate a small area around the key direction.  The original
        // vertex lights have a broad response; this keeps low-poly faces
        // from switching between hard fully-lit and fully-dark bands.
        float diffuse=0.0;
        const vec2 areaSamples[5]=vec2[5](vec2(0.0),vec2(0.16,0.0),vec2(-0.16,0.0),
                                          vec2(0.0,0.16),vec2(0.0,-0.16));
        for (int sampleIndex=0;sampleIndex<5;++sampleIndex) {
            vec3 areaL=normalize(L+lightTangent*areaSamples[sampleIndex].x+
                                  lightBitangent*areaSamples[sampleIndex].y);
            float wrapped=clamp((dot(N,areaL)+0.35)/1.35,0.0,1.0);
            diffuse+=wrapped*wrapped*(3.0-2.0*wrapped);
        }
        diffuse/=5.0;
        float nDotL=dot(N,L);
        float nDotV=max(dot(N,V),0.0);
        float nDotH=max(dot(N,H),0.0);
        float vDotH=max(dot(V,H),0.0);
        float roughness=clamp(sqrt(2.0/(max(shininess,1.0)+2.0)),0.28,0.82);
        float a2=roughness*roughness;
        a2*=a2;
        float denominator=nDotH*nDotH*(a2-1.0)+1.0;
        float distribution=a2/max(3.14159265*denominator*denominator,0.001);
        float k=(roughness+1.0)*(roughness+1.0)/8.0;
        float geometryV=nDotV/(nDotV*(1.0-k)+k);
        float geometryL=max(nDotL,0.0)/(max(nDotL,0.0)*(1.0-k)+k);
        vec3 fresnel=vec3(0.04)+(vec3(1.0)-vec3(0.04))*pow(1.0-vDotH,5.0);
        vec3 specular=distribution*geometryV*geometryL*fresnel/max(4.0*nDotV*max(nDotL,0.0),0.01);
        vec3 R=reflect(-V,N);
        float reflectedHeight=dot(R,normalize(environmentUp))*0.5+0.5;
        vec3 sky=mix(vec3(0.055,0.065,0.085),reflectionColor,clamp(reflectedHeight,0.0,1.0));
        vec3 rim=vec3(0.04)+(reflectionColor-vec3(0.04))*pow(1.0-nDotV,5.0);
        float hemisphere=dot(N,normalize(environmentUp))*0.5+0.5;
        vec3 lowerAmbient=ambientColor*vec3(0.58,0.54,0.50);
        vec3 upperAmbient=mix(ambientColor,reflectionColor,0.24);
        vec3 ambient=mix(lowerAmbient,upperAmbient,hemisphere)*ambientIntensity*materialAmbient;
        color=albedo*(ambient+keyColor*keyIntensity*diffuse*visibility)
             +keyColor*keyIntensity*specular*visibility
             +sky*rim*reflectionIntensity;
        if (useSpotLight) {
            vec3 toSpot=spotPosition-viewPosition;
            float spotDistance=length(toSpot);
            vec3 spotL=toSpot/max(spotDistance,0.0001);
            float coneAngle=dot(normalize(spotDirection),-spotL);
            float cone=smoothstep(spotOuterCone,spotInnerCone,coneAngle);
            float normalizedDistance=clamp(spotDistance/max(spotRange,0.0001),0.0,1.0);
            float attenuation=1.0-normalizedDistance*normalizedDistance;
            attenuation*=attenuation;
            float spotWrapped=clamp((dot(N,spotL)+0.28)/1.28,0.0,1.0);
            float spotDiffuse=spotWrapped*spotWrapped*(3.0-2.0*spotWrapped);
            vec3 spotH=normalize(spotL+V);
            float spotSpecular=pow(max(dot(N,spotH),0.0),max(shininess,1.0));
            vec3 radiance=spotColor*spotIntensity*cone*attenuation;
            color+=albedo*radiance*spotDiffuse+radiance*spotSpecular*0.24;
        }
    }
    if (!translucent) alpha=1.0;
    fragmentColor=vec4(max(color,vec3(0.0)),alpha);
}
)GLSL";

constexpr const char* vertex_shadow=R"GLSL(#version 410 core
layout(location=0) in vec3 inPosition;
uniform vec3 shadowRight;
uniform vec3 shadowUp;
uniform vec3 shadowForward;
uniform vec3 shadowMinimum;
uniform vec3 shadowMaximum;
void main() {
    vec3 lightPosition=vec3(dot(inPosition,shadowRight),dot(inPosition,shadowUp),
                            dot(inPosition,shadowForward));
    vec3 normalized=(lightPosition-shadowMinimum)/max(shadowMaximum-shadowMinimum,vec3(0.0001));
    gl_Position=vec4(normalized*2.0-1.0,1.0);
}
)GLSL";

constexpr const char* fragment_shadow=R"GLSL(#version 410 core
void main() {}
)GLSL";

struct Vertex2D { float x{},y{},r{},g{},b{},a{},u{},v{}; };
struct VertexForward { float px{},py{},pz{},nx{},ny{},nz{},r{},g{},b{},a{},u{},v{}; };

void color_uniform(GLint location,Color color) {
    glUniform3f(location,std::pow(color.r/255.0f,2.2f),std::pow(color.g/255.0f,2.2f),
                std::pow(color.b/255.0f,2.2f));
}

float dot3(Vec3 a,Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vec3 cross3(Vec3 a,Vec3 b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
Vec3 normalize3(Vec3 value) {
    const float length=std::sqrt(std::max(dot3(value,value),1.0e-12f));
    return {value.x/length,value.y/length,value.z/length};
}

} // namespace

RenderEngine::RenderEngine(SDL_Window* window,AssetRepository& assets)
    : window_(window),assets_(assets) {
    context_=SDL_GL_CreateContext(window_);
    if (!context_) fail("OpenGL 4.1 context creation failed");
    if (!SDL_GL_MakeCurrent(window_,context_)) fail("OpenGL context activation failed");
    SDL_GL_SetSwapInterval(1);
    GLint major{},minor{};
    glGetIntegerv(GL_MAJOR_VERSION,&major);
    glGetIntegerv(GL_MINOR_VERSION,&minor);
    if (major<4||(major==4&&minor<1)) throw std::runtime_error("Sagas requires OpenGL 4.1 Core Profile");
    program_2d_=program(vertex_2d,fragment_2d);
    program_forward_=program(vertex_forward,fragment_forward);
    program_shadow_=program(vertex_shadow,fragment_shadow);

    glGenVertexArrays(1,&vao_2d_);
    glGenBuffers(1,&vbo_2d_);
    glBindVertexArray(vao_2d_);
    glBindBuffer(GL_ARRAY_BUFFER,vbo_2d_);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(Vertex2D),reinterpret_cast<void*>(offsetof(Vertex2D,x)));
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,sizeof(Vertex2D),reinterpret_cast<void*>(offsetof(Vertex2D,r)));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,sizeof(Vertex2D),reinterpret_cast<void*>(offsetof(Vertex2D,u)));

    glGenVertexArrays(1,&vao_forward_);
    glGenBuffers(1,&vbo_forward_);
    glBindVertexArray(vao_forward_);
    glBindBuffer(GL_ARRAY_BUFFER,vbo_forward_);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(VertexForward),reinterpret_cast<void*>(offsetof(VertexForward,px)));
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(VertexForward),reinterpret_cast<void*>(offsetof(VertexForward,nx)));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(VertexForward),reinterpret_cast<void*>(offsetof(VertexForward,r)));
    glEnableVertexAttribArray(3); glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,sizeof(VertexForward),reinterpret_cast<void*>(offsetof(VertexForward,u)));
    glBindVertexArray(0);

    glGenTextures(1,&shadow_texture_);
    glBindTexture(GL_TEXTURE_2D,shadow_texture_);
    glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,2048,2048,0,GL_DEPTH_COMPONENT,GL_FLOAT,nullptr);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_BORDER);
    const std::array<float,4> border{1,1,1,1};
    glTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,border.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_FUNC,GL_LEQUAL);
    glGenFramebuffers(1,&shadow_framebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER,shadow_framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,shadow_texture_,0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("OpenGL shadow framebuffer creation failed");
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_FRAMEBUFFER_SRGB);
}

RenderEngine::~RenderEngine() {
    if (context_) SDL_GL_MakeCurrent(window_,context_);
    for (const auto& [_,texture]:textures_) if (texture.handle) glDeleteTextures(1,&texture.handle);
    for (const auto& [_,texture]:raster_textures_) if (texture) glDeleteTextures(1,&texture);
    if (vbo_2d_) glDeleteBuffers(1,&vbo_2d_);
    if (vbo_forward_) glDeleteBuffers(1,&vbo_forward_);
    if (vao_2d_) glDeleteVertexArrays(1,&vao_2d_);
    if (vao_forward_) glDeleteVertexArrays(1,&vao_forward_);
    if (program_2d_) glDeleteProgram(program_2d_);
    if (program_forward_) glDeleteProgram(program_forward_);
    if (program_shadow_) glDeleteProgram(program_shadow_);
    if (shadow_framebuffer_) glDeleteFramebuffers(1,&shadow_framebuffer_);
    if (shadow_texture_) glDeleteTextures(1,&shadow_texture_);
    if (context_) SDL_GL_DestroyContext(context_);
}

RenderEngine::Texture& RenderEngine::texture(std::string_view logical) {
    const std::string key(logical);
    if (auto found=textures_.find(key);found!=textures_.end()) return found->second;
    png_image image{};
    image.version=PNG_IMAGE_VERSION;
    const auto file=assets_.path(logical).string();
    if (!png_image_begin_read_from_file(&image,file.c_str())) throw std::runtime_error("PNG read failed: "+file);
    image.format=PNG_FORMAT_RGBA;
    std::vector<std::uint8_t> pixels(PNG_IMAGE_SIZE(image));
    if (!png_image_finish_read(&image,nullptr,pixels.data(),0,nullptr)) {
        png_image_free(&image);
        throw std::runtime_error("PNG decode failed: "+file);
    }
    // The extraction tree stores N64 I/IA sprite masks as opaque grayscale
    // PNGs.  In the source combiner their intensity is coverage, not a black
    // rectangular background.  Recover that representation before upload.
    bool intensity_mask=true;
    for (std::size_t i=0;i<pixels.size();i+=4) {
        if (pixels[i]!=pixels[i+1] || pixels[i]!=pixels[i+2] || pixels[i+3]!=255) {
            intensity_mask=false;
            break;
        }
    }
    // Collage is genuine opaque grayscale artwork.  Unlike the I/IA glyphs
    // and decals around it, its dark pixels are image data rather than alpha.
    if (key.ends_with("/SmashBrosCollage.png") || key.ends_with("/StoneBackground.png")) intensity_mask=false;
    if (intensity_mask) for (std::size_t i=0;i<pixels.size();i+=4) {
        pixels[i+3]=pixels[i];
        pixels[i]=pixels[i+1]=pixels[i+2]=255;
    }
    GLuint handle{};
    glGenTextures(1,&handle);
    glBindTexture(GL_TEXTURE_2D,handle);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexImage2D(GL_TEXTURE_2D,0,GL_SRGB8_ALPHA8,static_cast<GLsizei>(image.width),static_cast<GLsizei>(image.height),
                 0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    auto [inserted,_]=textures_.emplace(key,Texture{handle,static_cast<float>(image.width),static_cast<float>(image.height)});
    png_image_free(&image);
    return inserted->second;
}

std::uint32_t RenderEngine::raster_texture(const std::shared_ptr<const RasterImage>& image) {
    if (!image||image->width<=0||image->height<=0||image->rgba.empty()) return 0;
    if (const auto found=raster_textures_.find(image);found!=raster_textures_.end()) return found->second;
    GLuint handle{};
    glGenTextures(1,&handle);
    glBindTexture(GL_TEXTURE_2D,handle);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexImage2D(GL_TEXTURE_2D,0,GL_SRGB8_ALPHA8,image->width,image->height,0,GL_RGBA,GL_UNSIGNED_BYTE,image->rgba.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    raster_textures_.emplace(image,handle);
    return handle;
}

void RenderEngine::begin(Color clear) {
    SDL_GL_MakeCurrent(window_,context_);
    shadows_ready_=false;
    for (auto it=raster_textures_.begin();it!=raster_textures_.end();) {
        if (it->first.use_count()==1) {
            glDeleteTextures(1,&it->second);
            it=raster_textures_.erase(it);
        } else ++it;
    }
    int drawable_width{},drawable_height{};
    SDL_GetWindowSizeInPixels(window_,&drawable_width,&drawable_height);
    const float target=16.0f/9.0f;
    if (drawable_height>0&&static_cast<float>(drawable_width)/drawable_height>target) {
        viewport_height_=drawable_height;
        viewport_width_=static_cast<int>(std::lround(drawable_height*target));
    } else {
        viewport_width_=drawable_width;
        viewport_height_=static_cast<int>(std::lround(drawable_width/target));
    }
    viewport_x_=(drawable_width-viewport_width_)/2;
    viewport_y_=(drawable_height-viewport_height_)/2;
    glDisable(GL_SCISSOR_TEST);
    glViewport(0,0,drawable_width,drawable_height);
    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(viewport_x_,viewport_y_,viewport_width_,viewport_height_);
    glViewport(viewport_x_,viewport_y_,viewport_width_,viewport_height_);
    glClearColor(clear.r/255.0f,clear.g/255.0f,clear.b/255.0f,clear.a/255.0f);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
}

void RenderEngine::draw_2d(std::span<const TriangleVertex> vertices,GLuint texture_handle) {
    if (vertices.empty()) return;
    std::vector<Vertex2D> data;
    data.reserve(vertices.size());
    for (const auto& vertex:vertices) data.push_back({vertex.position.x,vertex.position.y,
        vertex.color.r/255.0f,vertex.color.g/255.0f,vertex.color.b/255.0f,vertex.color.a/255.0f,
        vertex.uv.x,vertex.uv.y});
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(program_2d_);
    glUniform1i(glGetUniformLocation(program_2d_,"colorTexture"),0);
    glUniform1i(glGetUniformLocation(program_2d_,"useTexture"),texture_handle!=0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,texture_handle);
    glBindVertexArray(vao_2d_);
    glBindBuffer(GL_ARRAY_BUFFER,vbo_2d_);
    glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(data.size()*sizeof(Vertex2D)),data.data(),GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(data.size()));
    glDisable(GL_CULL_FACE);
}

void RenderEngine::sprite(std::string_view logical,Vec2 center,Vec2 scale,Color tint) {
    const auto& source=texture(logical);
    const float left=center.x-source.width*scale.x*0.5f;
    const float right=center.x+source.width*scale.x*0.5f;
    const float top=center.y-source.height*scale.y*0.5f;
    const float bottom=center.y+source.height*scale.y*0.5f;
    const std::array<TriangleVertex,6> vertices{{
        {{left,top},tint,{0,0}},{{right,top},tint,{1,0}},{{right,bottom},tint,{1,1}},
        {{left,top},tint,{0,0}},{{right,bottom},tint,{1,1}},{{left,bottom},tint,{0,1}}}};
    draw_2d(vertices,source.handle);
}

void RenderEngine::sprite_rect(std::string_view logical,float x,float y,float w,float h,Color tint) {
    const auto& source=texture(logical);
    sprite(logical,{x+w/2,y+h/2},{w/source.width,h/source.height},tint);
}

void RenderEngine::sprite_at(std::string_view logical,Vec2 top_left,Vec2 scale,Color tint) {
    // Source UI descriptors use top-left coordinates; keep that convention
    // out of scene code while the core sprite primitive remains center-based.
    const auto& source=texture(logical);
    sprite(logical,{top_left.x+source.width*scale.x*0.5f,
                    top_left.y+source.height*scale.y*0.5f},scale,tint);
}

void RenderEngine::fill(float x,float y,float w,float h,Color color) {
    const std::array<TriangleVertex,6> vertices{{
        {{x,y},color,{}},{{x+w,y},color,{}},{{x+w,y+h},color,{}},
        {{x,y},color,{}},{{x+w,y+h},color,{}},{{x,y+h},color,{}}}};
    draw_2d(vertices,0);
}

void RenderEngine::triangles(std::span<const TriangleVertex> vertices,
                             const std::shared_ptr<const RasterImage>& image) {
    draw_2d(vertices,raster_texture(image));
}

void RenderEngine::prepare_forward_shadows(std::span<const ForwardVertex> vertices,Vec3 light_direction) {
    shadows_ready_=false;
    if (vertices.empty()) return;
    shadow_forward_=normalize3({-light_direction.x,-light_direction.y,-light_direction.z});
    const Vec3 reference=std::abs(shadow_forward_.y)<0.92f ? Vec3{0,1,0} : Vec3{1,0,0};
    shadow_right_=normalize3(cross3(reference,shadow_forward_));
    shadow_up_=normalize3(cross3(shadow_forward_,shadow_right_));
    shadow_min_={std::numeric_limits<float>::infinity(),std::numeric_limits<float>::infinity(),
                 std::numeric_limits<float>::infinity()};
    shadow_max_={-std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),
                 -std::numeric_limits<float>::infinity()};
    std::vector<VertexForward> data;
    data.reserve(vertices.size());
    for (const auto& vertex:vertices) {
        if (!std::isfinite(vertex.position.x)||!std::isfinite(vertex.position.y)||!std::isfinite(vertex.position.z))
            continue;
        const Vec3 light{dot3(vertex.position,shadow_right_),dot3(vertex.position,shadow_up_),
                         dot3(vertex.position,shadow_forward_)};
        shadow_min_.x=std::min(shadow_min_.x,light.x); shadow_max_.x=std::max(shadow_max_.x,light.x);
        shadow_min_.y=std::min(shadow_min_.y,light.y); shadow_max_.y=std::max(shadow_max_.y,light.y);
        shadow_min_.z=std::min(shadow_min_.z,light.z); shadow_max_.z=std::max(shadow_max_.z,light.z);
        data.push_back({vertex.position.x,vertex.position.y,vertex.position.z,
            vertex.normal.x,vertex.normal.y,vertex.normal.z,1,1,1,1,vertex.uv.x,vertex.uv.y});
    }
    if (data.empty()) return;
    const float extent_x=std::max(shadow_max_.x-shadow_min_.x,1.0f);
    const float extent_y=std::max(shadow_max_.y-shadow_min_.y,1.0f);
    const float extent_z=std::max(shadow_max_.z-shadow_min_.z,1.0f);
    const float pad_xy=std::max(extent_x,extent_y)*0.025f+2.0f;
    const float pad_z=extent_z*0.02f+4.0f;
    shadow_min_.x-=pad_xy; shadow_max_.x+=pad_xy;
    shadow_min_.y-=pad_xy; shadow_max_.y+=pad_xy;
    shadow_min_.z-=pad_z; shadow_max_.z+=pad_z;

    glBindFramebuffer(GL_FRAMEBUFFER,shadow_framebuffer_);
    glViewport(0,0,2048,2048);
    glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f,3.0f);
    glUseProgram(program_shadow_);
    const auto vector_uniform=[&](const char* name,Vec3 value) {
        glUniform3f(glGetUniformLocation(program_shadow_,name),value.x,value.y,value.z);
    };
    vector_uniform("shadowRight",shadow_right_);
    vector_uniform("shadowUp",shadow_up_);
    vector_uniform("shadowForward",shadow_forward_);
    vector_uniform("shadowMinimum",shadow_min_);
    vector_uniform("shadowMaximum",shadow_max_);
    glBindVertexArray(vao_forward_);
    glBindBuffer(GL_ARRAY_BUFFER,vbo_forward_);
    glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(data.size()*sizeof(VertexForward)),data.data(),GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(data.size()));
    glDisable(GL_CULL_FACE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glViewport(viewport_x_,viewport_y_,viewport_width_,viewport_height_);
    shadows_ready_=true;
}

void RenderEngine::forward(std::span<const ForwardVertex> vertices,const ForwardMaterial& material) {
    if (vertices.empty()) return;
    std::vector<VertexForward> data;
    data.reserve(vertices.size());
    for (const auto& vertex:vertices) data.push_back({vertex.position.x,vertex.position.y,vertex.position.z,
        vertex.normal.x,vertex.normal.y,vertex.normal.z,vertex.color.r/255.0f,vertex.color.g/255.0f,
        vertex.color.b/255.0f,vertex.color.a/255.0f,vertex.uv.x,vertex.uv.y});
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    if (material.rdp.enabled && material.rdp.cull_mode) {
        glEnable(GL_CULL_FACE);
        glFrontFace(GL_CCW);
        glCullFace(material.rdp.cull_mode==0x600U ? GL_FRONT_AND_BACK :
                   material.rdp.cull_mode==0x200U ? GL_FRONT : GL_BACK);
    } else glDisable(GL_CULL_FACE);
    if (material.additive) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE);
        glDepthMask(GL_FALSE);
    } else if (material.translucent) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    } else {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
    glUseProgram(program_forward_);
    const auto uniform=[&](const char* name) { return glGetUniformLocation(program_forward_,name); };
    const auto& viewport=material.viewport;
    scissor_game(viewport[0],viewport[1],viewport[2],viewport[3]);
    glUniform2f(uniform("viewportScale"),viewport[2]/320.0f/(material.aspect*(4.0f/3.0f)),viewport[3]/240.0f);
    glUniform2f(uniform("viewportOffset"),(viewport[0]+viewport[2]*0.5f)/160.0f-1.0f,
                1.0f-(viewport[1]+viewport[3]*0.5f)/120.0f);
    glUniform1f(uniform("focal"),1.0f/std::tan(material.fov_y*0.008726646259971648f));
    glUniform1f(uniform("nearPlane"),material.near_plane);
    glUniform1f(uniform("farPlane"),material.far_plane);
    const GLuint texture_handle=raster_texture(material.texture);
    glUniform1i(uniform("colorTexture"),0);
    glUniform1i(uniform("shadowMap"),1);
    glUniform1i(uniform("useTexture"),texture_handle!=0);
    glUniform1i(uniform("useLighting"),material.lit);
    glUniform1i(uniform("rdpEnabled"),material.rdp.enabled);
    glUniform2ui(uniform("combineWords"),material.rdp.combine_hi,material.rdp.combine_lo);
    glUniform1i(uniform("combineCycles"),material.rdp.cycles);
    const auto rgba_uniform=[&](const char* name,Color c) {
        glUniform4f(uniform(name),c.r/255.0f,c.g/255.0f,c.b/255.0f,c.a/255.0f);
    };
    rgba_uniform("primitiveColor",material.rdp.primitive);
    rgba_uniform("environmentColor",material.rdp.environment);
    rgba_uniform("drawTint",material.rdp.tint);
    glUniform1f(uniform("alphaThreshold"),material.rdp.alpha_threshold);
    glUniform1i(uniform("alphaTest"),material.rdp.alpha_test);
    glUniform1i(uniform("textureGen"),material.rdp.texture_gen);
    glUniform1i(uniform("textureGenLinear"),material.rdp.texture_gen_linear);
    glUniform2f(uniform("generatedScale"),material.rdp.generated_scale.x,material.rdp.generated_scale.y);
    const auto diffuse=material.material_diffuse;
    const auto ambient=material.material_ambient;
    glUniform3f(uniform("n64Diffuse"),diffuse.r/255.0f,diffuse.g/255.0f,diffuse.b/255.0f);
    glUniform3f(uniform("n64Ambient"),ambient.r/255.0f,ambient.g/255.0f,ambient.b/255.0f);
    glUniform1i(uniform("translucent"),material.translucent);
    glUniform1i(uniform("shadowsReady"),shadows_ready_&&material.lit);
    glUniform2i(uniform("textureMode"),material.texture_mode_s,material.texture_mode_t);
    glUniform2i(uniform("textureMask"),material.texture_mask_s,material.texture_mask_t);
    glUniform2f(uniform("textureWindow"),std::max<unsigned>(material.texture_window_s,1),
                std::max<unsigned>(material.texture_window_t,1));
    color_uniform(uniform("ambientColor"),material.lights.ambient);
    glUniform1f(uniform("ambientIntensity"),material.lights.ambient_intensity);
    glUniform3f(uniform("keyDirection"),material.lights.key.direction.x,material.lights.key.direction.y,
                material.lights.key.direction.z);
    color_uniform(uniform("keyColor"),material.lights.key.color);
    glUniform1f(uniform("keyIntensity"),material.lights.key.intensity);
    glUniform1i(uniform("useSpotLight"),material.lights.spot.enabled);
    glUniform3f(uniform("spotPosition"),material.lights.spot.position.x,
                material.lights.spot.position.y,material.lights.spot.position.z);
    glUniform3f(uniform("spotDirection"),material.lights.spot.direction.x,
                material.lights.spot.direction.y,material.lights.spot.direction.z);
    color_uniform(uniform("spotColor"),material.lights.spot.color);
    glUniform1f(uniform("spotIntensity"),material.lights.spot.intensity);
    glUniform1f(uniform("spotRange"),material.lights.spot.range);
    glUniform1f(uniform("spotInnerCone"),material.lights.spot.inner_cone);
    glUniform1f(uniform("spotOuterCone"),material.lights.spot.outer_cone);
    glUniform3f(uniform("environmentUp"),material.lights.environment_up.x,
                material.lights.environment_up.y,material.lights.environment_up.z);
    color_uniform(uniform("reflectionColor"),material.lights.reflection);
    glUniform1f(uniform("reflectionIntensity"),material.lights.reflection_intensity);
    glUniform1f(uniform("shininess"),material.lights.shininess);
    color_uniform(uniform("materialDiffuse"),material.material_diffuse);
    color_uniform(uniform("materialAmbient"),material.material_ambient);
    glUniform3f(uniform("shadowRight"),shadow_right_.x,shadow_right_.y,shadow_right_.z);
    glUniform3f(uniform("shadowUp"),shadow_up_.x,shadow_up_.y,shadow_up_.z);
    glUniform3f(uniform("shadowForward"),shadow_forward_.x,shadow_forward_.y,shadow_forward_.z);
    glUniform3f(uniform("shadowMinimum"),shadow_min_.x,shadow_min_.y,shadow_min_.z);
    glUniform3f(uniform("shadowMaximum"),shadow_max_.x,shadow_max_.y,shadow_max_.z);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,texture_handle);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D,shadow_texture_);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao_forward_);
    glBindBuffer(GL_ARRAY_BUFFER,vbo_forward_);
    glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(data.size()*sizeof(VertexForward)),data.data(),GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(data.size()));
    glDisable(GL_CULL_FACE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    reset_scissor();
}

void RenderEngine::clear_depth() {
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    shadows_ready_=false;
}

void RenderEngine::scissor_game(float x, float y, float w, float h) {
    const float sx=viewport_width_/320.0f;
    const float sy=viewport_height_/240.0f;
    glEnable(GL_SCISSOR_TEST);
    glScissor(viewport_x_+static_cast<int>(std::lround(x*sx)),
              viewport_y_+static_cast<int>(std::lround((240.0f-y-h)*sy)),
              std::max(1,static_cast<int>(std::lround(w*sx))),
              std::max(1,static_cast<int>(std::lround(h*sy))));
}

void RenderEngine::reset_scissor() {
    glEnable(GL_SCISSOR_TEST);
    glScissor(viewport_x_,viewport_y_,viewport_width_,viewport_height_);
}

void RenderEngine::request_capture(std::filesystem::path path) { capture_path_=std::move(path); }

void RenderEngine::end() {
    glDepthMask(GL_TRUE);
    if (!capture_path_.empty()) {
        int width{},height{};
        SDL_GetWindowSizeInPixels(window_,&width,&height);
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width)*height*4);
        std::vector<std::uint8_t> top_down(pixels.size());
        glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        for (int y=0;y<height;++y)
            std::copy_n(pixels.data()+static_cast<std::size_t>(height-1-y)*width*4,
                        static_cast<std::size_t>(width)*4,top_down.data()+static_cast<std::size_t>(y)*width*4);
        png_image image{};
        image.version=PNG_IMAGE_VERSION;
        image.width=static_cast<png_uint_32>(width);
        image.height=static_cast<png_uint_32>(height);
        image.format=PNG_FORMAT_RGBA;
        const auto filename=capture_path_.string();
        if (!png_image_write_to_file(&image,filename.c_str(),0,top_down.data(),width*4,nullptr))
            throw std::runtime_error("PNG capture write failed: "+filename);
        capture_path_.clear();
    }
    SDL_GL_SwapWindow(window_);
}

} // namespace sagas
