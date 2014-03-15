#version 430

uniform sampler2D depthTexture;
uniform sampler2D particlesPos;
uniform usampler2D visParticles;

uniform mat4      MV_Matrix;
uniform mat4      iMV_Matrix;
uniform mat4      Proj_Matrix;
uniform vec2      depthRange;
uniform vec2      invFocalLen; // See http://stackoverflow.com/questions/17647222/ssao-changing-dramatically-with-camera-angle

layout(r32i) uniform iimage2D grid_chain;
layout(r32i) uniform iimage2D grid;

// Particles related
uniform float     smoothLength;
//uniform float     poly6Factor;
//uniform float     poly6GradFactor;
uniform int       particlesCount;
uniform uint      currentCycleID;

// outputs
out vec4 result; 

// Local variables
ivec2 frameSize; 
ivec2 iuv;

vec4 uvToWorld(ivec2 texCoord)
{
    // sample depth buffer
    float z = texelFetch(depthTexture, texCoord, 0).x;
    
    // clipping
    if(z == gl_DepthRange.far) discard;
    
    // Linearise "z"
    float near = depthRange.x;
    float far = depthRange.y;
    float linearZ = near / (far - z * (far - near)) * far;
    
    // convert texture coordinate to -invFocalLen .. +invFocalLen
    vec2 focal_uv = ((texCoord + vec2(0.5)) / frameSize * 2.0 - 1.0) * invFocalLen;

    // homogeneous space coordinates
    vec4 homoPos = vec4(focal_uv * linearZ, -linearZ, 1.0);
    
    // view space coords 
    // vsPos = homoPos.xyz / homoPos.w;

    // inverse to model coords
    vec4 ret = iMV_Matrix * homoPos;
    return ret ;
}

vec3 uvToViewSpace(ivec2 texCoord)
{
    // sample depth buffer
    float z = texelFetch(depthTexture, texCoord, 0).x;
    
    // clipping
    if(z == gl_DepthRange.far) discard;
    
    // Linearise "z"
    float near = depthRange.x;
    float far = depthRange.y;
    float linearZ = near / (far - z * (far - near)) * far;
    
    // convert texture coordinate to -invFocalLen .. +invFocalLen
    vec2 focal_uv = ((texCoord + vec2(0.5)) / frameSize * 2.0 - 1.0) * invFocalLen;

    // homogeneous space coordinates
    vec4 homoPos = vec4(focal_uv * linearZ, -linearZ, 1.0);
    
    // view space coords 
    return homoPos.xyz / homoPos.w;
}

float ViewSpaceDepth_to_ZBufferDepth(vec3 viewSpacePos)
{
    // convert to ClipSpace
    vec4 clipSpacePos = Proj_Matrix * vec4(viewSpacePos, 1.0);
    float ndcDepth    = clipSpacePos.z/clipSpacePos.w;
    
    // Clip adjusted-z
    if (ndcDepth < -1.0)
        discard;
 
    // Transform into window coordinates coordinates 
    float near = 0;
    float far  = 1;
    return (abs(far - near) * ndcDepth + near + far) / 2.0;
}

vec3 GetParticlePos(int index)
{
    // Compute texture location
    ivec2 texSize = textureSize(particlesPos, 0);
    ivec2 texCoord = ivec2(index % texSize.x, index / texSize.x);
    
    // Get position value
    return texelFetch(particlesPos, texCoord, 0).xyz;
}

float GetDensity(vec3 worldPos)
{
    int visMapWidth = textureSize(visParticles, 0).x;
    
    float h_2 = smoothLength*smoothLength;
    
    float density = 0.0;
    for (int i = 0; i < particlesCount; i++)
    {
        // Check if we should skip this particle (not visible)
        uint partCycleID = texelFetch(visParticles, ivec2(i % visMapWidth, i / visMapWidth), 0).x;
        if (partCycleID != currentCycleID) continue;
    
        // Get particles position
        vec3 partPos = GetParticlePos(i);
        
        // find distance^2 between pixel and particles
        vec3 delta = partPos - worldPos;
        float r_2 = dot(delta, delta);

        // Check if out of range
        if (r_2 < h_2)
        {
            // append density
            float h_2_r_2_diff = h_2 - r_2;
            density += h_2_r_2_diff * h_2_r_2_diff * h_2_r_2_diff;    
        }
    }
    
    return density;
}

int expandBits(int x)
{
    x = (x | (x << 16)) & 0x030000FF;
    x = (x | (x <<  8)) & 0x0300F00F;
    x = (x | (x <<  4)) & 0x030C30C3;
    x = (x | (x <<  2)) & 0x09249249;

    return x;
}

int mortonNumber(ivec3 gridPos)
{
    return expandBits(gridPos.x) | (expandBits(gridPos.y) << 1) | (expandBits(gridPos.z) << 2);
}

uint calcGridHash(ivec3 gridPos)
{
    return mortonNumber(gridPos) % /*GRID_BUF_SIZE*/ (2048*2048);
}

