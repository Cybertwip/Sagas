#include <sagas/Scene3D.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <sstream>
#include <map>

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
            if (archive.u32(script_slot)==0) break; // Material animation chains are null-terminated.
            result[node][material]=archive.resolve(script_slot);
        }
    }
    return result;
}

struct ModelMatrices {
    std::vector<Matrix> world;
    std::vector<Matrix> parent;
};

ModelMatrices world_matrices(n64::AnimationDecoder& animation, const Model3D& model, float frame) {
    constexpr int kMaxDepth = 40;
    std::array<Matrix,kMaxDepth> parents{};
    std::array<char,kMaxDepth> have_parent{};
    ModelMatrices result;
    result.world.reserve(model.nodes.size());
    result.parent.reserve(model.nodes.size());
    Matrix model_matrix=model.root_transform ? Matrix{*model.root_transform}
        : multiply(multiply(translation({model.position.x,model.position.y,model.position.z}),
                            rotation({model.rotation.x,model.rotation.y,model.rotation.z})),
                   scale({model.scale.x,model.scale.y,model.scale.z}));
    if (model.fighter_root_animation) {
        auto root=model.fighter_root;
        n64::AnimationDecoder::apply(root,animation.sample16(*model.fighter_root_animation,frame,
                                                              animation.pose(root)));
        if (model.fighter_wrapper==Model3D::FighterWrapper::TransN) {
            // TransN is detached from the rendered hierarchy. The opening
            // callback accumulates its frame-to-frame translation in TopN.
            // Telescoping those deltas permits deterministic frame seeking.
            const auto start=animation.sample16(*model.fighter_root_animation,0,
                                                animation.pose(model.fighter_root));
            for (int axis=0;axis<3;++axis) root.translate[axis]-=start.tracks[4+axis];
            model_matrix=multiply(model_matrix,translation(root.translate));
        } else {
            model_matrix=multiply(model_matrix,multiply(multiply(translation(root.translate),rotation(root.rotate)),
                                                        scale(root.scale)));
        }
    }
    for (std::size_t node_index=0; node_index<model.nodes.size(); ++node_index) {
        auto node=model.nodes[node_index];
        if (node_index < model.animation.size() && model.animation[node_index])
            n64::AnimationDecoder::apply(node, model.fighter_animation
                ? animation.sample16(*model.animation[node_index],frame,animation.pose(node))
                : animation.sample(*model.animation[node_index],frame,animation.pose(node)));
        if (model.joint4_rotation_x && node_index<model.source_joint_ids.size() && model.source_joint_ids[node_index]==4)
            node.rotate[0]=*model.joint4_rotation_x;
        if (node_index<model.imported_rest_offsets.size())
            for (unsigned axis=0;axis<3;++axis) node.translate[axis]+=model.imported_rest_offsets[node_index][axis];
        const Matrix local=multiply(multiply(translation(node.translate),rotation(node.rotate)),scale(node.scale));
        // A sibling ends the previous branch. Disabled fighter descriptors
        // can leave depth gaps; they must not pick up a cousin's old matrix.
        for (int depth=std::max(node.depth,0);depth<kMaxDepth;++depth)
            have_parent[static_cast<std::size_t>(depth)]=false;
        Matrix parent=model_matrix;
        // Fighter DObjDesc trees often start at depth 4 (below TopN/TransN).
        // Parenting those to an unset identity slot threw fingers and held
        // fighters into world origin — the window in the opening room.
        if (node.depth>0 && node.depth<=kMaxDepth) {
            int ancestor=node.depth-1;
            while (ancestor>0 && !have_parent[static_cast<std::size_t>(ancestor)]) --ancestor;
            if (have_parent[static_cast<std::size_t>(ancestor)])
                parent=parents[static_cast<std::size_t>(ancestor)];
        }
        if (node_index<model.source_parent_ids.size() && model.source_parent_ids[node_index]>=0) {
            const auto found=std::find(model.source_joint_ids.begin(),model.source_joint_ids.end(),static_cast<unsigned>(model.source_parent_ids[node_index]));
            parent=(found!=model.source_joint_ids.end() && static_cast<std::size_t>(found-model.source_joint_ids.begin())<result.world.size())?result.world[found-model.source_joint_ids.begin()]:model_matrix;
        }
        const Matrix world=multiply(parent,local);
        if (node.depth>=0 && node.depth<kMaxDepth) {
            parents[static_cast<std::size_t>(node.depth)]=world;
            have_parent[static_cast<std::size_t>(node.depth)]=true;
        }
        result.parent.push_back(parent);
        result.world.push_back(world);
    }
    // Retain target animation rotations and translations. Reconstruct imported
    // pivot positions through the mapped deform hierarchy, which can skip the
    // target's hip/ankle helpers. Offsetting local target nodes instead makes
    // those helpers swing long imported feet around the wrong pivot in aerials.
    const auto stock=result.world;
    std::vector<bool> resolved(model.nodes.size());
    for (unsigned i=0;i<resolved.size();++i)
        resolved[i]=i>=model.imported_pivots.size() || model.imported_pivots[i].parent<0;
    for (unsigned pass=0;pass<model.nodes.size();++pass) {
        bool changed=false;
        for (unsigned i=0;i<model.imported_pivots.size();++i) {
            if (resolved[i]) continue;
            const auto& pivot=model.imported_pivots[i];const unsigned parent=pivot.parent;
            if (parent>=resolved.size() || !resolved[parent]) continue;
            const auto offset=transform_direction(stock[parent],pivot.segment_delta);
            const auto parent_delta=sub(transform(result.world[parent],{}),transform(stock[parent],{}));
            result.world[i].m[3]+=offset.x+parent_delta.x;
            result.world[i].m[7]+=offset.y+parent_delta.y;
            result.world[i].m[11]+=offset.z+parent_delta.z;
            resolved[i]=true;changed=true;
        }
        if (!changed) break;
    }
    return result;
}

} // namespace

