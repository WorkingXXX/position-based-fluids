#include "Precomp_OpenGL.h"
#include "Runner.hpp"
#include "ParamUtils.hpp"
#include "UIManager.h"

#define _USE_MATH_DEFINES
#include <math.h>

#include <GLFW/glfw3.h>

void Runner::run(Simulation &simulation, CVisual &renderer)
{
    // Create resource tracking file list (Shaders)
    time_t defaultTime = 0;
    const string *pCShaders = simulation.ShaderFileList();
    for (int iSrc = 0; pCShaders[iSrc] != ""; iSrc++)
        mKernelFilesTracker.push_back(make_pair(getPathForKernel(pCShaders[iSrc]), defaultTime));

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
    bool  KernelBuildOk = false;
    uint  prevParticleCount = 0;
    float simTime = 0.0f;
    float waveTime = 0.0f;
    float wavePos  = 0.0f;

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

                // Init buffers
                simulation.InitBuffers();
            }

            // Reset grid
            simulation.InitCells();

            // Init kernels
            KernelBuildOk = simulation.InitShaders();

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
            const float wave_push_length = Params.waveGenAmp * (Params.xMax - Params.xMin);

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
        for (uint i = 0; i < Params.subSteps; i++)
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
        OGLU_StartTimingSection("Draw UI");
        UIManager_Draw();
        OGLU_EndTimingSection();

        // Transfer to screen
        OGLU_StartTimingSection("Present-To-Screen");
        renderer.presentToScreen();
        OGLU_EndTimingSection();
    }
    while (!UIManager_WindowShouldClose());

#if defined(MAKE_VIDEO)
    pclose(ffmpeg);
#endif
}
