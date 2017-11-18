
#include "Base/Daedalus.h"

#include <stdio.h>

#include "Graphics/GL.h"
#include "Graphics/GraphicsContext.h"

#include "Graphics/ColourValue.h"

static u32 SCR_WIDTH = 640;
static u32 SCR_HEIGHT = 480;


template<> bool CSingleton< CGraphicsContext >::Create()
{
	DAEDALUS_ASSERT_Q(mpInstance == nullptr);

	mpInstance = new CGraphicsContext();
	return mpInstance->Initialise();
}


CGraphicsContext::~CGraphicsContext()
{
	// glew

	// FIXME: would be better in an separate SysGL file.
	if (gWindow)
	{
		glfwDestroyWindow(gWindow);
		gWindow = nullptr;
	}

	glfwTerminate();
}

static void error_callback(int error, const char* description)
{
	fprintf(stderr, "Error: %d - %s\n", error, description);
}

bool CGraphicsContext::Initialise()
{
	glfwSetErrorCallback(error_callback);

	// Initialise GLFW
	if( !glfwInit() )
	{
		fprintf( stderr, "Failed to initialize GLFW\n" );
		return false;
	}

	glfwWindowHint(GLFW_SAMPLES, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);

#ifdef DAEDALUS_OSX
	// OSX 10.7+ only supports 3.2 with core profile/forward compat.
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

	glfwWindowHint(GLFW_DEPTH_BITS, 24);
	//glfwWindowHint(GLFW_STENCIL_BITS, 0);

	// Open a window and create its OpenGL context
	gWindow = glfwCreateWindow( SCR_WIDTH, SCR_HEIGHT, "Daedalus", nullptr, nullptr );
	if (!gWindow)
	{
		fprintf( stderr, "Failed to open GLFW window\n" );

		glfwTerminate();
		return false;
	}

	glfwMakeContextCurrent(gWindow);

	// Ensure we can capture the escape key being pressed below
	//glfwEnable( GLFW_STICKY_KEYS );

	// Enable vertical sync (on cards that support it)
	glfwSwapInterval( 1 );

	// Initialise GLEW
	//glewExperimental = GL_TRUE;
	GLenum err = glewInit();
	if (err != GLEW_OK || !GLEW_VERSION_3_2)
	{
		fprintf( stderr, "Failed to initialize GLEW\n" );
		glfwDestroyWindow(gWindow);
		gWindow = nullptr;
		glfwTerminate();
		return false;
	}

	ClearAllSurfaces();

	// This is not valid in GLFW 3.0, and doesn't work with glfwGetWindowAttrib.
	//if (glfwGetWindowParam(GLFW_FSAA_SAMPLES) != 0)
	//	fprintf( stderr, "Full Screen Anti-Aliasing 4X has been enabled\n" );
	return true;
}

bool CGraphicsContext::IsInitialised() const
{
	return gWindow != nullptr;
}

void CGraphicsContext::GetScreenSize(u32 * width, u32 * height) const
{
	int window_width, window_height;
	glfwGetFramebufferSize(gWindow, &window_width, &window_height);

	*width  = window_width;
	*height = window_height;
}

void CGraphicsContext::ViewportType(u32 * width, u32 * height) const
{
	GetScreenSize(width, height);
}

void CGraphicsContext::ClearAllSurfaces()
{
	// FIXME: this should clear/flip a couple of times to ensure the front and backbuffers are cleared.
	// Not sure if it's necessary...
	ClearToBlack();
}

void CGraphicsContext::ClearToBlack()
{
	glDepthMask(GL_TRUE);
	glClearDepth( 1.0f );
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
}

void CGraphicsContext::ClearZBuffer()
{
	glDepthMask(GL_TRUE);
	glClearDepth( 1.0f );
	glClear( GL_DEPTH_BUFFER_BIT );
}

void CGraphicsContext::ClearColBuffer(const c32 & colour)
{
	glClearColor( colour.GetRf(), colour.GetGf(), colour.GetBf(), colour.GetAf() );
	glClear( GL_COLOR_BUFFER_BIT );
}

void CGraphicsContext::ClearColBufferAndDepth(const c32 & colour)
{
	glDepthMask(GL_TRUE);
	glClearDepth( 1.0f );
	glClearColor( colour.GetRf(), colour.GetGf(), colour.GetBf(), colour.GetAf() );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
}

void CGraphicsContext::BeginFrame()
{
	// Get window size (may be different than the requested size)
	u32 width, height;
	GetScreenSize(&width, &height);

	// Special case: avoid division by zero below
	height = height > 0 ? height : 1;

	glViewport( 0, 0, width, height );
	glScissor( 0, 0, width, height );
}

void CGraphicsContext::EndFrame()
{
}

void CGraphicsContext::UpdateFrame()
{
	glfwSwapBuffers(gWindow);
//	if( gCleanSceneEnabled ) //TODO: This should be optional
	{
		ClearColBuffer( c32(0xff000000) ); // ToDo : Use gFillColor instead?
	}
}
