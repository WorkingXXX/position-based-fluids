#include "Simulation.hpp"
#include "DataLoader.hpp"
#include "ParamUtils.hpp"
#include "ocl/OCLUtils.hpp"

#define _USE_MATH_DEFINES
#include <math.h>
#include <sstream>

#if defined(__APPLE__)
#include <OpenGL/OpenGL.h>
#elif defined(UNIX)
#include <GL/glx.h>
#else // _WINDOWS
#include <Windows.h>
#include <GL/gl.h>
#endif

using namespace std;

Simulation::Simulation(const cl::Context &clContext, const cl::Device &clDevice)
    : mCLContext(clContext),
      mCLDevice(clDevice),
      mPositions(NULL),
      mVelocities(NULL),
	  mPredictions(NULL),
	  mDeltas(NULL),
      mFriendsList(NULL),
      mCells(NULL),
      mParticlesList(NULL)
{
}

Simulation::~Simulation()
{
    glFinish();
    mQueue.finish();

    // Delete buffers
    delete[] mCells;
    delete[] mPositions;
    delete[] mVelocities;
}

void Simulation::CreateParticles()
{
    // Compute particle count per axis
	int ParticlesPerAxis = (int)ceil(pow(Params.particleCount, 1/3.0));

	// Build particles blcok
	float d = Params.h * Params.setupSpacing;
    float offsetX = (1.0f - ParticlesPerAxis * d) / 2.0f;
    float offsetY = 0.3;
    float offsetZ = (1.0f - ParticlesPerAxis * d) / 2.0f;
    for (cl_uint i = 0; i< Params.particleCount; i++)
    {
        cl_uint x = ((cl_uint)(i / pow(ParticlesPerAxis, 1)) % ParticlesPerAxis);
        cl_uint y = ((cl_uint)(i / pow(ParticlesPerAxis, 0)) % ParticlesPerAxis);
        cl_uint z = ((cl_uint)(i / pow(ParticlesPerAxis, 2)) % ParticlesPerAxis);

        mPositions[i].s[0] = offsetX + (x /*+ (y % 2) * .5*/) * d;
        mPositions[i].s[1] = offsetY + (y) * d;
        mPositions[i].s[2] = offsetZ + (z /*+ (y % 2) * .5*/) * d;
        mPositions[i].s[3] = 0;
    }

    random_shuffle(&mPositions[0],&mPositions[Params.particleCount-1]);
}

const std::string* Simulation::KernelFileList()
{
	static const std::string kernels[] = 
	{	
		"parameters.hpp",
		"predict_positions.cl",
		"init_cells_old.cl",
		"update_cells.cl",
		"compute_scaling.cl",
		"compute_delta.cl",
		"update_predicted.cl",
		"update_velocities.cl",
        "apply_viscosity.cl",
        "apply_vorticity.cl",
		"update_positions.cl",
		"build_friends_list.cl",
		""
	};

	return kernels;
}

