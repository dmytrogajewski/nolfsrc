#include "engine/server/model_geometry.h"
#include "engine/server/model_file.h"
#include "engine/server/model_loading.h"
#include "engine/server/model_cache.h"
#include "engine/portable/memory_stream.h"
#include "engine/portable/binary.h"
#include "engine/portable/wire.h"
#include "platform/system.h"
#include "model_fixture.h"
#include <cmath>
#include <iostream>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
bool near(LTVector a,LTVector b) { return a.DistSqr(b)<.000001f; }
std::shared_ptr<Model> load(const std::vector<std::byte>& bytes) {
    auto model=std::make_shared<Model>(&g_DefAlloc,&g_DefAlloc); nolf::MemoryStream stream(bytes);
    require(model->Load(&stream,&g_DefAlloc)==LT_OK,"mesh header load failed");
    CModelLoadRequest request; request.m_pStream=&stream; request.m_bUnknown10=LTFALSE;
    require(model->LoadChildModels(&request)==LT_OK,"mesh records load failed"); return model;
}
void fixtures() {
    auto bytes=nolf_test::tiny_model({},"start",true); std::string error;
    require(model_ValidateFile(bytes,&error),"complete triangle ABC rejected");
    for (std::size_t i=0;i<bytes.size();++i) require(!model_ValidateFile(std::span(bytes).first(i)),"truncated mesh ABC accepted");
    auto model=load(bytes); ModelInstance instance; instance.BindModel(model,false,false); instance.SetPos({10,20,30});
    require(model->m_Pieces.GetSize()==1,"mesh piece missing"); const auto& mesh=*model->m_Pieces[0];
    require(mesh.vertices().size()==3 && mesh.triangles().size()==1 && mesh.triangles()[0].indices[2]==2 && mesh.triangles()[0].uv[1][0]==1 &&
        mesh.vertices()[1].weight_count==2 && mesh.vertices()[2].replacement==0xffff && mesh.vertices()[0].normal.z==1,"native geometry fields do not describe disk data");
    auto pose=model_EvaluatePose(instance); require(pose.has_value(),"owned pose evaluation failed");
    auto vertices=model_SkinVertices(*pose,mesh);
    require(near(vertices[0],{10,20,40}) && near(vertices[1],{12,20,40}) && near(vertices[2],{10,21,40}),"weighted, blended or rigid native skin position wrong");
    ModelInstance other; other.BindModel(model,false,false); other.SetPos({100,0,0});
    auto second=model_EvaluatePose(other); require(second && near(model_SkinVertices(*second,mesh)[0],{100,0,10}) && near(model_SkinVertices(*pose,mesh)[0],vertices[0]),"shared model pose matrices overwrote another instance");
    instance.m_Unknown1fc=+[](HOBJECT object,HMODELNODE,LTMatrix*,void*) { static_cast<ModelInstance*>(object)->BindModel({},false,false); };
    require(!model_EvaluatePose(instance) && !instance.m_ModelOwner,"node callback rebind published incomplete pose");
    auto doomed=std::make_unique<ModelInstance>(); doomed->BindModel(model,false,false);
    doomed->m_Unknown200=&doomed;
    doomed->m_Unknown1fc=+[](HOBJECT,HMODELNODE,LTMatrix*,void* owner) { static_cast<std::unique_ptr<ModelInstance>*>(owner)->reset(); };
    require(!model_EvaluatePose(*doomed) && !doomed,"node callback deletion left evaluator using stale instance");
    // Locate the first triangle's index from the independent fixture layout.
    std::size_t offset=0;
    while (true) {
        nolf::BinaryReader reader{std::span<const std::byte>(bytes).subspan(offset)}; const auto length=reader.integer<std::uint16_t>();
        const auto name=reader.bytes(length); const auto next=reader.integer<std::uint32_t>();
        if (std::string_view(reinterpret_cast<const char*>(name.data()),name.size())=="Pieces") { offset+=2+length+4; break; }
        require(next!=UINT32_MAX,"fixture pieces not found"); offset=next;
    }
    const auto first_corner_index=offset+8+2+8+4+2+2+8+4+8;
    auto bad=bytes; nolf::wire::store(bad.data()+first_corner_index,std::uint16_t(3)); require(!model_ValidateFile(bad),"out of range triangle accepted");
    const auto first_weight=first_corner_index+22+4+4;
    bad=bytes; nolf::wire::store(bad.data()+first_weight,std::uint32_t(1)); require(!model_ValidateFile(bad),"missing skeletal bone accepted");
    bad=bytes; nolf::wire::store(bad.data()+first_weight+4,std::uint32_t(0x7fc00000)); require(!model_ValidateFile(bad),"nonfinite vertex weight accepted");
}
void corpus(const char* data) {
    auto assets=std::make_shared<nolf::Assets>(); assets->mount_game(data); ModelCache cache(assets);
    unsigned models=0; std::uint64_t pieces=0,lods=0,vertices=0,triangles=0;
    for (const auto& name : assets->names()) if (name.ends_with(".abc")) {
        try {
            auto model=cache.Load(name); ModelInstance instance; instance.BindModel(model,false,false);
            const auto pose=model_EvaluatePose(instance); require(bool(pose),"original model pose failed");
            for (auto* piece : std::span(model->m_Pieces.GetArray(),model->m_Pieces.GetSize())) {
                ++pieces;
                for (uint32 i=0;i<=piece->m_Array34.GetSize();++i) {
                    auto* mesh=piece->GetPieceData(i); const auto skinned=model_SkinVertices(*pose,*mesh);
                    for (const auto& triangle : mesh->triangles()) for (unsigned corner=0;corner<3;++corner) {
                        require(triangle.indices[corner]<skinned.size() && std::isfinite(triangle.uv[corner][0]) && std::isfinite(triangle.uv[corner][1]),"original triangle UV/index invalid");
                    }
                    ++lods; vertices+=skinned.size(); triangles+=mesh->triangles().size();
                }
            }
            ++models;
        } catch (const std::exception& error) { throw std::runtime_error(name+": "+error.what()); }
    }
    require(models && vertices && triangles,"empty original model geometry corpus");
    std::cout<<"Skinned "<<models<<" original ABC models, "<<pieces<<" pieces, "<<lods<<" mesh LODs, "<<vertices<<" vertices, "<<triangles<<" triangles\n";
}
}
int main(int argc,char** argv) {
    try { fixtures(); if (argc==2) corpus(argv[1]); else if (argc!=1) throw std::invalid_argument("usage: nolf-model-geometry-tests [game-data]"); std::cout<<"Native model geometry checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
