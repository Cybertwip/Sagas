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
Vec3 transform_normal(const Matrix& m, Vec3 v) {
    // Normals transform by inverse-transpose.  Using the model matrix
    // directly is only valid for rigid/uniform transforms and caused the
    // animated hand/fighter highlights to shear or flip as joints scaled.
    const float a=m.m[0], b=m.m[1], c=m.m[2];
    const float d=m.m[4], e=m.m[5], f=m.m[6];
    const float g=m.m[8], h=m.m[9], i=m.m[10];
    const float determinant=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    if (std::abs(determinant)<1.0e-8f) return transform_direction(m,v);
    const float inverse=1.0f/determinant;
    return {
        ((e*i-f*h)*v.x+(f*g-d*i)*v.y+(d*h-e*g)*v.z)*inverse,
        ((c*h-b*i)*v.x+(a*i-c*g)*v.y+(b*g-a*h)*v.z)*inverse,
        ((b*f-c*e)*v.x+(c*d-a*f)*v.y+(a*e-b*d)*v.z)*inverse
    };
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

std::vector<std::vector<std::optional<n64::Address>>> material_animation_table(
    n64::RelocArchive& archive,n64::Address table,
    const std::vector<std::vector<n64::Material>>& materials) {
    std::vector<std::vector<std::optional<n64::Address>>> result(materials.size());
    for (std::size_t node=0;node<materials.size();++node) {
        result[node].resize(materials[node].size());
        const n64::Address node_slot{table.file,table.offset+static_cast<std::uint32_t>(node*4)};
        if (archive.u32(node_slot)==0) continue;
        const auto scripts=archive.resolve(node_slot);
        if (!scripts) continue;
        for (std::size_t material=0;material<materials[node].size();++material) {
            const n64::Address script_slot{scripts->file,scripts->offset+static_cast<std::uint32_t>(material*4)};
            if (archive.u32(script_slot)!=0) result[node][material]=archive.resolve(script_slot);
        }
    }
    return result;
}

std::vector<Matrix> world_matrices(n64::AnimationDecoder& animation, const Model3D& model, float frame) {
    std::array<Matrix,18> parents{};
    std::vector<Matrix> result;
    result.reserve(model.nodes.size());
    Matrix model_matrix=model.root_transform ? Matrix{*model.root_transform}
        : multiply(multiply(translation({model.position.x,model.position.y,model.position.z}),
                            rotation({model.rotation.x,model.rotation.y,model.rotation.z})),
                   scale({model.scale.x,model.scale.y,model.scale.z}));
    if (!model.root_transform && model.fighter_root_animation) {
        auto root=model.fighter_root;
        n64::AnimationDecoder::apply(root,animation.sample16(*model.fighter_root_animation,frame,
                                                              animation.pose(root)));
        model_matrix=multiply(model_matrix,multiply(multiply(translation(root.translate),rotation(root.rotate)),
                                                    scale(root.scale)));
    }
    for (std::size_t node_index=0; node_index<model.nodes.size(); ++node_index) {
        auto node=model.nodes[node_index];
        if (node_index < model.animation.size() && model.animation[node_index])
            n64::AnimationDecoder::apply(node, model.fighter_animation
                ? animation.sample16(*model.animation[node_index],frame,animation.pose(node))
                : animation.sample(*model.animation[node_index],frame,animation.pose(node)));
        const Matrix local=multiply(multiply(translation(node.translate),rotation(node.rotate)),scale(node.scale));
        Matrix world=multiply(model_matrix,local);
        if (node.depth>0 && node.depth<=18) world=multiply(parents[node.depth-1],local);
        if (node.depth>=0 && node.depth<18) parents[node.depth]=world;
        result.push_back(world);
    }
    return result;
}

} // namespace