bool Simulation::InitKernels()
{
	// Setup OpenCL Ranges
    const cl_uint globalSize = ceil(Params.particleCount / 32.0f) * 32;
    mGlobalRange = cl::NDRange(globalSize);
    mLocalRange = cl::NullRange;

    // setup kernel sources
    OCLUtils clSetup;
    vector<string> kernelSources;
    string header = clSetup.readSource(getPathForKernel("hesp.hpp"));

	// Load kernel sources
	const std::string* pKernels = KernelFileList();
	for (int iSrc = 0; pKernels[iSrc]  != ""; iSrc++)
	{
		string source = clSetup.readSource(getPathForKernel(pKernels[iSrc]));
		kernelSources.push_back(header + source);
	}

	// Setup kernel compiler flags
    std::ostringstream clflags;
    clflags << "-cl-mad-enable -cl-no-signed-zeros -cl-fast-relaxed-math ";

#ifdef USE_DEBUG
    clflags << "-DUSE_DEBUG ";
#endif // USE_DEBUG

    clflags << std::showpoint;
    //clflags << "-DSYSTEM_MIN_X="      << Params.xMin << "f ";
    //clflags << "-DSYSTEM_MAX_X="      << Params.xMax << "f ";
    //clflags << "-DSYSTEM_MIN_Y="      << Params.yMin << "f ";
    //clflags << "-DSYSTEM_MAX_Y="      << Params.yMax << "f ";
    //clflags << "-DSYSTEM_MIN_Z="      << Params.zMin << "f ";
    //clflags << "-DSYSTEM_MAX_Z="      << Params.zMax << "f ";
	//clflags << "-DGRID_RES="          << (int)Params.gridRes << " ";
    //clflags << "-DTIMESTEP="          << Params.timeStepLength << "f ";
    //clflags << "-DREST_DENSITY="      << Params.restDensity << "f ";
	//clflags << "-DPBF_H="             << Params.h << "f ";
    //clflags << "-DPBF_H_2="           << Params.h_2 << "f ";
	//clflags << "-DEPSILON="           << Params.epsilon << "f ";
	
	clflags << "-DEND_OF_CELL_LIST="            << (int)(-1)         << " ";

	clflags << "-DFRIENDS_CIRCLES="             << (int)(Params.friendsCircles)     << " ";  // Defines how many friends circle are we going to scan for
	clflags << "-DMAX_PARTICLES_IN_CIRCLE="     << (int)(Params.particlesPerCircle) << " ";  // Defines the max number of particles per cycle
	clflags << "-DPARTICLE_FRIENDS_BLOCK_SIZE=" << (int)(Params.friendsCircles + Params.friendsCircles * Params.particlesPerCircle) << " ";  // FRIENDS_CIRCLES + FRIENDS_CIRCLES * MAX_PARTICLES_IN_CIRCLE

	clflags << "-DGRID_SIZE="         << (int)(Params.gridRes * Params.gridRes * Params.gridRes) << " ";
    clflags << "-DPOLY6_FACTOR="      << 315.0f / (64.0f * M_PI * pow(Params.h, 9)) << "f ";
    clflags << "-DGRAD_SPIKY_FACTOR=" << 45.0f / (M_PI * pow(Params.h, 6)) << "f ";

	// Compile kernels
    cl::Program program = clSetup.createProgram(kernelSources, mCLContext, mCLDevice, clflags.str());
	if (program() == 0)
		return false;

	// Build kernels table
	mKernels = clSetup.createKernelsMap(program);

	// Copy Params (Host) => mParams (GPU)
	mQueue = cl::CommandQueue(mCLContext, mCLDevice, CL_QUEUE_PROFILING_ENABLE);
	mQueue.enqueueWriteBuffer(mParameters, CL_TRUE, 0, sizeof(Params), &Params);
    mQueue.finish();

	return true;
}

void Simulation::InitBuffers()
{
	// Define CL buffer sizes
	mBufferSizeParticles      = Params.particleCount * sizeof(cl_float4);
    mBufferSizeParticlesList  = Params.particleCount * sizeof(cl_int);
    mBufferSizeScalingFactors = Params.particleCount * sizeof(cl_float);

    // Allocate CPU buffers
    delete[] mPositions;   mPositions   = new cl_float4[Params.particleCount];
    delete[] mVelocities;  mVelocities  = new cl_float4[Params.particleCount];
    delete[] mPredictions; mPredictions = new cl_float4[Params.particleCount]; // (used for debugging)
    delete[] mDeltas;      mDeltas      = new cl_float4[Params.particleCount]; // (used for debugging)
	delete[] mFriendsList; mFriendsList = new cl_uint  [Params.particleCount * 200]; // (used for debugging)
	
	// Position particles
    CreateParticles();

    // Initialize particle speed arrays
    for (cl_uint i = 0; i < Params.particleCount; ++i)
    {
        mVelocities[i].s[0] = 0;
        mVelocities[i].s[1] = 0;
        mVelocities[i].s[2] = 0;
        mVelocities[i].s[3] = 1; // <= "m" == 1?
    }

	// Create buffers
    mPositionsBuffer       = cl::BufferGL(mCLContext, CL_MEM_READ_WRITE, mSharingBufferID); // buffer could be changed to be CL_MEM_WRITE_ONLY but for debugging also reading it might be helpful
	mPredictedBuffer       = cl::Buffer(mCLContext, CL_MEM_READ_WRITE, mBufferSizeParticles);
    mVelocitiesBuffer      = cl::Buffer(mCLContext, CL_MEM_READ_WRITE, mBufferSizeParticles);
    mDeltaBuffer           = cl::Buffer(mCLContext, CL_MEM_READ_WRITE, mBufferSizeParticles);
    mDeltaVelocityBuffer   = cl::Buffer(mCLContext, CL_MEM_READ_WRITE, mBufferSizeParticles);
    mOmegaBuffer           = cl::Buffer(mCLContext, CL_MEM_READ_WRITE, mBufferSizeParticles);
    mScalingFactorsBuffer  = cl::Buffer(mCLContext, CL_MEM_READ_WRITE, mBufferSizeScalingFactors);
	mParameters            = cl::Buffer(mCLContext, CL_MEM_READ_ONLY,  sizeof(Params));

	if (mQueue() != 0)
		mQueue.flush();

    // Copy mPositions (Host) => mPositionsBuffer (GPU) (we have to lock the shared buffer)
	vector<cl::Memory> sharedBuffers;
    sharedBuffers.push_back(mPositionsBuffer);
    mQueue = cl::CommandQueue(mCLContext, mCLDevice);
    mQueue.enqueueAcquireGLObjects(&sharedBuffers);
    mQueue.enqueueWriteBuffer(mPositionsBuffer, CL_TRUE, 0, mBufferSizeParticles, mPositions);
    mQueue.enqueueReleaseGLObjects(&sharedBuffers);
    mQueue.finish();

	// Copy mVelocities (Host) => mVelocitiesBuffer (GPU)
	mQueue.enqueueWriteBuffer(mVelocitiesBuffer, CL_TRUE, 0, mBufferSizeParticles, mVelocities);
    mQueue.finish();
}

