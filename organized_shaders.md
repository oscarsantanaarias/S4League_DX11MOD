# Shader / FX Dump — Organized

> **12 shaders** · Original offsets preserved.

## Contents

1. [`023e3a28`](#023e3a28)
2. [`023e33d8`](#023e33d8)
3. [`023e4198`](#023e4198)
4. [`023f8288`](#023f8288)
5. [`023e26b8`](#023e26b8)
6. [`023f7b50`](#023f7b50)
7. [`023f9a30`](#023f9a30)
8. [`023f9870`](#023f9870)
9. [`023e2c38`](#023e2c38)
10. [`023f9f18`](#023f9f18)
11. [`023e8160`](#023e8160)
12. [`023e4590`](#023e4590)

---

## 1. `023e3a28`

**Shaders / functions:** `VS_BLUR`, `PS_BLUR`

**Techniques:** `main`

```hlsl
matrix g_matWVP;
texture g_TexDiffuseMap;
sampler DiffuseMapSampler = sampler_state {
 Texture = ;
 MipFilter = LINEAR;
 MinFilter = LINEAR;
 MagFilter = LINEAR;
 AddressU = CLAMP;
 AddressV = CLAMP;
}
;
struct sPS_BLUR {
 float4 Position : POSITION;
 float2 TexCoord0 : TEXCOORD0;
 float2 TexCoord1 : TEXCOORD1;
 float2 TexCoord2 : TEXCOORD2;
 float2 TexCoord3 : TEXCOORD3;
 float2 TexCoord4 : TEXCOORD4;
 float2 TexCoord5 : TEXCOORD5;
 float2 TexCoord6 : TEXCOORD6;
 float2 TexCoord7 : TEXCOORD7;
}
;
sPS_BLUR VS_BLUR ( float4 InPosition : POSITION, float2 InDiffuseMapUV : TEXCOORD0 ) {
 sPS_BLUR Out = ( sPS_BLUR )0;
 Out.Position = mul( InPosition, g_matWVP );
 Out.TexCoord0 = InDiffuseMapUV + float2( 0, 0.005 );
 Out.TexCoord1 = InDiffuseMapUV + float2( 0, -0.005 );
 Out.TexCoord2 = InDiffuseMapUV + float2( 0.005, 0 );
 Out.TexCoord3 = InDiffuseMapUV + float2( -0.005, 0 );
 Out.TexCoord4 = InDiffuseMapUV + float2( 0.005, 0.005 );
 Out.TexCoord5 = InDiffuseMapUV + float2( 0.005, -0.005 );
 Out.TexCoord6 = InDiffuseMapUV + float2( -0.005, 0.005 );
 Out.TexCoord7 = InDiffuseMapUV + float2( -0.005, -0.005 );
 return Out;
}
float4 PS_BLUR( sPS_BLUR In ) : COLOR {
 float4 FinalColor = ( float4 )0;
 FinalColor += tex2D( DiffuseMapSampler, In.TexCoord0 );
 FinalColor += tex2D( DiffuseMapSampler, In.TexCoord1 );
 FinalColor += tex2D( DiffuseMapSampler, In.TexCoord2 );
 FinalColor += tex2D( DiffuseMapSampler, In.TexCoord3 );
 FinalColor += tex2D( DiffuseMapSampler, In.TexCoord4 );
 FinalColor += tex2D( DiffuseMapSampler, In.TexCoord5 );
 FinalColor += tex2D( DiffuseMapSampler, In.TexCoord6 );
 FinalColor += tex2D( DiffuseMapSampler, In.TexCoord7 );
 return FinalColor * 0.2;
}
technique main {
 pass p0 {
 VertexShader = compile vs_2_0 VS_BLUR();
 PixelShader = compile ps_2_0 PS_BLUR();
 }
}
```

---

## 2. `023e33d8`

**Shaders / functions:** `VS_BLUR`, `PS_BLUR`

**Techniques:** `main`

```hlsl
matrix g_matWVP;
texture g_TexDiffuseMap;
float g_fColorRev;
float g_fOrgColorRev;
float g_fPeriColorRev;
sampler DiffuseMapSampler = sampler_state {
 Texture = ;
 MipFilter = LINEAR;
 MinFilter = LINEAR;
 MagFilter = LINEAR;
 AddressU = CLAMP;
 AddressV = CLAMP;
}
;
struct sPS_BLUR {
 float4 Position : POSITION;
 float2 TexCoord0 : TEXCOORD0;
 float2 TexCoord1 : TEXCOORD1;
 float2 TexCoord2 : TEXCOORD2;
 float2 TexCoord3 : TEXCOORD3;
 float2 TexCoord4 : TEXCOORD4;
}
;
sPS_BLUR VS_BLUR ( float4 InPosition : POSITION, float2 InDiffuseMapUV : TEXCOORD0 ) {
 sPS_BLUR Out = ( sPS_BLUR )0;
 Out.Position = mul( InPosition, g_matWVP );
 Out.TexCoord0 = InDiffuseMapUV;
 Out.TexCoord1 = InDiffuseMapUV + float2( 0, 0.005 );
 Out.TexCoord2 = InDiffuseMapUV + float2( 0, -0.005 );
 Out.TexCoord3 = InDiffuseMapUV + float2( 0.005, 0 );
 Out.TexCoord4 = InDiffuseMapUV + float2( -0.005, 0 );
 return Out;
}
float4 PS_BLUR( sPS_BLUR In ) : COLOR {
 float4 FinalColor = ( float4 )0;
 FinalColor += tex2D( DiffuseMapSampler, In.TexCoord0 ) * g_fOrgColorRev;
 FinalColor += tex2D( DiffuseMapSampler, In.TexCoord1 ) * g_fPeriColorRev;
 FinalColor += tex2D( DiffuseMapSampler, In.TexCoord2 ) * g_fPeriColorRev;
 FinalColor += tex2D( DiffuseMapSampler, In.TexCoord3 ) * g_fPeriColorRev;
 FinalColor += tex2D( DiffuseMapSampler, In.TexCoord4 ) * g_fPeriColorRev;
 FinalColor *= g_fColorRev;
 return FinalColor;
}
technique main {
 pass p0 {
 VertexShader = compile vs_2_0 VS_BLUR();
 PixelShader = compile ps_2_0 PS_BLUR();
 }
}
```

---

## 3. `023e4198`

**Shaders / functions:** `VS_DiffuseMap_ShadeMap_Light1`

**Techniques:** `Technique_DiffuseMap_ShadeMap_Light1`

```hlsl
matrix g_matWVP;
matrix g_matWorld;
float3 g_vEyePos;
float3 g_vLightPos;
float4 g_vLightColor;
struct sPS_DiffuseMap_ShadeMap_Light1 {
 float4 Position : POSITION;
 float2 DiffuseMapUV : TEXCOORD0;
 float2 ShadeMapUV : TEXCOORD1;
}
;
sPS_DiffuseMap_ShadeMap_Light1 VS_DiffuseMap_ShadeMap_Light1 ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0 ) {
 sPS_DiffuseMap_ShadeMap_Light1 Out = ( sPS_DiffuseMap_ShadeMap_Light1 )0;
 float3 tmpPos = ( float3 )mul( InPosition, g_matWorld );
 float3 tmpNormal = normalize( mul( InNormal, (float3x3)g_matWorld ) );
 float3 L = normalize( g_vLightPos - tmpPos );
 Out.Position = mul( InPosition, g_matWVP );
 Out.DiffuseMapUV = InDiffuseMapUV;
 Out.ShadeMapUV = 0.5 * dot( tmpNormal, L ) + 0.5;
 return Out;
}
technique Technique_DiffuseMap_ShadeMap_Light1 {
 pass p0 {
 VertexShader = compile vs_1_1 VS_DiffuseMap_ShadeMap_Light1();
 PixelShader = null;
 }
}
```

---

## 4. `023f8288`

**Shaders / functions:** `VS_DiffuseMap_Rigid`, `VS_DiffuseMap_Skinned`, `VS_DiffuseMap_BumpMap_Rigid`, `VS_DiffuseMap_BumpMap_Skinned`, `VS_FX`, `PS_DiffuseMap`

**Techniques:** `Technique_DiffuseMap_Rigid`, `Technique_DiffuseMap_Skinned`, `Technique_DiffuseMap_BumpMap_Rigid`, `Technique_DiffuseMap_BumpMap_Skinned`, `Technique_FX`

```hlsl
matrix g_matWVP;
matrix g_matViewProj;
matrix g_matBone[48];
matrix g_matTexture;
float3 g_LuminanceConv = {
 0.2125, 0.7154, 0.0721
}
;
texture g_TexSceneMap;
texture g_TexDiffuseMap;
sampler SceneMapSampler = sampler_state {
 Texture = ;
 AddressU = CLAMP;
 AddressV = CLAMP;
 AddressW = CLAMP;
 MIPFILTER = LINEAR;
 MINFILTER = LINEAR;
 MAGFILTER = LINEAR;
}
;
sampler DiffuseMapSampler = sampler_state {
 Texture = ;
 AddressU = CLAMP;
 AddressV = CLAMP;
 AddressW = CLAMP;
 MIPFILTER = LINEAR;
 MINFILTER = LINEAR;
 MAGFILTER = LINEAR;
}
;
struct sPS_DiffuseMap {
 float4 Position : POSITION;
 float2 DiffuseMapUV : TEXCOORD0;
 float4 ScreenMapUV : TEXCOORD1;
}
;
sPS_DiffuseMap VS_DiffuseMap_Rigid ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0 ) {
 sPS_DiffuseMap Out = ( sPS_DiffuseMap )0;
 Out.Position = mul( InPosition, g_matWVP );
 Out.DiffuseMapUV = InDiffuseMapUV;
 Out.ScreenMapUV = mul( InPosition, g_matTexture );
 return Out;
}
sPS_DiffuseMap VS_DiffuseMap_Skinned ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0, float4 InBoneIndex : TEXCOORD1, float4 InBoneWeight : TEXCOORD2 ) {
 sPS_DiffuseMap Out = ( sPS_DiffuseMap )0;
 float3 tmpPos = ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.x ] ) * InBoneWeight.x;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.y ] ) * InBoneWeight.y;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.z ] ) * InBoneWeight.z;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.w ] ) * InBoneWeight.w;
 Out.Position = mul( float4( tmpPos, 1 ), g_matViewProj );
 Out.DiffuseMapUV = InDiffuseMapUV;
 Out.ScreenMapUV = mul( InPosition, g_matTexture );
 return Out;
}
sPS_DiffuseMap VS_DiffuseMap_BumpMap_Rigid ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0, float3 InTangentS : TEXCOORD1 ) {
 sPS_DiffuseMap Out = ( sPS_DiffuseMap )0;
 Out.Position = mul( InPosition, g_matWVP );
 Out.DiffuseMapUV = InDiffuseMapUV;
 Out.ScreenMapUV = mul( InPosition, g_matTexture );
 return Out;
}
sPS_DiffuseMap VS_DiffuseMap_BumpMap_Skinned ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0, float3 InTangentS : TEXCOORD1, float4 InBoneIndex : TEXCOORD2, float4 InBoneWeight : TEXCOORD3 ) {
 sPS_DiffuseMap Out = ( sPS_DiffuseMap )0;
 float3 tmpPos = ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.x ] ) * InBoneWeight.x;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.y ] ) * InBoneWeight.y;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.z ] ) * InBoneWeight.z;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.w ] ) * InBoneWeight.w;
 Out.Position = mul( float4( tmpPos, 1 ), g_matViewProj );
 Out.DiffuseMapUV = InDiffuseMapUV;
 Out.ScreenMapUV = mul( InPosition, g_matTexture );
 return Out;
}
sPS_DiffuseMap VS_FX ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float4 InDiffuseColor : COLOR, float2 InDiffuseMapUV : TEXCOORD0 ) {
 sPS_DiffuseMap Out = ( sPS_DiffuseMap )0;
 Out.Position = mul( InPosition, g_matWVP );
 Out.DiffuseMapUV = InDiffuseMapUV;
 Out.ScreenMapUV = mul( InPosition, g_matTexture );
 return Out;
}
float4 PS_DiffuseMap( sPS_DiffuseMap In ) : COLOR {
 float4 yComponent;
 yComponent.x = dot( ( float3 )tex2D( DiffuseMapSampler, In.DiffuseMapUV + float2( -0.01, 0.0 ) ), g_LuminanceConv );
 yComponent.y = dot( ( float3 )tex2D( DiffuseMapSampler, In.DiffuseMapUV + float2( 0.01, 0.0 ) ), g_LuminanceConv );
 yComponent.z = dot( ( float3 )tex2D( DiffuseMapSampler, In.DiffuseMapUV + float2( 0.0, -0.01 ) ), g_LuminanceConv );
 yComponent.w = dot( ( float3 )tex2D( DiffuseMapSampler, In.DiffuseMapUV + float2( 0.0, 0.01 ) ), g_LuminanceConv );
 float3 Normal = normalize( float3( yComponent.x - yComponent.y, yComponent.z - yComponent.w, 0.05 ) );
 float4 FinalColor = tex2Dproj( SceneMapSampler, float4( Normal.x * 0.015 + In.ScreenMapUV.x, Normal.z * 0.015 + In.ScreenMapUV.y, In.ScreenMapUV.z, In.ScreenMapUV.w + Normal.y * 35 ) );
 return FinalColor;
}
technique Technique_DiffuseMap_Rigid {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap();
 FogEnable = false;
 AlphaBlendEnable = false;
 }
}
technique Technique_DiffuseMap_Skinned {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap();
 FogEnable = false;
 AlphaBlendEnable = false;
 }
}
technique Technique_DiffuseMap_BumpMap_Rigid {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap();
 FogEnable = false;
 AlphaBlendEnable = false;
 }
}
technique Technique_DiffuseMap_BumpMap_Skinned {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap();
 FogEnable = false;
 AlphaBlendEnable = false;
 }
}
technique Technique_FX {
 pass p0 {
 VertexShader = compile vs_2_0 VS_FX();
 PixelShader = compile ps_2_0 PS_DiffuseMap();
 FogEnable = false;
 AlphaBlendEnable = false;
 }
}
```

---

## 5. `023e26b8`

**Shaders / functions:** `VS_TextureNoise`, `VS_ProjectiveShadow`, `VS_ProjectiveShadowCast`

**Techniques:** `Technique_TextureNoise`, `Technique_ProjectiveShadow`, `Technique_ProjectiveShadowCast`

```hlsl
matrix g_matWVP;
matrix g_matTexture;
struct sPS {
 float4 Position : POSITION;
 float4 DiffuseColor : COLOR0;
 float2 TexCoord0 : TEXCOORD0;
}
;
sPS VS_TextureNoise ( float4 InPosition : POSITION, float2 InDiffuseMapUV : TEXCOORD0 ) {
 sPS Out = ( sPS )0;
 Out.Position = mul( InPosition, g_matWVP );
 Out.DiffuseColor = float4( 1, 1, 1, 1 );
 Out.TexCoord0 = mul( InDiffuseMapUV, ( float3x3 )g_matTexture ).xy;
 return Out;
}
sPS VS_ProjectiveShadow ( float4 InPosition : POSITION, float4 InDiffuseColor : COLOR0, float2 InDiffuseMapUV : TEXCOORD0 ) {
 sPS Out = ( sPS )0;
 Out.Position = mul( InPosition, g_matWVP );
 Out.DiffuseColor = InDiffuseColor;
 Out.TexCoord0 = InDiffuseMapUV;
 return Out;
}
sPS VS_ProjectiveShadowCast ( float4 InPosition : POSITION ) {
 sPS Out = ( sPS )0;
 Out.Position = mul( InPosition, g_matWVP );
 Out.TexCoord0 = mul( InPosition, g_matTexture ).xy;
 return Out;
}
technique Technique_TextureNoise {
 pass p0 {
 VertexShader = compile vs_1_1 VS_TextureNoise();
 PixelShader = null;
 }
}
technique Technique_ProjectiveShadow {
 pass p0 {
 VertexShader = compile vs_1_1 VS_ProjectiveShadow();
 PixelShader = null;
 }
}
technique Technique_ProjectiveShadowCast {
 pass p0 {
 VertexShader = compile vs_1_1 VS_ProjectiveShadowCast();
 PixelShader = null;
 }
}
```

---

## 6. `023f7b50`

**Shaders / functions:** `VS`, `PS`

**Techniques:** `main`

```hlsl
matrix g_matWVP;
float3 g_LuminanceConv = {
 0.2125, 0.7154, 0.0721
}
;
texture g_TexSceneMap;
texture g_TexDiffuseMap;
sampler SceneMapSampler = sampler_state {
 Texture = ;
 AddressU = CLAMP;
 AddressV = CLAMP;
 AddressW = CLAMP;
 MIPFILTER = LINEAR;
 MINFILTER = LINEAR;
 MAGFILTER = LINEAR;
}
;
sampler DiffuseMapSampler = sampler_state {
 Texture = ;
 AddressU = CLAMP;
 AddressV = CLAMP;
 AddressW = CLAMP;
 MIPFILTER = LINEAR;
 MINFILTER = LINEAR;
 MAGFILTER = LINEAR;
}
;
struct sPS {
 float4 Position : POSITION;
 float2 DiffuseMapUV : TEXCOORD0;
}
;
sPS VS ( float4 InPosition : POSITION, float2 InDiffuseMapUV : TEXCOORD0 ) {
 sPS Out = ( sPS )0;
 Out.Position = mul( InPosition, g_matWVP );
 Out.DiffuseMapUV = InDiffuseMapUV;
 return Out;
}
float4 PS( sPS In ) : COLOR {
 float4 FinalColor = 0;
 float4 DiffuseColor = tex2D( DiffuseMapSampler, In.DiffuseMapUV );
 float4 yComponent;
 yComponent.x= dot( ( float3 )tex2D( DiffuseMapSampler, In.DiffuseMapUV.xy + float2( -0.01, 0.0 ) ), g_LuminanceConv );
 yComponent.y= dot( ( float3 )tex2D( DiffuseMapSampler, In.DiffuseMapUV.xy + float2( 0.01, 0.0 ) ), g_LuminanceConv );
 yComponent.z= dot( ( float3 )tex2D( DiffuseMapSampler, In.DiffuseMapUV.xy + float2( 0.0, -0.01 ) ), g_LuminanceConv );
 yComponent.w= dot( ( float3 )tex2D( DiffuseMapSampler, In.DiffuseMapUV.xy + float2( 0.0, 0.01 ) ), g_LuminanceConv );
 float3 Normal = normalize( float3( yComponent.x - yComponent.y, yComponent.z - yComponent.w, 0.05 ) );
 FinalColor = tex2D( SceneMapSampler, float2( Normal.x * 0.015 + In.DiffuseMapUV.x, Normal.z * 0.015 + In.DiffuseMapUV.y ) );
 FinalColor.a= 1;
 return FinalColor;
}
technique main {
 pass p0 {
 VertexShader = compile vs_2_0 VS();
 PixelShader = compile ps_2_0 PS();
 }
}
```

---

## 7. `023f9a30`

**Shaders / functions:** `VS`, `PS`

**Techniques:** `main`

```hlsl
matrix g_matWVP;
float2 g_TexCoordOffset;
texture g_TexDiffuseMap;
texture g_TexBumpMap;
sampler DiffuseMapSampler = sampler_state {
 Texture = ;
 MipFilter = LINEAR;
 MinFilter = LINEAR;
 MagFilter = LINEAR;
 AddressU = WRAP;
 AddressV = WRAP;
}
;
sampler BumpMapSampler = sampler_state {
 Texture = ;
 MipFilter = LINEAR;
 MinFilter = LINEAR;
 MagFilter = LINEAR;
 AddressU = WRAP;
 AddressV = WRAP;
}
;
struct sPS {
 float4 Position : POSITION;
 float2 TexCoord0 : TEXCOORD0;
 float2 TexCoord1 : TEXCOORD1;
}
;
sPS VS ( float4 InPosition : POSITION, float2 InDiffuseMapUV : TEXCOORD0 ) {
 sPS Out = ( sPS )0;
 Out.Position = mul( InPosition, g_matWVP );
 Out.TexCoord0 = InDiffuseMapUV + g_TexCoordOffset;
 Out.TexCoord1 = InDiffuseMapUV + float2( g_TexCoordOffset.x * 2, g_TexCoordOffset.y );
 return Out;
}
float4 PS( sPS In ) : COLOR {
 float4 BumpColor = tex2D( BumpMapSampler, In.TexCoord1 );
 BumpColor.xy = ( BumpColor.xy - 0.5 ) * 2;
 float4 FinalColor = tex2D( DiffuseMapSampler, In.TexCoord0 + ( BumpColor.xy * 0.02 ) );
 return FinalColor;
}
technique main {
 pass p0 {
 VertexShader = compile vs_2_0 VS();
 PixelShader = compile ps_2_0 PS();
 }
}
```

---

## 8. `023f9870`

**Shaders / functions:** `VS`

**Techniques:** `main`

```hlsl
matrix g_matWVP;
float2 g_TexCoordOffset;
struct sPS {
 float4 Position : POSITION;
 float2 TexCoord0 : TEXCOORD0;
}
;
sPS VS ( float4 InPosition : POSITION, float2 InDiffuseMapUV : TEXCOORD0 ) {
 sPS Out = ( sPS )0;
 Out.Position = mul( InPosition, g_matWVP );
 Out.TexCoord0 = InDiffuseMapUV + g_TexCoordOffset;
 return Out;
}
technique main {
 pass p0 {
 VertexShader = compile vs_1_1 VS();
 PixelShader = null;
 }
}
```

---

## 9. `023e2c38`

**Shaders / functions:** `VS_BLUR`, `PS_BLUR`

**Techniques:** `main`

```hlsl
matrix g_matWVP;
float g_BlurFactor;
texture g_TexDiffuseMap;
texture g_TexBlurMap;
sampler DiffuseMapSampler = sampler_state {
 Texture = ;
 MipFilter = LINEAR;
 MinFilter = LINEAR;
 MagFilter = LINEAR;
 AddressU = CLAMP;
 AddressV = CLAMP;
}
;
sampler BlurMapSampler = sampler_state {
 Texture = ;
 MipFilter = LINEAR;
 MinFilter = LINEAR;
 MagFilter = LINEAR;
 AddressU = CLAMP;
 AddressV = CLAMP;
}
;
struct sPS_BLUR {
 float4 Position : POSITION;
 float2 TexCoord0 : TEXCOORD0;
 float2 TexCoord1 : TEXCOORD1;
 float2 TexCoord2 : TEXCOORD2;
 float2 TexCoord3 : TEXCOORD3;
 float2 TexCoord4 : TEXCOORD4;
 float2 TexCoord5 : TEXCOORD5;
}
;
sPS_BLUR VS_BLUR ( float4 InPosition : POSITION, float2 InDiffuseMapUV : TEXCOORD0 ) {
 sPS_BLUR Out = ( sPS_BLUR )0;
 Out.Position = mul( InPosition, g_matWVP );
 float x = Out.Position.x * -0.005;
 float y = Out.Position.y * 0.005;
 Out.TexCoord0 = InDiffuseMapUV;
 Out.TexCoord1 = InDiffuseMapUV + float2( x, y );
 Out.TexCoord2 = InDiffuseMapUV + float2( x * 1.5, y * 1.5 );
 Out.TexCoord3 = InDiffuseMapUV + float2( x * 2, y * 2 );
 Out.TexCoord4 = InDiffuseMapUV + float2( x * 2.5, y * 2.5 );
 Out.TexCoord5 = InDiffuseMapUV + float2( x * 3, y * 3 );
 return Out;
}
float4 PS_BLUR( sPS_BLUR In ) : COLOR {
 float4 FinalColor = ( float4 )0;
 FinalColor += tex2D( DiffuseMapSampler, In.TexCoord1 );
 FinalColor += tex2D( DiffuseMapSampler, In.TexCoord2 );
 FinalColor += tex2D( DiffuseMapSampler, In.TexCoord3 );
 FinalColor += tex2D( DiffuseMapSampler, In.TexCoord4 );
 FinalColor += tex2D( DiffuseMapSampler, In.TexCoord5 );
 FinalColor *= 0.2;
 FinalColor.a = tex2D( BlurMapSampler, In.TexCoord0 ).a * g_BlurFactor;
 return FinalColor;
}
technique main {
 pass p0 {
 VertexShader = compile vs_2_0 VS_BLUR();
 PixelShader = compile ps_2_0 PS_BLUR();
 }
}
```

---

## 10. `023f9f18`

**Shaders / functions:** `VS_DiffuseMap_Rigid`, `VS_DiffuseMap_Skinned`, `VS_DiffuseMap_BumpMap_Rigid`, `VS_DiffuseMap_BumpMap_Skinned`, `PS_DiffuseMap_BumpMap_RefractionMap`

**Techniques:** `Technique_DiffuseMap_Rigid`, `Technique_DiffuseMap_Skinned`, `Technique_DiffuseMap_BumpMap_Rigid`, `Technique_DiffuseMap_BumpMap_Skinned`

```hlsl
matrix g_matBone[48];
matrix g_matWVP;
matrix g_matViewProj;
matrix g_matTexture;
float4 g_TexFactor;
float2 g_TexCoordOffset;
float g_DiffuseColorWeight;
texture g_TexDiffuseMap;
texture g_TexBumpMap;
texture g_TexRefractionMap;
sampler DiffuseMapSampler = sampler_state {
 Texture = ;
 MipFilter = LINEAR;
 MinFilter = LINEAR;
 MagFilter = LINEAR;
 AddressU = WRAP;
 AddressV = WRAP;
}
;
sampler BumpMapSampler = sampler_state {
 Texture = ;
 MipFilter = LINEAR;
 MinFilter = LINEAR;
 MagFilter = LINEAR;
 AddressU = WRAP;
 AddressV = WRAP;
}
;
sampler RefractionMapSampler = sampler_state {
 Texture = ;
 MipFilter = LINEAR;
 MinFilter = LINEAR;
 MagFilter = LINEAR;
 AddressU = WRAP;
 AddressV = WRAP;
}
;
struct sPS_DiffuseMap_BumpMap_RefractionMap {
 float4 Position : POSITION;
 float2 DiffuseMapUV : TEXCOORD0;
 float4 RefractionMapUV : TEXCOORD1;
}
;
sPS_DiffuseMap_BumpMap_RefractionMap VS_DiffuseMap_Rigid ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0 ) {
 sPS_DiffuseMap_BumpMap_RefractionMap Out = ( sPS_DiffuseMap_BumpMap_RefractionMap )0;
 Out.Position = mul( InPosition, g_matWVP );
 Out.DiffuseMapUV = InDiffuseMapUV;
 Out.RefractionMapUV = mul( Out.Position, g_matTexture );
 return Out;
}
sPS_DiffuseMap_BumpMap_RefractionMap VS_DiffuseMap_Skinned ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0, float4 InBoneIndex : TEXCOORD1, float4 InBoneWeight : TEXCOORD2 ) {
 sPS_DiffuseMap_BumpMap_RefractionMap Out = ( sPS_DiffuseMap_BumpMap_RefractionMap )0;
 float3 tmpPos = ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.x ] ) * InBoneWeight.x;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.y ] ) * InBoneWeight.y;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.z ] ) * InBoneWeight.z;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.w ] ) * InBoneWeight.w;
 Out.Position = mul( float4( tmpPos, 1 ), g_matViewProj );
 Out.DiffuseMapUV = InDiffuseMapUV;
 Out.RefractionMapUV = mul( Out.Position, g_matTexture );
 return Out;
}
sPS_DiffuseMap_BumpMap_RefractionMap VS_DiffuseMap_BumpMap_Rigid ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0, float3 InTangentS : TEXCOORD1 ) {
 sPS_DiffuseMap_BumpMap_RefractionMap Out = ( sPS_DiffuseMap_BumpMap_RefractionMap )0;
 Out.Position = mul( InPosition, g_matWVP );
 Out.DiffuseMapUV = InDiffuseMapUV;
 Out.RefractionMapUV = mul( Out.Position, g_matTexture );
 return Out;
}
sPS_DiffuseMap_BumpMap_RefractionMap VS_DiffuseMap_BumpMap_Skinned ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0, float3 InTangentS : TEXCOORD1, float4 InBoneIndex : TEXCOORD2, float4 InBoneWeight : TEXCOORD3 ) {
 sPS_DiffuseMap_BumpMap_RefractionMap Out = ( sPS_DiffuseMap_BumpMap_RefractionMap )0;
 float3 tmpPos = ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.x ] ) * InBoneWeight.x;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.y ] ) * InBoneWeight.y;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.z ] ) * InBoneWeight.z;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.w ] ) * InBoneWeight.w;
 Out.Position = mul( float4( tmpPos, 1 ), g_matViewProj );
 Out.DiffuseMapUV = InDiffuseMapUV;
 Out.RefractionMapUV = mul( Out.Position, g_matTexture );
 return Out;
}
float4 PS_DiffuseMap_BumpMap_RefractionMap( sPS_DiffuseMap_BumpMap_RefractionMap In ) : COLOR {
 float4 BumpMapColor = tex2D( BumpMapSampler, In.DiffuseMapUV + g_TexCoordOffset );
 BumpMapColor.xy = ( BumpMapColor.xy - 0.5 ) * 2;
 BumpMapColor.zw = 0;
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DiffuseMapUV + BumpMapColor * 0.02 );
 float4 RefractionMapColor = tex2Dproj( RefractionMapSampler, In.RefractionMapUV + BumpMapColor );
 float4 FinalColor = ( DiffuseMapColor * g_DiffuseColorWeight + RefractionMapColor * ( 1 - g_DiffuseColorWeight ) ) * g_TexFactor;
 FinalColor.a = 0.5;
 return FinalColor;
}
technique Technique_DiffuseMap_Rigid {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_RefractionMap();
 FogEnable = false;
 }
}
technique Technique_DiffuseMap_Skinned {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_RefractionMap();
 FogEnable = false;
 }
}
technique Technique_DiffuseMap_BumpMap_Rigid {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_RefractionMap();
 FogEnable = false;
 }
}
technique Technique_DiffuseMap_BumpMap_Skinned {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_RefractionMap();
 FogEnable = false;
 }
}
```

---

## 11. `023e8160`

**Shaders / functions:** `VS_FX`, `VS_DiffuseMap_LightMap`, `VS_DiffuseMap_LightMap_SpecularMap`, `VS_DiffuseMap_LightMap_Low`, `VS_DiffuseMap_Light0_Rigid`, `VS_DiffuseMap_Light0_Skinned`, `VS_DiffuseMap_Light1_Rigid`, `VS_DiffuseMap_Light1_Skinned`, `VS_DiffuseMap_SpecularMap_Light1_Rigid`, `VS_DiffuseMap_SpecularMap_Light1_Skinned`, `VS_DiffuseMap_BumpMap_Light0_Rigid`, `VS_DiffuseMap_BumpMap_Light0_Skinned`, `VS_DiffuseMap_BumpMap_Light1_Rigid`, `VS_DiffuseMap_BumpMap_Light1_Rigid_Low`, `VS_DiffuseMap_BumpMap_Light1_Skinned`, `VS_DiffuseMap_BumpMap_SpecularMap_Light1_Rigid`, `VS_DiffuseMap_BumpMap_SpecularMap_Light1_Skinned`, `VS_DiffuseMap_BumpMap_Light1_Skinned_Low`, `VS_DiffuseMap_BumpMap_SpecularMap_Light1_Skinned_Low`, `PS_FX`, `PS_FX_ContrastSaturation`, `PS_DiffuseMap_LightMap`, `PS_DiffuseMap_LightMap_ContrastSaturation`, `PS_DiffuseMap_LightMap_SpecularMap`, `PS_DiffuseMap_LightMap_SpecularMap_ContrastSaturation`, `PS_DiffuseMap_Light0`, `PS_DiffuseMap_Light0_ContrastSaturation`, `PS_DiffuseMap_Light0_TextureFactor`, `PS_DiffuseMap_Light0_TextureFactor_ContrastSaturation`, `PS_DiffuseMap_Light1`, `PS_DiffuseMap_Light1_ContrastSaturation`, `PS_DiffuseMap_Light1_TextureFactor`, `PS_DiffuseMap_Light1_TextureFactor_ContrastSaturation`, `PS_DiffuseMap_SpecularMap_Light1`, `PS_DiffuseMap_SpecularMap_Light1_ContrastSaturation`, `PS_DiffuseMap_SpecularMap_Light1_TextureFactor`, `PS_DiffuseMap_SpecularMap_Light1_TextureFactor_ContrastSaturation`, `PS_DiffuseMap_ShadeMap_Light1`, `PS_DiffuseMap_ShadeMap_Light1_ContrastSaturation`, `PS_DiffuseMap_ShadeMap_Light1_TextureFactor`, `PS_DiffuseMap_ShadeMap_Light1_TextureFactor_ContrastSaturation`, `PS_DiffuseMap_SpecularMap_ShadeMap_Light1`, `PS_DiffuseMap_SpecularMap_ShadeMap_Light1_ContrastSaturation`, `PS_DiffuseMap_SpecularMap_ShadeMap_Light1_TextureFactor`, `PS_DiffuseMap_SpecularMap_ShadeMap_Light1_TextureFactor_ContrastSaturation`, `PS_DiffuseMap_BumpMap_Light1`, `PS_DiffuseMap_BumpMap_Light1_ContrastSaturation`, `PS_DiffuseMap_BumpMap_Light1_TextureFactor`, `PS_DiffuseMap_BumpMap_Light1_TextureFactor_ContrastSaturation`, `PS_DiffuseMap_BumpMap_SpecularMap_Light1`, `PS_DiffuseMap_BumpMap_SpecularMap_Light1_ContrastSaturation`, `PS_DiffuseMap_BumpMap_SpecularMap_Light1_TextureFactor`, `PS_DiffuseMap_BumpMap_SpecularMap_Light1_TextureFactor_ContrastSaturation`, `PS_DiffuseMap_BumpMap_ShadeMap_Light1`, `PS_DiffuseMap_BumpMap_ShadeMap_Light1_ContrastSaturation`, `PS_DiffuseMap_BumpMap_ShadeMap_Light1_TextureFactor`, `PS_DiffuseMap_BumpMap_ShadeMap_Light1_TextureFactor_ContrastSaturation`, `PS_DiffuseMap_BumpMap_SpecularMap_ShadeMap_Light1`, `PS_DiffuseMap_BumpMap_SpecularMap_ShadeMap_Light1_ContrastSaturation`, `PS_DiffuseMap_BumpMap_SpecularMap_ShadeMap_Light1_TextureFactor`, `PS_DiffuseMap_BumpMap_SpecularMap_ShadeMap_Light1_TextureFactor_ContrastSaturation`

**Techniques:** `Technique_FX`, `Technique_DiffuseMap_LightMap`, `Technique_DiffuseMap_LightMap_SpecularMap`, `Technique_DiffuseMap_Light0_Rigid`, `Technique_DiffuseMap_Light0_Skinned`, `Technique_DiffuseMap_BumpMap_Light0_Rigid`, `Technique_DiffuseMap_BumpMap_Light0_Skinned`, `Technique_DiffuseMap_Light1_Rigid`, `Technique_DiffuseMap_Light1_Skinned`, `Technique_DiffuseMap_SpecularMap_Light1_Rigid`, `Technique_DiffuseMap_SpecularMap_Light1_Skinned`, `Technique_DiffuseMap_ShadeMap_Light1_Rigid`, `Technique_DiffuseMap_ShadeMap_Light1_Skinned`, `Technique_DiffuseMap_SpecularMap_ShadeMap_Light1_Rigid`, `Technique_DiffuseMap_SpecularMap_ShadeMap_Light1_Skinned`, `Technique_DiffuseMap_BumpMap_Light1_Rigid`, `Technique_DiffuseMap_BumpMap_Light1_Skinned`, `Technique_DiffuseMap_BumpMap_SpecularMap_Light1_Rigid`, `Technique_DiffuseMap_BumpMap_SpecularMap_Light1_Skinned`, `Technique_DiffuseMap_BumpMap_ShadeMap_Light1_Rigid`, `Technique_DiffuseMap_BumpMap_ShadeMap_Light1_Skinned`, `Technique_DiffuseMap_BumpMap_SpecularMap_ShadeMap_Light1_Rigid`, `Technique_DiffuseMap_BumpMap_SpecularMap_ShadeMap_Light1_Skinned`, `Technique_DiffuseMap_BumpMap_Light1_Skinned_Low`, `Technique_DiffuseMap_BumpMap_SpecularMap_Light1_Skinned_Low`, `Technique_DiffuseMap_BumpMap_ShadeMap_Light1_Skinned_Low`, `Technique_DiffuseMap_BumpMap_SpecularMap_ShadeMap_Light1_Skinned_Low`, `Technique_DiffuseMap_LightMap_Low`, `Technique_DiffuseMap_BumpMap_Light1_Rigid_Low`

```hlsl
int g_SpecularShiness = 16;
float g_BumpDepth = 0.75;
matrix g_matBone[48];
matrix g_matWVP;
matrix g_matWorld;
matrix g_matViewProj;
float4 g_TexFactor;
float3 g_vEyePos;
float3 g_vLightPos;
float4 g_vLightColor;
texture g_TexDiffuseMap;
texture g_TexLightMap;
texture g_TexBumpMap;
texture g_TexSpecularMap;
texture g_TexShadeMap;
float g_Contrast;
float g_Saturation;
float3 g_SaturationConst = float3( 0.2126, 0.7152, 0.0722 );
sampler DiffuseMapSampler = sampler_state {
 Texture = ;
 MipFilter = LINEAR;
 MinFilter = LINEAR;
 MagFilter = LINEAR;
 AddressU = WRAP;
 AddressV = WRAP;
}
;
sampler LightMapSampler = sampler_state {
 Texture = ;
 MipFilter = LINEAR;
 MinFilter = LINEAR;
 MagFilter = LINEAR;
 AddressU = WRAP;
 AddressV = WRAP;
}
;
sampler BumpMapSampler = sampler_state {
 Texture = ;
 MipFilter = LINEAR;
 MinFilter = LINEAR;
 MagFilter = LINEAR;
 AddressU = WRAP;
 AddressV = WRAP;
}
;
sampler SpecularMapSampler = sampler_state {
 Texture = ;
 MipFilter = LINEAR;
 MinFilter = LINEAR;
 MagFilter = LINEAR;
 AddressU = WRAP;
 AddressV = WRAP;
}
;
sampler ShadeMapSampler = sampler_state {
 Texture = ;
 MipFilter = LINEAR;
 MinFilter = LINEAR;
 MagFilter = LINEAR;
 AddressU = CLAMP;
 AddressV = CLAMP;
}
;
struct sPS_FX {
 float4 Pos : POSITION;
 float4 DiffuseColor : COLOR;
 float2 DifMapUV : TEXCOORD0;
}
;
struct sPS_DiffuseMap_LightMap {
 float4 Pos : POSITION;
 float2 DifMapUV : TEXCOORD0;
 float2 LightMapUV : TEXCOORD1;
}
;
struct sPS_DiffuseMap_LightMap_SpecularMap {
 float4 Pos : POSITION;
 float2 DifMapUV : TEXCOORD0;
 float2 LightMapUV : TEXCOORD1;
 float SpePower : TEXCOORD2;
}
;
struct sPS_DiffuseMap_Light0 {
 float4 Pos : POSITION;
 float2 DifMapUV : TEXCOORD0;
}
;
struct sPS_DiffuseMap_Light1 {
 float4 Pos : POSITION;
 float2 DifMapUV : TEXCOORD0;
 float DifPow : TEXCOORD1;
}
;
struct sPS_DiffuseMap_SpecularMap_Light1 {
 float4 Pos : POSITION;
 float2 DifMapUV : TEXCOORD0;
 float DifPow : TEXCOORD1;
 float SpePower : TEXCOORD2;
}
;
struct sPS_DiffuseMap_BumpMap_Light1 {
 float4 Pos : POSITION;
 float2 DifMapUV : TEXCOORD0;
 float3 L : TEXCOORD1;
}
;
struct sPS_DiffuseMap_BumpMap_SpecularMap_Light1 {
 float4 Pos : POSITION;
 float2 DifMapUV : TEXCOORD0;
 float3 L : TEXCOORD1;
 float3 H : TEXCOORD2;
}
;
sPS_FX VS_FX ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float4 InDiffuseColor : COLOR, float2 InDiffuseMapUV : TEXCOORD0 ) {
 sPS_FX Out = ( sPS_FX )0;
 Out.Pos = mul( InPosition, g_matWVP );
 Out.DifMapUV = InDiffuseMapUV;
 Out.DiffuseColor = InDiffuseColor;
 return Out;
}
sPS_DiffuseMap_LightMap VS_DiffuseMap_LightMap ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0, float2 InLightMapUV : TEXCOORD1 ) {
 sPS_DiffuseMap_LightMap Out = ( sPS_DiffuseMap_LightMap )0;
 Out.Pos = mul( InPosition, g_matWVP );
 Out.DifMapUV = InDiffuseMapUV;
 Out.LightMapUV = InLightMapUV;
 return Out;
}
sPS_DiffuseMap_LightMap_SpecularMap VS_DiffuseMap_LightMap_SpecularMap ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0, float2 InLightMapUV : TEXCOORD1 ) {
 sPS_DiffuseMap_LightMap_SpecularMap Out = ( sPS_DiffuseMap_LightMap_SpecularMap )0;
 float3 tmpPos = mul( InPosition, g_matWorld );
 float3 tmpNormal = normalize( mul( InNormal, (float3x3)g_matWorld ) );
 float3 L = normalize( g_vLightPos - tmpPos );
 float3 V = normalize( g_vEyePos - tmpPos );
 float3 H = normalize( L + V );
 Out.Pos = mul( InPosition, g_matWVP );
 Out.DifMapUV = InDiffuseMapUV;
 Out.LightMapUV = InLightMapUV;
 Out.SpePower = pow( max( dot( tmpNormal, H ), 0 ), g_SpecularShiness );
 return Out;
}
sPS_DiffuseMap_Light0 VS_DiffuseMap_LightMap_Low( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0, float2 InLightMapUV : TEXCOORD1 ) {
 sPS_DiffuseMap_Light0 Out = ( sPS_DiffuseMap_Light0 )0;
 Out.Pos = mul( InPosition, g_matWVP );
 Out.DifMapUV = InDiffuseMapUV;
 return Out;
}
sPS_DiffuseMap_Light0 VS_DiffuseMap_Light0_Rigid ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0 ) {
 sPS_DiffuseMap_Light0 Out = ( sPS_DiffuseMap_Light0 )0;
 Out.Pos = mul( InPosition, g_matWVP );
 Out.DifMapUV = InDiffuseMapUV;
 return Out;
}
sPS_DiffuseMap_Light0 VS_DiffuseMap_Light0_Skinned ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0, float4 InBoneIndex : TEXCOORD1, float4 InBoneWeight : TEXCOORD2 ) {
 sPS_DiffuseMap_Light0 Out = ( sPS_DiffuseMap_Light0 )0;
 float3 tmpPos = ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.x ] ) * InBoneWeight.x;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.y ] ) * InBoneWeight.y;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.z ] ) * InBoneWeight.z;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.w ] ) * InBoneWeight.w;
 Out.Pos = mul( float4( tmpPos, 1 ), g_matViewProj );
 Out.DifMapUV = InDiffuseMapUV;
 return Out;
}
sPS_DiffuseMap_Light1 VS_DiffuseMap_Light1_Rigid ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0 ) {
 sPS_DiffuseMap_Light1 Out = ( sPS_DiffuseMap_Light1 )0;
 float3 tmpPos = mul( InPosition, g_matWorld );
 float3 tmpNormal = normalize( mul( InNormal, (float3x3)g_matWorld ) );
 float3 L = normalize( g_vLightPos - tmpPos );
 Out.Pos = mul( InPosition, g_matWVP );
 Out.DifMapUV = InDiffuseMapUV;
 Out.DifPow = 0.5 * dot( tmpNormal, L ) + 0.5;
 return Out;
}
sPS_DiffuseMap_Light1 VS_DiffuseMap_Light1_Skinned ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0, float4 InBoneIndex : TEXCOORD1, float4 InBoneWeight : TEXCOORD2 ) {
 sPS_DiffuseMap_Light1 Out = ( sPS_DiffuseMap_Light1 )0;
 float3 tmpPos = ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.x ] ) * InBoneWeight.x;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.y ] ) * InBoneWeight.y;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.z ] ) * InBoneWeight.z;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.w ] ) * InBoneWeight.w;
 float3 tmpNormal = ( float3 )mul( float4( InNormal, 1 ), ( float3x3 )g_matBone[ ( int )InBoneIndex.x ] ) * InBoneWeight.x;
 tmpNormal += ( float3 )mul( float4( InNormal, 1 ), ( float3x3 )g_matBone[ ( int )InBoneIndex.y ] ) * InBoneWeight.y;
 tmpNormal += ( float3 )mul( float4( InNormal, 1 ), ( float3x3 )g_matBone[ ( int )InBoneIndex.z ] ) * InBoneWeight.z;
 tmpNormal += ( float3 )mul( float4( InNormal, 1 ), ( float3x3 )g_matBone[ ( int )InBoneIndex.w ] ) * InBoneWeight.w;
 tmpNormal = normalize( tmpNormal );
 float3 L = normalize( g_vLightPos - tmpPos );
 Out.Pos = mul( float4( tmpPos, 1 ), g_matViewProj );
 Out.DifMapUV = InDiffuseMapUV;
 Out.DifPow = 0.5 * dot( tmpNormal, L ) + 0.5;
 return Out;
}
sPS_DiffuseMap_SpecularMap_Light1 VS_DiffuseMap_SpecularMap_Light1_Rigid ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0 ) {
 sPS_DiffuseMap_SpecularMap_Light1 Out= ( sPS_DiffuseMap_SpecularMap_Light1 )0;
 float3 tmpPos = mul( InPosition, g_matWorld );
 float3 tmpNormal = normalize( mul( InNormal, (float3x3)g_matWorld ) );
 float3 L = normalize( g_vLightPos - tmpPos );
 float3 V = normalize( g_vEyePos - tmpPos );
 float3 H = normalize( L + V );
 Out.Pos = mul( InPosition, g_matWVP );
 Out.DifMapUV = InDiffuseMapUV;
 Out.DifPow = 0.5 * dot( tmpNormal, L ) + 0.5;
 Out.SpePower = pow( max( dot( tmpNormal, H ), 0 ), g_SpecularShiness );
 return Out;
}
sPS_DiffuseMap_SpecularMap_Light1 VS_DiffuseMap_SpecularMap_Light1_Skinned ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0, float4 InBoneIndex : TEXCOORD1, float4 InBoneWeight : TEXCOORD2 ) {
 sPS_DiffuseMap_SpecularMap_Light1 Out= ( sPS_DiffuseMap_SpecularMap_Light1 )0;
 float3 tmpPos = ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.x ] ) * InBoneWeight.x;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.y ] ) * InBoneWeight.y;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.z ] ) * InBoneWeight.z;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.w ] ) * InBoneWeight.w;
 float3 tmpNormal = ( float3 )mul( float4( InNormal, 1 ), ( float3x3 )g_matBone[ ( int )InBoneIndex.x ] ) * InBoneWeight.x;
 tmpNormal += ( float3 )mul( float4( InNormal, 1 ), ( float3x3 )g_matBone[ ( int )InBoneIndex.y ] ) * InBoneWeight.y;
 tmpNormal += ( float3 )mul( float4( InNormal, 1 ), ( float3x3 )g_matBone[ ( int )InBoneIndex.z ] ) * InBoneWeight.z;
 tmpNormal += ( float3 )mul( float4( InNormal, 1 ), ( float3x3 )g_matBone[ ( int )InBoneIndex.w ] ) * InBoneWeight.w;
 tmpNormal = normalize( tmpNormal );
 float3 L = normalize( g_vLightPos - tmpPos );
 float3 V = normalize( g_vEyePos - tmpPos );
 float3 H = normalize( L + V );
 Out.Pos = mul( float4( tmpPos, 1 ), g_matViewProj );
 Out.DifMapUV = InDiffuseMapUV;
 Out.DifPow = 0.5 * dot( tmpNormal, L ) + 0.5;
 Out.SpePower = pow( max( dot( tmpNormal, H ), 0 ), g_SpecularShiness );
 return Out;
}
sPS_DiffuseMap_Light0 VS_DiffuseMap_BumpMap_Light0_Rigid ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0, float3 InTangentS : TEXCOORD1 ) {
 sPS_DiffuseMap_Light0 Out = ( sPS_DiffuseMap_Light0 )0;
 Out.Pos = mul( InPosition, g_matWVP );
 Out.DifMapUV = InDiffuseMapUV;
 return Out;
}
sPS_DiffuseMap_Light0 VS_DiffuseMap_BumpMap_Light0_Skinned ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0, float3 InTangentS : TEXCOORD1, float4 InBoneIndex : TEXCOORD2, float4 InBoneWeight : TEXCOORD3 ) {
 sPS_DiffuseMap_Light0 Out = ( sPS_DiffuseMap_Light0 )0;
 float3 tmpPos = ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.x ] ) * InBoneWeight.x;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.y ] ) * InBoneWeight.y;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.z ] ) * InBoneWeight.z;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.w ] ) * InBoneWeight.w;
 Out.Pos = mul( float4( tmpPos, 1 ), g_matViewProj );
 Out.DifMapUV = InDiffuseMapUV;
 return Out;
}
sPS_DiffuseMap_BumpMap_Light1 VS_DiffuseMap_BumpMap_Light1_Rigid ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0, float3 InTangentS : TEXCOORD1 ) {
 sPS_DiffuseMap_BumpMap_Light1 Out = ( sPS_DiffuseMap_BumpMap_Light1 )0;
 float3 tmpPos = mul( InPosition, g_matWorld );
 float3 tmpNormal = normalize( mul( InNormal, (float3x3)g_matWorld ) );
 float3x3 matTBN = float3x3( InTangentS, cross( InTangentS, tmpNormal ), tmpNormal );
 float3 L = normalize( g_vLightPos - tmpPos );
 Out.Pos = mul( InPosition, g_matWVP );
 Out.DifMapUV = InDiffuseMapUV;
 Out.L = normalize( mul( matTBN, L ) );
 return Out;
}
sPS_DiffuseMap_Light1 VS_DiffuseMap_BumpMap_Light1_Rigid_Low( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0, float3 InTangentS : TEXCOORD1 ) {
 sPS_DiffuseMap_Light1 Out = ( sPS_DiffuseMap_Light1 )0;
 float3 tmpPos = mul( InPosition, g_matWorld );
 float3 tmpNormal = normalize( mul( InNormal, (float3x3)g_matWorld ) );
 float3x3 matTBN = float3x3( InTangentS, cross( InTangentS, tmpNormal ), tmpNormal );
 float3 L = normalize( g_vLightPos - tmpPos );
 Out.Pos = mul( InPosition, g_matWVP );
 Out.DifMapUV = InDiffuseMapUV;
 Out.DifPow = 0.5 * dot( tmpNormal, L ) + 0.5;
 return Out;
}
sPS_DiffuseMap_BumpMap_Light1 VS_DiffuseMap_BumpMap_Light1_Skinned ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0, float3 InTangentS : TEXCOORD1, float4 InBoneIndex : TEXCOORD2, float4 InBoneWeight : TEXCOORD3 ) {
 sPS_DiffuseMap_BumpMap_Light1 Out = ( sPS_DiffuseMap_BumpMap_Light1 )0;
 float3 tmpPos = ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.x ] ) * InBoneWeight.x;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.y ] ) * InBoneWeight.y;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.z ] ) * InBoneWeight.z;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.w ] ) * InBoneWeight.w;
 float3 tmpNormal = ( float3 )mul( float4( InNormal, 1 ), ( float3x3 )g_matBone[ ( int )InBoneIndex.x ] ) * InBoneWeight.x;
 tmpNormal += ( float3 )mul( float4( InNormal, 1 ), ( float3x3 )g_matBone[ ( int )InBoneIndex.y ] ) * InBoneWeight.y;
 tmpNormal += ( float3 )mul( float4( InNormal, 1 ), ( float3x3 )g_matBone[ ( int )InBoneIndex.z ] ) * InBoneWeight.z;
 tmpNormal += ( float3 )mul( float4( InNormal, 1 ), ( float3x3 )g_matBone[ ( int )InBoneIndex.w ] ) * InBoneWeight.w;
 tmpNormal = normalize( tmpNormal );
 float3x3 matTBN = float3x3( InTangentS, cross( InTangentS, tmpNormal ), tmpNormal );
 float3 L = normalize( g_vLightPos - tmpPos );
 Out.Pos = mul( float4( tmpPos, 1 ), g_matViewProj );
 Out.DifMapUV = InDiffuseMapUV;
 Out.L = normalize( mul( matTBN, L ) );
 return Out;
}
sPS_DiffuseMap_BumpMap_SpecularMap_Light1 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Rigid ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0, float3 InTangentS : TEXCOORD1 ) {
 sPS_DiffuseMap_BumpMap_SpecularMap_Light1 Out = ( sPS_DiffuseMap_BumpMap_SpecularMap_Light1 )0;
 float3 tmpPos = mul( InPosition, g_matWorld );
 float3 tmpNormal = normalize( mul( InNormal, (float3x3)g_matWorld ) );
 float3x3 matTBN = float3x3( InTangentS, cross( InTangentS, tmpNormal ), tmpNormal );
 float3 L = normalize( g_vLightPos - tmpPos );
 float3 V = normalize( g_vEyePos - tmpPos );
 float3 H = normalize( L + V );
 Out.Pos = mul( InPosition, g_matWVP );
 Out.DifMapUV = InDiffuseMapUV;
 Out.L = normalize( mul( matTBN, L ) );
 Out.H = normalize( mul( matTBN, H ) );
 return Out;
}
sPS_DiffuseMap_BumpMap_SpecularMap_Light1 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Skinned ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0, float3 InTangentS : TEXCOORD1, float4 InBoneIndex : TEXCOORD2, float4 InBoneWeight : TEXCOORD3 ) {
 sPS_DiffuseMap_BumpMap_SpecularMap_Light1 Out = ( sPS_DiffuseMap_BumpMap_SpecularMap_Light1 )0;
 float3 tmpPos = ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.x ] ) * InBoneWeight.x;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.y ] ) * InBoneWeight.y;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.z ] ) * InBoneWeight.z;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.w ] ) * InBoneWeight.w;
 float3 tmpNormal = ( float3 )mul( float4( InNormal, 1 ), ( float3x3 )g_matBone[ ( int )InBoneIndex.x ] ) * InBoneWeight.x;
 tmpNormal += ( float3 )mul( float4( InNormal, 1 ), ( float3x3 )g_matBone[ ( int )InBoneIndex.y ] ) * InBoneWeight.y;
 tmpNormal += ( float3 )mul( float4( InNormal, 1 ), ( float3x3 )g_matBone[ ( int )InBoneIndex.z ] ) * InBoneWeight.z;
 tmpNormal += ( float3 )mul( float4( InNormal, 1 ), ( float3x3 )g_matBone[ ( int )InBoneIndex.w ] ) * InBoneWeight.w;
 tmpNormal = normalize( tmpNormal );
 float3x3 matTBN = float3x3( InTangentS, cross( InTangentS, tmpNormal ), tmpNormal );
 float3 L = normalize( g_vLightPos - tmpPos );
 float3 V = normalize( g_vEyePos - tmpPos );
 float3 H = normalize( L + V );
 Out.Pos = mul( float4( tmpPos, 1 ), g_matViewProj );
 Out.DifMapUV = InDiffuseMapUV;
 Out.L = normalize( mul( matTBN, L ) );
 Out.H = normalize( mul( matTBN, H ) );
 return Out;
}
sPS_DiffuseMap_Light1 VS_DiffuseMap_BumpMap_Light1_Skinned_Low ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0, float3 InTangentS : TEXCOORD1, float4 InBoneIndex : TEXCOORD2, float4 InBoneWeight : TEXCOORD3 ) {
 sPS_DiffuseMap_Light1 Out = ( sPS_DiffuseMap_Light1 )0;
 float3 tmpPos = ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.x ] ) * InBoneWeight.x;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.y ] ) * InBoneWeight.y;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.z ] ) * InBoneWeight.z;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.w ] ) * InBoneWeight.w;
 float3 tmpNormal = ( float3 )mul( float4( InNormal, 1 ), ( float3x3 )g_matBone[ ( int )InBoneIndex.x ] ) * InBoneWeight.x;
 tmpNormal += ( float3 )mul( float4( InNormal, 1 ), ( float3x3 )g_matBone[ ( int )InBoneIndex.y ] ) * InBoneWeight.y;
 tmpNormal += ( float3 )mul( float4( InNormal, 1 ), ( float3x3 )g_matBone[ ( int )InBoneIndex.z ] ) * InBoneWeight.z;
 tmpNormal += ( float3 )mul( float4( InNormal, 1 ), ( float3x3 )g_matBone[ ( int )InBoneIndex.w ] ) * InBoneWeight.w;
 tmpNormal = normalize( tmpNormal );
 float3 L = normalize( g_vLightPos - tmpPos );
 Out.Pos = mul( float4( tmpPos, 1 ), g_matViewProj );
 Out.DifMapUV = InDiffuseMapUV;
 Out.DifPow = 0.5 * dot( tmpNormal, L ) + 0.5;
 return Out;
}
sPS_DiffuseMap_SpecularMap_Light1 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Skinned_Low ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0, float3 InTangentS : TEXCOORD1, float4 InBoneIndex : TEXCOORD2, float4 InBoneWeight : TEXCOORD3 ) {
 sPS_DiffuseMap_SpecularMap_Light1 Out= ( sPS_DiffuseMap_SpecularMap_Light1 )0;
 float3 tmpPos = ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.x ] ) * InBoneWeight.x;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.y ] ) * InBoneWeight.y;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.z ] ) * InBoneWeight.z;
 tmpPos += ( float3 )mul( InPosition, ( float4x3 )g_matBone[ ( int )InBoneIndex.w ] ) * InBoneWeight.w;
 float3 tmpNormal = ( float3 )mul( float4( InNormal, 1 ), ( float3x3 )g_matBone[ ( int )InBoneIndex.x ] ) * InBoneWeight.x;
 tmpNormal += ( float3 )mul( float4( InNormal, 1 ), ( float3x3 )g_matBone[ ( int )InBoneIndex.y ] ) * InBoneWeight.y;
 tmpNormal += ( float3 )mul( float4( InNormal, 1 ), ( float3x3 )g_matBone[ ( int )InBoneIndex.z ] ) * InBoneWeight.z;
 tmpNormal += ( float3 )mul( float4( InNormal, 1 ), ( float3x3 )g_matBone[ ( int )InBoneIndex.w ] ) * InBoneWeight.w;
 tmpNormal = normalize( tmpNormal );
 float3 L = normalize( g_vLightPos - tmpPos );
 float3 V = normalize( g_vEyePos - tmpPos );
 float3 H = normalize( L + V );
 Out.Pos = mul( float4( tmpPos, 1 ), g_matViewProj );
 Out.DifMapUV = InDiffuseMapUV;
 Out.DifPow = 0.5 * dot( tmpNormal, L ) + 0.5;
 Out.SpePower = pow( max( dot( tmpNormal, H ), 0 ), g_SpecularShiness );
 return Out;
}
float4 PS_FX( sPS_FX In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 Final = DiffuseMapColor * In.DiffuseColor;
 return Final;
}
float4 PS_FX_ContrastSaturation( sPS_FX In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 Final = DiffuseMapColor * In.DiffuseColor;
 Final.rgb = Final.rgb - g_Contrast * ( Final.rgb - 1.0f ) * Final.rgb * ( Final.rgb - 2.0f );
 float Lum = dot( Final.rgb, g_SaturationConst );
 Final.rgb = saturate( lerp( Lum.xxx, Final.rgb, g_Saturation ) );
 return Final;
}
float4 PS_DiffuseMap_LightMap( sPS_DiffuseMap_LightMap In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 LightMapColor = tex2D( LightMapSampler, In.LightMapUV );
 float4 Final = DiffuseMapColor * ( LightMapColor * 2 );
 Final.a = DiffuseMapColor.a;
 return Final;
}
float4 PS_DiffuseMap_LightMap_ContrastSaturation( sPS_DiffuseMap_LightMap In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 LightMapColor = tex2D( LightMapSampler, In.LightMapUV );
 float4 Final = DiffuseMapColor * ( LightMapColor * 2 );
 Final.a = DiffuseMapColor.a;
 Final.rgb = Final.rgb - g_Contrast * ( Final.rgb - 1.0f ) * Final.rgb * ( Final.rgb - 2.0f );
 float Lum = dot( Final.rgb, g_SaturationConst );
 Final.rgb = saturate( lerp( Lum.xxx, Final.rgb, g_Saturation ) );
 return Final;
}
float4 PS_DiffuseMap_LightMap_SpecularMap( sPS_DiffuseMap_LightMap_SpecularMap In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 LightMapColor = tex2D( LightMapSampler, In.LightMapUV );
 float4 SpecularMapColor = tex2D( SpecularMapSampler, In.DifMapUV );
 float4 Final = DiffuseMapColor * ( LightMapColor * 2 ) + ( SpecularMapColor * In.SpePower );
 Final.a = DiffuseMapColor.a;
 return Final;
}
float4 PS_DiffuseMap_LightMap_SpecularMap_ContrastSaturation( sPS_DiffuseMap_LightMap_SpecularMap In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 LightMapColor = tex2D( LightMapSampler, In.LightMapUV );
 float4 SpecularMapColor = tex2D( SpecularMapSampler, In.DifMapUV );
 float4 Final = DiffuseMapColor * ( LightMapColor * 2 ) + ( SpecularMapColor * In.SpePower );
 Final.a = DiffuseMapColor.a;
 Final.rgb = Final.rgb - g_Contrast * ( Final.rgb - 1.0f ) * Final.rgb * ( Final.rgb - 2.0f );
 float Lum = dot( Final.rgb, g_SaturationConst );
 Final.rgb = saturate( lerp( Lum.xxx, Final.rgb, g_Saturation ) );
 return Final;
}
float4 PS_DiffuseMap_Light0( sPS_DiffuseMap_Light0 In ) : COLOR {
 float4 Final = tex2D( DiffuseMapSampler, In.DifMapUV );
 return Final;
}
float4 PS_DiffuseMap_Light0_ContrastSaturation( sPS_DiffuseMap_Light0 In ) : COLOR {
 float4 Final = tex2D( DiffuseMapSampler, In.DifMapUV );
 Final.rgb = Final.rgb - g_Contrast * ( Final.rgb - 1.0f ) * Final.rgb * ( Final.rgb - 2.0f );
 float Lum = dot( Final.rgb, g_SaturationConst );
 Final.rgb = saturate( lerp( Lum.xxx, Final.rgb, g_Saturation ) );
 return Final;
}
float4 PS_DiffuseMap_Light0_TextureFactor( sPS_DiffuseMap_Light0 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 Final = DiffuseMapColor * g_TexFactor;
 return Final;
}
float4 PS_DiffuseMap_Light0_TextureFactor_ContrastSaturation( sPS_DiffuseMap_Light0 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 Final = DiffuseMapColor * g_TexFactor;
 Final.rgb = Final.rgb - g_Contrast * ( Final.rgb - 1.0f ) * Final.rgb * ( Final.rgb - 2.0f );
 float Lum = dot( Final.rgb, g_SaturationConst );
 Final.rgb = saturate( lerp( Lum.xxx, Final.rgb, g_Saturation ) );
 return Final;
}
float4 PS_DiffuseMap_Light1( sPS_DiffuseMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 Final = DiffuseMapColor * ( g_vLightColor * ( In.DifPow + 0.5 ) );
 Final.a = DiffuseMapColor.a;
 return Final;
}
float4 PS_DiffuseMap_Light1_ContrastSaturation( sPS_DiffuseMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 Final = DiffuseMapColor * ( g_vLightColor * ( In.DifPow + 0.5 ) );
 Final.a = DiffuseMapColor.a;
 Final.rgb = Final.rgb - g_Contrast * ( Final.rgb - 1.0f ) * Final.rgb * ( Final.rgb - 2.0f );
 float Lum = dot( Final.rgb, g_SaturationConst );
 Final.rgb = saturate( lerp( Lum.xxx, Final.rgb, g_Saturation ) );
 return Final;
}
float4 PS_DiffuseMap_Light1_TextureFactor( sPS_DiffuseMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 Final = DiffuseMapColor * ( g_vLightColor * ( In.DifPow + 0.5 ) ) * g_TexFactor;
 Final.a = DiffuseMapColor.a * g_TexFactor.a;
 return Final;
}
float4 PS_DiffuseMap_Light1_TextureFactor_ContrastSaturation( sPS_DiffuseMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 Final = DiffuseMapColor * ( g_vLightColor * ( In.DifPow + 0.5 ) ) * g_TexFactor;
 Final.a = DiffuseMapColor.a * g_TexFactor.a;
 Final.rgb = Final.rgb - g_Contrast * ( Final.rgb - 1.0f ) * Final.rgb * ( Final.rgb - 2.0f );
 float Lum = dot( Final.rgb, g_SaturationConst );
 Final.rgb = saturate( lerp( Lum.xxx, Final.rgb, g_Saturation ) );
 return Final;
}
float4 PS_DiffuseMap_SpecularMap_Light1( sPS_DiffuseMap_SpecularMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 SpecularMapColor = tex2D( SpecularMapSampler, In.DifMapUV );
 float4 Final = DiffuseMapColor * ( g_vLightColor * ( In.DifPow + 0.5 ) ) + ( SpecularMapColor * In.SpePower );
 Final.a = DiffuseMapColor.a;
 return Final;
}
float4 PS_DiffuseMap_SpecularMap_Light1_ContrastSaturation( sPS_DiffuseMap_SpecularMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 SpecularMapColor = tex2D( SpecularMapSampler, In.DifMapUV );
 float4 Final = DiffuseMapColor * ( g_vLightColor * ( In.DifPow + 0.5 ) ) + ( SpecularMapColor * In.SpePower );
 Final.a = DiffuseMapColor.a;
 Final.rgb = Final.rgb - g_Contrast * ( Final.rgb - 1.0f ) * Final.rgb * ( Final.rgb - 2.0f );
 float Lum = dot( Final.rgb, g_SaturationConst );
 Final.rgb = saturate( lerp( Lum.xxx, Final.rgb, g_Saturation ) );
 return Final;
}
float4 PS_DiffuseMap_SpecularMap_Light1_TextureFactor( sPS_DiffuseMap_SpecularMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 SpecularMapColor = tex2D( SpecularMapSampler, In.DifMapUV );
 float4 Final = ( DiffuseMapColor * ( g_vLightColor * ( In.DifPow + 0.5 ) ) + ( SpecularMapColor * In.SpePower ) ) * g_TexFactor;
 Final.a = DiffuseMapColor.a * g_TexFactor.a;
 return Final;
}
float4 PS_DiffuseMap_SpecularMap_Light1_TextureFactor_ContrastSaturation( sPS_DiffuseMap_SpecularMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 SpecularMapColor = tex2D( SpecularMapSampler, In.DifMapUV );
 float4 Final = ( DiffuseMapColor * ( g_vLightColor * ( In.DifPow + 0.5 ) ) + ( SpecularMapColor * In.SpePower ) ) * g_TexFactor;
 Final.a = DiffuseMapColor.a * g_TexFactor.a;
 Final.rgb = Final.rgb - g_Contrast * ( Final.rgb - 1.0f ) * Final.rgb * ( Final.rgb - 2.0f );
 float Lum = dot( Final.rgb, g_SaturationConst );
 Final.rgb = saturate( lerp( Lum.xxx, Final.rgb, g_Saturation ) );
 return Final;
}
float4 PS_DiffuseMap_ShadeMap_Light1( sPS_DiffuseMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 ShadeColor = tex2D( ShadeMapSampler, In.DifPow );
 float4 Final = DiffuseMapColor * ( g_vLightColor * ShadeColor * 2 );
 Final.a = DiffuseMapColor.a;
 return Final;
}
float4 PS_DiffuseMap_ShadeMap_Light1_ContrastSaturation( sPS_DiffuseMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 ShadeColor = tex2D( ShadeMapSampler, In.DifPow );
 float4 Final = DiffuseMapColor * ( g_vLightColor * ShadeColor * 2 );
 Final.a = DiffuseMapColor.a;
 Final.rgb = Final.rgb - g_Contrast * ( Final.rgb - 1.0f ) * Final.rgb * ( Final.rgb - 2.0f );
 float Lum = dot( Final.rgb, g_SaturationConst );
 Final.rgb = saturate( lerp( Lum.xxx, Final.rgb, g_Saturation ) );
 return Final;
}
float4 PS_DiffuseMap_ShadeMap_Light1_TextureFactor( sPS_DiffuseMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 ShadeColor = tex2D( ShadeMapSampler, In.DifPow );
 float4 Final = DiffuseMapColor * ( g_vLightColor * ShadeColor * 2 ) * g_TexFactor;
 Final.a = DiffuseMapColor.a * g_TexFactor.a;
 return Final;
}
float4 PS_DiffuseMap_ShadeMap_Light1_TextureFactor_ContrastSaturation( sPS_DiffuseMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 ShadeColor = tex2D( ShadeMapSampler, In.DifPow );
 float4 Final = DiffuseMapColor * ( g_vLightColor * ShadeColor * 2 ) * g_TexFactor;
 Final.a = DiffuseMapColor.a * g_TexFactor.a;
 Final.rgb = Final.rgb - g_Contrast * ( Final.rgb - 1.0f ) * Final.rgb * ( Final.rgb - 2.0f );
 float Lum = dot( Final.rgb, g_SaturationConst );
 Final.rgb = saturate( lerp( Lum.xxx, Final.rgb, g_Saturation ) );
 return Final;
}
float4 PS_DiffuseMap_SpecularMap_ShadeMap_Light1( sPS_DiffuseMap_SpecularMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 SpecularMapColor = tex2D( SpecularMapSampler, In.DifMapUV );
 float4 ShadeColor = tex2D( ShadeMapSampler, In.DifPow );
 float4 Final = DiffuseMapColor * ( g_vLightColor * ShadeColor * 2 ) + ( SpecularMapColor * In.SpePower );
 Final.a = DiffuseMapColor.a;
 return Final;
}
float4 PS_DiffuseMap_SpecularMap_ShadeMap_Light1_ContrastSaturation( sPS_DiffuseMap_SpecularMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 SpecularMapColor = tex2D( SpecularMapSampler, In.DifMapUV );
 float4 ShadeColor = tex2D( ShadeMapSampler, In.DifPow );
 float4 Final = DiffuseMapColor * ( g_vLightColor * ShadeColor * 2 ) + ( SpecularMapColor * In.SpePower );
 Final.a = DiffuseMapColor.a;
 Final.rgb = Final.rgb - g_Contrast * ( Final.rgb - 1.0f ) * Final.rgb * ( Final.rgb - 2.0f );
 float Lum = dot( Final.rgb, g_SaturationConst );
 Final.rgb = saturate( lerp( Lum.xxx, Final.rgb, g_Saturation ) );
 return Final;
}
float4 PS_DiffuseMap_SpecularMap_ShadeMap_Light1_TextureFactor( sPS_DiffuseMap_SpecularMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 SpecularMapColor = tex2D( SpecularMapSampler, In.DifMapUV );
 float4 ShadeColor = tex2D( ShadeMapSampler, In.DifPow );
 float4 Final = ( DiffuseMapColor * ( g_vLightColor * ShadeColor * 2 ) + ( SpecularMapColor * In.SpePower ) ) * g_TexFactor;
 Final.a = DiffuseMapColor.a * g_TexFactor.a;
 return Final;
}
float4 PS_DiffuseMap_SpecularMap_ShadeMap_Light1_TextureFactor_ContrastSaturation( sPS_DiffuseMap_SpecularMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 SpecularMapColor = tex2D( SpecularMapSampler, In.DifMapUV );
 float4 ShadeColor = tex2D( ShadeMapSampler, In.DifPow );
 float4 Final = ( DiffuseMapColor * ( g_vLightColor * ShadeColor * 2 ) + ( SpecularMapColor * In.SpePower ) ) * g_TexFactor;
 Final.a = DiffuseMapColor.a * g_TexFactor.a;
 Final.rgb = Final.rgb - g_Contrast * ( Final.rgb - 1.0f ) * Final.rgb * ( Final.rgb - 2.0f );
 float Lum = dot( Final.rgb, g_SaturationConst );
 Final.rgb = saturate( lerp( Lum.xxx, Final.rgb, g_Saturation ) );
 return Final;
}
float4 PS_DiffuseMap_BumpMap_Light1( sPS_DiffuseMap_BumpMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 BumpMapColor = tex2D( BumpMapSampler, In.DifMapUV );
 BumpMapColor = ( BumpMapColor - 0.5 ) * 2;
 float DifPow = g_BumpDepth * dot( BumpMapColor, In.L ) + 0.5;
 float4 Final = DiffuseMapColor * ( g_vLightColor * ( DifPow + 0.5 ) );
 Final.a = DiffuseMapColor.a;
 return Final;
}
float4 PS_DiffuseMap_BumpMap_Light1_ContrastSaturation( sPS_DiffuseMap_BumpMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 BumpMapColor = tex2D( BumpMapSampler, In.DifMapUV );
 BumpMapColor = ( BumpMapColor - 0.5 ) * 2;
 float DifPow = g_BumpDepth * dot( BumpMapColor, In.L ) + 0.5;
 float4 Final = DiffuseMapColor * ( g_vLightColor * ( DifPow + 0.5 ) );
 Final.a = DiffuseMapColor.a;
 Final.rgb = Final.rgb - g_Contrast * ( Final.rgb - 1.0f ) * Final.rgb * ( Final.rgb - 2.0f );
 float Lum = dot( Final.rgb, g_SaturationConst );
 Final.rgb = saturate( lerp( Lum.xxx, Final.rgb, g_Saturation ) );
 return Final;
}
float4 PS_DiffuseMap_BumpMap_Light1_TextureFactor( sPS_DiffuseMap_BumpMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 BumpMapColor = tex2D( BumpMapSampler, In.DifMapUV );
 BumpMapColor = ( BumpMapColor - 0.5 ) * 2;
 float DifPow = g_BumpDepth * dot( BumpMapColor, In.L ) + 0.5;
 float4 Final = DiffuseMapColor * ( g_vLightColor * ( DifPow + 0.5 ) ) * g_TexFactor;
 Final.a = DiffuseMapColor.a * g_TexFactor.a;
 return Final;
}
float4 PS_DiffuseMap_BumpMap_Light1_TextureFactor_ContrastSaturation( sPS_DiffuseMap_BumpMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 BumpMapColor = tex2D( BumpMapSampler, In.DifMapUV );
 BumpMapColor = ( BumpMapColor - 0.5 ) * 2;
 float DifPow = g_BumpDepth * dot( BumpMapColor, In.L ) + 0.5;
 float4 Final = DiffuseMapColor * ( g_vLightColor * ( DifPow + 0.5 ) ) * g_TexFactor;
 Final.a = DiffuseMapColor.a * g_TexFactor.a;
 Final.rgb = Final.rgb - g_Contrast * ( Final.rgb - 1.0f ) * Final.rgb * ( Final.rgb - 2.0f );
 float Lum = dot( Final.rgb, g_SaturationConst );
 Final.rgb = saturate( lerp( Lum.xxx, Final.rgb, g_Saturation ) );
 return Final;
}
float4 PS_DiffuseMap_BumpMap_SpecularMap_Light1( sPS_DiffuseMap_BumpMap_SpecularMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 SpecularMapColor = tex2D( SpecularMapSampler, In.DifMapUV );
 float4 BumpMapColor = tex2D( BumpMapSampler, In.DifMapUV );
 BumpMapColor = ( BumpMapColor - 0.5 ) * 2;
 float DifPow = g_BumpDepth * dot( BumpMapColor, In.L ) + 0.5;
 float SpePower = pow( max( dot( BumpMapColor, In.H ), 0 ), g_SpecularShiness );
 float4 Final = DiffuseMapColor * ( g_vLightColor * ( DifPow + 0.5 ) ) + ( SpecularMapColor * SpePower );
 Final.a = DiffuseMapColor.a;
 return Final;
}
float4 PS_DiffuseMap_BumpMap_SpecularMap_Light1_ContrastSaturation( sPS_DiffuseMap_BumpMap_SpecularMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 SpecularMapColor = tex2D( SpecularMapSampler, In.DifMapUV );
 float4 BumpMapColor = tex2D( BumpMapSampler, In.DifMapUV );
 BumpMapColor = ( BumpMapColor - 0.5 ) * 2;
 float DifPow = g_BumpDepth * dot( BumpMapColor, In.L ) + 0.5;
 float SpePower = pow( max( dot( BumpMapColor, In.H ), 0 ), g_SpecularShiness );
 float4 Final = DiffuseMapColor * ( g_vLightColor * ( DifPow + 0.5 ) ) + ( SpecularMapColor * SpePower );
 Final.a = DiffuseMapColor.a;
 Final.rgb = Final.rgb - g_Contrast * ( Final.rgb - 1.0f ) * Final.rgb * ( Final.rgb - 2.0f );
 float Lum = dot( Final.rgb, g_SaturationConst );
 Final.rgb = saturate( lerp( Lum.xxx, Final.rgb, g_Saturation ) );
 return Final;
}
float4 PS_DiffuseMap_BumpMap_SpecularMap_Light1_TextureFactor( sPS_DiffuseMap_BumpMap_SpecularMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 SpecularMapColor = tex2D( SpecularMapSampler, In.DifMapUV );
 float4 BumpMapColor = tex2D( BumpMapSampler, In.DifMapUV );
 BumpMapColor = ( BumpMapColor - 0.5 ) * 2;
 float DifPow = g_BumpDepth * dot( BumpMapColor, In.L ) + 0.5;
 float SpePower = pow( max( dot( BumpMapColor, In.H ), 0 ), g_SpecularShiness );
 float4 Final = ( DiffuseMapColor * ( g_vLightColor * ( DifPow + 0.5 ) ) + ( SpecularMapColor * SpePower ) ) * g_TexFactor;
 Final.a = DiffuseMapColor.a * g_TexFactor.a;
 return Final;
}
float4 PS_DiffuseMap_BumpMap_SpecularMap_Light1_TextureFactor_ContrastSaturation( sPS_DiffuseMap_BumpMap_SpecularMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 SpecularMapColor = tex2D( SpecularMapSampler, In.DifMapUV );
 float4 BumpMapColor = tex2D( BumpMapSampler, In.DifMapUV );
 BumpMapColor = ( BumpMapColor - 0.5 ) * 2;
 float DifPow = g_BumpDepth * dot( BumpMapColor, In.L ) + 0.5;
 float SpePower = pow( max( dot( BumpMapColor, In.H ), 0 ), g_SpecularShiness );
 float4 Final = ( DiffuseMapColor * ( g_vLightColor * ( DifPow + 0.5 ) ) + ( SpecularMapColor * SpePower ) ) * g_TexFactor;
 Final.a = DiffuseMapColor.a * g_TexFactor.a;
 Final.rgb = Final.rgb - g_Contrast * ( Final.rgb - 1.0f ) * Final.rgb * ( Final.rgb - 2.0f );
 float Lum = dot( Final.rgb, g_SaturationConst );
 Final.rgb = saturate( lerp( Lum.xxx, Final.rgb, g_Saturation ) );
 return Final;
}
float4 PS_DiffuseMap_BumpMap_ShadeMap_Light1( sPS_DiffuseMap_BumpMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 BumpMapColor = tex2D( BumpMapSampler, In.DifMapUV );
 BumpMapColor = ( BumpMapColor - 0.5 ) * 2;
 float ShadeMapUV = g_BumpDepth * dot( BumpMapColor, In.L ) + 0.5;
 float4 ShadeMapColor = tex2D( ShadeMapSampler, ShadeMapUV );
 float4 Final = DiffuseMapColor * ( g_vLightColor * ShadeMapColor * 2 );
 Final.a = DiffuseMapColor.a;
 return Final;
}
float4 PS_DiffuseMap_BumpMap_ShadeMap_Light1_ContrastSaturation( sPS_DiffuseMap_BumpMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 BumpMapColor = tex2D( BumpMapSampler, In.DifMapUV );
 BumpMapColor = ( BumpMapColor - 0.5 ) * 2;
 float ShadeMapUV = g_BumpDepth * dot( BumpMapColor, In.L ) + 0.5;
 float4 ShadeMapColor = tex2D( ShadeMapSampler, ShadeMapUV );
 float4 Final = DiffuseMapColor * ( g_vLightColor * ShadeMapColor * 2 );
 Final.a = DiffuseMapColor.a;
 Final.rgb = Final.rgb - g_Contrast * ( Final.rgb - 1.0f ) * Final.rgb * ( Final.rgb - 2.0f );
 float Lum = dot( Final.rgb, g_SaturationConst );
 Final.rgb = saturate( lerp( Lum.xxx, Final.rgb, g_Saturation ) );
 return Final;
}
float4 PS_DiffuseMap_BumpMap_ShadeMap_Light1_TextureFactor( sPS_DiffuseMap_BumpMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 BumpMapColor = tex2D( BumpMapSampler, In.DifMapUV );
 BumpMapColor = ( BumpMapColor - 0.5 ) * 2;
 float ShadeMapUV = g_BumpDepth * dot( BumpMapColor, In.L ) + 0.5;
 float4 ShadeMapColor = tex2D( ShadeMapSampler, ShadeMapUV );
 float4 Final = DiffuseMapColor * ( g_vLightColor * ShadeMapColor * 2 ) * g_TexFactor;
 Final.a = DiffuseMapColor.a * g_TexFactor.a;
 return Final;
}
float4 PS_DiffuseMap_BumpMap_ShadeMap_Light1_TextureFactor_ContrastSaturation( sPS_DiffuseMap_BumpMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 BumpMapColor = tex2D( BumpMapSampler, In.DifMapUV );
 BumpMapColor = ( BumpMapColor - 0.5 ) * 2;
 float ShadeMapUV = g_BumpDepth * dot( BumpMapColor, In.L ) + 0.5;
 float4 ShadeMapColor = tex2D( ShadeMapSampler, ShadeMapUV );
 float4 Final = DiffuseMapColor * ( g_vLightColor * ShadeMapColor * 2 ) * g_TexFactor;
 Final.a = DiffuseMapColor.a * g_TexFactor.a;
 Final.rgb = Final.rgb - g_Contrast * ( Final.rgb - 1.0f ) * Final.rgb * ( Final.rgb - 2.0f );
 float Lum = dot( Final.rgb, g_SaturationConst );
 Final.rgb = saturate( lerp( Lum.xxx, Final.rgb, g_Saturation ) );
 return Final;
}
float4 PS_DiffuseMap_BumpMap_SpecularMap_ShadeMap_Light1( sPS_DiffuseMap_BumpMap_SpecularMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 SpecularMapColor = tex2D( SpecularMapSampler, In.DifMapUV );
 float4 BumpMapColor = tex2D( BumpMapSampler, In.DifMapUV );
 BumpMapColor = ( BumpMapColor - 0.5 ) * 2;
 float ShadeMapUV = g_BumpDepth * dot( BumpMapColor, In.L ) + 0.5;
 float SpePower = pow( max( dot( BumpMapColor, In.H ), 0 ), g_SpecularShiness );
 float4 ShadeMapColor = tex2D( ShadeMapSampler, ShadeMapUV );
 float4 Final = DiffuseMapColor * ( g_vLightColor * ShadeMapColor * 2 ) + ( SpecularMapColor * SpePower );
 Final.a = DiffuseMapColor.a;
 return Final;
}
float4 PS_DiffuseMap_BumpMap_SpecularMap_ShadeMap_Light1_ContrastSaturation( sPS_DiffuseMap_BumpMap_SpecularMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 SpecularMapColor = tex2D( SpecularMapSampler, In.DifMapUV );
 float4 BumpMapColor = tex2D( BumpMapSampler, In.DifMapUV );
 BumpMapColor = ( BumpMapColor - 0.5 ) * 2;
 float ShadeMapUV = g_BumpDepth * dot( BumpMapColor, In.L ) + 0.5;
 float SpePower = pow( max( dot( BumpMapColor, In.H ), 0 ), g_SpecularShiness );
 float4 ShadeMapColor = tex2D( ShadeMapSampler, ShadeMapUV );
 float4 Final = DiffuseMapColor * ( g_vLightColor * ShadeMapColor * 2 ) + ( SpecularMapColor * SpePower );
 Final.a = DiffuseMapColor.a;
 Final.rgb = Final.rgb - g_Contrast * ( Final.rgb - 1.0f ) * Final.rgb * ( Final.rgb - 2.0f );
 float Lum = dot( Final.rgb, g_SaturationConst );
 Final.rgb = saturate( lerp( Lum.xxx, Final.rgb, g_Saturation ) );
 return Final;
}
float4 PS_DiffuseMap_BumpMap_SpecularMap_ShadeMap_Light1_TextureFactor( sPS_DiffuseMap_BumpMap_SpecularMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 SpecularMapColor = tex2D( SpecularMapSampler, In.DifMapUV );
 float4 BumpMapColor = tex2D( BumpMapSampler, In.DifMapUV );
 BumpMapColor = ( BumpMapColor - 0.5 ) * 2;
 float ShadeMapUV = g_BumpDepth * dot( BumpMapColor, In.L ) + 0.5;
 float SpePower = pow( max( dot( BumpMapColor, In.H ), 0 ), g_SpecularShiness );
 float4 ShadeMapColor = tex2D( ShadeMapSampler, ShadeMapUV );
 float4 Final = ( DiffuseMapColor * ( g_vLightColor * ShadeMapColor * 2 ) + ( SpecularMapColor * SpePower ) ) * g_TexFactor;
 Final.a = DiffuseMapColor.a * g_TexFactor.a;
 return Final;
}
float4 PS_DiffuseMap_BumpMap_SpecularMap_ShadeMap_Light1_TextureFactor_ContrastSaturation( sPS_DiffuseMap_BumpMap_SpecularMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DifMapUV );
 float4 SpecularMapColor = tex2D( SpecularMapSampler, In.DifMapUV );
 float4 BumpMapColor = tex2D( BumpMapSampler, In.DifMapUV );
 BumpMapColor = ( BumpMapColor - 0.5 ) * 2;
 float ShadeMapUV = g_BumpDepth * dot( BumpMapColor, In.L ) + 0.5;
 float SpePower = pow( max( dot( BumpMapColor, In.H ), 0 ), g_SpecularShiness );
 float4 ShadeMapColor = tex2D( ShadeMapSampler, ShadeMapUV );
 float4 Final = ( DiffuseMapColor * ( g_vLightColor * ShadeMapColor * 2 ) + ( SpecularMapColor * SpePower ) ) * g_TexFactor;
 Final.a = DiffuseMapColor.a * g_TexFactor.a;
 Final.rgb = Final.rgb - g_Contrast * ( Final.rgb - 1.0f ) * Final.rgb * ( Final.rgb - 2.0f );
 float Lum = dot( Final.rgb, g_SaturationConst );
 Final.rgb = saturate( lerp( Lum.xxx, Final.rgb, g_Saturation ) );
 return Final;
}
technique Technique_FX {
 pass p0 {
 VertexShader = compile vs_2_0 VS_FX();
 PixelShader = compile ps_2_0 PS_FX();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_FX();
 PixelShader = compile ps_2_0 PS_FX();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_FX();
 PixelShader = compile ps_2_0 PS_FX_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_FX();
 PixelShader = compile ps_2_0 PS_FX_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_LightMap {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_LightMap();
 PixelShader = compile ps_2_0 PS_DiffuseMap_LightMap();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_LightMap();
 PixelShader = compile ps_2_0 PS_DiffuseMap_LightMap();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_LightMap();
 PixelShader = compile ps_2_0 PS_DiffuseMap_LightMap_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_LightMap();
 PixelShader = compile ps_2_0 PS_DiffuseMap_LightMap_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_LightMap_SpecularMap {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_LightMap_SpecularMap();
 PixelShader = compile ps_2_0 PS_DiffuseMap_LightMap_SpecularMap();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_LightMap_SpecularMap();
 PixelShader = compile ps_2_0 PS_DiffuseMap_LightMap_SpecularMap();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_LightMap_SpecularMap();
 PixelShader = compile ps_2_0 PS_DiffuseMap_LightMap_SpecularMap_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_LightMap_SpecularMap();
 PixelShader = compile ps_2_0 PS_DiffuseMap_LightMap_SpecularMap_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_Light0_Rigid {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light0_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light0();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light0_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light0_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light0_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light0_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light0_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light0_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_Light0_Skinned {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light0_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light0();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light0_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light0_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light0_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light0_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light0_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light0_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_BumpMap_Light0_Rigid {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light0_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light0();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light0_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light0_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light0_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light0_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light0_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light0_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_BumpMap_Light0_Skinned {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light0_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light0();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light0_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light0_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light0_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light0_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light0_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light0_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_Light1_Rigid {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light1_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light1_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light1_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_Light1_Skinned {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light1_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light1_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light1_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_SpecularMap_Light1_Rigid {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_SpecularMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_SpecularMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_SpecularMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_SpecularMap_Light1_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_SpecularMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_SpecularMap_Light1_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_SpecularMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_SpecularMap_Light1_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_SpecularMap_Light1_Skinned {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_SpecularMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_SpecularMap_Light1_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_SpecularMap_Light1_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_SpecularMap_Light1_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_ShadeMap_Light1_Rigid {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_ShadeMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_ShadeMap_Light1_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_ShadeMap_Light1_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_ShadeMap_Light1_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_ShadeMap_Light1_Skinned {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_ShadeMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_ShadeMap_Light1_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_ShadeMap_Light1_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_ShadeMap_Light1_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_SpecularMap_ShadeMap_Light1_Rigid {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_SpecularMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_SpecularMap_ShadeMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_SpecularMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_SpecularMap_ShadeMap_Light1_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_SpecularMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_SpecularMap_ShadeMap_Light1_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_SpecularMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_SpecularMap_ShadeMap_Light1_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_SpecularMap_ShadeMap_Light1_Skinned {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_SpecularMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_SpecularMap_ShadeMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_SpecularMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_SpecularMap_ShadeMap_Light1_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_SpecularMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_SpecularMap_ShadeMap_Light1_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_SpecularMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_SpecularMap_ShadeMap_Light1_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_BumpMap_Light1_Rigid {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_Light1_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_Light1_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_Light1_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_BumpMap_Light1_Skinned {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_Light1_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_Light1_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_Light1_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_BumpMap_SpecularMap_Light1_Rigid {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_SpecularMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_SpecularMap_Light1_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_SpecularMap_Light1_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_SpecularMap_Light1_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_BumpMap_SpecularMap_Light1_Skinned {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_SpecularMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_SpecularMap_Light1_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_SpecularMap_Light1_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_SpecularMap_Light1_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_BumpMap_ShadeMap_Light1_Rigid {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_ShadeMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_ShadeMap_Light1_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_ShadeMap_Light1_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_ShadeMap_Light1_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_BumpMap_ShadeMap_Light1_Skinned {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_ShadeMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_ShadeMap_Light1_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_ShadeMap_Light1_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_ShadeMap_Light1_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_BumpMap_SpecularMap_ShadeMap_Light1_Rigid {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_SpecularMap_ShadeMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_SpecularMap_ShadeMap_Light1_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_SpecularMap_ShadeMap_Light1_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Rigid();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_SpecularMap_ShadeMap_Light1_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_BumpMap_SpecularMap_ShadeMap_Light1_Skinned {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_SpecularMap_ShadeMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_SpecularMap_ShadeMap_Light1_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_SpecularMap_ShadeMap_Light1_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Skinned();
 PixelShader = compile ps_2_0 PS_DiffuseMap_BumpMap_SpecularMap_ShadeMap_Light1_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_BumpMap_Light1_Skinned_Low {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Skinned_Low();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Skinned_Low();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light1_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Skinned_Low();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light1_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Skinned_Low();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light1_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_BumpMap_SpecularMap_Light1_Skinned_Low {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Skinned_Low();
 PixelShader = compile ps_2_0 PS_DiffuseMap_SpecularMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Skinned_Low();
 PixelShader = compile ps_2_0 PS_DiffuseMap_SpecularMap_Light1_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Skinned_Low();
 PixelShader = compile ps_2_0 PS_DiffuseMap_SpecularMap_Light1_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Skinned_Low();
 PixelShader = compile ps_2_0 PS_DiffuseMap_SpecularMap_Light1_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_BumpMap_ShadeMap_Light1_Skinned_Low {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Skinned_Low();
 PixelShader = compile ps_2_0 PS_DiffuseMap_ShadeMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Skinned_Low();
 PixelShader = compile ps_2_0 PS_DiffuseMap_ShadeMap_Light1_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Skinned_Low();
 PixelShader = compile ps_2_0 PS_DiffuseMap_ShadeMap_Light1_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Skinned_Low();
 PixelShader = compile ps_2_0 PS_DiffuseMap_ShadeMap_Light1_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_BumpMap_SpecularMap_ShadeMap_Light1_Skinned_Low {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Skinned_Low();
 PixelShader = compile ps_2_0 PS_DiffuseMap_SpecularMap_ShadeMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Skinned_Low();
 PixelShader = compile ps_2_0 PS_DiffuseMap_SpecularMap_ShadeMap_Light1_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Skinned_Low();
 PixelShader = compile ps_2_0 PS_DiffuseMap_SpecularMap_ShadeMap_Light1_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_SpecularMap_Light1_Skinned_Low();
 PixelShader = compile ps_2_0 PS_DiffuseMap_SpecularMap_ShadeMap_Light1_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_LightMap_Low {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_LightMap_Low();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light0();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_LightMap_Low();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light0_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_LightMap_Low();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light0_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_LightMap_Low();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light0_TextureFactor_ContrastSaturation();
 }
}
technique Technique_DiffuseMap_BumpMap_Light1_Rigid_Low {
 pass p0 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Rigid_Low();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Rigid_Low();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light1_TextureFactor();
 }
 pass p2 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Rigid_Low();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light1_ContrastSaturation();
 }
 pass p3 {
 VertexShader = compile vs_2_0 VS_DiffuseMap_BumpMap_Light1_Rigid_Low();
 PixelShader = compile ps_2_0 PS_DiffuseMap_Light1_TextureFactor_ContrastSaturation();
 }
}
```

---

## 12. `023e4590`

**Shaders / functions:** `VS_FX`, `VS_DiffuseMap_LightMap_Low`, `VS_DiffuseMap_LightMap`, `VS_DiffuseMap_LightMap_SpecularMap`, `VS_DiffuseMap_Light0`, `VS_DiffuseMap_Light1`, `VS_DiffuseMap_ShadeMap_Light1`, `VS_DiffuseMap_SpecularMap_Light1`, `VS_DiffuseMap_SpecularMap_ShadeMap_Light1`, `PS_FX`, `PS_DiffuseMap_LightMap`, `PS_DiffuseMap_LightMap_SpecularMap`, `PS_DiffuseMap_Light0`, `PS_DiffuseMap_Light0_TextureFactor`, `PS_DiffuseMap_Light1`, `PS_DiffuseMap_Light1_TextureFactor`, `PS_DiffuseMap_SpecularMap_Light1`, `PS_DiffuseMap_SpecularMap_Light1_TextureFactor`, `PS_DiffuseMap_ShadeMap_Light1`, `PS_DiffuseMap_ShadeMap_Light1_TextureFactor`, `PS_DiffuseMap_SpecularMap_ShadeMap_Light1`, `PS_DiffuseMap_SpecularMap_ShadeMap_Light1_TextureFactor`

**Techniques:** `Technique_FX`, `Technique_DiffuseMap_LightMap`, `Technique_DiffuseMap_LightMap_SpecularMap`, `Technique_DiffuseMap_Light0`, `Technique_DiffuseMap_Light1`, `Technique_DiffuseMap_SpecularMap_Light1`, `Technique_DiffuseMap_ShadeMap_Light1`, `Technique_DiffuseMap_SpecularMap_ShadeMap_Light1`, `Technique_DiffuseMap_LightMap_Low`

```hlsl
int g_SpecularShiness = 16;
matrix g_matWVP;
matrix g_matWorld;
float4 g_TexFactor;
float3 g_vEyePos;
float3 g_vLightPos;
float4 g_vLightColor;
texture g_TexDiffuseMap;
texture g_TexLightMap;
texture g_TexSpecularMap;
texture g_TexShadeMap;
sampler DiffuseMapSampler = sampler_state {
 Texture = ;
 MipFilter = LINEAR;
 MinFilter = LINEAR;
 MagFilter = LINEAR;
 AddressU = WRAP;
 AddressV = WRAP;
}
;
sampler LightMapSampler = sampler_state {
 Texture = ;
 MipFilter = LINEAR;
 MinFilter = LINEAR;
 MagFilter = LINEAR;
 AddressU = WRAP;
 AddressV = WRAP;
}
;
sampler SpecularMapSampler = sampler_state {
 Texture = ;
 MipFilter = LINEAR;
 MinFilter = LINEAR;
 MagFilter = LINEAR;
 AddressU = WRAP;
 AddressV = WRAP;
}
;
sampler ShadeMapSampler = sampler_state {
 Texture = ;
 MipFilter = LINEAR;
 MinFilter = LINEAR;
 MagFilter = LINEAR;
 AddressU = CLAMP;
 AddressV = CLAMP;
}
;
struct sPS_FX {
 float4 Position : POSITION;
 float4 DiffuseColor : COLOR;
 float2 DiffuseMapUV : TEXCOORD0;
}
;
struct sPS_DiffuseMap_LightMap {
 float4 Position : POSITION;
 float2 DiffuseMapUV : TEXCOORD0;
 float2 LightMapUV : TEXCOORD1;
}
;
struct sPS_DiffuseMap_LightMap_SpecularMap {
 float4 Position : POSITION;
 float2 DiffuseMapUV : TEXCOORD0;
 float2 LightMapUV : TEXCOORD1;
 float2 SpecularMapUV : TEXCOORD2;
 float SpecularPower : COLOR0;
}
;
struct sPS_DiffuseMap_Light0 {
 float4 Position : POSITION;
 float2 DiffuseMapUV : TEXCOORD0;
}
;
struct sPS_DiffuseMap_Light1 {
 float4 Position : POSITION;
 float2 DiffuseMapUV : TEXCOORD0;
 float DiffusePower : COLOR0;
}
;
struct sPS_DiffuseMap_ShadeMap_Light1 {
 float4 Position : POSITION;
 float2 DiffuseMapUV : TEXCOORD0;
 float2 ShadeMapUV : TEXCOORD1;
}
;
struct sPS_DiffuseMap_SpecularMap_Light1 {
 float4 Position : POSITION;
 float2 DiffuseMapUV : TEXCOORD0;
 float2 SpecularMapUV : TEXCOORD1;
 float DiffusePower : COLOR0;
 float SpecularPower : COLOR1;
}
;
struct sPS_DiffuseMap_SpecularMap_ShadeMap_Light1 {
 float4 Position : POSITION;
 float2 DiffuseMapUV : TEXCOORD0;
 float2 ShadeMapUV : TEXCOORD1;
 float2 SpecularMapUV : TEXCOORD2;
 float SpecularPower : COLOR0;
}
;
sPS_FX VS_FX ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float4 InDiffuseColor : COLOR, float2 InDiffuseMapUV : TEXCOORD0 ) {
 sPS_FX Out = ( sPS_FX )0;
 Out.Position = mul( InPosition, g_matWVP );
 Out.DiffuseMapUV = InDiffuseMapUV;
 Out.DiffuseColor = InDiffuseColor;
 return Out;
}
sPS_DiffuseMap_Light0 VS_DiffuseMap_LightMap_Low ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0, float2 InLightMapUV : TEXCOORD1 ) {
 sPS_DiffuseMap_Light0 Out = ( sPS_DiffuseMap_Light0 )0;
 Out.Position = mul( InPosition, g_matWVP );
 Out.DiffuseMapUV = InDiffuseMapUV;
 return Out;
}
sPS_DiffuseMap_LightMap VS_DiffuseMap_LightMap ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0, float2 InLightMapUV : TEXCOORD1 ) {
 sPS_DiffuseMap_LightMap Out = ( sPS_DiffuseMap_LightMap )0;
 Out.Position = mul( InPosition, g_matWVP );
 Out.DiffuseMapUV = InDiffuseMapUV;
 Out.LightMapUV = InLightMapUV;
 return Out;
}
sPS_DiffuseMap_LightMap_SpecularMap VS_DiffuseMap_LightMap_SpecularMap ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0, float2 InLightMapUV : TEXCOORD1 ) {
 sPS_DiffuseMap_LightMap_SpecularMap Out = ( sPS_DiffuseMap_LightMap_SpecularMap )0;
 float3 tmpPos = mul( InPosition, g_matWorld );
 float3 tmpNormal = normalize( mul( InNormal, (float3x3)g_matWorld ) );
 float3 L = normalize( g_vLightPos - tmpPos );
 float3 V = normalize( g_vEyePos - tmpPos );
 float3 H = normalize( L + V );
 Out.Position = mul( InPosition, g_matWVP );
 Out.DiffuseMapUV = InDiffuseMapUV;
 Out.LightMapUV = InLightMapUV;
 Out.SpecularMapUV = InDiffuseMapUV;
 Out.SpecularPower = pow( max( dot( tmpNormal, H ), 0 ), g_SpecularShiness );
 return Out;
}
sPS_DiffuseMap_Light0 VS_DiffuseMap_Light0 ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0 ) {
 sPS_DiffuseMap_Light0 Out = ( sPS_DiffuseMap_Light0 )0;
 Out.Position = mul( InPosition, g_matWVP );
 Out.DiffuseMapUV = InDiffuseMapUV;
 return Out;
}
sPS_DiffuseMap_Light1 VS_DiffuseMap_Light1 ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0 ) {
 sPS_DiffuseMap_Light1 Out = ( sPS_DiffuseMap_Light1 )0;
 float3 tmpPos = mul( InPosition, g_matWorld );
 float3 tmpNormal = normalize( mul( InNormal, (float3x3)g_matWorld ) );
 float3 L = normalize( g_vLightPos - tmpPos );
 Out.Position = mul( InPosition, g_matWVP );
 Out.DiffuseMapUV = InDiffuseMapUV;
 Out.DiffusePower = 0.5 * dot( tmpNormal, L ) + 0.5;
 return Out;
}
sPS_DiffuseMap_ShadeMap_Light1 VS_DiffuseMap_ShadeMap_Light1 ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0 ) {
 sPS_DiffuseMap_ShadeMap_Light1 Out = ( sPS_DiffuseMap_ShadeMap_Light1 )0;
 float3 tmpPos = mul( InPosition, g_matWorld );
 float3 tmpNormal = normalize( mul( InNormal, (float3x3)g_matWorld ) );
 float3 L = normalize( g_vLightPos - tmpPos );
 Out.Position = mul( InPosition, g_matWVP );
 Out.DiffuseMapUV = InDiffuseMapUV;
 Out.ShadeMapUV = 0.5 * dot( tmpNormal, L ) + 0.5;
 return Out;
}
sPS_DiffuseMap_SpecularMap_Light1 VS_DiffuseMap_SpecularMap_Light1 ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0 ) {
 sPS_DiffuseMap_SpecularMap_Light1 Out= ( sPS_DiffuseMap_SpecularMap_Light1 )0;
 float3 tmpPos = mul( InPosition, g_matWorld );
 float3 tmpNormal = normalize( mul( InNormal, (float3x3)g_matWorld ) );
 float3 L = normalize( g_vLightPos - tmpPos );
 float3 V = normalize( g_vEyePos - tmpPos );
 float3 H = normalize( L + V );
 Out.Position = mul( InPosition, g_matWVP );
 Out.DiffuseMapUV = InDiffuseMapUV;
 Out.SpecularMapUV = InDiffuseMapUV;
 Out.DiffusePower = 0.5 * dot( tmpNormal, L ) + 0.5;
 Out.SpecularPower = pow( max( dot( tmpNormal, H ), 0 ), g_SpecularShiness );
 return Out;
}
sPS_DiffuseMap_SpecularMap_ShadeMap_Light1 VS_DiffuseMap_SpecularMap_ShadeMap_Light1 ( float4 InPosition : POSITION, float3 InNormal : NORMAL, float2 InDiffuseMapUV : TEXCOORD0 ) {
 sPS_DiffuseMap_SpecularMap_ShadeMap_Light1 Out= ( sPS_DiffuseMap_SpecularMap_ShadeMap_Light1 )0;
 float3 tmpPos = mul( InPosition, g_matWorld );
 float3 tmpNormal = normalize( mul( InNormal, (float3x3)g_matWorld ) );
 float3 L = normalize( g_vLightPos - tmpPos );
 float3 V = normalize( g_vEyePos - tmpPos );
 float3 H = normalize( L + V );
 Out.Position = mul( InPosition, g_matWVP );
 Out.DiffuseMapUV = InDiffuseMapUV;
 Out.ShadeMapUV = 0.5 * dot( tmpNormal, L ) + 0.5;
 Out.SpecularMapUV = InDiffuseMapUV;
 Out.SpecularPower = pow( max( dot( tmpNormal, H ), 0 ), g_SpecularShiness );
 return Out;
}
float4 PS_FX( sPS_FX In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DiffuseMapUV );
 float4 FinalColor = DiffuseMapColor * In.DiffuseColor;
 return FinalColor;
}
float4 PS_DiffuseMap_LightMap( sPS_DiffuseMap_LightMap In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DiffuseMapUV );
 float4 LightMapColor = tex2D( LightMapSampler, In.LightMapUV );
 float4 FinalColor = DiffuseMapColor * ( LightMapColor * 2 );
 FinalColor.a = DiffuseMapColor.a;
 return FinalColor;
}
float4 PS_DiffuseMap_LightMap_SpecularMap( sPS_DiffuseMap_LightMap_SpecularMap In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DiffuseMapUV );
 float4 LightMapColor = tex2D( LightMapSampler, In.LightMapUV );
 float4 SpecularMapColor = tex2D( SpecularMapSampler, In.SpecularMapUV );
 float4 FinalColor = DiffuseMapColor * ( LightMapColor * 2 ) + ( SpecularMapColor * In.SpecularPower );
 FinalColor.a = DiffuseMapColor.a;
 return FinalColor;
}
float4 PS_DiffuseMap_Light0( sPS_DiffuseMap_Light0 In ) : COLOR {
 float4 FinalColor = tex2D( DiffuseMapSampler, In.DiffuseMapUV );
 return FinalColor;
}
float4 PS_DiffuseMap_Light0_TextureFactor( sPS_DiffuseMap_Light0 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DiffuseMapUV );
 float4 FinalColor = DiffuseMapColor * g_TexFactor;
 return FinalColor;
}
float4 PS_DiffuseMap_Light1( sPS_DiffuseMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DiffuseMapUV );
 float4 FinalColor = DiffuseMapColor * ( g_vLightColor * ( In.DiffusePower + 0.5 ) );
 FinalColor.a = DiffuseMapColor.a;
 return FinalColor;
}
float4 PS_DiffuseMap_Light1_TextureFactor( sPS_DiffuseMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DiffuseMapUV );
 float4 FinalColor = DiffuseMapColor * ( g_vLightColor * ( In.DiffusePower + 0.5 ) ) * g_TexFactor;
 FinalColor.a = DiffuseMapColor.a * g_TexFactor.a;
 return FinalColor;
}
float4 PS_DiffuseMap_SpecularMap_Light1( sPS_DiffuseMap_SpecularMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DiffuseMapUV );
 float4 SpecularMapColor = tex2D( SpecularMapSampler, In.SpecularMapUV );
 float4 FinalColor = DiffuseMapColor * ( g_vLightColor * ( In.DiffusePower + 0.5 ) ) + ( SpecularMapColor * In.SpecularPower );
 FinalColor.a = DiffuseMapColor.a;
 return FinalColor;
}
float4 PS_DiffuseMap_SpecularMap_Light1_TextureFactor( sPS_DiffuseMap_SpecularMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DiffuseMapUV );
 float4 SpecularMapColor = tex2D( SpecularMapSampler, In.SpecularMapUV );
 float4 FinalColor = ( DiffuseMapColor * ( g_vLightColor * ( In.DiffusePower + 0.5 ) ) + ( SpecularMapColor * In.SpecularPower ) ) * g_TexFactor;
 FinalColor.a = DiffuseMapColor.a * g_TexFactor.a;
 return FinalColor;
}
float4 PS_DiffuseMap_ShadeMap_Light1( sPS_DiffuseMap_ShadeMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DiffuseMapUV );
 float4 ShadeColor = tex2D( ShadeMapSampler, In.ShadeMapUV );
 float4 FinalColor = DiffuseMapColor * ( g_vLightColor * ShadeColor * 2 );
 FinalColor.a = DiffuseMapColor.a;
 return FinalColor;
}
float4 PS_DiffuseMap_ShadeMap_Light1_TextureFactor( sPS_DiffuseMap_ShadeMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DiffuseMapUV );
 float4 ShadeColor = tex2D( ShadeMapSampler, In.ShadeMapUV );
 float4 FinalColor = DiffuseMapColor * ( g_vLightColor * ShadeColor * 2 ) * g_TexFactor;
 FinalColor.a = DiffuseMapColor.a * g_TexFactor.a;
 return FinalColor;
}
float4 PS_DiffuseMap_SpecularMap_ShadeMap_Light1( sPS_DiffuseMap_SpecularMap_ShadeMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DiffuseMapUV );
 float4 SpecularMapColor = tex2D( SpecularMapSampler, In.SpecularMapUV );
 float4 ShadeColor = tex2D( ShadeMapSampler, In.ShadeMapUV );
 float4 FinalColor = DiffuseMapColor * ( g_vLightColor * ShadeColor * 2 ) + ( SpecularMapColor * In.SpecularPower );
 FinalColor.a = DiffuseMapColor.a;
 return FinalColor;
}
float4 PS_DiffuseMap_SpecularMap_ShadeMap_Light1_TextureFactor( sPS_DiffuseMap_SpecularMap_ShadeMap_Light1 In ) : COLOR {
 float4 DiffuseMapColor = tex2D( DiffuseMapSampler, In.DiffuseMapUV );
 float4 SpecularMapColor = tex2D( SpecularMapSampler, In.SpecularMapUV );
 float4 ShadeColor = tex2D( ShadeMapSampler, In.ShadeMapUV );
 float4 FinalColor = ( DiffuseMapColor * ( g_vLightColor * ShadeColor * 2 ) + ( SpecularMapColor * In.SpecularPower ) ) * g_TexFactor;
 FinalColor.a = DiffuseMapColor.a * g_TexFactor.a;
 return FinalColor;
}
technique Technique_FX {
 pass p0 {
 VertexShader = compile vs_2_0 VS_FX();
 PixelShader = compile ps_2_0 PS_FX();
 }
 pass p1 {
 VertexShader = compile vs_2_0 VS_FX();
 PixelShader = compile ps_2_0 PS_FX();
 }
}
technique Technique_DiffuseMap_LightMap {
 pass p0 {
 VertexShader = compile vs_1_1 VS_DiffuseMap_LightMap();
 PixelShader = compile ps_1_1 PS_DiffuseMap_LightMap();
 }
 pass p1 {
 VertexShader = compile vs_1_1 VS_DiffuseMap_LightMap();
 PixelShader = compile ps_1_1 PS_DiffuseMap_LightMap();
 }
}
technique Technique_DiffuseMap_LightMap_SpecularMap {
 pass p0 {
 VertexShader = compile vs_1_1 VS_DiffuseMap_LightMap_SpecularMap();
 PixelShader = compile ps_1_1 PS_DiffuseMap_LightMap_SpecularMap();
 }
 pass p1 {
 VertexShader = compile vs_1_1 VS_DiffuseMap_LightMap_SpecularMap();
 PixelShader = compile ps_1_1 PS_DiffuseMap_LightMap_SpecularMap();
 }
}
technique Technique_DiffuseMap_Light0 {
 pass p0 {
 VertexShader = compile vs_1_1 VS_DiffuseMap_Light0();
 PixelShader = compile ps_1_1 PS_DiffuseMap_Light0();
 }
 pass p1 {
 VertexShader = compile vs_1_1 VS_DiffuseMap_Light0();
 PixelShader = compile ps_1_1 PS_DiffuseMap_Light0_TextureFactor();
 }
}
technique Technique_DiffuseMap_Light1 {
 pass p0 {
 VertexShader = compile vs_1_1 VS_DiffuseMap_Light1();
 PixelShader = compile ps_1_1 PS_DiffuseMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_1_1 VS_DiffuseMap_Light1();
 PixelShader = compile ps_1_1 PS_DiffuseMap_Light1_TextureFactor();
 }
}
technique Technique_DiffuseMap_SpecularMap_Light1 {
 pass p0 {
 VertexShader = compile vs_1_1 VS_DiffuseMap_SpecularMap_Light1();
 PixelShader = compile ps_1_1 PS_DiffuseMap_SpecularMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_1_1 VS_DiffuseMap_SpecularMap_Light1();
 PixelShader = compile ps_1_1 PS_DiffuseMap_SpecularMap_Light1_TextureFactor();
 }
}
technique Technique_DiffuseMap_ShadeMap_Light1 {
 pass p0 {
 VertexShader = compile vs_1_1 VS_DiffuseMap_ShadeMap_Light1();
 PixelShader = compile ps_1_1 PS_DiffuseMap_ShadeMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_1_1 VS_DiffuseMap_ShadeMap_Light1();
 PixelShader = compile ps_1_1 PS_DiffuseMap_ShadeMap_Light1_TextureFactor();
 }
}
technique Technique_DiffuseMap_SpecularMap_ShadeMap_Light1 {
 pass p0 {
 VertexShader = compile vs_1_1 VS_DiffuseMap_SpecularMap_ShadeMap_Light1();
 PixelShader = compile ps_1_1 PS_DiffuseMap_SpecularMap_ShadeMap_Light1();
 }
 pass p1 {
 VertexShader = compile vs_1_1 VS_DiffuseMap_SpecularMap_ShadeMap_Light1();
 PixelShader = compile ps_1_1 PS_DiffuseMap_SpecularMap_ShadeMap_Light1_TextureFactor();
 }
}
technique Technique_DiffuseMap_LightMap_Low {
 pass p0 {
 VertexShader = compile vs_1_1 VS_DiffuseMap_LightMap_Low();
 PixelShader = compile ps_1_1 PS_DiffuseMap_Light0();
 }
 pass p1 {
 VertexShader = compile vs_1_1 VS_DiffuseMap_LightMap_Low();
 PixelShader = compile ps_1_1 PS_DiffuseMap_Light0_TextureFactor();
 }
}
```