void Scene3DLoader::apply_custom_mesh(Model3D& model,std::span<const std::byte> bytes) {
    std::istringstream input(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
    std::string magic;unsigned count{};input>>magic>>count;
    if ((magic!="SGMESH1" && magic!="SGMESH2") || count>128) throw std::runtime_error("Invalid custom mesh header");
    std::map<unsigned,Vec3> binds;std::map<unsigned,int> source_parents;
    for (unsigned i=0;i<count;++i) {
        unsigned id;Vec3 point;int parent=-1;input>>id;if (magic=="SGMESH2") input>>parent;
        input>>point.x>>point.y>>point.z;binds[id]=point;source_parents[id]=parent;
    }
    auto rest=model;rest.animation.clear();rest.fighter_root_animation.reset();
    n64::AnimationDecoder decoder(archive_);
    const auto matrices=world_matrices(decoder,rest,0);
    model.imported_rest_offsets.resize(model.nodes.size());
    std::vector<unsigned> ancestors;
    for (unsigned i=0;i<model.nodes.size();++i) {
        const auto depth=model.nodes[i].depth;
        while (!ancestors.empty() && model.nodes[ancestors.back()].depth>=depth) ancestors.pop_back();
        const unsigned id=model.source_joint_ids[i];
        const auto stock=transform(matrices.world[i],{});
        const auto origin=binds.contains(id)?binds[id]:stock;
        Vec3 parent_origin{};
        if (!ancestors.empty()) {
            const auto parent=ancestors.back();const auto pid=model.source_joint_ids[parent];
            parent_origin=binds.contains(pid)?binds[pid]:transform(matrices.world[parent],{});
        }
        const auto delta=sub(origin,parent_origin);const auto& m=matrices.parent[i].m;
        const std::array<float,3> local{m[0]*delta.x+m[4]*delta.y+m[8]*delta.z,m[1]*delta.x+m[5]*delta.y+m[9]*delta.z,m[2]*delta.x+m[6]*delta.y+m[10]*delta.z};
        for (unsigned axis=0;axis<3;++axis) model.imported_rest_offsets[i][axis]=local[axis]-model.nodes[i].translate[axis];
        ancestors.push_back(i);
    }
    if (magic=="SGMESH2") {
        model.imported_rest_offsets.clear();model.imported_pivots.resize(model.nodes.size());
        for (unsigned i=0;i<model.nodes.size();++i) {
            const auto id=model.source_joint_ids[i];int parent_id=source_parents.contains(id)?source_parents[id]:-1;
            // The imported pelvis height belongs to the stable root frame,
            // so a spinning pelvis cannot orbit the whole mesh around itself.
            if (id==5) parent_id=4;
            const auto found=std::find(model.source_joint_ids.begin(),model.source_joint_ids.end(),parent_id);
            if (parent_id<0 || found==model.source_joint_ids.end() || !binds.contains(id)) continue;
            const unsigned parent=found-model.source_joint_ids.begin();
            Vec3 difference=sub(binds[id],transform(matrices.world[i],{}));
            if (id!=5 && binds.contains(parent_id))
                difference=sub(difference,sub(binds[parent_id],transform(matrices.world[parent],{})));
            const auto& m=matrices.world[parent].m;
            model.imported_pivots[i]={static_cast<int>(parent),{m[0]*difference.x+m[4]*difference.y+m[8]*difference.z,
                m[1]*difference.x+m[5]*difference.y+m[9]*difference.z,m[2]*difference.x+m[6]*difference.y+m[10]*difference.z}};
        }
    }
    auto texture=std::make_shared<RasterImage>();input>>texture->width>>texture->height;
    if (texture->width<0 || texture->height<0 || texture->width>2048 || texture->height>2048) throw std::runtime_error("Invalid custom texture size");
    for (int i=0;i<texture->width*texture->height;++i) {unsigned pixel;input>>pixel;texture->rgba.insert(texture->rgba.end(),{static_cast<std::uint8_t>(((pixel>>11)&31)*255/31),static_cast<std::uint8_t>(((pixel>>6)&31)*255/31),static_cast<std::uint8_t>(((pixel>>1)&31)*255/31),255});}
    input>>count;if (count>300000 || count%3) throw std::runtime_error("Invalid custom vertex count");
    const auto node_for=[&](unsigned joint) {const auto it=std::find(model.source_joint_ids.begin(),model.source_joint_ids.end(),joint);if (it==model.source_joint_ids.end()) throw std::runtime_error("Custom mesh references absent joint");return static_cast<std::uint16_t>(it-model.source_joint_ids.begin());};
    n64::Mesh mesh;
    for (unsigned i=0;i<count;++i) {
        n64::Vertex vertex;unsigned a,b,r,g,blue,alpha;
        input>>a>>vertex.skin_weight>>vertex.x>>vertex.y>>vertex.z>>b>>vertex.skin_position.x>>vertex.skin_position.y>>vertex.skin_position.z>>vertex.u>>vertex.v>>r>>g>>blue>>alpha;
        if (!input || !std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z) || !std::isfinite(vertex.skin_weight) || vertex.skin_weight<0 || vertex.skin_weight>1) throw std::runtime_error("Invalid custom mesh vertex");
        vertex.transform_node=node_for(a);vertex.skin_node=node_for(b);vertex.color={static_cast<std::uint8_t>(r),static_cast<std::uint8_t>(g),static_cast<std::uint8_t>(blue),255};
        if (!texture->rgba.empty()) {vertex.texture=texture;vertex.color={255,255,255,255};}
        mesh.vertices.push_back(vertex);
    }
    model.meshes.assign(model.nodes.size(),{});model.parent_meshes.clear();model.materials.clear();model.material_animation.clear();
    if (!model.meshes.empty()) model.meshes[0]=std::move(mesh);
}