Model3D Scene3DLoader::model(std::string_view descriptor, std::string_view animation,
                            GeometryLayout layout, std::string_view material_symbol,
                            std::string_view material_animation_symbol) {
    const auto desc = archive_.symbol(descriptor);
    if (!desc) throw std::runtime_error("missing model descriptor symbol: " + std::string(descriptor));
    Model3D model;
    model.nodes = n64::SkeletonDecoder(archive_).decode(*desc);
    model.meshes.resize(model.nodes.size());
    model.parent_meshes.resize(model.nodes.size());
    n64::DisplayListDecoder decoder(archive_);
    std::vector<std::vector<n64::Material>> materials(model.nodes.size());
    if (!material_symbol.empty()) {
        const auto table=archive_.symbol(material_symbol);
        if (!table) throw std::runtime_error("missing material symbol: " + std::string(material_symbol));
        materials=decoder.materials(*table,model.nodes.size());
    }
    model.materials=materials;
    model.material_animation.resize(model.nodes.size());
    if (!material_animation_symbol.empty()) {
        const auto table=archive_.symbol(material_animation_symbol);
        if (!table) throw std::runtime_error("missing material animation symbol: "+
                                             std::string(material_animation_symbol));
        model.material_animation=material_animation_table(archive_,*table,model.materials);
    }
    for (std::size_t i=0; i<model.nodes.size(); ++i) if (model.nodes[i].display_list) {
        if (layout == GeometryLayout::JointPairs) {
            const auto pair=*model.nodes[i].display_list;
            if (const auto parent=archive_.resolve(pair))
                model.parent_meshes[i]=decoder.decode(*parent,materials[i]);
            if (const auto local=archive_.resolve({pair.file,pair.offset+4}))
                model.meshes[i]=decoder.decode(*local,materials[i]);
        }
        else if (layout == GeometryLayout::DisplayListLinks)
            model.meshes[i] = decoder.decode_links(*model.nodes[i].display_list,materials[i]);
        else
            model.meshes[i] = decoder.decode(*model.nodes[i].display_list,materials[i]);
    }
    if (!animation.empty()) {
        const auto symbol = archive_.symbol(animation);
        if (!symbol) throw std::runtime_error("missing animation symbol: " + std::string(animation));
        model.animation = n64::AnimationDecoder(archive_).table(*symbol, model.nodes.size());
    } else model.animation.resize(model.nodes.size());
    return model;
}

Model3D Scene3DLoader::fighter_model(std::string_view descriptor, GeometryLayout layout) {
    const auto desc=archive_.symbol(descriptor);
    if (!desc) throw std::runtime_error("missing fighter descriptor symbol: "+std::string(descriptor));
    Model3D model;
    model.nodes=n64::SkeletonDecoder(archive_).decode(*desc);
    model.meshes.resize(model.nodes.size());
    model.parent_meshes.resize(model.nodes.size());
    model.animation.resize(model.nodes.size());
    n64::DisplayListDecoder decoder(archive_);
    const auto materials=decoder.materials({desc->file,0},model.nodes.size());
    model.materials=materials;
    model.material_animation.resize(model.nodes.size());
    for (std::size_t i=0;i<model.nodes.size();++i) if (model.nodes[i].display_list) {
        if (layout==GeometryLayout::JointPairs) {
            const auto pair=*model.nodes[i].display_list;
            if (const auto parent=archive_.resolve(pair))
                model.parent_meshes[i]=decoder.decode(*parent,materials[i]);
            if (const auto local=archive_.resolve({pair.file,pair.offset+4}))
                model.meshes[i]=decoder.decode(*local,materials[i]);
        }
        else if (layout==GeometryLayout::DisplayListLinks)
            model.meshes[i]=decoder.decode_links(*model.nodes[i].display_list,materials[i]);
        else model.meshes[i]=decoder.decode(*model.nodes[i].display_list,materials[i]);
    }
    return model;
}

