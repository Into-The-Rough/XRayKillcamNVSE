#include "Config.h"
#include "Runtime.h"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace XRay::Config {
static float ReadFloat(const char* path,const char* section,const char* name,float value,float lo,float hi) {
    char fallback[32],text[64];sprintf_s(fallback,"%.6g",value);
    GetPrivateProfileStringA(section,name,fallback,text,sizeof text,path);
    char* end=nullptr;float result=strtof(text,&end);
    return end==text||*end||!std::isfinite(result)?value:Clamp(result,lo,hi);
}
static bool ReadBool(const char* path,const char* section,const char* name,bool value) {
    return GetPrivateProfileIntA(section,name,value?1:0,path)!=0;
}
static const char* Triggers(const Settings& s) {
    static char text[64];text[0]=0;
    const char* names[]={s.kills?"kills":nullptr,s.cripples?"cripples":nullptr,s.criticals?"criticals":nullptr,s.groin?"groin shots":nullptr};
    for(auto n:names)if(n){if(text[0])strcat_s(text,", ");strcat_s(text,n);}
    return text[0]?text:"no trigger";
}
void Load(const char* path) {
    const Settings d;Settings s=settings;
    auto& a=s.appearance;
    a.opacity=ReadFloat(path,"Appearance","Opacity",d.appearance.opacity,0,1);
    a.scale=ReadFloat(path,"Appearance","Scale",d.appearance.scale,.5f,1.5f);
    a.up=ReadFloat(path,"Appearance","Up",d.appearance.up,-8,8);
    a.forward=ReadFloat(path,"Appearance","Forward",d.appearance.forward,-8,8);
    a.side=ReadFloat(path,"Appearance","Side",d.appearance.side,-8,8);
    a.depthTolerance=ReadFloat(path,"Appearance","DepthTolerance",d.appearance.depthTolerance,0,4);
    s.automatic=ReadBool(path,"Automatic","Enabled",d.automatic);
    s.revealSeconds=ReadFloat(path,"Automatic","Duration",d.revealSeconds,1,30);
    s.kills=ReadBool(path,"Automatic","Kills",d.kills);
    s.cripples=ReadBool(path,"Automatic","Cripples",d.cripples);
    s.criticals=ReadBool(path,"Automatic","Criticals",d.criticals);
    s.groin=ReadBool(path,"Automatic","Groin",d.groin);
    s.everyHit=ReadBool(path,"Automatic","EveryHit",d.everyHit);
    s.regions[TorsoRegion]=ReadBool(path,"Automatic","Torso",d.regions[TorsoRegion]);
    s.regions[LegsRegion]=ReadBool(path,"Automatic","Legs",d.regions[LegsRegion]);
    s.regions[ArmsRegion]=ReadBool(path,"Automatic","Arms",d.regions[ArmsRegion]);
    s.feralGhouls=ReadBool(path,"Automatic","FeralGhouls",d.feralGhouls);
    s.superMutants=ReadBool(path,"Automatic","SuperMutants",d.superMutants);
    s.horses=ReadBool(path,"Automatic","Horses",d.horses);
    s.bloodSpray=ReadBool(path,"Automatic","BloodSpray",d.bloodSpray);
    s.bloodAmount=ReadFloat(path,"Automatic","BloodAmount",d.bloodAmount,0,4);
    s.killSeverity=ReadFloat(path,"Automatic","KillSeverity",d.killSeverity,1,3);
    s.sounds=ReadBool(path,"Automatic","Sounds",d.sounds);
    s.soundVolume=ReadFloat(path,"Automatic","SoundVolume",d.soundVolume,0,1);
    s.nativeCamera=ReadBool(path,"Cinematic","Enabled",d.nativeCamera);
    s.cameraSeconds=ReadFloat(path,"Cinematic","Duration",d.cameraSeconds,.5f,10);
    s.impactSpeed=ReadFloat(path,"Cinematic","ImpactSpeed",d.impactSpeed,.05f,1);
    s.closeUp=ReadBool(path,"Cinematic","CloseUp",d.closeUp);
    s.closeUpDistance=ReadFloat(path,"Cinematic","CloseUpDistance",d.closeUpDistance,10,80);
    s.closeUpOrbit=ReadFloat(path,"Cinematic","CloseUpOrbit",d.closeUpOrbit,0,90);
    settings=s;
    Log("settings: automatic X-rays %s, %.1fs or as long as their killcam; %s; torso %s, legs %s, arms %s; feral ghouls %s, super mutants %s, horses %s; "
        "blood spray %s at %.1f, kill severity %.1f; gore sounds %s at %.2f; killcam %s, %.1fs timer, impact at %.2f speed; close-up %s at %.0f units, %.0f deg/s",
        s.automatic?"on":"off",s.revealSeconds,s.everyHit?"every hit":Triggers(s),s.regions[TorsoRegion]?"on":"off",s.regions[LegsRegion]?"on":"off",
        s.regions[ArmsRegion]?"on":"off",s.feralGhouls?"on":"off",s.superMutants?"on":"off",s.horses?"on":"off",s.bloodSpray?"on":"off",s.bloodAmount,s.killSeverity,
        s.sounds?"on":"off",s.soundVolume,s.nativeCamera?"on":"off",s.cameraSeconds,s.impactSpeed,s.closeUp?"on":"off",s.closeUpDistance,s.closeUpOrbit);
}
}
