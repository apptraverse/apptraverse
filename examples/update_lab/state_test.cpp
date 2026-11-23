#include "model.h"
#include "apptraverse/distill.h"
#include "apptraverse/overlay_domain_storage.h"
#include <iostream>
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
using namespace update_lab;
void Require(bool value, char const* message) { if(!value)Fail(message); }
int main() {
  RegisterModel();
  Frame frame{1,4,"language/2/de",17,Bytes(10000,42)};frame.request=123;
  auto encoded=Encode(frame);std::vector<FramePiece> pieces;
  for(std::size_t n=0;n<encoded.size();n+=512)
    pieces.push_back({123,static_cast<std::uint32_t>(encoded.size()),static_cast<std::uint32_t>(n),
        Bytes{encoded.begin()+n,encoded.begin()+std::min(n+512,encoded.size())}});
  FrameAssembler assembler;std::optional<Frame> result;
  for(auto it=pieces.rbegin();it!=pieces.rend();++it) {
    result=assembler.Receive(*it);
    if(it+1!=pieces.rend()) Require(!assembler.Receive(*it),"duplicate piece must not complete a partial frame");
  }
  Require(result && result->payload==frame.payload && result->chunk==17,"unordered native transport pieces reassemble exactly");
  FrameAssembler overlapping;
  Require(!overlapping.Receive({123,static_cast<std::uint32_t>(encoded.size()),0,Bytes{encoded.begin(),encoded.begin()+512}}),"partial transport frame");
  Require(!overlapping.Receive({123,static_cast<std::uint32_t>(encoded.size()),256,Bytes{encoded.begin()+256,encoded.begin()+768}}),"overlapping retransmission remains partial");
  auto recovered=overlapping.Receive({123,static_cast<std::uint32_t>(encoded.size()),700,Bytes{encoded.begin()+700,encoded.end()}});
  Require(recovered&&recovered->payload==frame.payload,"overlapping retransmission with changed fragment sizes");
  {
    Catalog catalog{2, 5, {}, {}, {{"test", {4, 5}}, {"older", {4}}}};
    catalog.releases = {{4, 1, "supply/4", {{"ru", "language/4/ru", 22, 4}}},
                        {5, 3, "supply/5", {{"ru", "", 22, 5}}}};
    catalog.components = {{DeltaId(4, 5), 100, ""}, {ExeId("test", 3), 5000, ""}, {"supply/5", 300, ""}};
    ResolveRequest query{"test", 4, 1, {kLatest, "ru", false}, {{"ru", 4}}, {{DeltaId(4, 5), 64}}};
    UpdatePlan plan{};
    Require(Resolve(catalog, query, plan).empty() && plan.bytes == 5100 && plan.remaining == 5036
            && plan.executable == 3 && plan.resources.empty() && plan.image.resources.empty(),
            "future release, partial byte credit, installed locale now embedded");
    query.current = kLatest; query.executable = 3;
    Require(Resolve(catalog, query, plan).empty() && plan.bytes == 300 && plan.supply.steps == std::vector<std::string>{"supply/5"},
            "clean install resolves one full state");
    query.current = 4; query.platform = "older"; query.executable = 1;
    Require(Resolve(catalog, query, plan).empty() && plan.intent.release == 4 && plan.bytes == 0,
            "latest is scoped to the platform");
    query.platform = "test";
    catalog.components.erase(catalog.components.begin());
    Require(Resolve(catalog, query, plan).find(DeltaId(4,5)) != std::string::npos && plan.intent.release == 4,
            "missing adjacent edge fails without a full fallback or partial result");
  }
  auto r0=BuildCompleteSupply(0);auto r1=BuildCompleteSupply(1);auto r2=BuildCompleteSupply(2);auto r3=BuildCompleteSupply(3);
  {
    StatePackage package;auto bytes=FullState(r0,0);
    Require(Decode(bytes,package)&&package.kind==0&&package.to==0,"package starts with typed metadata without file signature");
    Require(bytes.size()==24+package.compressed.size(),"package metadata has no magic string");
    ZSTD_FrameHeader header{};
    auto result=ZSTD_getFrameHeader_advanced(&header,package.compressed.data(),package.compressed.size(),ZSTD_f_zstd1_magicless);
    Require(result==0&&header.checksumFlag==0&&header.frameContentSize==package.raw_size,"magicless frame without checksum");
  }
  {
    auto d0 = DistillRelease(0), d1 = DistillRelease(1), d2 = DistillRelease(2);
    LanguagePack original, changed, german;
    auto en = FindLanguage(d0.definition, "en");
    auto de = FindLanguage(d2.definition, "de");
    Require(en && de && ReadLanguage(d0.embedded, *en, original) && ReadLanguage(d1.embedded, *en, changed)
            && original.text[Greeting] != changed.text[Greeting], "embedded translation changed");
    Require(!d2.embedded.state.contains(ae::ObjId{de->root}) &&
            ReadLanguage(d2.downloads.at("de"), *de, german), "optional root and its strings belong to one download");
  }
  ae::RamDomainStorage state;unsigned version=0;
  Require(ApplyStatePackage(FullState(r0,0),state,version),"full load");
  for(unsigned r=1;r<4;++r) {
    auto target=BuildCompleteSupply(r);
    Require(ApplyStatePackage(DeltaState(state,target,r-1,r),state,version),"delta apply");
    Bytes actual,expected;apptraverse::SerializeUserStorage(state,actual);apptraverse::SerializeUserStorage(target,expected);
    Require(actual==expected,"delta must reproduce complete native image");
  }
  Require(!ApplyStatePackage(DeltaState(r0,r1,0,1),state,version),"wrong base version rejected");
  auto deleted=BuildSupply(3);
  deleted.state.erase(kSupplyId);deleted.object_scopes.erase(kSupplyId);
  deleted.state[ae::ObjId{123}]=std::nullopt;
  deleted.state[kPrefabId]->at(TextObject::kClassId).erase(1);
  Require(ApplyStatePackage(DeltaState(state,deleted,3,4),state,version),"delta deletion apply");
  Bytes exact,expected;apptraverse::SerializeUserStorage(state,exact);apptraverse::SerializeUserStorage(deleted,expected);
  Require(exact==expected,"layer removal, object removal, tombstone and scopes preserved");
  ae::RamDomainStorage empty_before,empty_after;
  empty_before.state[ae::ObjId{1}]=std::nullopt;
  empty_before.state[ae::ObjId{2}].emplace();(*empty_before.state[ae::ObjId{2}])[123];
  (*empty_before.state[ae::ObjId{2}])[456][0]=Bytes{1};
  empty_after.state[ae::ObjId{1}].emplace();empty_after.state[ae::ObjId{2}].emplace();
  (*empty_after.state[ae::ObjId{2}])[456];(*empty_after.state[ae::ObjId{2}])[789];
  empty_after.state[ae::ObjId{3}].emplace();
  unsigned empty_version=0;
  Require(ApplyStatePackage(DeltaState(empty_before,empty_after,0,1),empty_before,empty_version),"empty native map delta apply");
  apptraverse::SerializeUserStorage(empty_before,exact);apptraverse::SerializeUserStorage(empty_after,expected);
  Require(exact==expected,"empty objects/classes and tombstone replacement preserved");
  ae::RamDomainStorage user;apptraverse::OverlayDomainStorage overlay{user,r0,{kPrefabId,kSupplyId}};ae::Domain domain{overlay};
  auto clone=ClonePrefab(domain);auto clone_id=clone.id();clone->name="custom";clone.Save();
  r0.state=r3.state;r0.object_scopes=r3.object_scopes;
  ae::DomainGraph graph{&domain};auto loaded=graph.LoadVersion(ae::Version<1>{},*clone,clone.id());
  Require(loaded.IsOk()&&clone->badge=="S1 / prefab R3","existing clone inherits new S1 through native origin");
  Require(clone.id()==clone_id&&clone->name=="custom","clone identity and S0 override preserved");
  clone.Save();Require(user.Load({clone_id,TextObject::kClassId,1}).result==ae::DomainLoadResult::kEmpty,"inherited S1 not pinned in USER");
  std::cout<<"PASS native localization, full/deltas, removals, base guard, S1 origin, identity, overrides, unordered/overlapping transport\n";
}
