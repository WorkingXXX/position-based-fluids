#ifndef __SIMULATION_HPP
#define __SIMULATION_HPP

#include <vector>
#include <map>
#include <string>
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <assert.h>
#include <algorithm>

#include "hesp.hpp"
#include "Parameters.hpp"
#include "Particle.hpp"
#include "OCLPerfMon.h"

#include <GLFW/glfw3.h>

// Macro used for the end of cell list
static const int END_OF_CELL_LIST = -1;

using std::map;
using std::vector;
using std::string;

class Simulation
{
private:
    // Avoid copy
    Simulation &operator=(const Simulation &other);
    Simulation (const Simulation &other);

	// Init particles positions
	void CreateParticles();

    // Copy current positions and velocities
    void dumpData( cl_float4 * (&positions), cl_float4 * (&velocities) );
    
public:

    // OpenCL objects supplied by OpenCL setup
    const cl::Context &mCLContext;
    const cl::Device &mCLDevice;

    // holds all OpenCL kernels required for the simulation
    map<string, cl::Kernel> mKernels;

    // command queue all OpenCL calls are run on
    cl::CommandQueue mQueue;

    // ranges used for executing the kernels
    cl::NDRange mGlobalRange;
    cl::NDRange mLocalRange;

	// OCL buffer sizes
    size_t mBufferSizeParticles;
    size_t mBufferSizeCells;
    size_t mBufferSizeParticlesList;
    size_t mBufferSizeScalingFactors;

    // The device memory buffers holding the simulation data
    cl::Buffer mCellsBuffer;
    cl::Buffer mParticlesListBuffer;
	cl::Buffer mFriendsListBuffer;
    cl::BufferGL mPositionsYinBuffer;
    cl::BufferGL mPositionsYangBuffer;
    cl::Buffer mPredictedBuffer;
    cl::Buffer mVelocitiesYinBuffer;
    cl::Buffer mVelocitiesYangBuffer;
    cl::Buffer mScalingFactorsBuffer;
    cl::Buffer mDeltaBuffer;
    cl::Buffer mDeltaVelocityBuffer;
    cl::Buffer mOmegaBuffer;
	cl::Buffer mParameters;

    // Radix buffers
    cl::Buffer mInKeysBuffer;
    cl::Buffer mInPermutationBuffer;
    cl::Buffer mOutKeysBuffer;
    cl::Buffer mOutPermutationBuffer;
    cl::Buffer mHistogramBuffer;
    cl::Buffer mGlobSumBuffer;
    cl::Buffer mHistoTempBuffer;

    // Lengths of each cell in each direction
    cl_float4 mCellLength;

    // Array for the cells
    cl_int *mCells;
    cl_int *mParticlesList;

    // Private member functions
    void updateCells();
    void updatePositions();
    void updateVelocities();
    void applyViscosity();
    void applyVorticity();
    void predictPositions();
	void buildFriendsList();
    void updatePredicted(int iterationIndex);
    void computeScaling(int iterationIndex);
    void computeDelta(int iterationIndex);
    void radixsort();

public:
    // Default constructor.
    explicit Simulation(const cl::Context &clContext, const cl::Device &clDevice);

    // Destructor.
    ~Simulation ();

    // Create all buffer and particles
	void InitBuffers();

    // Init Grid
    void InitCells();

    // Load and build kernels
    bool InitKernels();

    // Perform single simulation step
    void Step();

    // Get a list of kernel files
	const std::string* KernelFileList();

public:

	// Open GL Sharing buffers
    GLuint mSharingYinBufferID;
    GLuint mSharingYangBufferID;

	// Performance measurement
	OCLPerfMon PerfData;

	// Rendering state
	bool      bPauseSim;
	bool      bReadFriendsList;
	cl_float  fWavePos;
	
    // debug buffers (placed in host memory)
    cl_float4* mPositions;
    cl_float4* mVelocities;
    cl_float4* mPredictions;
    cl_float4* mDeltas;
	cl_uint*   mFriendsList;
};

#endif // __SIMULATION_HPP