Model3D Scene3DLoader::model(std::string_view descriptor, std::string_view animation,
                            GeometryLayout layout, std::string_view material_symbol,
                            std::string_view material_animation_symbol) {
    const auto desc = archive_.symbol(descriptor);
    if (!desc) throw std::runtime_error("missing model descriptor symbol: " + std::string(descriptor));
    const auto symbol=[&](std::string_view name) -> std::optional<n64::Address> {
        if (name.empty()) return {};
        const auto address=archive_.symbol(name);
        if (!address) throw std::runtime_error("missing model resource: "+std::string(name));
        return address;
    };
    return model(*desc,symbol(animation),layout,symbol(material_symbol),symbol(material_animation_symbol));
}

Model3D Scene3DLoader::model(n64::Address desc,std::optional<n64::Address> animation,
                            GeometryLayout layout,std::optional<n64::Address> material_table,
                            std::optional<n64::Address> material_animation) {
    Model3D model;
    model.nodes = n64::SkeletonDecoder(archive_).decode(desc);
    model.meshes.resize(model.nodes.size());
    model.parent_meshes.resize(model.nodes.size());
    n64::DisplayListDecoder decoder(archive_);
    std::vector<std::vector<n64::Material>> materials(model.nodes.size());
    if (material_table) materials=decoder.materials(*material_table,model.nodes.size());
    model.materials=materials;
    model.material_animation.resize(model.nodes.size());
    if (material_animation) model.material_animation=material_animation_table(
        archive_,*material_animation,model.materials);
    if (layout==GeometryLayout::JointPairs) {
        std::vector<std::optional<n64::Address>> pairs;
        pairs.reserve(model.nodes.size());
        for (const auto& node:model.nodes) pairs.push_back(node.display_list);
        auto decoded=decoder.decode_joint_tree(pairs,materials);
        model.parent_meshes=std::move(decoded.before);
        model.meshes=std::move(decoded.after);
    } else {
        std::vector<std::optional<n64::Address>> display_lists;
        display_lists.reserve(model.nodes.size());
        for (const auto& node:model.nodes) display_lists.push_back(node.display_list);
        model.meshes=decoder.decode_model_tree(display_lists,materials,
                                               layout==GeometryLayout::DisplayListLinks);
    }
    if (animation) model.animation=n64::AnimationDecoder(archive_).table(*animation,model.nodes.size());
    else model.animation.resize(model.nodes.size());
    return model;
}

Model3D Scene3DLoader::weapon(n64::Address attributes,unsigned render_flags) {
    const auto data=archive_.resolve(attributes);
    if (!data) throw std::runtime_error("weapon has no display data");
    const auto materials=archive_.resolve({attributes.file,attributes.offset+4});
    const auto animation=archive_.resolve({attributes.file,attributes.offset+8});
    const auto matanim=archive_.resolve({attributes.file,attributes.offset+12});
    const auto layout=(render_flags&2)?GeometryLayout::DisplayListLinks:GeometryLayout::Direct;
    if (render_flags&1) return model(*data,animation,layout,materials,matanim);
    // WPDesc flags distinguish a raw display list from a DObjDesc tree.
    // Thunder Jolt Air uses a raw list; interpreting it as nodes overruns its file.
    Model3D result;result.nodes.push_back({0,0,{}, {0,0,0},{0,0,0},{1,1,1},data});
    result.parent_meshes.resize(1);result.materials.resize(1);result.material_animation.resize(1);
    n64::DisplayListDecoder decoder(archive_);
    if (materials) result.materials=decoder.materials(*materials,1);
    if (matanim) result.material_animation=material_animation_table(archive_,*matanim,result.materials);
    const std::array<std::optional<n64::Address>,1> lists{data};
    result.meshes=decoder.decode_model_tree(lists,result.materials,(render_flags&2)!=0);
    if (animation) result.animation=n64::AnimationDecoder(archive_).table(*animation,1);else result.animation.resize(1);
    return result;
}

