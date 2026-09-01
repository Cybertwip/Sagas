#include <sagas/Scene3D.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace sagas {
namespace {

struct Matrix {
    std::array<float,16> m{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
};
Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix out{{}};
    for (int row=0; row<4; ++row) for (int col=0; col<4; ++col)
        for (int k=0; k<4; ++k) out.m[row*4+col] += a.m[row*4+k] * b.m[k*4+col];
    return out;
}
Matrix translation(const std::array<float,3>& v) {
    Matrix out; out.m[3]=v[0]; out.m[7]=v[1]; out.m[11]=v[2]; return out;
}
Matrix scale(const std::array<float,3>& v) {
    Matrix out; out.m[0]=v[0]; out.m[5]=v[1]; out.m[10]=v[2]; return out;
}
Matrix rotation(const std::array<float,3>& v) {
    const float cx=std::cos(v[0]), sx=std::sin(v[0]), cy=std::cos(v[1]), sy=std::sin(v[1]), cz=std::cos(v[2]), sz=std::sin(v[2]);
    Matrix x; x.m={1,0,0,0, 0,cx,-sx,0, 0,sx,cx,0, 0,0,0,1};
    Matrix y; y.m={cy,0,sy,0, 0,1,0,0, -sy,0,cy,0, 0,0,0,1};
    Matrix z; z.m={cz,-sz,0,0, sz,cz,0,0, 0,0,1,0, 0,0,0,1};
    return multiply(multiply(z,y),x);
}
Vec3 transform(const Matrix& m, Vec3 v) {
    return {m.m[0]*v.x+m.m[1]*v.y+m.m[2]*v.z+m.m[3],
            m.m[4]*v.x+m.m[5]*v.y+m.m[6]*v.z+m.m[7],
            m.m[8]*v.x+m.m[9]*v.y+m.m[10]*v.z+m.m[11]};
}
Vec3 transform_direction(const Matrix& m, Vec3 v) {
    return {m.m[0]*v.x+m.m[1]*v.y+m.m[2]*v.z,
            m.m[4]*v.x+m.m[5]*v.y+m.m[6]*v.z,
            m.m[8]*v.x+m.m[9]*v.y+m.m[10]*v.z};
}
Vec3 sub(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
float dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
Vec3 normalize(Vec3 v) {
    const float length=std::sqrt(std::max(1e-12f,dot(v,v))); return {v.x/length,v.y/length,v.z/length};
}
Color modulate(Color a, Color b) {
    return {static_cast<std::uint8_t>(a.r*b.r/255), static_cast<std::uint8_t>(a.g*b.g/255),
            static_cast<std::uint8_t>(a.b*b.b/255), static_cast<std::uint8_t>(a.a*b.a/255)};
}

} // namespace

Model3D Scene3DLoader::model(std::string_view descriptor, std::string_view animation, GeometryLayout layout) {
    const auto desc = archive_.symbol(descriptor);
    if (!desc) throw std::runtime_error("missing model descriptor symbol: " + std::string(descriptor));
    Model3D model;
    model.nodes = n64::SkeletonDecoder(archive_).decode(*desc);
    model.meshes.resize(model.nodes.size());
    n64::DisplayListDecoder decoder(archive_);
    for (std::size_t i=0; i<model.nodes.size(); ++i) if (model.nodes[i].display_list)
        model.meshes[i] = layout == GeometryLayout::DisplayListLinks
            ? decoder.decode_links(*model.nodes[i].display_list) : decoder.decode(*model.nodes[i].display_list);
    if (!animation.empty()) {
        const auto symbol = archive_.symbol(animation);
        if (!symbol) throw std::runtime_error("missing animation symbol: " + std::string(animation));
        model.animation = n64::AnimationDecoder(archive_).table(*symbol, model.nodes.size());
    } else model.animation.resize(model.nodes.size());
    return model;
}

Model3D Scene3DLoader::display_list(std::string_view symbol, GeometryLayout layout) {
    const auto address = archive_.symbol(symbol);
    if (!address) throw std::runtime_error("missing display-list symbol: " + std::string(symbol));
    Model3D model;
    model.nodes.push_back({0,0,{}, {0,0,0},{0,0,0},{1,1,1}, address});
    n64::DisplayListDecoder decoder(archive_);
    model.meshes.push_back(layout == GeometryLayout::DisplayListLinks
        ? decoder.decode_links(*address) : decoder.decode(*address));
    model.animation.resize(1);
    return model;
}

Camera3D Scene3DLoader::camera(std::string_view animation, float frame, Camera3D initial_camera) {
    const auto script = archive_.symbol(animation);
    if (!script) throw std::runtime_error("missing camera animation symbol: " + std::string(animation));
    n64::JointPose initial;
    initial.tracks = {initial_camera.eye.x, initial_camera.eye.y, initial_camera.eye.z, 0,
                      initial_camera.at.x, initial_camera.at.y, initial_camera.at.z,
                      initial_camera.up.x, initial_camera.up.z, initial_camera.fov_y};
    const auto pose = n64::AnimationDecoder(archive_).sample(*script, frame, initial);
    Camera3D result;
    result.eye={pose.tracks[0],pose.tracks[1],pose.tracks[2]};
    result.at={pose.tracks[4],pose.tracks[5],pose.tracks[6]};
    result.up={pose.tracks[8],1,0};
    result.near_plane=initial_camera.near_plane;
    result.far_plane=initial_camera.far_plane;
    if (std::isfinite(pose.tracks[9]) && pose.tracks[9] > 1 && pose.tracks[9] < 179) result.fov_y=pose.tracks[9];
    return result;
}

void Scene3DRenderer::draw(RenderEngine& render, const Model3D& model, const Camera3D& camera,
                           float frame, Color tint, LightingRig lights) {
    struct Projected { TriangleVertex vertex; float depth; };
    struct RenderTriangle {
        std::array<Projected,3> points;
        std::shared_ptr<const RasterImage> texture;
    };
    std::vector<RenderTriangle> triangles;
    std::array<Matrix,18> parents{};
    const Vec3 forward=normalize(sub(camera.at,camera.eye));
    const Vec3 right=normalize(cross(forward,camera.up));
    const Vec3 up=cross(right,forward);
    const float focal=1.0f/std::tan(camera.fov_y*0.008726646259971648f);
    const Matrix model_matrix=multiply(multiply(translation({model.position.x,model.position.y,model.position.z}),
                                                rotation({model.rotation.x,model.rotation.y,model.rotation.z})),
                                       scale({model.scale.x,model.scale.y,model.scale.z}));
    for (std::size_t node_index=0; node_index<model.nodes.size(); ++node_index) {
        auto node=model.nodes[node_index];
        if (node_index < model.animation.size() && model.animation[node_index])
            n64::AnimationDecoder::apply(node, animation_.sample(*model.animation[node_index],frame,animation_.pose(node)));
        const Matrix local=multiply(multiply(translation(node.translate),rotation(node.rotate)),scale(node.scale));
        Matrix world=multiply(model_matrix,local);
        if (node.depth>0 && node.depth<=18) world=multiply(parents[node.depth-1],local);
        if (node.depth>=0 && node.depth<18) parents[node.depth]=world;
        const auto& mesh=model.meshes[node_index];
        for (std::size_t i=0;i+2<mesh.vertices.size();i+=3) {
            std::array<Projected,3> triangle{};
            bool visible=true;
            for (int j=0;j<3;++j) {
                const auto& source=mesh.vertices[i+j];
                const Vec3 point=transform(world,{source.x,source.y,source.z});
                const Vec3 relative=sub(point,camera.eye);
                const float depth=dot(relative,forward);
                if (depth<camera.near_plane || depth>camera.far_plane) visible=false;
                Color color=modulate(source.color,tint);
                if (source.lit) color=LightingSystem::shade(color,transform_direction(world,source.normal),lights);
                triangle[j]={{{160+dot(relative,right)*focal*150/depth,
                                120-dot(relative,up)*focal*150/depth},color,{source.u,source.v}},depth};
            }
            if (visible) triangles.push_back({triangle,mesh.vertices[i].texture});
        }
    }
    std::stable_sort(triangles.begin(),triangles.end(),[](const auto& a,const auto& b){
        return a.points[0].depth+a.points[1].depth+a.points[2].depth>
               b.points[0].depth+b.points[1].depth+b.points[2].depth;
    });
    std::vector<TriangleVertex> output;
    std::shared_ptr<const RasterImage> texture;
    const auto flush = [&] {
        render.triangles(output,texture);
        output.clear();
    };
    for (const auto& triangle:triangles) {
        if (!output.empty() && triangle.texture != texture) flush();
        texture=triangle.texture;
        for (const auto& point:triangle.points) output.push_back(point.vertex);
    }
    flush();
}

} // namespace sagas
