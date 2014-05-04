// Important: below arrays are Row Major (http://en.wikipedia.org/wiki/Row-major_order)
// friendsData = struct
// {
//     Uint32 friendsCount[MAX_FRIENDS_CIRCLES][MAX_PARTICLES_COUNT];
//     Uint32 friendIndex[MAX_FRIENDS_CIRCLES][MAX_FRIENDS_IN_CIRCLE][MAX_PARTICLES_COUNT];
// }
// 
// Related defines
//     MAX_PARTICLES_COUNT          Defines how many particles exists in the simulation
//     MAX_FRIENDS_CIRCLES          Defines how many friends circle are we going to scan for
//     MAX_FRIENDS_IN_CIRCLE        Defines the max number of particles per cycle
//     
// Cached values
//     FRIENDS_BLOCK_SIZE = MAX_PARTICLES_COUNT * MAX_FRIENDS_CIRCLES;
// 
// Access patterns
//     friendsCount[CircleIndex][ParticleIndex]             ==> Data[CircleIndex * MAX_PARTICLES_COUNT + ParticleIndex];
//     friendIndex[CircleIndex][FriendIndex][ParticleIndex] ==> Data[FRIENDS_BLOCK_SIZE +                                             // Skip friendsCount block
//                                                                   CircleIndex * (MAX_PARTICLES_COUNT * MAX_FRIENDS_IN_CIRCLE) +    // Offset to relevent circle
//                                                                   FriendIndex * (MAX_PARTICLES_COUNT)                              // Offset to relevent friend_index
//                                                                   ParticleIndex];                                                  // Offset to particle_index

__kernel void buildFriendsList(__constant struct Parameters *Params,
                               cbufferf_readonly imgPredicted,
                               const __global uint *cells,
                               __global int4 *friends_list,
                               const int N)
{
    const int i = get_global_id(0);
    if (i >= N) return;
    
    const float MIN_R = 0.3f * Params->h;
    
    // Read "i" particles data
    float3 predicted_i = cbufferf_read(imgPredicted, i).xyz;

    // const int3 gridoffsets[27] = {
    //     (int3) (-1, -1, -1),
    //     (int3) (-1, -1, 0),
    //     (int3) (-1, -1, 1),
    //     (int3) (-1, 0, -1),
    //     (int3) (-1, 0, 0),
    //     (int3) (-1, 0, 1),
    //     (int3) (-1, 1, -1),
    //     (int3) (-1, 1, 0),
    //     (int3) (-1, 1, 1),

    //     (int3) (0, -1, -1),
    //     (int3) (0, -1, 0),
    //     (int3) (0, -1, 1),
    //     (int3) (0, 0, -1),
    //     (int3) (0, 0, 0),
    //     (int3) (0, 0, 1),
    //     (int3) (0, 1, -1),
    //     (int3) (0, 1, 0),
    //     (int3) (0, 1, 1),

    //     (int3) (1, -1, -1),
    //     (int3) (1, -1, 0),
    //     (int3) (1, -1, 1),
    //     (int3) (1, 0, -1),
    //     (int3) (1, 0, 0),
    //     (int3) (1, 0, 1),
    //     (int3) (1, 1, -1),
    //     (int3) (1, 1, 0),
    //     (int3) (1, 1, 1)
    // };
    const int3 gridoffsets[9] = {
            (int3) (0, -1, -1),
            (int3) (0, -1, 0),
            (int3) (0, -1, 1),
            (int3) (0, 0, -1),
            (int3) (0, 0, 0),
            (int3) (0, 0, 1),
            (int3) (0, 1, -1),
            (int3) (0, 1, 0),
            (int3) (0, 1, 1)
    };

    const int3 gridxoffset = (int3) (1, 0, 0);

    // int neighborCells[27];
    int neighborCells[9];

    // Start grid scan
    int3 current_cell = convert_int3(predicted_i / Params->h);

    for (int o = 0; o < 9; ++o)
    {
        int numcells = 0;
        int entries = 0;
        int cell = -1;

        for (int j = -1; j <= 1; ++j)
        {
            int3 offset_cell = current_cell + gridoffsets[o] + j * gridxoffset;
            uint cell_index = calcGridHash(offset_cell);  

            int2 cell_boundary = (int2)(cells[cell_index*2+0], cells[cell_index*2+1]);

            int c = cell_boundary.x;

            if (cell == -1) cell = c;
            if (c != -1)
            {
                int end = cell_boundary.y + 1;
                entries += end - c;
            }
        }

        if(cell == END_OF_CELL_LIST)
        {
            neighborCells[o] = -1;
        }
        else 
        {
            neighborCells[o] = cell + (entries<<24);   
        }

        //neighborCells[o] = cell + (entries<<24);

        
        
        // if(i==0) {
        //     printf("friend: %d, current_cell(%d,%d,%d), gridoffsets(%d,%d,%d), offset_cell(%d,%d,%d), cell_index: %d\n",
        //             i,          
        //             current_cell.x,current_cell.y,current_cell.z, 
        //             gridoffsets[o].x,gridoffsets[o].y,gridoffsets[o].z, 
        //             offset_cell.x,offset_cell.y,offset_cell.z, 
        //             cell_index);
        // }

        // if(i==0) {
        //     printf("friend: %d, %d: (%d,%d,%d)\n",i,o,cell_boundary.x,cell_boundary.y - cell_boundary.x + 1,neighborCells[o]);
        // }
    }

    for (int o = 0; o < 3; ++o)
    {
        // if(i==0) {
        //     printf("friend: %d, %d: (%d,%d,%d)\n",i,o,neighborCells[o*3+0],neighborCells[o*3+1],neighborCells[o*3+2]);
        // }
        friends_list[i * 3 + o] = (int4)(neighborCells[o*3+0],neighborCells[o*3+1],neighborCells[o*3+2],0); 
    }    
}