Stage3D Scene3DLoader::stage(std::string_view header) {
    const auto address=archive_.symbol(header);
    if (!address) throw std::runtime_error("missing stage header: "+std::string(header));
    const auto pointer=[&](std::uint32_t offset) {
        return archive_.resolve({address->file,address->offset+offset});
    };
    Stage3D result;
    for (unsigned i=0;i<4;++i) {
        result.blast_bounds[i]=static_cast<float>(archive_.s16({address->file,address->offset+116+i*2}));
        result.camera_bounds[i]=static_cast<float>(archive_.s16({address->file,address->offset+108+i*2}));
    }
    result.camera_angle=archive_.f32({address->file,address->offset+104});
    const auto mask=archive_.u32({address->file,address->offset+68})>>24;
    for (unsigned i=0;i<4;++i) if (const auto desc=pointer(i*16))
        result.layers[i]=model(*desc,pointer(i*16+4),
            mask&(1U<<i) ? GeometryLayout::DisplayListLinks : GeometryLayout::Direct,
            pointer(i*16+8),pointer(i*16+12));
    if (const auto geometry=pointer(64)) {
        const auto field=[&](unsigned offset) { return archive_.resolve({geometry->file,geometry->offset+offset}); };
        const auto vertices=field(4), indices=field(8), links=field(12), groups=field(16);
        const unsigned group_count=static_cast<std::uint16_t>(archive_.s16(*geometry));
        if (vertices && indices && links && groups) {
            for (unsigned group=0;group<group_count;++group) for (unsigned type=0;type<4;++type) {
                const auto group_offset=groups->offset+group*18+2+type*4;
                const unsigned first=static_cast<std::uint16_t>(archive_.s16({groups->file,group_offset}));
                const unsigned line_count=static_cast<std::uint16_t>(archive_.s16({groups->file,group_offset+2}));
                for (unsigned line=first;line<first+line_count;++line) {
                    const unsigned start=static_cast<std::uint16_t>(archive_.s16({links->file,links->offset+line*4}));
                    const unsigned length=static_cast<std::uint16_t>(archive_.s16({links->file,links->offset+line*4+2}));
                    const auto point=[&](unsigned index) {
                        const unsigned id=static_cast<std::uint16_t>(archive_.s16({indices->file,indices->offset+index*2}));
                        const auto at=vertices->offset+id*6;
                        return std::pair{Vec2{static_cast<float>(archive_.s16({vertices->file,at})),
                                             static_cast<float>(archive_.s16({vertices->file,at+2}))},
                                         static_cast<unsigned>(static_cast<std::uint16_t>(archive_.s16({vertices->file,at+4})))};
                    };
                    for (unsigned i=1;i<length;++i) {
                        const auto a=point(start+i-1),b=point(start+i);
                        const unsigned flags=a.second|b.second;
                        result.collision.push_back({a.first,b.first,type,flags,type==0 && (flags&0x4000U)!=0,line});
                    }
                }
            }
        }
        const auto count=static_cast<std::uint16_t>(archive_.s16({geometry->file,geometry->offset+20}));
        if (const auto objects=archive_.resolve({geometry->file,geometry->offset+24}))
            for (unsigned i=0;i<count;++i) {
                const n64::Address item{objects->file,objects->offset+i*6};
                const auto kind=archive_.s16(item);
                if (kind>=0 && kind<4)
                    result.player_spawns[kind]={static_cast<float>(archive_.s16({item.file,item.offset+2})),
                                               static_cast<float>(archive_.s16({item.file,item.offset+4})),0};
                if (kind>=0x15 && kind<=0x17) {
                    auto& position=kind==0x15 ? result.movie_player1 :
                                   kind==0x16 ? result.movie_player2 : result.movie_player3;
                    position={static_cast<float>(archive_.s16({item.file,item.offset+2})),
                              static_cast<float>(archive_.s16({item.file,item.offset+4})),0};
                }
            }
    }
    return result;
}

