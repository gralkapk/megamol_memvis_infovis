#version 140

#extension GL_ARB_shader_storage_buffer_object : require
#extension GL_EXT_gpu_shader4 : require

#define CLIP
#define DEPTH
#define SMALL_SPRITE_LIGHTING
#define CALC_CAM_SYS

#ifdef DEBUG
#undef CLIP
#define RETICLE
#define AXISHINTS
#endif // DEBUG

//#define BULLSHIT

#ifndef FLACH
#define FLACH
#endif


struct SphereParams {
    float posX; float posY; float posZ;
};

layout(packed, binding = 2) buffer shader_data {
    SphereParams theBuffer[];
};



uniform float inRad;



uniform vec4 viewAttr;

uniform float scaling;
uniform float alphaScaling;
uniform float zNear;

#ifndef CALC_CAM_SYS
uniform vec3 camIn;
uniform vec3 camUp;
uniform vec3 camRight;
uniform mat4 modelViewProjection;
uniform mat4 modelViewInverse;
uniform mat4 modelView;
#else
uniform mat4 modelViewProjection;
uniform mat4 modelViewInverse;
uniform mat4 modelView;
#endif // CALC_CAM_SYS

// clipping plane attributes
uniform vec4 clipDat;
uniform vec4 clipCol;
uniform int instanceOffset;
uniform int attenuateSubpixel;

uniform vec4 inConsts1;
in float colIdx;
uniform sampler1D colTab;

uniform uint idx;
uniform int pik;
uniform uint idxOffset;

out vec4 objPos;
out vec4 camPos;
//varying vec4 lightPos;
out float squarRad;
out float rad;
out float effectiveDiameter;

#ifdef DEFERRED_SHADING
out float pointSize;
#endif

#ifdef RETICLE
out vec2 centerFragment;
#endif // RETICLE

out vec4 vsColor;

#define CONSTRAD inConsts1.x
#define MIN_COLV inConsts1.y
#define MAX_COLV inConsts1.z
#define COLTAB_SIZE inConsts1.w