void Simulation::InitCells()
{
	// Allocate host buffers
	const cl_uint cellCount = Params.gridRes * Params.gridRes * Params.gridRes;
    delete[] mCells;         mCells         = new cl_int[cellCount];
    delete[] mParticlesList; mParticlesList = new cl_int[Params.particleCount];

    // Init cells
    for (cl_uint i = 0; i < cellCount; ++i)
        mCells[i] = END_OF_CELL_LIST;

    // Init particles
    for (cl_uint i = 0; i < Params.particleCount; ++i)
        mParticlesList[i] = END_OF_CELL_LIST;

    // Write buffer for cells
	mBufferSizeCells = cellCount * sizeof(cl_int);
    mCellsBuffer = cl::Buffer(mCLContext, CL_MEM_READ_WRITE, mBufferSizeCells);
    mQueue.enqueueWriteBuffer(mCellsBuffer, CL_TRUE, 0, mBufferSizeCells, mCells);

    // Write buffer for particles
    mParticlesListBuffer = cl::Buffer(mCLContext, CL_MEM_READ_WRITE, mBufferSizeParticlesList);
    mQueue.enqueueWriteBuffer(mParticlesListBuffer, CL_TRUE, 0, mBufferSizeParticlesList, mParticlesList);

	// Init Friends list buffer
	int BufSize = Params.particleCount * 200 * sizeof(cl_uint);
	memset(mFriendsList, 0, BufSize);
	mFriendsListBuffer = cl::Buffer(mCLContext, CL_MEM_READ_WRITE, BufSize);
    mQueue.enqueueWriteBuffer(mFriendsListBuffer, CL_TRUE, 0, BufSize, mFriendsList);
}

void Simulation::updatePositions()
{
	int param = 0;
    mKernels["updatePositions"].setArg(param++, mPositionsBuffer);
    mKernels["updatePositions"].setArg(param++, mPredictedBuffer);
    mKernels["updatePositions"].setArg(param++, mVelocitiesBuffer);
    mKernels["updatePositions"].setArg(param++, mDeltaVelocityBuffer);
    mKernels["updatePositions"].setArg(param++, Params.particleCount);

	mQueue.enqueueNDRangeKernel(mKernels["updatePositions"], 0, mGlobalRange, mLocalRange, NULL, PerfData.GetTrackerEvent("updatePositions"));
}

void Simulation::updateVelocities()
{
	int param = 0;
	mKernels["updateVelocities"].setArg(param++, mParameters);
    mKernels["updateVelocities"].setArg(param++, mPositionsBuffer);
    mKernels["updateVelocities"].setArg(param++, mPredictedBuffer);
    mKernels["updateVelocities"].setArg(param++, mVelocitiesBuffer);
    mKernels["updateVelocities"].setArg(param++, Params.particleCount);

    mQueue.enqueueNDRangeKernel(mKernels["updateVelocities"], 0, mGlobalRange, mLocalRange, NULL, PerfData.GetTrackerEvent("updateVelocities"));
}