void Scene3DLoader::set_fighter_part(Model3D& model,FighterKind kind,unsigned joint,int part) {
    const auto found=std::find(model.source_joint_ids.begin(),model.source_joint_ids.end(),joint);
    if (found==model.source_joint_ids.end() || joint<4) return;
    const unsigned node=found-model.source_joint_ids.begin();
    if (part<0) {model.meshes[node]={};model.parent_meshes[node]={};return;}
    static constexpr n64::Address attributes[]{{221,0x580},{203,0x428},{213,0x4a4},{225,0x708},{217,0x610},{236,0x488},{239,0x5bc},{247,0x47c},{229,0x808},{209,0x46c},{243,0x41c},{233,0x474}};
    const auto attr=attributes[static_cast<unsigned>(kind)];
    const auto container=archive_.resolve({attr.file,attr.offset+0x328});
    const auto desc=container?archive_.resolve({container->file,container->offset+(joint-4)*4}):std::nullopt;
    if (!desc) return; // Default geometry is already present.
    const n64::Address variant{desc->file,desc->offset+static_cast<unsigned>(part)*40};
    const auto display=archive_.resolve(variant);
    n64::DisplayListDecoder decoder(archive_);
    auto materials=decoder.materials({variant.file,variant.offset+4},1);
    const auto costumes=archive_.resolve({variant.file,variant.offset+8});
    if (costumes) {
        n64::AnimationDecoder animation(archive_);
        for(unsigned i=0;i<materials[0].size();++i) if(const auto script=archive_.resolve({costumes->file,costumes->offset+i*4})) {
            auto& material=materials[0][i];n64::MaterialPose initial;initial.colors[0]=material.primitive;
            if(material.light1)initial.colors[3]=*material.light1;
            if(material.light2)initial.colors[4]=*material.light2;
            const auto pose=animation.sample_material(*script,0,initial);
            material.primitive=pose.colors[0];if(material.light1)material.light1=pose.colors[3];if(material.light2)material.light2=pose.colors[4];
            if(material.sprites)material.image=archive_.resolve({material.sprites->file,material.sprites->offset+4U*static_cast<unsigned>(pose.tracks[0])});
            if(material.palettes)material.palette=archive_.resolve({material.palettes->file,material.palettes->offset+4U*static_cast<unsigned>(pose.tracks[9])});
        }
    }
    model.materials[node]=materials[0];model.parent_meshes[node]={};
    const auto flags=std::to_integer<unsigned>(archive_.bytes(variant.file)[variant.offset+16]);
    if (!display) model.meshes[node]={};
    else if(flags&1) {
        const std::array<std::optional<n64::Address>,1> pairs{display};
        auto decoded=decoder.decode_joint_tree(pairs,materials,true);
        model.parent_meshes[node]=std::move(decoded.before[0]);model.meshes[node]=std::move(decoded.after[0]);
    } else model.meshes[node]=decoder.decode(*display,materials[0]);
    // The part's material animation table has the same one-node layout as its materials.
    model.material_animation[node]=material_animation_table(archive_,{variant.file,variant.offset+12},materials)[0];
}

Model3D Scene3DLoader::fighter_motion(FighterKind kind, unsigned clip, std::uint32_t flags) {
    const auto spec=fighter_model_spec(kind);
    auto actor=fighter_model(spec.descriptor,spec.joint_pairs ? GeometryLayout::JointPairs :
                             GeometryLayout::Direct,spec.setup_parts,flags);
    const bool wrapper=(flags&0xc0000000U)!=0;
    const auto scripts=n64::AnimationDecoder(archive_).table({clip,0},actor.nodes.size()+(wrapper?1:0));
    if (wrapper) {
        actor.fighter_root.scale={1,1,1};
        actor.fighter_root_animation=scripts.front();
        actor.fighter_wrapper=(flags&0x80000000U) ? Model3D::FighterWrapper::XRotN :
                                                               Model3D::FighterWrapper::TransN;
    }
    actor.animation.assign(scripts.begin()+(wrapper?1:0),scripts.end());
    actor.fighter_animation=true;
    return actor;
}