void main(void) {
    float theColIdx;
    vec4 theColor;
    vec4 inPos;

    inPos = vec4(theBuffer[gl_VertexID + instanceOffset].posX,
        theBuffer[gl_VertexID + instanceOffset].posY,
        0.0f, 1.0f);

    rad = inConsts1.x;

    // remove the sphere radius from the w coordinates to the rad varyings
    //vec4 inPos = gl_Vertex;
    //rad = (CONSTRAD < -0.5) ? inPos.w : CONSTRAD;
    //inPos.w = 1.0;
    //inPos = vec4(0.0, 0.0, 0.0, 1.0);
    //rad = 1.0;

    // float cid = MAX_COLV - MIN_COLV;
    // if (cid < 0.000001) {
    //     vsColor = theColor;
    // } else {
    //     cid = (theColIdx - MIN_COLV) / cid;
    //     cid = clamp(cid, 0.0, 1.0);
        
    //     cid *= (1.0 - 1.0 / COLTAB_SIZE);
    //     cid += 0.5 / COLTAB_SIZE;
        
    //     vsColor = texture(colTab, cid);
    // }

if (inConsts1.w == 0) {
    vsColor = vec4(1.0f);
}else{
    // lookup in texture
    float cid = inConsts1.z - inConsts1.y;
     cid = (theBuffer[gl_VertexID + instanceOffset].posZ - inConsts1.y) / cid;
     cid = clamp(cid, 0.0, 1.0);
        
     cid *= (1.0 - 1.0 / inConsts1.w);
     cid += 0.5 / inConsts1.w;
        
     vsColor = texture(colTab, cid);
}




    rad *= scaling;

    squarRad = rad * rad;



    // object pivot point in object space    
    objPos = inPos; // no w-div needed, because w is 1.0 (Because I know)

    // calculate cam position
    camPos = modelViewInverse[3]; // (C) by Christoph
    camPos.xyz -= objPos.xyz; // cam pos to glyph space

    // calculate light position in glyph space
    //lightPos = modelViewInverse * gl_LightSource[0].position;



    // Sphere-Touch-Plane-Approach™
    vec2 winHalf = 2.0 / viewAttr.zw; // window size

    vec2 d, p, q, h, dd;

    // get camera orthonormal coordinate system
    vec4 tmp;

#ifdef CALC_CAM_SYS
    // camera coordinate system in object space
    tmp = modelViewInverse[3] + modelViewInverse[2];
    vec3 camIn = normalize(tmp.xyz);
    tmp = modelViewInverse[3] + modelViewInverse[1];
    vec3 camUp = tmp.xyz;
    vec3 camRight = normalize(cross(camIn, camUp));
    camUp = cross(camIn, camRight);
#endif // CALC_CAM_SYS

    vec2 mins, maxs;
    vec3 testPos;
    vec4 projPos;

    // projected camera vector
    vec3 c2 = vec3(dot(camPos.xyz, camRight), dot(camPos.xyz, camUp), dot(camPos.xyz, camIn));

    vec3 cpj1 = camIn * c2.z + camRight * c2.x;
    vec3 cpm1 = camIn * c2.x - camRight * c2.z;

    vec3 cpj2 = camIn * c2.z + camUp * c2.y;
    vec3 cpm2 = camIn * c2.y - camUp * c2.z;
    
    d.x = length(cpj1);
    d.y = length(cpj2);

    dd = vec2(1.0) / d;

    p = squarRad * dd;
    q = d - p;
    h = sqrt(p * q);
    //h = vec2(0.0);
    
    p *= dd;
    h *= dd;

    cpj1 *= p.x;
    cpm1 *= h.x;
    cpj2 *= p.y;
    cpm2 *= h.y;

    // TODO: rewrite only using four projections, additions in homogenous coordinates and delayed perspective divisions.
    testPos = objPos.xyz + cpj1 + cpm1;
    projPos = modelViewProjection * vec4(testPos, 1.0);
    //projPos /= projPos.w;
    mins = projPos.xy;
    maxs = projPos.xy;

    testPos -= 2.0 * cpm1;
    projPos = modelViewProjection * vec4(testPos, 1.0);
    //projPos /= projPos.w;
    mins = min(mins, projPos.xy);
    maxs = max(maxs, projPos.xy);

    testPos = objPos.xyz + cpj2 + cpm2;
    projPos = modelViewProjection * vec4(testPos, 1.0);
    //projPos /= projPos.w;
    mins = min(mins, projPos.xy);
    maxs = max(maxs, projPos.xy);

    testPos -= 2.0 * cpm2;
    projPos = modelViewProjection * vec4(testPos, 1.0);
    //projPos /= projPos.w;
    mins = min(mins, projPos.xy);
    maxs = max(maxs, projPos.xy);
    //gl_Position = vec4((mins + maxs) * 0.5, 0.0, (od > clipDat.w) ? 0.0 : 1.0);
    //gl_Position = vec4((mins + maxs) * 0.5, 0.0, 1.0);
    gl_Position = modelViewProjection*objPos;
    maxs = (maxs - mins) * 0.5 * winHalf;
    //gl_PointSize = max(maxs.x, maxs.y) + 0.5;
    gl_PointSize = rad*(modelViewProjection[0][0]);

    if (pik==1) {
    if (uint(gl_VertexID)+idxOffset == idx) {
        vsColor = vec4(1.0, 0.0, 0.0f, 1.0f);
        gl_PointSize *= 100;
    }
}

    if (attenuateSubpixel == 1) {
        effectiveDiameter = gl_PointSize;
    } else {
        effectiveDiameter = 1.0;
    }

    //vec4 projPos = gl_ModelViewProjectionMatrix * vec4(objPos.xyz, 1.0);
    //projPos /= projPos.w;
    //gl_Position = projPos;
    //float camDist = sqrt(dot(camPos.xyz, camPos.xyz));
    //gl_PointSize = max((rad / camDist) * zNear, 1.0);

}