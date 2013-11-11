#define _USE_MATH_DEFINES
#include <math.h>

#include <cstdlib>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <sstream>

#if defined(__APPLE__)
  #include <OpenGL/OpenGL.h>
#elif defined(UNIX)
  #include <GL/glx.h>
#else // _WINDOWS
  #include <Windows.h>
  #include <GL/gl.h>
#endif 

#include "hesp.hpp"
#include "ocl/clsetup.hpp"
#include "io/ConfigReader.hpp"
#include "io/PartReader.hpp"
#include "visual/visual.hpp"
#include "Simulation.hpp"
#include "Runner.hpp"
#include "DataLoader.hpp"

static const int WINDOW_WIDTH = 1280;
static const int WINDOW_HEIGHT = 720;


using std::ofstream;
using std::vector;
using std::cout;
using std::cerr;
using std::cin;
using std::endl;
using std::string;
using std::exception;
using std::runtime_error;

int main() {
  try {
    DataLoader dataLoader;
    // Reading the configuration file
    string parameters_filename = dataLoader.getPathForScenario("dam_coarse.par");
    cout << parameters_filename << endl;
    ConfigReader configReader;
    ConfigParameters parameters = configReader.read(parameters_filename);

    // reading the part(particle) file
    string part_filename = dataLoader.getPathForScenario(parameters.partInputFile);
    cout << part_filename << endl;
    PartReader partReader;
	//vector<Particle> particles = partReader.read(part_filename);

	// Compute smooth length
    float h = (parameters.xMax - parameters.xMin) / parameters.xN;

	// Compute particle count
	int partCount = 20000;
	int ParticlesPerAxis = (int)ceil(pow(partCount, 1/3.0));
	partCount = ParticlesPerAxis * ParticlesPerAxis * ParticlesPerAxis;

	// Build particles blcok
	vector<Particle> particles;
	particles.resize(partCount);
	float d = h * 0.990;
	float offsetX = 0.1;
	float offsetY = 0.1;
	float offsetZ = 0.1;
	for (int i = 0; i< partCount; i++)
	{
		int x = ((int)(i / pow(ParticlesPerAxis, 1)) % ParticlesPerAxis);
		int y = ((int)(i / pow(ParticlesPerAxis, 0)) % ParticlesPerAxis);
		int z = ((int)(i / pow(ParticlesPerAxis, 2)) % ParticlesPerAxis);

        memset(&particles[i],0,sizeof(Particle));
		particles[i].m = 1;
		particles[i].x[0] = offsetX + (x + (y % 2) * .5) * d;
		particles[i].x[1] = offsetY + (y) * d;
		particles[i].x[2] = offsetZ + (z + (y % 2) * .5) * d;
	}

    // For visualization
    CVisual renderer(&dataLoader, WINDOW_WIDTH, WINDOW_HEIGHT);
    renderer.initWindow("HESP Project");
    GLuint sharingBufferID = renderer.createSharingBuffer( particles.size()
                             * sizeof(cl_float4) );

    // setup kernel sources
    CSetupCL clSetup;
    vector<string> kernelSources;
    string header = clSetup.readSource(dataLoader.getPathForKernel("hesp.hpp"));
    string source;

    source = clSetup.readSource(dataLoader.getPathForKernel("predict_positions.cl"));
    kernelSources.push_back(header + source);
    source = clSetup.readSource(dataLoader.getPathForKernel("init_cells_old.cl"));
    kernelSources.push_back(header + source);
    source = clSetup.readSource(dataLoader.getPathForKernel("update_cells.cl"));
    kernelSources.push_back(header + source);
    source = clSetup.readSource(dataLoader.getPathForKernel("compute_scaling.cl"));
    kernelSources.push_back(header + source);
    source = clSetup.readSource(dataLoader.getPathForKernel("compute_delta.cl"));
    kernelSources.push_back(header + source);
    source = clSetup.readSource(dataLoader.getPathForKernel("update_predicted.cl"));
    kernelSources.push_back(header + source);
    source = clSetup.readSource(dataLoader.getPathForKernel("update_velocities.cl"));
    kernelSources.push_back(header + source);
    source = clSetup.readSource(dataLoader.getPathForKernel("apply_vorticity_and_viscosity.cl"));
    kernelSources.push_back(header + source);
    source = clSetup.readSource(dataLoader.getPathForKernel("update_positions.cl"));
    kernelSources.push_back(header + source);
    source = clSetup.readSource(dataLoader.getPathForKernel("calc_hash.cl"));
    kernelSources.push_back(header + source);
#if !defined(USE_LINKEDCELL)
    source = clSetup.readSource(dataLoader.getPathForKernel("radix_histogram.cl"));
    kernelSources.push_back(header + source);
    source = clSetup.readSource(dataLoader.getPathForKernel("radix_scan.cl"));
    kernelSources.push_back(header + source);
    source = clSetup.readSource(dataLoader.getPathForKernel("radix_paste.cl"));
    kernelSources.push_back(header + source);
    source = clSetup.readSource(dataLoader.getPathForKernel("radix_reorder.cl"));
    kernelSources.push_back(header + source);
    source = clSetup.readSource(dataLoader.getPathForKernel("init_cells.cl"));
    kernelSources.push_back(header + source);
    source = clSetup.readSource(dataLoader.getPathForKernel("find_cells.cl"));
    kernelSources.push_back(header + source);
#endif

    cout << "Setting up OpenCL..." << endl;

    cl::Platform platform = clSetup.selectPlatform();
    //cl::Platform platform = clSetup.getPlatforms()[1];

#if defined(__APPLE__)
    CGLContextObj glContext = CGLGetCurrentContext();
    CGLShareGroupObj shareGroup = CGLGetShareGroup(glContext);

    cl_context_properties properties[] = {
      CL_CONTEXT_PROPERTY_USE_CGL_SHAREGROUP_APPLE,
      (cl_context_properties)shareGroup,
      0
    };
#elif defined(UNIX)
    cl_context_properties properties[] = {
      CL_GL_CONTEXT_KHR, (cl_context_properties) glXGetCurrentContext(),
      CL_GLX_DISPLAY_KHR, (cl_context_properties) glXGetCurrentDisplay(),
      CL_CONTEXT_PLATFORM, (cl_context_properties) (platform)(),
      0
    };
#elif defined(_WINDOWS)
	cl_context_properties properties[] = 
	{
		CL_GL_CONTEXT_KHR, (cl_context_properties) wglGetCurrentContext(),  
		CL_WGL_HDC_KHR, (cl_context_properties) wglGetCurrentDC(), 
		CL_CONTEXT_PLATFORM, (cl_context_properties) (platform)(),
		0
	};	
#else
    cl_context_properties properties[] = {
      CL_GL_CONTEXT_KHR, (cl_context_properties) glXGetCurrentContext(),
      CL_GLX_DISPLAY_KHR, (cl_context_properties) glXGetCurrentDisplay(),
      CL_CONTEXT_PLATFORM, (cl_context_properties) (platform)(),
      0
    };
#endif // __APPLE__

    vector<cl::Device> devices;
    // Get a vector of devices on this platform
    platform.getDevices(CL_DEVICE_TYPE_GPU, &devices);
    cl::Device device = devices.at(0);
    cl::Context context = clSetup.createContext(properties);

    std::ostringstream clflags;
    clflags << "-cl-mad-enable -cl-no-signed-zeros -cl-fast-relaxed-math ";

#ifdef USE_DEBUG
    clflags << "-DUSE_DEBUG ";
#endif // USE_DEBUG

#ifdef USE_LINKEDCELL
    clflags << "-DUSE_LINKEDCELL ";
#endif // USE_LINKEDCELL

    clflags << std::showpoint;
    clflags << "-DSYSTEM_MIN_X=" << parameters.xMin << "f ";
    clflags << "-DSYSTEM_MAX_X=" << parameters.xMax << "f ";
    clflags << "-DSYSTEM_MIN_Y=" << parameters.yMin << "f ";
    clflags << "-DSYSTEM_MAX_Y=" << parameters.yMax << "f ";
    clflags << "-DSYSTEM_MIN_Z=" << parameters.zMin << "f ";
    clflags << "-DSYSTEM_MAX_Z=" << parameters.zMax << "f ";
    clflags << "-DNUMBER_OF_CELLS_X=" << parameters.xN << "f ";
    clflags << "-DNUMBER_OF_CELLS_Y=" << parameters.yN << "f ";
    clflags << "-DNUMBER_OF_CELLS_Z=" << parameters.zN << "f ";
    clflags << "-DCELL_LENGTH_X=" << (parameters.xMax - parameters.xMin) / parameters.xN << "f ";
    clflags << "-DCELL_LENGTH_Y=" << (parameters.yMax - parameters.yMin) / parameters.yN << "f ";
    clflags << "-DCELL_LENGTH_Z=" << (parameters.zMax - parameters.zMin) / parameters.zN << "f ";
    clflags << "-DTIMESTEP=" << parameters.timeStepLength << "f ";
    clflags << "-DREST_DENSITY=" << parameters.restDensity << "f ";
    clflags << "-DPBF_H=" << h << "f ";
    clflags << "-DPBF_H_2=" << pow(h, 2) << "f ";
    clflags << "-DPOLY6_FACTOR=" << 315.0f / (64.0f * M_PI * pow(h, 9)) << "f ";
    clflags << "-DGRAD_SPIKY_FACTOR=" << 45.0f / (M_PI * pow(h, 6)) << "f ";

    cl::Program program = clSetup.createProgram(kernelSources, context,
                          device, clflags.str());

    map<string, cl::Kernel> kernels = clSetup.createKernelsMap(program);
    Simulation simulation(parameters, particles, kernels,
                          context, device, sharingBufferID);

    Runner runner;
    runner.run(parameters, simulation, renderer);

  } catch (const cl::Error &ecl) {
    cerr << "OpenCL Error caught: " << ecl.what() << "(" << ecl.err() << ")" << endl;
    exit(-1);
  } catch (const exception &e) {
    cerr << "STD Error caught: " << e.what() << endl;
    exit(-1);
  }

  return 0;
}