void Simulation::applyViscosity()
{
	int param = 0;
	mKernels["applyViscosity"].setArg(param++, mParameters);
    mKernels["applyViscosity"].setArg(param++, mPredictedBuffer);
    mKernels["applyViscosity"].setArg(param++, mVelocitiesBuffer);
    mKernels["applyViscosity"].setArg(param++, mDeltaVelocityBuffer);
    mKernels["applyViscosity"].setArg(param++, mOmegaBuffer);
	mKernels["applyViscosity"].setArg(param++, mFriendsListBuffer);
    mKernels["applyViscosity"].setArg(param++, Params.particleCount);

    mQueue.enqueueNDRangeKernel(mKernels["applyViscosity"], 0, mGlobalRange, mLocalRange, NULL, PerfData.GetTrackerEvent("applyViscosity"));
}

void Simulation::applyVorticity()
{
    int param = 0;
	mKernels["applyVorticity"].setArg(param++, mParameters);
    mKernels["applyVorticity"].setArg(param++, mPredictedBuffer);
    mKernels["applyVorticity"].setArg(param++, mDeltaVelocityBuffer);
    mKernels["applyVorticity"].setArg(param++, mOmegaBuffer);
	mKernels["applyVorticity"].setArg(param++, mFriendsListBuffer);
    mKernels["applyVorticity"].setArg(param++, Params.particleCount);

    mQueue.enqueueNDRangeKernel(mKernels["applyVorticity"], 0, mGlobalRange, mLocalRange, NULL, PerfData.GetTrackerEvent("applyVorticity"));
}

void Simulation::predictPositions()
{
	int param = 0;
	mKernels["predictPositions"].setArg(param++, mParameters);
    mKernels["predictPositions"].setArg(param++, mPositionsBuffer);
    mKernels["predictPositions"].setArg(param++, mPredictedBuffer);
    mKernels["predictPositions"].setArg(param++, mVelocitiesBuffer);
    mKernels["predictPositions"].setArg(param++, Params.particleCount);

    mQueue.enqueueNDRangeKernel(mKernels["predictPositions"], 0, mGlobalRange, mLocalRange, NULL, PerfData.GetTrackerEvent("predictPositions"));
}

void Simulation::buildFriendsList()
{
	int param = 0;
	mKernels["buildFriendsList"].setArg(param++, mParameters);
    mKernels["buildFriendsList"].setArg(param++, mPredictedBuffer);
    mKernels["buildFriendsList"].setArg(param++, mCellsBuffer);
    mKernels["buildFriendsList"].setArg(param++, mParticlesListBuffer);
    mKernels["buildFriendsList"].setArg(param++, mFriendsListBuffer);
    mKernels["buildFriendsList"].setArg(param++, Params.particleCount);

	mQueue.enqueueNDRangeKernel(mKernels["buildFriendsList"], 0, mGlobalRange, mLocalRange, NULL, PerfData.GetTrackerEvent("buildFriendsList"));
}

void Simulation::updatePredicted(int iterationIndex)
{
	int param = 0;
    mKernels["updatePredicted"].setArg(param++, mPredictedBuffer);
    mKernels["updatePredicted"].setArg(param++, mDeltaBuffer);
    mKernels["updatePredicted"].setArg(param++, Params.particleCount);

	mQueue.enqueueNDRangeKernel(mKernels["updatePredicted"], 0, mGlobalRange, mLocalRange, NULL, PerfData.GetTrackerEvent("updatePredicted", iterationIndex));
}

void Simulation::computeDelta(int iterationIndex, cl_float waveGenerator)
{
    int param = 0;
	mKernels["computeDelta"].setArg(param++, mParameters);
	mKernels["computeDelta"].setArg(param++, mDeltaBuffer);
    mKernels["computeDelta"].setArg(param++, mPredictedBuffer);
    mKernels["computeDelta"].setArg(param++, mScalingFactorsBuffer);
	mKernels["computeDelta"].setArg(param++, mFriendsListBuffer);
    mKernels["computeDelta"].setArg(param++, waveGenerator);
    mKernels["computeDelta"].setArg(param++, Params.particleCount);

	mQueue.enqueueNDRangeKernel(mKernels["computeDelta"], 0, mGlobalRange, mLocalRange, NULL, PerfData.GetTrackerEvent("computeDelta", iterationIndex));
}

