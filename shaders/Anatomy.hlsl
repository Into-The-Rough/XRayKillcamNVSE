row_major float4x4 View : register(c0);
row_major float4x4 Projection : register(c4);
float4 Origin : register(c8);
float4 Head0 : register(c9);
float4 Head1 : register(c10);
float4 Head2 : register(c11);
float4 HeadPosition : register(c12);
float4 Fit : register(c13);
float4 Offset : register(c14);
#define FIT_ANCHORS 9
float4 FitCenters[FIT_ANCHORS] : register(c15);
float4 FitCoefficients[FIT_ANCHORS+4] : register(c24);
#define MAX_PIECES 64
float4 Pieces[MAX_PIECES*3] : register(c37);
//rest frame space to posed frame space for each influence, rows of scaled rotation and translation
#define INFLUENCES 7
float4 Influences[INFLUENCES*3] : register(c229);
float4 Material : register(c0);
float4 Screen : register(c1);
float4 MaskProjectionZ : register(c2);
float4 MaskProjectionW : register(c3);
float4 MaskDepth : register(c4);
float4 MaskBounds : register(c5);
float4 ShotEntry : register(c6);
float4 ShotDirection : register(c7);
float4 ShotTime : register(c8);
float4 ShotSection : register(c9);
float4 SkullWindow : register(c10);
float4 Surface : register(c11);
float4 Cap : register(c12);
float4 Bullet : register(c13);
float4 Counts : register(c14);
//x: anatomy units per pixel per unit of eye depth
float4 PixelSize : register(c15);
//skin weight of the region's bones where the reveal starts and where it is complete
float4 RevealRange : register(c16);
int BoneCount : register(i0);
int BrainCount : register(i1);
int ClusterCount : register(i2);
//bone seeds past the first table row
int BoneMore : register(i3);
sampler2D Mask : register(s0);
sampler2D Anatomy : register(s1);
sampler2D Exterior : register(s2);
//rows: bone seeds, brain seeds, clusters, more bone seeds, 256 texels each
sampler2D Table : register(s3);
#define TABLE_STEP (1.0/256)
float4 Entry(float index,float row){return tex2Dlod(Table,float4((index+.5)*TABLE_STEP,(row+.5)*.25,0,0));}

