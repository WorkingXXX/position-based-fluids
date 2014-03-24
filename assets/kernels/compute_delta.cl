__kernel void computeDelta(__constant struct Parameters *Params,
                           volatile __global int *debugBuf,
                           __global float4 *delta,
                           const __global float4 *predicted, // xyz=predicted, w=scaling
                           const __global uint *friends_list,
                           const float wave_generator,
                           const int N)
{
    const int i = get_global_id(0);

    if (i >= N) return;
    
#ifdef LOCALMEM
    #define local_size       (256)

    const uint local_id = get_local_id(0);
    const uint group_id = get_group_id(0);

    // Load data into shared block
    __local float4 loc_predicted[local_size]; 
    loc_predicted[local_id] = predicted[i];
    barrier(CLK_LOCAL_MEM_FENCE);
#endif

	__private float4 particle = predicted[i];

    uint2 randSeed = (uint2)(1 + get_global_id(0), 1);

    // Sum of lambdas
    float3 sum = (float3)0.0f;
    const float h_2_cache = Params->h_2;
    const float h_cache = Params->h;

    // equation (13)
    const float q_2 = pow(Params->surfaceTenstionDist * h_cache, 2);
    const float poly6_q = pow(h_2_cache - q_2, 3);

#ifdef LOCALMEM
    int localHit =0;
    int localMiss = 0;
#endif

    // Start grid scan
	uint listsCount = friends_list[i];
	for(int listIndex = 0; listIndex < listsCount; ++listIndex) {
		
		int startIndex = MAX_PARTICLES_COUNT + i * (27*2) + listIndex * 2 + 0;
		int lengthIndex = startIndex + 1;
		
		uint start = friends_list[startIndex];
		uint length = friends_list[lengthIndex];
		uint end = start + length;
		
		// iterate over all particles in this cell
		for(int j_index = start; j_index < end; ++j_index) {
			// Skip self
			if (i == j_index)
				continue;

			// Get j particle data
#ifdef LOCALMEM
			float4 j_data;
			if (j_index / local_size == group_id)
			{
				j_data = loc_predicted[j_index % local_size];
			//     localHit++;
			//     atomic_inc(&stats[0]);
			}
			else
			{
				j_data = predicted[j_index];
			//     localMiss++;
			//     atomic_inc(&stats[1]);
			}
#else
			const float4 j_data = predicted[j_index];
#endif

			// Compute r, length(r) and length(r)^2
			const float3 r         = particle.xyz - j_data.xyz;
			const float r_length_2 = dot(r, r);

			if (r_length_2 < h_2_cache)
			{
				const float r_length   = sqrt(r_length_2);

				const float3 gradient_spiky = r / (r_length) *
											  (h_cache - r_length) *
											  (h_cache - r_length);

				const float r_2_diff = h_2_cache - r_length_2;
				const float poly6_r = r_2_diff * r_2_diff * r_2_diff;

				const float r_q_radio = poly6_r / poly6_q;
				const float s_corr = Params->surfaceTenstionK * r_q_radio * r_q_radio * r_q_radio * r_q_radio;

				// Sum for delta p of scaling factors and grad spiky (equation 12)
				sum += (particle.w + j_data.w + s_corr) * gradient_spiky;
			}
		}
    }

    // equation (12)
    float3 delta_p = (-GRAD_SPIKY_FACTOR*sum) / Params->restDensity;

    float randDist = 0.005f;
    float3 future = particle.xyz + delta_p;

    // Prime the random... DO NOT REMOVE
    frand(&randSeed);
    frand(&randSeed);
    frand(&randSeed);

    // Clamp Y
    if (future.y < Params->yMin) future.y = Params->yMin + frand(&randSeed) * randDist;
    if (future.z < Params->zMin) future.z = Params->zMin + frand(&randSeed) * randDist;
    else if (future.z > Params->zMax) future.z = Params->zMax - frand(&randSeed) * randDist;
    if (future.x < (Params->xMin + wave_generator))  future.x = Params->xMin + wave_generator + frand(&randSeed) * randDist;
    else if (future.x > (Params->xMax))  future.x = Params->xMax                  - frand(&randSeed) * randDist;

    // Compute delta
    delta[i].xyz = future - particle.xyz;

//    if(group_id == 0) {
  //      printf("%d: hits: %d vs miss: %d\n", i, localHit,localMiss);
    //}

    // #if defined(USE_DEBUG)
    //printf("compute_delta: result: i: %d (N=%d)\ndelta: [%f,%f,%f,%f]\n",
    //      i, NeighborCount,
    //      delta[i].x, delta[i].y, delta[i].z, minR);
    //printf("Particle i=%d: Neighbors=%d/%d ClosestParticle=%fh\n", i, NeighborCount, ScanCount, minR/Params->h);
    // #endif
}