Model3D Scene3DLoader::display_list(std::string_view symbol, GeometryLayout layout,
                                   std::string_view material_symbol,
                                   std::string_view material_animation_symbol) {
    const auto address = archive_.symbol(symbol);
    if (!address) throw std::runtime_error("missing display-list symbol: " + std::string(symbol));
    Model3D model;
    model.nodes.push_back({0,0,{}, {0,0,0},{0,0,0},{1,1,1}, address});
    model.parent_meshes.resize(1);
    n64::DisplayListDecoder decoder(archive_);
    std::vector<n64::Material> materials;
    if (!material_symbol.empty()) {
        const auto table=archive_.symbol(material_symbol);
        if (!table) throw std::runtime_error("missing material symbol: "+std::string(material_symbol));
        auto decoded=decoder.materials(*table,1);
        materials=std::move(decoded.front());
    }
    model.materials.push_back(materials);
    model.material_animation.resize(1);
    if (!material_animation_symbol.empty()) {
        const auto table=archive_.symbol(material_animation_symbol);
        if (!table) throw std::runtime_error("missing material animation symbol: "+
                                             std::string(material_animation_symbol));
        model.material_animation=material_animation_table(archive_,*table,model.materials);
    }
    if (layout == GeometryLayout::JointPairs) {
        if (const auto parent=archive_.resolve(*address))
            model.parent_meshes[0]=decoder.decode(*parent,materials);
        if (const auto local=archive_.resolve({address->file,address->offset+4}))
            model.meshes.push_back(decoder.decode(*local,materials));
        else model.meshes.emplace_back();
    }
    else if (layout == GeometryLayout::DisplayListLinks)
        model.meshes.push_back(decoder.decode_links(*address,materials));
    else
        model.meshes.push_back(decoder.decode(*address,materials));
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

void Scene3DRenderer::begin() {
    triangles_.clear();
    batching_ = true;
}

Model3D Scene3DRenderer::placed_at_joint(const Model3D& model, float model_frame,
                                         const Model3D& carrier, float carrier_frame,
                                         std::size_t carrier_joint) {
    Model3D placed=model;
    const auto carrier_matrices=world_matrices(animation_,carrier,carrier_frame);
    if (carrier_joint>=carrier_matrices.size() || model.nodes.empty()) return placed;
    auto first_child=model.fighter_root;
    if (model.fighter_root_animation)
        n64::AnimationDecoder::apply(first_child,animation_.sample16(
            *model.fighter_root_animation,model_frame,animation_.pose(first_child)));
    const Matrix attachment=multiply(carrier_matrices[carrier_joint],
                                     translation({-first_child.translate[0],-first_child.translate[1],
                                                  -first_child.translate[2]}));
    // The original helper subtracts TopN's first child (TransN), then copies
    // the holding joint's world position/orientation into TopN.  The common
    // model descriptor starts below that wrapper and must not be used as the
    // attachment offset.
    Matrix root;
    for (int column=0;column<3;++column) {
        Vec3 axis{carrier_matrices[carrier_joint].m[column],
                  carrier_matrices[carrier_joint].m[4+column],
                  carrier_matrices[carrier_joint].m[8+column]};
        axis=normalize(axis);
        root.m[column]=axis.x;
        root.m[4+column]=axis.y;
        root.m[8+column]=axis.z;
    }
    root.m[3]=attachment.m[3];
    root.m[7]=attachment.m[7];
    root.m[11]=attachment.m[11];
    placed.root_transform=root.m;
    return placed;
}

void Scene3DRenderer::draw(RenderEngine& render, const Model3D& model, const Camera3D& camera,
                           float frame, Color tint, LightingRig lights) {
    const bool immediate = !batching_;
    const Vec3 forward=normalize(sub(camera.at,camera.eye));
    const Vec3 right=normalize(cross(forward,camera.up));
    const Vec3 up=cross(right,forward);
    // All lighting is evaluated in camera space by the forward shader.
    // Transform the directional light once per model rather than once per
    // fragment, while preserving its surface-to-light convention.
    const Vec3 key=normalize(lights.key.direction);
    lights.key.direction=normalize({dot(key,right),dot(key,up),dot(key,forward)});
    const Vec3 environment_up=normalize(lights.environment_up);
    lights.environment_up=normalize({dot(environment_up,right),dot(environment_up,up),
                                     dot(environment_up,forward)});
    if (lights.spot.enabled) {
        const Vec3 relative=sub(lights.spot.position,camera.eye);
        lights.spot.position={dot(relative,right),dot(relative,up),dot(relative,forward)};
        const Vec3 direction=normalize(lights.spot.direction);
        lights.spot.direction=normalize({dot(direction,right),dot(direction,up),dot(direction,forward)});
    }
    const auto matrices=world_matrices(animation_,model,frame);
    std::vector<std::uint16_t> runtime_flags(model.nodes.size());
    for (std::size_t node=0;node<model.nodes.size();++node) {
        if (node<model.animation.size()&&model.animation[node]) {
            const auto pose=model.fighter_animation
                ? animation_.sample16(*model.animation[node],frame,animation_.pose(model.nodes[node]))
                : animation_.sample(*model.animation[node],frame,animation_.pose(model.nodes[node]));
            runtime_flags[node]=pose.flags;
        }
    }
    std::array<std::size_t,18> latest_at_depth{};
    int hidden_depth=-1;
    for (std::size_t node_index=0; node_index<model.nodes.size(); ++node_index) {
        const int node_depth=model.nodes[node_index].depth;
        if (hidden_depth>=0&&node_depth<=hidden_depth) hidden_depth=-1;
        if (hidden_depth>=0) continue;
        if ((runtime_flags[node_index]&2U)!=0) {
            hidden_depth=node_depth;
            continue;
        }
        const Matrix& world=matrices[node_index];
        std::vector<n64::MaterialPose> material_poses;
        if (node_index<model.materials.size()) {
            material_poses.resize(model.materials[node_index].size());
            for (std::size_t material=0;material<material_poses.size();++material) {
                const auto& source=model.materials[node_index][material];
                auto& pose=material_poses[material];
                pose.colors[0]=source.primitive;
                if (source.light1) pose.colors[3]=*source.light1;
                if (source.light2) pose.colors[4]=*source.light2;
                if (node_index<model.material_animation.size()&&
                    material<model.material_animation[node_index].size()&&
                    model.material_animation[node_index][material])
                    pose=animation_.sample_material(*model.material_animation[node_index][material],
                                                    std::max(frame-model.material_animation_start,0.0f),pose);
            }
        }
        const auto render_mesh=[&](const n64::Mesh& mesh,const Matrix& mesh_world) {
        for (std::size_t i=0;i+2<mesh.vertices.size();i+=3) {
            std::array<Vec3,3> world_points{};
            for (int j=0;j<3;++j) {
                const auto& source=mesh.vertices[i+j];
                world_points[j]=transform(mesh_world,{source.x,source.y,source.z});
            }
            const float edge0=std::sqrt(std::max(1.0e-12f,dot(sub(world_points[1],world_points[0]),
                                                              sub(world_points[1],world_points[0]))));
            const float edge1=std::sqrt(std::max(1.0e-12f,dot(sub(world_points[2],world_points[1]),
                                                              sub(world_points[2],world_points[1]))));
            const float edge2=std::sqrt(std::max(1.0e-12f,dot(sub(world_points[0],world_points[2]),
                                                              sub(world_points[0],world_points[2]))));
            // Skinned fighter joints that lost their parent matrix produce a
            // handful of room-sized triangles with the wrong vertex colors.
            // Those do not belong in the intro camera.
            if (model.fighter_animation && std::max({edge0,edge1,edge2})>4000.0f) continue;
            Vec3 face_normal=normalize(cross(sub(world_points[1],world_points[0]),
                                             sub(world_points[2],world_points[0])));
            std::array<ProjectedVertex,3> triangle{};
            for (int j=0;j<3;++j) {
                const auto& source=mesh.vertices[i+j];
                const Vec3 point=world_points[j];
                const Vec3 relative=sub(point,camera.eye);
                const bool valid_source_normal=dot(source.normal,source.normal)>1.0e-6f;
                const Vec3 world_normal=source.lit&&valid_source_normal
                    ? normalize(transform_normal(mesh_world,source.normal)) : face_normal;
                Color surface=source.color;
                if (source.material_index<material_poses.size()) {
                    const bool animated=node_index<model.material_animation.size()&&
                        source.material_index<model.material_animation[node_index].size()&&
                        model.material_animation[node_index][source.material_index].has_value();
                    if (animated) surface=source.lit ? material_poses[source.material_index].colors[0]
                                                    : modulate(surface,material_poses[source.material_index].colors[0]);
                }
                triangle[j]={{dot(relative,right),dot(relative,up),dot(relative,forward)},
                             modulate(surface,tint),{source.u,source.v},
                             normalize({dot(world_normal,right),dot(world_normal,up),dot(world_normal,forward)})};
            }
            const auto& sampler=mesh.vertices[i];
            auto material_light1=sampler.light1;
            auto material_light2=sampler.light2;
            bool animated_translucency{};
            if (sampler.material_index<material_poses.size()) {
                material_light1=material_poses[sampler.material_index].colors[3];
                material_light2=material_poses[sampler.material_index].colors[4];
                animated_translucency=material_poses[sampler.material_index].colors[0].a<255;
            }
            triangles_.push_back({triangle,sampler.texture,sampler.texture_mode_s,sampler.texture_mode_t,
                                  sampler.texture_mask_s,sampler.texture_mask_t,
                                  sampler.texture_window_s,sampler.texture_window_t,lights,
                                  material_light1,material_light2,camera.fov_y,camera.near_plane,camera.far_plane,
                                  sampler.lit||model.receive_lighting,
                                  sampler.translucent||tint.a<255||animated_translucency});
        }
        };
        if ((runtime_flags[node_index]&1U)==0&&node_index<model.parent_meshes.size() &&
            !model.parent_meshes[node_index].vertices.empty()) {
            const auto depth=model.nodes[node_index].depth;
            const Matrix& parent_world=(depth>0 && depth<=18)
                ? matrices[latest_at_depth[static_cast<std::size_t>(depth-1)]] : world;
            render_mesh(model.parent_meshes[node_index],parent_world);
        }
        if ((runtime_flags[node_index]&1U)==0) render_mesh(model.meshes[node_index],world);
        const auto depth=model.nodes[node_index].depth;
        if (depth>=0 && depth<18) latest_at_depth[static_cast<std::size_t>(depth)]=node_index;
    }
    if (immediate) flush(render);
}

void Scene3DRenderer::flush(RenderEngine& render) {
    if (triangles_.empty()) return;
    std::stable_sort(triangles_.begin(),triangles_.end(),[](const auto& a,const auto& b) {
        if (a.translucent!=b.translucent) return !a.translucent;
        if (!a.translucent) return false;
        const float az=a.points[0].position.z+a.points[1].position.z+a.points[2].position.z;
        const float bz=b.points[0].position.z+b.points[1].position.z+b.points[2].position.z;
        return az>bz;
    });

    const auto convert=[](const ProjectedVertex& vertex) {
        return ForwardVertex{vertex.position,vertex.normal,vertex.color,vertex.uv};
    };
    std::vector<ForwardVertex> shadow_geometry;
    shadow_geometry.reserve(triangles_.size()*3);
    for (const auto& triangle:triangles_) if (!triangle.translucent)
        for (const auto& point:triangle.points) shadow_geometry.push_back(convert(point));
    render.prepare_forward_shadows(shadow_geometry,triangles_.front().lights.key.direction);

    const auto make_material=[](const ProjectedTriangle& triangle) {
        ForwardMaterial material;
        material.texture=triangle.texture;
        material.lights=triangle.lights;
        // MObj light1/light2 are the part's local Lights1 (diffuse/ambient),
        // not a replacement for the scene rig. Overwriting the key color
        // with those bytes is what tinted fighter meshes strange colors.
        if (triangle.material_light1) material.material_diffuse=*triangle.material_light1;
        if (triangle.material_light2) material.material_ambient=*triangle.material_light2;
        material.fov_y=triangle.fov_y;
        material.near_plane=triangle.near_plane;
        material.far_plane=triangle.far_plane;
        material.texture_mode_s=triangle.texture_mode_s;
        material.texture_mode_t=triangle.texture_mode_t;
        material.texture_mask_s=triangle.texture_mask_s;
        material.texture_mask_t=triangle.texture_mask_t;
        material.texture_window_s=triangle.texture_window_s;
        material.texture_window_t=triangle.texture_window_t;
        material.lit=triangle.lit;
        material.translucent=triangle.translucent;
        return material;
    };
    const auto same_color=[](Color a,Color b) {
        return a.r==b.r&&a.g==b.g&&a.b==b.b&&a.a==b.a;
    };
    const auto same_material=[&](const ForwardMaterial& a,const ForwardMaterial& b) {
        return a.texture.get()==b.texture.get() && a.fov_y==b.fov_y &&
            a.near_plane==b.near_plane && a.far_plane==b.far_plane &&
            a.texture_mode_s==b.texture_mode_s && a.texture_mode_t==b.texture_mode_t &&
            a.texture_mask_s==b.texture_mask_s && a.texture_mask_t==b.texture_mask_t &&
            a.texture_window_s==b.texture_window_s && a.texture_window_t==b.texture_window_t &&
            a.lit==b.lit && a.translucent==b.translucent &&
            same_color(a.lights.ambient,b.lights.ambient) &&
            same_color(a.lights.key.color,b.lights.key.color) &&
            same_color(a.lights.reflection,b.lights.reflection) &&
            a.lights.ambient_intensity==b.lights.ambient_intensity &&
            a.lights.key.intensity==b.lights.key.intensity &&
            a.lights.key.direction.x==b.lights.key.direction.x &&
            a.lights.key.direction.y==b.lights.key.direction.y &&
            a.lights.key.direction.z==b.lights.key.direction.z &&
            a.lights.spot.enabled==b.lights.spot.enabled &&
            a.lights.spot.position.x==b.lights.spot.position.x &&
            a.lights.spot.position.y==b.lights.spot.position.y &&
            a.lights.spot.position.z==b.lights.spot.position.z &&
            a.lights.spot.direction.x==b.lights.spot.direction.x &&
            a.lights.spot.direction.y==b.lights.spot.direction.y &&
            a.lights.spot.direction.z==b.lights.spot.direction.z &&
            same_color(a.lights.spot.color,b.lights.spot.color) &&
            a.lights.spot.intensity==b.lights.spot.intensity &&
            a.lights.spot.range==b.lights.spot.range &&
            a.lights.spot.inner_cone==b.lights.spot.inner_cone &&
            a.lights.spot.outer_cone==b.lights.spot.outer_cone &&
            a.lights.environment_up.x==b.lights.environment_up.x &&
            a.lights.environment_up.y==b.lights.environment_up.y &&
            a.lights.environment_up.z==b.lights.environment_up.z &&
            a.lights.reflection_intensity==b.lights.reflection_intensity &&
            a.lights.shininess==b.lights.shininess;
    };
    std::vector<ForwardVertex> batch;
    ForwardMaterial material{};
    bool have_material{};
    const auto submit=[&]() {
        if (!batch.empty()) render.forward(batch,material);
        batch.clear();
    };
    for (const auto& triangle:triangles_) {
        const auto next=make_material(triangle);
        if (have_material&&!same_material(material,next)) submit();
        material=next;
        have_material=true;
        for (const auto& point:triangle.points) batch.push_back(convert(point));
    }
    submit();
    triangles_.clear();
}

void Scene3DRenderer::end(RenderEngine& render) {
    flush(render);
    batching_=false;
}

} // namespace sagas