void SampleDensityAndGradient(vec3 worldPos, out float density, out vec3 gradient)
{
    // Cash h^2
    float h_2 = smoothLength*smoothLength;
	float h = smoothLength;

    // Reset density sum
    density = 0.0;
    gradient = vec3(0);
    int count = 0;
    float poly6_density = 0.0;
    float spiky_density = 0.0;
    vec3 poly6_gradient = vec3(0);
    vec3 spiky_gradient = vec3(0);
    
    // Compute Grid position
    ivec3 gridCell = ivec3(worldPos / smoothLength);

    float PI = 3.141592;
    float POLY6_KERNEL      =  315.0 / (64.0 * PI * pow(smoothLength,9));
    float GRAD_POLY6_KERNEL =  945.0 / (32.0 * PI * pow(smoothLength,9));
    float SPIKY_KERNEL      =   15.0 / (       PI * pow(smoothLength,6));
    float GRAD_SPIKY_KERNEL =   45.0 / (       PI * pow(smoothLength,6));

    // scan 3x3x3 cells
    for (int iz = -1; iz <= +1; iz++)
    {
        for (int iy = -1; iy <= +1; iy++)
        {
            for (int ix = -1; ix <= +1; ix++)
            {
                // Find first cell particle
                uint gridOffset = calcGridHash(gridCell + ivec3(ix, iy, iz));
                int partIdx = imageLoad(grid, ivec2(gridOffset % 2048, gridOffset / 2048)).x;
                
                // Scan chain
                while (partIdx != -1)
                {
                    // Get particles position
                    vec3 partPos = GetParticlePos(partIdx);
                    
                    // find distance^2 between pixel and particles
                    vec3 delta = worldPos - partPos;
                    float r_2 = dot(delta, delta);
                    
                    // Check if out of range
                    if (r_2 < h_2)
                    {
                        // count in-range particles
                        count++;

                        // sum gradient
                        float r_length = sqrt(r_2);
                        float h_r_diff = h - r_length;
                        
                        spiky_gradient += h_r_diff * h_r_diff * (delta/r_length);
                        spiky_density  += h_r_diff * h_r_diff * h_r_diff;    
                        
                        float h_2_r_2_diff = h_2 - r_2;
                        poly6_gradient += delta * h_2_r_2_diff * h_2_r_2_diff;
                        poly6_density  += h_2_r_2_diff * h_2_r_2_diff * h_2_r_2_diff;    
                    }
                    
                    // Get next particle in chain
                    partIdx = imageLoad(grid_chain, ivec2(partIdx % 2048, partIdx / 2048)).x;
                }
            }
        }
    }

    // poly6 + poly6 gradient
    density  = poly6_density  * POLY6_KERNEL; 
    gradient = poly6_gradient * GRAD_POLY6_KERNEL; 

    // poly6 + spiky gradient 
    density  = poly6_density  * POLY6_KERNEL;
    gradient = spiky_gradient * GRAD_SPIKY_KERNEL;

    // spiky + spiky gradient 
    density  = spiky_density  * SPIKY_KERNEL;
    gradient = spiky_gradient * GRAD_SPIKY_KERNEL;
}

void main()
{
    // Get frame coord
    iuv = ivec2(gl_FragCoord.xy);
    
    // get buffer size (we assume same buffer size for depthTexture and target)
    frameSize = textureSize(depthTexture, 0);
    
    // get camera position (model space)
    vec3 cameraPos = iMV_Matrix[3].xyz;
    
    // get pixels position (model space)
    vec3 modelPos = uvToWorld(iuv).xyz;
    
    // compute pixel-camera normal (model space)
    vec3 pixelCamNorm = normalize(cameraPos - modelPos);
    
    float TargetDensity = 0.0;

    float PI = 3.141592;
    float POLY6_KERNEL      = 315.0 / (64.0 * PI * pow(smoothLength,9));
    float SPIKY_KERNEL      =  15.0 / (       PI * pow(smoothLength,6));

    // ignore scale
    //TargetDensity = 1.0/400000000.0;

    // with scale
    TargetDensity = POLY6_KERNEL / 400000000.0;
    
    TargetDensity = SPIKY_KERNEL / 90000.0;
    
    //result = vec4(GetDensity(modelPos), modelPos.x, 0, 1);
    //return;
    
    int   iterations = 0;
    vec3  currPos = modelPos;
    float currDensity = 1.0E10;
    vec3  prevPos = currPos;
    float prevDensity = currDensity;
	vec3 gradient = vec3(0);
	float camNormalGradient_Length;
	vec3 camNormalGradient;
	float stepSize;
    float tp = 0.0;
    for (int iIter = 0; iIter < 3; iIter++)
    {
        iterations++;
        
        // Compute shifted test point
        prevPos = currPos;
        currPos = modelPos + tp * pixelCamNorm;

        // Store prev value
        prevDensity = currDensity;

        // get test point density
        SampleDensityAndGradient(currPos, currDensity, gradient);
        
        // Compute gradient along camera normal
        camNormalGradient_Length = dot(pixelCamNorm, gradient);
        camNormalGradient = pixelCamNorm * camNormalGradient_Length;
        camNormalGradient_Length = max(camNormalGradient_Length, 0.9);
        
        // Compute step size
        stepSize = (currDensity - TargetDensity) / camNormalGradient_Length;
        
        // update tp
        tp += stepSize; 
        
        // Exit when reached taget
        // if (abs(currDensity - TargetDensity) < 0.000000000005)
        //    break;
    }
    
    // Interpolate position
    float ratio = (TargetDensity - currDensity) / (prevDensity - currDensity);
    vec3 shellPos = currPos;
   
    // Convert back to viewspace
    vec3 viewSpaceDepth = (MV_Matrix * vec4(shellPos, 1.0)).xyz;
    
    // Convert viewspace to z-buffer depth
    float zbufferDepth = ViewSpaceDepth_to_ZBufferDepth(viewSpaceDepth);
    
    // Compose result
    result = vec4(gradient*1000000, 1);
    //result = vec4(camNormalGradient*10000000, 1);
    //result = vec4(currDensity*000000000,stepSize*100,0, 1);
    result = vec4(zbufferDepth, iterations, 0, 1);
    
    
    // Test: input depth, output depth, vsInput, vsOutput 
    // float zbufferInput = texelFetch(depthTexture, iuv, 0).x;
    // vec3 vsPos = uvToViewSpace(iuv);
    // result = vec4(zbufferInput, zbufferDepth, vsPos.z, viewSpaceDepth.z);
}