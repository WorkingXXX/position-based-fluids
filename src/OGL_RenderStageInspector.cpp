#include "OGL_RenderStageInspector.h"
#include "OGL_Utils.h"

GLuint OGSI_Shader;
int    OGSI_StageToVisualize = MAXINT;
bool   OGSI_SaveInspectionToFile = false;
int    OGSI_Stages_Count;
string OGSI_Stages[255];

void OGSI_Setup(GLuint inspectionShader)
{
    OGSI_Shader = inspectionShader;
}

void OGSI_StartCycle()
{
    OGSI_Stages_Count = 0;
}

void OGSI_SetVisualizeStage(int stageIndex, bool saveInspectionToFile)
{
    OGSI_StageToVisualize = stageIndex;
    OGSI_SaveInspectionToFile = saveInspectionToFile;
}

bool OGSI_InspectTexture(GLuint textureID, char* szBufferTitle, float blitGain, float blitOffset)
{
    // Check if we should top rendering
    if (OGSI_StageToVisualize < OGSI_Stages_Count)
        return true;

    // Update inspection list buffer
    OGSI_Stages[OGSI_Stages_Count]  = string(szBufferTitle);

    // check if we need to render inspection point
    if (OGSI_Stages_Count == OGSI_StageToVisualize)
    {
        // Save texture to file (if save is enabled)
        if (OGSI_SaveInspectionToFile)
            OGLU_SaveTextureToFile(textureID, "inspect.raw");

        // Select shader
        glUseProgram(g_SelectedProgram = OGSI_Shader);

        // Update uniforms
        OGLU_BindTextureToUniform("ImageSrc", 0, textureID);
        glUniform1f(UniformLoc("offset"), blitOffset);
        glUniform1f(UniformLoc("gain"),   blitGain);
    
        // Select target and render quad
        g_ScreenFBO.SetAsDrawTarget();
        OGLU_RenderQuad(0, 0, 1.0, 1.0);

        glUseProgram(g_SelectedProgram = OGSI_Shader);
    }

    OGSI_Stages_Count++;

    return (OGSI_StageToVisualize < OGSI_Stages_Count);
}