struct MaskOut {float4 position:POSITION;float3 local:TEXCOORD0;float3 eye:TEXCOORD1;float reveal:TEXCOORD2;};
MaskOut MaskVS(float3 world:POSITION,float3 local:TEXCOORD0,float reveal:TEXCOORD1) {
    MaskOut o;
    float4 eye=mul(float4(world-Origin.xyz,1),View);
    o.position=mul(eye,Projection);o.local=local;o.eye=eye.xyz;o.reveal=reveal;return o;
}
struct MaskPixel {float4 color:COLOR0;float depth:DEPTH;};
float TrackDistance(float3 local) {
    float3 delta=local-ShotEntry.xyz;
    float along=clamp(dot(delta,ShotDirection.xyz),0,ShotDirection.w*ShotTime.x);
    return length(delta-ShotDirection.xyz*along);
}
MaskPixel MaskPS(MaskOut i) {
    MaskPixel o;
    //skin moved by the region's bones is revealed, fading out where the next region takes over
    float lower=smoothstep(RevealRange.x,RevealRange.y,i.reveal);
    float grain=sin(dot(i.local,float3(1.9,2.3,1.7)))*sin(dot(i.local,float3(-3.1,1.4,2.6)));
    float3 tissue=float3(0.085,0.025,0.022)*(1+0.16*grain);
    if(ShotTime.z>0.5) {
        lower*=1-smoothstep(ShotTime.y-1,ShotTime.y,length(i.local-ShotEntry.xyz));
        float wound=1-smoothstep(ShotEntry.w,ShotSection.w*1.42,TrackDistance(i.local));
        float flash=(1-smoothstep(0,.022,ShotTime.w))*(1-smoothstep(.25,2,length(i.local-ShotEntry.xyz)));
        tissue=lerp(tissue,float3(.30,.025,.018),wound)+float3(.6,.23,.055)*flash;
    }
    o.color=float4(tissue*lower,lower);
    //bias only the depth test in game units, the silhouette and near-plane clipping stay unchanged
    float4 eye=float4(i.eye.xy,max(i.eye.z-MaskDepth.x,0.0001),1);
    float depth=saturate(dot(eye,MaskProjectionZ)/dot(eye,MaskProjectionW));
    o.depth=MaskDepth.y+depth*MaskDepth.z;
    return o;
}
struct AnatomyOut {float4 position:POSITION;float3 normal:TEXCOORD0;float3 eye:TEXCOORD1;float ao:TEXCOORD2;float3 local:TEXCOORD3;float3 donor:TEXCOORD4;};
void FitAnatomy(inout float3 p,inout float3 n) {
    float3 moved=p+FitCoefficients[FIT_ANCHORS].xyz+FitCoefficients[FIT_ANCHORS+1].xyz*p.x+FitCoefficients[FIT_ANCHORS+2].xyz*p.y+FitCoefficients[FIT_ANCHORS+3].xyz*p.z;
    float3 jx=float3(1,0,0)+FitCoefficients[FIT_ANCHORS+1].xyz;
    float3 jy=float3(0,1,0)+FitCoefficients[FIT_ANCHORS+2].xyz;
    float3 jz=float3(0,0,1)+FitCoefficients[FIT_ANCHORS+3].xyz;
    [unroll] for(int i=0;i<FIT_ANCHORS;++i) {
        float3 diff=p-FitCenters[i].xyz;
        float square=dot(diff,diff),inverse=rsqrt(square+1e-8);
        float3 weight=FitCoefficients[i].xyz;
        moved+=weight*((square+1e-8)*inverse-0.0001);
        jx+=weight*(diff.x*inverse);jy+=weight*(diff.y*inverse);jz+=weight*(diff.z*inverse);
    }
    n=normalize(cross(jy,jz)*n.x+cross(jz,jx)*n.y+cross(jx,jy)*n.z);
    p=moved;
}
AnatomyOut Project(float3 donor,float3 position,float3 normal,float ao,float bind,float bones) {
    AnatomyOut o;
    FitAnatomy(position,normal);
    float3 local=position*Fit.xyz+Offset.xyz;
    float3 n=normal/Fit.xyz;
    //linear blend of the mesh's two influences, the same skinning the game's own skin receives
    float pair=floor(bones+.5),high=floor(pair/16+.0001);
    int a=(int)(high*3),b=(int)((pair-high*16)*3);
    float4 rest=float4(local,1);
    float3 first=float3(dot(Influences[a],rest),dot(Influences[a+1],rest),dot(Influences[a+2],rest));
    float3 second=float3(dot(Influences[b],rest),dot(Influences[b+1],rest),dot(Influences[b+2],rest));
    local=lerp(second,first,bind);
    n=lerp(float3(dot(Influences[b].xyz,n),dot(Influences[b+1].xyz,n),dot(Influences[b+2].xyz,n)),
        float3(dot(Influences[a].xyz,n),dot(Influences[a+1].xyz,n),dot(Influences[a+2].xyz,n)),bind);
    float3 world=float3(dot(Head0.xyz,local),dot(Head1.xyz,local),dot(Head2.xyz,local))*HeadPosition.w+HeadPosition.xyz;
    float3 worldNormal=float3(dot(Head0.xyz,n),dot(Head1.xyz,n),dot(Head2.xyz,n));
    float4 eye=mul(float4(world-Origin.xyz,1),View);
    o.position=mul(eye,Projection);o.normal=mul(float4(worldNormal,0),View).xyz;
    o.eye=eye.xyz;o.ao=ao;o.local=local;o.donor=donor;return o;
}
AnatomyOut AnatomyVS(float3 position:POSITION,float3 normal:NORMAL,float ao:TEXCOORD0,float bind:TEXCOORD1,float bones:TEXCOORD2) {
    return Project(position,position,normal,ao,bind,bones);
}
//plates keep their rest position for classification and move rigidly in donor space before fitting
AnatomyOut PlateVS(float3 position:POSITION,float3 normal:NORMAL,float ao:TEXCOORD0,float piece:TEXCOORD1,float bind:TEXCOORD2,float bones:TEXCOORD3) {
    int i=(int)(piece+.5)*3;float4 p=float4(position,1);
    float3 moved=float3(dot(Pieces[i],p),dot(Pieces[i+1],p),dot(Pieces[i+2],p));
    float3 n=float3(dot(Pieces[i].xyz,normal),dot(Pieces[i+1].xyz,normal),dot(Pieces[i+2].xyz,normal));
    return Project(position,moved,n,ao,bind,bones);
}
//debris rows map head-space rest vertices to world relative to the camera origin
AnatomyOut DebrisVS(float3 position:POSITION,float3 normal:NORMAL,float ao:TEXCOORD0,float piece:TEXCOORD1) {
    AnatomyOut o;int i=(int)(piece+.5)*3;float4 p=float4(position,1);
    float3 relative=float3(dot(Pieces[i],p),dot(Pieces[i+1],p),dot(Pieces[i+2],p));
    float3 worldNormal=float3(dot(Pieces[i].xyz,normal),dot(Pieces[i+1].xyz,normal),dot(Pieces[i+2].xyz,normal));
    float4 eye=mul(float4(relative,1),View);
    o.position=mul(eye,Projection);o.normal=mul(float4(worldNormal,0),View).xyz;
    o.eye=eye.xyz;o.ao=ao;o.local=position;o.donor=position;return o;
}
float3 LightSurface(AnatomyOut i,float3 color,float gloss) {
    float3 n=normalize(i.normal),v=normalize(-i.eye),l=normalize(float3(-0.4,0.6,-0.8));
    n*=dot(n,v)<0?-1:1;
    float diffuse=saturate(dot(n,l));
    float spec=pow(saturate(dot(n,normalize(l+v))),gloss)*0.20;
    return color*(0.35+0.65*diffuse)*i.ao+spec;
}
//a closed outward surface seen from inside means we look through a cut into the solid
bool BackFacing(AnatomyOut i) {
    float3 g=cross(ddx(i.eye),ddy(i.eye));
    return dot(g,-i.eye)*dot(g,i.normal)<0;
}
//sine-free lattice hash keeps fp32 noise free of periodic banding
float Hash(float3 p) {
    p=frac(p*.1031);p+=dot(p,p.yzx+33.33);
    return frac((p.x+p.y)*p.z);
}
float Noise(float3 p) {
    float3 i=floor(p),f=frac(p);f=f*f*(3-2*f);
    float a=lerp(Hash(i),Hash(i+float3(1,0,0)),f.x),b=lerp(Hash(i+float3(0,1,0)),Hash(i+float3(1,1,0)),f.x);
    float c=lerp(Hash(i+float3(0,0,1)),Hash(i+float3(1,0,1)),f.x),d=lerp(Hash(i+float3(0,1,1)),Hash(i+float3(1,1,1)),f.x);
    return lerp(lerp(a,b,f.y),lerp(c,d,f.y),f.z);
}
float Wobble(float3 p) {return (Noise(p)*.65+Noise(p*2.3+7.1)*.35)*2-1;}
//one blood jet: camera-relative origin and age, direction and gravity, speeds, spread and emission time, sizes, lives, seed and stretch
float4 Jet[5] : register(c37);
float4 Droplet(float index,float salt) {
    float seed=Jet[4].x;
    return float4(Hash(float3(index,seed,salt)),Hash(float3(index+.37,salt,seed)),Hash(float3(salt,index+.71,seed)),Hash(float3(seed,salt,index+.53)));
}
//each droplet leaves within the emission time and flies ballistically, stretched along its velocity, collapsed outside its life
AnatomyOut SprayVS(float3 position:POSITION,float3 normal:NORMAL,float ao:TEXCOORD0,float piece:TEXCOORD1) {
    AnatomyOut o;
    float4 a=Droplet(piece,1.3),b=Droplet(piece,7.9);
    float life=lerp(Jet[3].z,Jet[3].w,a.x),t=Jet[0].w-a.y*Jet[2].w;
    float3 heading=normalize(Jet[1].xyz+(b.xyz*2-1)*Jet[2].z+float3(0,0,1e-4));
    float speed=lerp(Jet[2].x,Jet[2].y,a.z*a.z);
    float3 velocity=heading*speed-float3(0,0,Jet[1].w*t);
    float3 centre=Jet[0].xyz+heading*(speed*t)-float3(0,0,.5*Jet[1].w*t*t);
    float size=t>=0&&t<=life?lerp(Jet[3].x,Jet[3].y,a.w)*(1-smoothstep(life*.6,life,t)):0;
    float3 along=normalize(velocity+float3(0,0,1e-4));
    float stretch=1+length(velocity)*Jet[4].y;
    float3 relative=centre+(position+along*(dot(position,along)*(stretch-1)))*size;
    float4 eye=mul(float4(relative,1),View);
    o.position=mul(eye,Projection);o.normal=mul(float4(normal,0),View).xyz;
    o.eye=eye.xyz;o.ao=ao;o.local=position;o.donor=position;return o;
}
float3 CutSurface(float3 p) {
    float pores=Noise(p*9),soak=Noise(p*1.3+3.7);
    return lerp(Cap.rgb,Cap.rgb*float3(.62,.35,.32),soak*.7)*(.84+.16*pores);
}
float4 Decode(float w) {
    float state=floor(w*(1.0/1024)+.0001),rest=w-1024*state,cluster=floor(rest*(1.0/32)+.0001);
    return float4(state,cluster,rest-32*cluster,0);
}
float NearCluster(float3 p) {
    float nearest=1e6,index=0;
    [loop] for(int k=0;k<ClusterCount;k++) {
        float4 c=Entry(index,2);index+=1;
        if(c.w>0)nearest=min(nearest,length(p-c.xyz)/c.w);
    }
    return nearest;
}
float4 AnatomyPS(AnatomyOut i):COLOR0 {
    bool back=BackFacing(i);
    float pixelWidth=abs(i.eye.z)*PixelSize.x;
    float3 color=Material.rgb;float gloss=Material.w;
    float3 blood=float3(.42,.05,.035);
    if(ShotTime.z>0.5) {
        float track=TrackDistance(i.local);
        if(Surface.x>0.5) {
            if(ShotTime.x>0)clip(track-ShotEntry.w);
            float near=NearCluster(i.donor);
            //the cutaway keeps only the broken ring itself, cracks beyond it go with the window
            if(i.local.x>SkullWindow.w&&near>.62)clip(-dot(i.local-SkullWindow.xyz,ShotSection.xyz));
            //no breakable seed reaches more than 2.2 crack radii from its cluster, so distant bone skips the search
            [branch] if(Counts.x>.5&&near<2.2) {
                float best=1e8,second=1e8,index=0;float4 a=0,b=0;
                [loop] for(int k=0;k<BoneCount;k++) {
                    float4 s=Entry(index,0);index+=1;float3 d=i.donor-s.xyz;float q=dot(d,d);
                    if(q<best){second=best;b=a;best=q;a=s;}
                    else if(q<second){second=q;b=s;}
                }
                index=0;
                [loop] for(int m=0;m<BoneMore;m++) {
                    float4 s=Entry(index,3);index+=1;float3 d=i.donor-s.xyz;float q=dot(d,d);
                    if(q<best){second=best;b=a;best=q;a=s;}
                    else if(q<second){second=q;b=s;}
                }
                float4 ka=Decode(a.w),kb=Decode(b.w);
                bool broken=ka.x>.5&&best<ka.z*ka.z;
                if(Surface.z<0.5&&broken)clip(-1);
                if(Counts.x>1.5&&near<1.2) {
                    float gap=(second-best)/(2*max(length(b.xyz-a.xyz),.0001));
                    float width=max(.025,pixelWidth*.8);
                    bool open=kb.x>.5&&second<kb.z*kb.z;
                    //a border with broken bone on either side is a raw break: erode it into a ragged chipped edge
                    bool rough=ka.x>.5||open;
                    float chip=Noise(i.donor*6.3)*.6+Noise(i.donor*15.7)*.4,ragged=(ka.x>.5?.015:.03)+(ka.x>.5?.09:.16)*chip*chip;
                    if(rough)clip(gap-ragged);
                    float strength=ka.x<.5&&kb.x<.5?min(ka.z,kb.z):0;
                    float fade=1-smoothstep(.35,1,near),taper=1-smoothstep(0,1,near);
                    //sign by seed order so both sides agree where the wandering crack line lies
                    float side=dot(a.xyz-b.xyz,float3(1,1.618,2.414))>0?1:-1;
                    float bend=abs(gap*side+Wobble(i.donor*1.6)*.07);
                    float crack=strength*fade*(1-smoothstep(width*.35,width*(.6+.6*taper),bend));
                    float stain=(rough?1-smoothstep(ragged,ragged+.09,gap):0)*smoothstep(.25,.7,Noise(i.donor*5.2));
                    stain=max(stain,(1-smoothstep(.1,.55,near))*.4*smoothstep(.45,.8,Noise(i.donor*1.4+11)));
                    color=lerp(color,blood,saturate(stain));
                    color=lerp(color,float3(.09,.025,.018),saturate(crack));
                }
            }
        } else if(Surface.y>0.5) {
            float best=1e8,index=0;float4 a=0;
            [loop] for(int k=0;k<BrainCount;k++) {
                float4 s=Entry(index,1);index+=1;float3 d=i.donor-s.xyz;float q=dot(d,d);
                if(q<best){best=q;a=s;}
            }
            if(Counts.y>.5) {
                float reach=Decode(a.w).z,distance=sqrt(best);
                if(Surface.z<0.5)clip(distance-reach);
                float bruise=1-saturate((distance-reach)/1.1);
                color=lerp(color,float3(.36,.05,.035),bruise*bruise*.85);
                color=lerp(color,float3(.16,.02,.015),1-smoothstep(.02,.2,distance-reach));
            }
        }
        float3 tip=ShotEntry.xyz+ShotDirection.xyz*(ShotDirection.w*ShotTime.x);
        if(ShotTime.x>0&&ShotTime.x<1) {
            float3 delta=i.local-tip;
            float3 projected=ShotDirection.xyz-ShotSection.xyz*dot(ShotDirection.xyz,ShotSection.xyz);
            float body=clamp(-dot(delta,projected)/max(dot(projected,projected),1e-6),0,Bullet.x);
            delta+=ShotDirection.xyz*body;
            float toward=dot(delta,ShotSection.xyz);
            if(toward> -ShotSection.w)clip(length(delta-ShotSection.xyz*toward)-max(.34,ShotSection.w*1.4));
        }
    }
    //plates keep their rest coordinates so the pores ride with the moving piece
    [branch] if(back){color=CutSurface(Surface.z>.5?i.donor:i.local);gloss=Cap.w;}
    return float4(LightSurface(i,color,gloss),1);
}
float4 AnatomyExteriorPS(AnatomyOut i,float2 pixel:VPOS):COLOR0 {
    clip(.001-tex2D(Mask,(pixel+.5)*Screen.xy).a);
    AnatomyPS(i);
    return 1;
}
AnatomyOut BulletVS(float3 position:POSITION,float3 normal:NORMAL,float ao:TEXCOORD0) {
    AnatomyOut o;
    float3 world=float3(dot(Head0.xyz,position),dot(Head1.xyz,position),dot(Head2.xyz,position))*HeadPosition.w+HeadPosition.xyz;
    float3 worldNormal=float3(dot(Head0.xyz,normal),dot(Head1.xyz,normal),dot(Head2.xyz,normal));
    float4 eye=mul(float4(world-Origin.xyz,1),View);
    o.position=mul(eye,Projection);o.normal=mul(float4(worldNormal,0),View).xyz;
    o.eye=eye.xyz;o.ao=ao;o.local=position;o.donor=position;return o;
}
float4 BulletPS(AnatomyOut i):COLOR0 {
    return float4(LightSurface(i,float3(.94,.63,.25),90)+float3(.08,.035,.006),1);
}
//the sharp highlight of a wet surface, amount 0 for dry tissue
float3 Wet(AnatomyOut i,float amount) {
    float3 n=normalize(i.normal),v=normalize(-i.eye),l=normalize(float3(-0.4,0.6,-0.8));
    n*=dot(n,v)<0?-1:1;
    return pow(saturate(dot(n,normalize(l+v))),40)*.5*amount;
}
float4 DamagePS(AnatomyOut i):COLOR0 {
    bool back=BackFacing(i);
    float3 color=back?CutSurface(i.local):Material.rgb*(.94+.06*Wobble(i.local*2.3));
    float wet=Surface.w;
    //torn soft tissue leaves soaked in blood
    if(Surface.y>.5&&Surface.z>.5){float soak=smoothstep(.3,.75,Noise(i.local*2.1+5.3))*.75;color=lerp(color,float3(.46,.03,.02),soak);wet=soak;}
    return float4(LightSurface(i,color,back?Cap.w:Material.w)+Wet(i,wet),1);
}
//blood inside the window shows through the anatomy, the way the X-ray shows bone
float4 SprayThroughPS(AnatomyOut i,float2 pixel:VPOS):COLOR0 {
    clip(tex2D(Mask,(pixel+.5)*Screen.xy).a-.001);
    return float4(LightSurface(i,Material.rgb*(.94+.06*Wobble(i.local*2.3)),Material.w)+Wet(i,Surface.w),1);
}
float4 ExteriorPS(AnatomyOut i,float2 pixel:VPOS):COLOR0 {
    clip(.001-tex2D(Mask,(pixel+.5)*Screen.xy).a);
    return 1;
}
struct QuadOut {float4 position:POSITION;float2 uv:TEXCOORD0;};
QuadOut CompositeVS(float2 position:POSITION,float2 uv:TEXCOORD0) {
    QuadOut o;o.position=float4(position,0,1);o.uv=uv;return o;
}
float4 CompositePS(QuadOut i):COLOR0 {
    float4 mask=tex2D(Mask,i.uv);
    float exterior=tex2D(Exterior,i.uv).a;
    clip(max(mask.a,exterior)-0.001);
    float4 a=tex2D(Anatomy,i.uv);
    float surround=0;
    const float2 directions[4]={float2(1,0),float2(-1,0),float2(0,1),float2(0,-1)};
    [unroll] for(int n=0;n<4;++n) {
        float2 uv=i.uv+directions[n]*Screen.xy*1.5;
        float inside=all(uv>=MaskBounds.xy)&&all(uv<=MaskBounds.zw);
        surround+=tex2D(Mask,uv).a*inside*0.25;
    }
    float3 tissue=mask.rgb/max(mask.a,0.0001);
    float3 interior=lerp(tissue,a.rgb,a.a);
    float rim=saturate((1-surround)*2);
    float3 color=lerp(interior,float3(0.24,0.075,0.055),rim*0.65);
    float coverage=mask.a*surround;
    float outside=exterior*(1-coverage),alpha=coverage+outside;
    color=(color*coverage+a.rgb*outside)/max(alpha,.0001);
    return float4(color,alpha*Screen.z);
}