void Simulation::computeScaling(int iterationIndex)
{
	int param = 0;
    mKernels["computeScaling"].setArg(param++, mParameters);
	mKernels["computeScaling"].setArg(param++, mPredictedBuffer);
    mKernels["computeScaling"].setArg(param++, mScalingFactorsBuffer);
	mKernels["computeScaling"].setArg(param++, mFriendsListBuffer);
	mKernels["computeScaling"].setArg(param++, Params.particleCount);

	mQueue.enqueueNDRangeKernel(mKernels["computeScaling"], 0, mGlobalRange, mLocalRange, NULL, PerfData.GetTrackerEvent("computeScaling", iterationIndex));
}

void Simulation::updateCells()
{
	int param = 0;
    mKernels["initCellsOld"].setArg(param++, mCellsBuffer);
    mKernels["initCellsOld"].setArg(param++, mParticlesListBuffer);
	mKernels["initCellsOld"].setArg(param++, (cl_uint)(Params.gridRes * Params.gridRes * Params.gridRes));
    mKernels["initCellsOld"].setArg(param++, Params.particleCount);
    mQueue.enqueueNDRangeKernel(mKernels["initCellsOld"], 0, cl::NDRange(max(mBufferSizeParticlesList, mBufferSizeCells)), mLocalRange, NULL, PerfData.GetTrackerEvent("initCells"));

	param = 0;
    mKernels["updateCells"].setArg(param++, mParameters);
    mKernels["updateCells"].setArg(param++, mPredictedBuffer);
    mKernels["updateCells"].setArg(param++, mCellsBuffer);
    mKernels["updateCells"].setArg(param++, mParticlesListBuffer);
    mKernels["updateCells"].setArg(param++, Params.particleCount);
    mQueue.enqueueNDRangeKernel(mKernels["updateCells"], 0, mGlobalRange, mLocalRange, NULL, PerfData.GetTrackerEvent("updateCells"));
}

void Simulation::Step(bool bPauseSim, cl_float waveGenerator)
{
	// Why is this here?
    glFinish();

	// Enqueue GL buffer acquire
	vector<cl::Memory> sharedBuffers;
    sharedBuffers.push_back(mPositionsBuffer);
    mQueue.enqueueAcquireGLObjects(&sharedBuffers);
	// Predicit positions 
    this->predictPositions();

	// Update cells
	this->updateCells();

	// Build friends list
	this->buildFriendsList();
	//mQueue.enqueueReadBuffer(mFriendsListBuffer, CL_TRUE, 0, mBufferSizeParticles, mFriendsList);
	//mQueue.finish(); 
	
	for (unsigned int i = 0; i < Params.simIterations; ++i)
    {
		// Compute scaling value
        this->computeScaling(i);
        /*mQueue.enqueueReadBuffer(mPredictedBuffer, CL_TRUE, 0, mBufferSizeParticles, mPredictions);
        if (mQueue.finish() != CL_SUCCESS)
        	_asm nop;*/

		// Compute position delta
        this->computeDelta(i, waveGenerator);
        /*mQueue.enqueueReadBuffer(mDeltaBuffer, CL_TRUE, 0, mBufferSizeParticles, mDeltas);
        if (mQueue.finish() != CL_SUCCESS)
        	_asm nop;*/

		// Update predicted position
        this->updatePredicted(i);
    }

	// Recompute velocities
    this->updateVelocities();

	// Update vorticity and Viscosity
    this->applyViscosity();
    this->applyVorticity();

	// Update particle postions
	if (!bPauseSim)
		this->updatePositions();

	// Release OpenGL shared object, allowing openGL do to it's thing...
    mQueue.enqueueReleaseGLObjects(&sharedBuffers);
    mQueue.finish(); 

	// Collect performance data
	PerfData.UpdateTimings();
}

void Simulation::dumpData( cl_float4 * (&positions), cl_float4 * (&velocities) )
{
    mQueue.enqueueReadBuffer(mPositionsBuffer, CL_TRUE, 0, mBufferSizeParticles, mPositions);
    mQueue.enqueueReadBuffer(mVelocitiesBuffer, CL_TRUE, 0, mBufferSizeParticles, mVelocities);

    // just a safety measure to be absolutely sure everything is transferred
    // from device to host
    mQueue.finish();

    positions = mPositions;
    velocities = mVelocities;
}
