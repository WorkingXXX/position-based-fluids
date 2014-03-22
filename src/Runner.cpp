#include "Precomp_OpenGL.h"
#include "Runner.hpp"
#include "ParamUtils.hpp"
#include "UIManager.h"

#define _USE_MATH_DEFINES
#include <math.h>

#include <GLFW/glfw3.h>

void Runner::run(Simulation &simulation, CVisual &renderer)
{
    // Create resource tracking file list (Kernels)
    time_t defaultTime = 0;
    const string *pKernels = simulation.KernelFileList();
    for (int iSrc = 0; pKernels[iSrc] != ""; iSrc++)
        mKernelFilesTracker.push_back(make_pair(getPathForKernel(pKernels[iSrc]), defaultTime));

    // Append parameter file to resource tracking file list
    mKernelFilesTracker.push_back(make_pair(getPathForScenario("dam_coarse.par"), defaultTime));

    // Create shader tracking list
    const string *pShaders = renderer.ShaderFileList();
    for (int iSrc = 0; pShaders[iSrc] != ""; iSrc++)
        mShaderFilesTracker.push_back(make_pair(getPathForShader(pShaders[iSrc]), defaultTime));

    // Init render (background, camera etc...)
    renderer.initSystemVisual(simulation);

    // Init UIManager
    UIManager_Init(renderer.mWindow, &renderer, &simulation);

    // Main loop
    bool KernelBuildOk = false;
    cl_uint prevParticleCount = 0;
    cl_float simTime = 0.0f;
    cl_float waveTime = 0.0f;
    cl_float wavePos  = 0.0f;

	float avg_time = 0.0f;
	int count = 0;
    do
    {
        // Check file changes
        if (DetectResourceChanges(mKernelFilesTracker) || renderer.UICmd_ResetSimulation)
        {
            // Reading the configuration file
            LoadParameters(getScenario("dam_coarse.par"));

            // Check if particle count changed
            if ((prevParticleCount != Params.particleCount) || renderer.UICmd_ResetSimulation || Params.resetSimOnChange)
            {
                // Store new particle count
                prevParticleCount = Params.particleCount;

                // Notify renderer for parameter changed
                renderer.parametersChanged();

                // Generate shared buffer
                simulation.mSharedPingBufferID = renderer.createSharingBuffer(Params.particleCount * sizeof(cl_float4));
                simulation.mSharedPongBufferID = renderer.createSharingBuffer(Params.particleCount * sizeof(cl_float4));
                simulation.mSharedParticlesPos = renderer.createSharingTexture(2048, (Params.particleCount + 2048 - 1) / 2048);

                // Generated friends list shared buffer
                int nFriendListSize = Params.particleCount * Params.friendsCircles * (1 + Params.particlesPerCircle);
                simulation.mSharedFriendsList   = OGLU_GenerateTexture(2048, (nFriendListSize + 2048 - 1) / 2048, GL_R32UI);

                // Init buffers
                simulation.InitBuffers();
            }

            // Reset grid
            simulation.InitCells();

            // Init kernels
            KernelBuildOk = simulation.InitKernels();

            // Reset wavee
            waveTime = 0.0f;

            // Turn off sim reset request
            renderer.UICmd_ResetSimulation = false;
        }

        // Auto reload shaders
        if (DetectResourceChanges(mShaderFilesTracker))
        {
            renderer.initShaders();
        }

        // Make sure that kernels are valid
        if (!KernelBuildOk)
            continue;

        // Generate waves
        if (renderer.UICmd_GenerateWaves)
        {
            // Wave consts
            const cl_float wave_push_length = Params.waveGenAmp * (Params.xMax - Params.xMin);

            // Update the wave position
            float t = Params.waveGenFreq * waveTime;
            wavePos = (float)(1 - cos(2.0f * M_PI * pow(fmod(t, 1.0f), Params.waveGenDuty))) * wave_push_length / 2.0f;

            // Update wave running time
            if (!renderer.UICmd_PauseSimulation)
                waveTime += Params.timeStep;
        }
        else
        {
            waveTime = 0.0f;
        }

        // Load simulation settings
        simulation.bPauseSim        = renderer.UICmd_PauseSimulation;
        simulation.bReadFriendsList = renderer.UICmd_FriendsHistogarm;
        simulation.fWavePos         = wavePos;

        // Sub frames
        for (cl_uint i = 0; i < Params.subSteps; i++)
        {
            // Execute simulation
            simulation.Step();

            // Incremenent time
            if (!renderer.UICmd_PauseSimulation)
                simTime += Params.timeStep;
        }

        // Visualize particles
        renderer.renderParticles();

        // Draw UI
        UIManager_Draw();
        
        renderer.presentToScreen();

		PM_PERFORMANCE_TRACKER *pTracker1 = simulation.PerfData.Trackers[36];
		PM_PERFORMANCE_TRACKER *pTracker2 = simulation.PerfData.Trackers[39];
		PM_PERFORMANCE_TRACKER *pTracker3 = simulation.PerfData.Trackers[42];
		float avg = 0.0f;
		avg += pTracker1->total_time;
		avg += pTracker2->total_time;
		avg += pTracker3->total_time;
		avg /= 3;

		avg_time = (avg+count*avg_time)/(count+1);
		count++;

		if(simTime > 10.0f) {
			std::cout << "scaling average: " << avg_time << std::endl; 
		 //   for (size_t i = 0; i < simulation.PerfData.Trackers.size(); i++)
			//{
			//	PM_PERFORMANCE_TRACKER *pTracker = simulation.PerfData.Trackers[36];
			//	std::cout << i << ": " << pTracker->eventName << std::endl;
			//}
			break;
		}

    }
    while (true);

#if defined(MAKE_VIDEO)
    pclose(ffmpeg);
#endif
}