Model3D Scene3DLoader::fighter_model(std::string_view descriptor, GeometryLayout layout,
                                    std::array<std::uint32_t,2> setup_parts,
                                    std::uint32_t animation_flags, unsigned costume) {
    const auto desc=archive_.symbol(descriptor);
    if (!desc) throw std::runtime_error("missing fighter descriptor symbol: "+std::string(descriptor));
    Model3D model;
    const auto source_nodes=n64::SkeletonDecoder(archive_).decode(*desc);
    std::unordered_map<unsigned,int> attachment_parents;
    n64::DisplayListDecoder decoder(archive_);
    auto source_materials=decoder.materials({desc->file,0},source_nodes.size());
    // FTData.o_attributes and FTCommonPart.p_costume_matanim_joints from
    // ftdata.c / lbCommonAddMObjForFighterPartsDObj. Costume 0 is evaluated
    // once, before decoding the DL (including its palette loads).
    static const std::unordered_map<std::uint32_t,n64::Address> attributes{
        {296,{203,0x428}}, {313,{209,0x46c}}, {317,{213,0x4a4}},
        {320,{217,0x610}}, {323,{221,0x580}}, {324,{225,0x708}},
        {338,{247,0x47c}}, {332,{236,0x488}}, {328,{229,0x808}},
        {341,{243,0x41c}}, {330,{233,0x474}}, {335,{239,0x5bc}}
    };
    if (const auto entry=attributes.find(desc->file); entry!=attributes.end()) {
        const auto attr=entry->second;
        // Motion descriptors enable auxiliary joints before the figatree is
        // bound. Omitting these shifts every subsequent animation pointer.
        if (const auto hidden=archive_.resolve({attr.file,attr.offset+0x2d0})) {
            for (unsigned bit=3;bit<27;++bit) if (animation_flags&(0x80000000U>>bit)) {
                const auto joint=archive_.u32({hidden->file,hidden->offset+bit*16});
                if (joint>=4 && joint-4<source_nodes.size()) {
                    attachment_parents[joint]=static_cast<int>(archive_.u32({hidden->file,hidden->offset+bit*16+4}));
                    const unsigned index=joint-4;
                    setup_parts[index/32]|=0x80000000U>>(index%32);
                }
            }
        }
        const auto parts=archive_.resolve({attr.file,attr.offset+0x2d4});
        const auto costumes=parts ? archive_.resolve({parts->file,parts->offset+8}) : std::nullopt;
        if (costumes) {
            const auto scripts=material_animation_table(archive_,*costumes,source_materials);
            n64::AnimationDecoder animation(archive_);
            for (std::size_t i=0;i<source_materials.size();++i)
                for (std::size_t j=0;j<source_materials[i].size();++j) {
                    if (!scripts[i][j]) continue;
                    auto& material=source_materials[i][j];
                    n64::MaterialPose initial;
                    initial.colors[0]=material.primitive;
                    if (material.light1) initial.colors[3]=*material.light1;
                    if (material.light2) initial.colors[4]=*material.light2;
                    const auto pose=animation.sample_material(*scripts[i][j],static_cast<float>(costume),initial);
                    material.primitive=pose.colors[0];
                    if (material.light1) material.light1=pose.colors[3];
                    if (material.light2) material.light2=pose.colors[4];
                    if (material.sprites)
                        material.image=archive_.resolve({material.sprites->file,
                            material.sprites->offset+4U*static_cast<unsigned>(pose.tracks[0])});
                    if (material.palettes)
                        material.palette=archive_.resolve({material.palettes->file,
                            material.palettes->offset+4U*static_cast<unsigned>(pose.tracks[9])});
                }
        }
    }
    const auto enabled=[&](std::size_t index) {
        const auto word=index/32;
        return word<setup_parts.size() &&
               (setup_parts[word]&(1U<<(31U-static_cast<unsigned>(index%32))))!=0;
    };
    model.nodes.reserve(source_nodes.size());
    model.materials.reserve(source_nodes.size());
    for (std::size_t i=0;i<source_nodes.size();++i) {
        if (!enabled(i)) continue;
        model.nodes.push_back(source_nodes[i]);
        model.source_joint_ids.push_back(static_cast<unsigned>(i)+4);
        model.source_parent_ids.push_back(attachment_parents.contains(i+4)?attachment_parents.at(i+4):-1);
        model.materials.push_back(source_materials[i]);
    }
    model.meshes.resize(model.nodes.size());
    model.parent_meshes.resize(model.nodes.size());
    model.animation.resize(model.nodes.size());
    model.material_animation.resize(model.nodes.size());
    if (layout==GeometryLayout::JointPairs) {
        std::vector<std::optional<n64::Address>> pairs;
        pairs.reserve(model.nodes.size());
        for (const auto& node:model.nodes) pairs.push_back(node.display_list);
        auto decoded=decoder.decode_joint_tree(pairs,model.materials,true);
        model.parent_meshes=std::move(decoded.before);
        model.meshes=std::move(decoded.after);
    } else {
        std::vector<std::optional<n64::Address>> display_lists;
        display_lists.reserve(model.nodes.size());
        for (const auto& node:model.nodes) display_lists.push_back(node.display_list);
        model.meshes=decoder.decode_model_tree(display_lists,model.materials,
                                               layout==GeometryLayout::DisplayListLinks,true);
    }
    model.is_fighter=true;
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
    Camera3D result=initial_camera;
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

Vec3 Scene3DRenderer::joint_point(const Model3D& model,float frame,unsigned joint,Vec3 offset) {
    const auto found=std::find(model.source_joint_ids.begin(),model.source_joint_ids.end(),joint);
    if (found==model.source_joint_ids.end()) {
        // Joint 0 is TopN, outside the common-part descriptor tree.
        const auto root=multiply(multiply(translation({model.position.x,model.position.y,model.position.z}),
            rotation({model.rotation.x,model.rotation.y,model.rotation.z})),scale({model.scale.x,model.scale.y,model.scale.z}));
        return transform(root,offset);
    }
    const auto matrices=world_matrices(animation_,model,frame);
    const auto& matrix=matrices.world[static_cast<std::size_t>(found-model.source_joint_ids.begin())];
    const auto point=transform(matrix,{offset.x,offset.y,offset.z});
    return point;
}

Vec3 Scene3DRenderer::fighter_position(const Model3D& model, float frame) {
    auto position=model.position;
    if (model.fighter_wrapper==Model3D::FighterWrapper::TransN && model.fighter_root_animation) {
        const auto initial=animation_.pose(model.fighter_root);
        const auto start=animation_.sample16(*model.fighter_root_animation,0,initial);
        const auto current=animation_.sample16(*model.fighter_root_animation,frame,initial);
        position.x+=current.tracks[4]-start.tracks[4];
        position.y+=current.tracks[5]-start.tracks[5];
        position.z+=current.tracks[6]-start.tracks[6];
    }
    return position;
}

Model3D Scene3DRenderer::captured_at_joint(const Model3D& model,float frame,
    const Model3D& carrier,float carrier_frame,unsigned source_joint) {
    const auto it=std::find(carrier.source_joint_ids.begin(),carrier.source_joint_ids.end(),source_joint);
    if (it==carrier.source_joint_ids.end()) throw std::runtime_error("Missing capture attachment joint");
    const auto matrices=world_matrices(animation_,carrier,carrier_frame);
    const auto& anchor=matrices.world[it-carrier.source_joint_ids.begin()];
    Matrix root=anchor;
    const float sizes[]{model.scale.x,model.scale.y,model.scale.z};
    for (int c=0;c<3;++c) {
        const auto axis=normalize({anchor.m[c],anchor.m[c+4],anchor.m[c+8]});
        root.m[c]=axis.x*sizes[c];root.m[c+4]=axis.y*sizes[c];root.m[c+8]=axis.z*sizes[c];
    }
    auto child=model.fighter_root;
    if (model.fighter_root_animation) n64::AnimationDecoder::apply(child,animation_.sample16(*model.fighter_root_animation,frame,animation_.pose(child)));
    const auto offset=transform_direction(root,{child.translate[0],child.translate[1],child.translate[2]});
    root.m[3]-=offset.x;root.m[7]-=offset.y;root.m[11]-=offset.z;
    auto placed=model;placed.root_transform=root.m;
    // Capture uses the current wrapper translation, not accumulated locomotion deltas.
    if (placed.fighter_wrapper==Model3D::FighterWrapper::TransN) placed.fighter_wrapper=Model3D::FighterWrapper::XRotN;
    return placed;
}

Model3D Scene3DRenderer::placed_at_joint(const Model3D& model, float model_frame,
                                         const Model3D& carrier, float carrier_frame,
                                         std::size_t carrier_joint) {
    Model3D placed=model;
    const auto carrier_matrices=world_matrices(animation_,carrier,carrier_frame);
    if (carrier_joint>=carrier_matrices.world.size() || model.nodes.empty()) return placed;
    auto first_child=model.fighter_root;
    if (model.fighter_root_animation)
        n64::AnimationDecoder::apply(first_child,animation_.sample16(
            *model.fighter_root_animation,model_frame,animation_.pose(first_child)));
    const Matrix attachment=multiply(carrier_matrices.world[carrier_joint],
                                     translation({-first_child.translate[0],-first_child.translate[1],
                                                  -first_child.translate[2]}));
    // The original helper subtracts TopN's first child (TransN), then copies
    // the holding joint's world position/orientation into TopN.  The common
    // model descriptor starts below that wrapper and must not be used as the
    // attachment offset.
    Matrix root;
    for (int column=0;column<3;++column) {
        Vec3 axis{carrier_matrices.world[carrier_joint].m[column],
                  carrier_matrices.world[carrier_joint].m[4+column],
                  carrier_matrices.world[carrier_joint].m[8+column]};
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
    int hidden_depth=-1;
    for (std::size_t node_index=0; node_index<model.nodes.size(); ++node_index) {
        const int node_depth=model.nodes[node_index].depth;
        if (hidden_depth>=0&&node_depth<=hidden_depth) hidden_depth=-1;
        if (hidden_depth>=0) continue;
        if ((runtime_flags[node_index]&2U)!=0) {
            hidden_depth=node_depth;
            continue;
        }
        const Matrix& world=matrices.world[node_index];
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
        const auto vertex_matrix=[&](const n64::Vertex& vertex) -> const Matrix& {
            if (vertex.transform_node<matrices.world.size())
                return vertex.transform_parent ? matrices.parent[vertex.transform_node]
                                               : matrices.world[vertex.transform_node];
            return mesh_world;
        };
        for (std::size_t i=0;i+2<mesh.vertices.size();i+=3) {
            std::array<Vec3,3> world_points{};
            for (int j=0;j<3;++j) {
                const auto& source=mesh.vertices[i+j];
                world_points[j]=transform(vertex_matrix(source),{source.x,source.y,source.z});
                if (source.skin_node<matrices.world.size()) {
                    const auto second=transform(matrices.world[source.skin_node],source.skin_position);
                    const auto first=world_points[j];const float w=source.skin_weight;
                    world_points[j]={first.x*w+second.x*(1-w),first.y*w+second.y*(1-w),first.z*w+second.z*(1-w)};
                }
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
            if ((model.is_fighter||model.fighter_animation) && std::max({edge0,edge1,edge2})>20000.0f) continue;
            Vec3 face_normal=normalize(cross(sub(world_points[1],world_points[0]),
                                             sub(world_points[2],world_points[0])));
            std::array<ProjectedVertex,3> triangle{};
            for (int j=0;j<3;++j) {
                const auto& source=mesh.vertices[i+j];
                const Vec3 point=world_points[j];
                const Vec3 relative=sub(point,camera.eye);
                const bool valid_source_normal=dot(source.normal,source.normal)>1.0e-6f;
                const Vec3 world_normal=source.lit&&valid_source_normal
                    ? normalize(transform_normal(vertex_matrix(source),source.normal)) : face_normal;
                Color surface=source.rdp.enabled ? source.shade : source.color;
                if (source.material_index<material_poses.size()) {
                    const bool animated=node_index<model.material_animation.size()&&
                        source.material_index<model.material_animation[node_index].size()&&
                        model.material_animation[node_index][source.material_index].has_value();
                    if (animated && !source.rdp.enabled) surface=source.lit ? material_poses[source.material_index].colors[0]
                                                    : modulate(surface,material_poses[source.material_index].colors[0]);
                }
                triangle[j]={{dot(relative,right),dot(relative,up),dot(relative,forward)},
                             source.rdp.enabled ? surface : modulate(surface,tint),{source.u,source.v},
                             normalize({dot(world_normal,right),dot(world_normal,up),dot(world_normal,forward)})};
            }
            const auto& sampler=mesh.vertices[i];
            auto material_light1=sampler.light1;
            auto material_light2=sampler.light2;
            auto rdp=sampler.rdp;
            rdp.tint=tint;
            bool animated_translucency{};
            if (sampler.material_index<material_poses.size()) {
                const auto& source=model.materials[node_index][sampler.material_index];
                if (source.light1) material_light1=material_poses[sampler.material_index].colors[3];
                if (source.light2) material_light2=material_poses[sampler.material_index].colors[4];
                const bool animated=node_index<model.material_animation.size() &&
                    sampler.material_index<model.material_animation[node_index].size() &&
                    model.material_animation[node_index][sampler.material_index];
                if (animated) {
                    rdp.primitive=material_poses[sampler.material_index].colors[0];
                    animated_translucency=rdp.primitive.a<255;
                }
            }
            triangles_.push_back({triangle,sampler.texture,sampler.texture_mode_s,sampler.texture_mode_t,
                                  sampler.texture_mask_s,sampler.texture_mask_t,
                                  sampler.texture_window_s,sampler.texture_window_t,lights,
                                  material_light1,material_light2,camera.fov_y,camera.near_plane,camera.far_plane,
                                  sampler.lit&&model.receive_lighting,
                                  sampler.translucent||tint.a<255||animated_translucency||model.additive,
                                  model.additive,rdp,camera.viewport,camera.aspect});
        }
        };
        if ((runtime_flags[node_index]&1U)==0&&node_index<model.parent_meshes.size() &&
            !model.parent_meshes[node_index].vertices.empty()) {
            render_mesh(model.parent_meshes[node_index],matrices.parent[node_index]);
        }
        if ((runtime_flags[node_index]&1U)==0) render_mesh(model.meshes[node_index],world);
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
    for (const auto& triangle:triangles_) if (!triangle.translucent && !triangle.rdp.enabled)
        for (const auto& point:triangle.points) shadow_geometry.push_back(convert(point));
    render.prepare_forward_shadows(shadow_geometry,triangles_.front().lights.key.direction);

    const auto make_material=[](const ProjectedTriangle& triangle) {
        ForwardMaterial material;
        material.texture=triangle.texture;
        material.rdp=triangle.rdp;
        material.viewport=triangle.viewport;
        material.aspect=triangle.aspect;
        material.lights=triangle.lights;
        // MObj light1/light2 are the part's local Lights1 (diffuse/ambient),
        // not a replacement for the scene rig. Overwriting the key color
        // with those bytes is what tinted fighter meshes strange colors.
        if (triangle.material_light1) material.material_diffuse=*triangle.material_light1;
        if (triangle.material_light2) material.material_ambient=*triangle.material_light2;
        else if (triangle.rdp.enabled) material.material_ambient={32,32,32,255};
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
        material.additive=triangle.additive;
        return material;
    };
    const auto same_color=[](Color a,Color b) {
        return a.r==b.r&&a.g==b.g&&a.b==b.b&&a.a==b.a;
    };
    const auto same_material=[&](const ForwardMaterial& a,const ForwardMaterial& b) {
        const auto& x=a.rdp;
        const auto& y=b.rdp;
        return a.viewport==b.viewport && a.aspect==b.aspect && x.enabled==y.enabled && x.combine_hi==y.combine_hi && x.combine_lo==y.combine_lo &&
            x.cycles==y.cycles && x.cull_mode==y.cull_mode && x.texture_gen==y.texture_gen && x.texture_gen_linear==y.texture_gen_linear &&
            x.generated_scale.x==y.generated_scale.x && x.generated_scale.y==y.generated_scale.y &&
            x.alpha_threshold==y.alpha_threshold && x.alpha_test==y.alpha_test && same_color(x.primitive,y.primitive) &&
            same_color(x.environment,y.environment) && same_color(x.tint,y.tint) &&
            a.texture.get()==b.texture.get() && a.fov_y==b.fov_y &&
            a.near_plane==b.near_plane && a.far_plane==b.far_plane &&
            a.texture_mode_s==b.texture_mode_s && a.texture_mode_t==b.texture_mode_t &&
            a.texture_mask_s==b.texture_mask_s && a.texture_mask_t==b.texture_mask_t &&
            a.texture_window_s==b.texture_window_s && a.texture_window_t==b.texture_window_t &&
            a.lit==b.lit && a.translucent==b.translucent && a.additive==b.additive &&
            same_color(a.material_diffuse,b.material_diffuse) &&
            same_color(a.material_ambient,b.material_ambient) &&
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
