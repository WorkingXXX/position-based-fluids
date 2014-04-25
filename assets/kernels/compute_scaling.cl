__kernel void computeScaling(__constant struct Parameters *Params,
                             cbufferf_readonly imgPredicted,
                             __global float *density,
                             __global float *lambda,
                             const __global int *friends_list,
                             const int N)
{
    // Scaling = lambda
    const int i = get_global_id(0);

    // const size_t local_size = 400;
    // const uint li = get_local_id(0);
    // const uint group_id = get_group_id(0);

    // float4 i_data;
    // if (i >= N) {
    //     i_data = (float4)(0.0f);
    // } else {
    //     i_data = predicted[i];
    // }

    // // Load data into shared block
    // __local float4 loc_predicted[local_size]; //size=local_size*4*4
    // loc_predicted[li] = i_data;
    // barrier(CLK_LOCAL_MEM_FENCE | CLK_GLOBAL_MEM_FENCE);    

    if (i >= N) return;

    // Read particle "i" position
    float3 particle_i = cbufferf_read(imgPredicted, i).xyz;
    
    // Cache parameters
    const float e = Params->epsilon * Params->restDensity;

    // Sum of rho_i, |nabla p_k C_i|^2 and nabla p_k C_i for k = i
    float density_sum = 0.0f;
    float gradient_sum_k = 0.0f;
    float3 gradient_sum_k_i = (float3) 0.0f;

    // read number of friends
    int totalFriends = 0;
    int circleParticles[MAX_FRIENDS_CIRCLES];
    for (int j = 0; j < MAX_FRIENDS_CIRCLES; j++)
        totalFriends += circleParticles[j] = friends_list[j * MAX_PARTICLES_COUNT + i];

    int proccedFriends = 0;
    for (int iCircle = 0; iCircle < MAX_FRIENDS_CIRCLES; iCircle++)
    {
        // Check if we want to process/skip next friends circle
        if (((float)proccedFriends) / totalFriends > 0.6f)
            continue;

        // Add next circle to process count
        proccedFriends += circleParticles[iCircle];

        // Compute friends start offset
        int baseIndex = FRIENDS_BLOCK_SIZE +                                      // Skip friendsCount block
                        iCircle * (MAX_PARTICLES_COUNT * MAX_FRIENDS_IN_CIRCLE) + // Offset to relevent circle
                        i;                                                        // Offset to particle_index                              

        // Process friends in circle
        for (int iFriend = 0; iFriend < circleParticles[iCircle]; iFriend++)
        {
            // Read friend index from friends_list
            const int j_index = friends_list[baseIndex + iFriend * MAX_PARTICLES_COUNT];

            // Get j particle data
            const float3 position_j = cbufferf_read(imgPredicted, j_index).xyz;

            const float3 r = particle_i - position_j;
            const float r_length_2 = dot(r,r);

            // Required for numerical stability
            if (r_length_2 < Params->h_2)
            {
                const float r_length = sqrt(r_length_2);

                // CAUTION: the two spiky kernels are only the same
                // because the result is only used sqaured
                // equation (8), if k = i
                const float h_r_diff = Params->h - r_length;
                const float3 gradient_spiky = GRAD_SPIKY_FACTOR * h_r_diff * h_r_diff *
                                              r / r_length;

                // equation (2)
                const float h2_r2_diff = Params->h_2 - r_length_2;
                density_sum += h2_r2_diff * h2_r2_diff * h2_r2_diff;

                // equation (9), denominator, if k = j
                gradient_sum_k += dot(gradient_spiky, gradient_spiky);

                // equation (8), if k = i
                gradient_sum_k_i += gradient_spiky;
            }
        }
    }

    // Apply Poly6 factor to density and save density
    density_sum *= POLY6_FACTOR;
    density[i] = density_sum;

    // equation (9), denominator, if k = i
    gradient_sum_k += dot(gradient_sum_k_i, gradient_sum_k_i);

    // equation (1)
    float density_constraint = (density_sum / Params->restDensity) - 1.0f;

    // equation (11)
    float scalingResult = -1.0f * density_constraint /
                          (gradient_sum_k / (Params->restDensity * Params->restDensity) + e);
                          
    lambda[i] = scalingResult;
}
