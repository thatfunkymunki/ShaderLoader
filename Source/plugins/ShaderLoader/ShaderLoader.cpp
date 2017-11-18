//
//		ShaderLoader.cpp
//
//		A freeframe effect that can be used to load shader files 
//		produced from "GLSL Sandbox" and "ShaderToy".
//
//		------------------------------------------------------------
//		Revisions :
//		11-01-15	Version 1.000
//		14-01-15	Included GetInputStatus to show whether texture inputs are being used or not
//					Limited viewport check in ProcessOpenGL to Isadora - no way to test difference
//		15-01-15	Global time start on shader load.
//		16-01-15	Fixed path truncation. Used PathAddExtension to fix "." in path problem
//					Version 1.001
//		19-01-15	Changed CreateRectangleTexture to GL_REPEAT rather than wrap
//					Always create these local textures so that deform shaders work
//					but only create only on change of incoming texture size
//					Version 1.002
//		20-01-15	Got iChannelResolution and iChannelTime working properly
//					All resolutions locked to viewport dimensions
//		21-01-15	Cleanup. Prepare for 4 input textures
//					Version 1.003
//		10.02.15	Cleanup - remove leftover print statements
//					Version 1.004
//		09.03.15	Provided for revised SharedToy spec with mainImage instead of main
//                  See ShaderToy example : "https://www.shadertoy.com/view/ldl3W8#"
//					Included shader file validation string "fragColor" as well as "gl_FragColor" in line with new spec
//					Version 1.004
//		26.03.15	Changed from LGPL to Simplified BSD licence
//		11-17-17	version 2.0 updating for 64 bit resolume 6
//
//		------------------------------------------------------------
//
//		Copyright (c) 2015, Lynn Jarvis, Leading Edge. Pty. Ltd. All rights reserved.
//
//		Redistribution and use in source and binary forms, with or without modification, 
//		are permitted provided that the following conditions are met:
//
//		1. Redistributions of source code must retain the above copyright notice, 
//		   this list of conditions and the following disclaimer.
//
//		2. Redistributions in binary form must reproduce the above copyright notice, 
//		   this list of conditions and the following disclaimer in the documentation 
//		   and/or other materials provided with the distribution.
//
//		THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"	AND ANY 
//		EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
//		OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE	ARE DISCLAIMED. 
//		IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
//		INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
//		PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
//		INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
//		LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
//		OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//		--------------------------------------------------------------
//
//
#include <FFGL.h>
#include <FFGLLib.h>
#include <stdio.h>
#include <string>
#include <fstream>
#include <limits>
#include <time.h> // for date
#include <Shlobj.h> // to get the program folder path
#include <Shlwapi.h> // for PathStripPath
#include <io.h> // for file existence check

#pragma comment(lib, "Shlwapi") // for PathStripPath

#include "ShaderLoader.h"

// To get the dll hModule since there is no main
#ifndef _delayimp_h
extern "C" IMAGE_DOS_HEADER __ImageBase;
#endif

#define GL_SHADING_LANGUAGE_VERSION	0x8B8C
#define GL_TEXTURE_WRAP_R			0x8072
#define GL_MIRRORED_REPEAT			0x8370


#define M_PI 3.1415926535897932384626433832795
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define FFPARAM_FILENAME    (0)
#define FFPARAM_UPDATE      (1)
#define FFPARAM_SELECT      (2)
#define FFPARAM_EDIT        (3)
#define FFPARAM_RELOAD      (4)
#define FFPARAM_SPEED       (5)
#define FFPARAM_MOUSEX      (6)
#define FFPARAM_MOUSEY      (7)
#define FFPARAM_MOUSELEFTX  (8)
#define FFPARAM_MOUSELEFTY  (9)
#define FFPARAM_RED         (10)
#define FFPARAM_GREEN       (11)
#define FFPARAM_BLUE        (12)
#define FFPARAM_ALPHA       (13)

#define STRINGIFY(A) #A

////////////////////////////////////////////////////////////////////////////////////////////////////
//  Plugin information
////////////////////////////////////////////////////////////////////////////////////////////////////
static CFFGLPluginInfo PluginInfo ( 
	ShaderLoader::CreateInstance,		// Create method
	"MK01",								// Plugin unique ID (4 chars)
	"Shader Loader",					// Plugin name											
	1,						   			// API major version number 													
	500,								// API minor version number	
	2,									// Plugin major version number
	000,								// Plugin minor version number
	FF_EFFECT,							// Plugin type
	"FreeFrame GLSL shader loader\nLoad shaders from GLSL Sandbox and Shadertoy\nFiles should be saved as text '.txt'.",		// Plugin description
	"by Lynn Jarvis - spout.zeal.co updated by munki"	// About

);


// Common vertex shader code as per FreeFrame examples
char *vertexShaderCode = STRINGIFY (
void main()
{
	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
	gl_TexCoord[0] = gl_MultiTexCoord0;
	gl_FrontColor = gl_Color;

} );


////////////////////////////////////////////////////////////////////////////////////////////////////
//  Constructor and destructor
////////////////////////////////////////////////////////////////////////////////////////////////////
ShaderLoader::ShaderLoader():CFreeFrameGLPlugin()
{
	HMODULE module;
	char path[MAX_PATH];

	
	// Debug console window so printf works
	FILE* pCout; // should really be freed on exit 
	AllocConsole();
	freopen_s(&pCout, "CONOUT$", "w", stdout); 
	printf("Shader Loader Vers 1.004\n");
	printf("GLSL version [%s]\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
	

	// Input properties allow for no texture or for two textures
	SetMinInputs(1);
	SetMaxInputs(1); 
	
	// Parameters
	SetParamInfo(FFPARAM_FILENAME,      "Shader Name",   FF_TYPE_TEXT,     "");
	SetParamInfo(FFPARAM_UPDATE,        "Update",        FF_TYPE_EVENT,    false );
	SetParamInfo(FFPARAM_SELECT,        "Select",        FF_TYPE_EVENT,    false );
	SetParamInfo(FFPARAM_EDIT,          "Edit",          FF_TYPE_EVENT,    false );
	SetParamInfo(FFPARAM_RELOAD,        "Reload",        FF_TYPE_EVENT,    false );
	SetParamInfo(FFPARAM_SPEED,         "Speed",         FF_TYPE_STANDARD, 0.5f); m_UserSpeed = 0.5f;
	SetParamInfo(FFPARAM_MOUSEX,        "X mouse",       FF_TYPE_STANDARD, 0.5f); m_UserMouseX = 0.5f;
	SetParamInfo(FFPARAM_MOUSEY,        "Y mouse",       FF_TYPE_STANDARD, 0.5f); m_UserMouseY = 0.5f;
	SetParamInfo(FFPARAM_MOUSELEFTX,    "X mouse left",  FF_TYPE_STANDARD, 0.5f); m_UserMouseLeftX = 0.5f;
	SetParamInfo(FFPARAM_MOUSELEFTY,    "Y mouse left",  FF_TYPE_STANDARD, 0.5f); m_UserMouseLeftY = 0.5f;
	SetParamInfo(FFPARAM_RED,           "Red",           FF_TYPE_STANDARD, 0.5f); m_UserRed = 0.5f;
	SetParamInfo(FFPARAM_GREEN,         "Green",         FF_TYPE_STANDARD, 0.5f); m_UserGreen = 0.5f;
	SetParamInfo(FFPARAM_BLUE,          "Blue",          FF_TYPE_STANDARD, 0.5f); m_UserBlue = 0.5f;
	SetParamInfo(FFPARAM_ALPHA,         "Alpha",         FF_TYPE_STANDARD, 1.0f); m_UserAlpha = 1.0f;

	//SetMinInputs(1);

	// Find the host executable name
	module = GetModuleHandle(NULL);
	GetModuleFileNameA(module, path, MAX_PATH);
	_splitpath_s(path, NULL, 0, NULL, 0, m_HostName, MAX_PATH, NULL, 0);
	
	// Isadora and Resolume act on button down.
	// Isadora activates all parameters on plugin load.
	// To prevent the file selection dialog pop up on load, allow one cycle for starting.
	// Magic and default Windows apps react on button-up, so when the dll loads
	// the parameters are not activated and all is OK.
	if(strstr(m_HostName, "Avenue") == 0 || strstr(m_HostName, "Arena") == 0 || strstr(m_HostName, "Isadora") == 0) {
		bStarted = false;
	}
	else {
		bStarted = true;
	}

	// Set defaults
	SetDefaults();

	// Flags
	bSpoutPanelOpened      = false;
	bInitialized           = false;
	bDialogOpen            = false;

	// File names
	m_UserInput[0]         = NULL;
	m_UserShaderName[0]    = NULL;
	m_ShaderPath[0]        = NULL;
	m_ShaderName[0]        = NULL;

}

////////////////////////////////////////////////////////////////////////////////////////////////////
//  Methods
////////////////////////////////////////////////////////////////////////////////////////////////////
FFResult ShaderLoader::InitGL(const FFGLViewportStruct *vp)
{
	// initialize gl extensions and make sure required features are supported
	m_extensions.Initialize();
	if (m_extensions.multitexture==0 || m_extensions.ARB_shader_objects==0)
		return FF_FAIL;

	// Problem noted that this viewport size might not match 
	// the viewport size in ProcessOpenGL so it is calculated later.
	m_vpWidth  = (float)vp->width;
	m_vpHeight = (float)vp->height;

	printf("InitGL - viewport (%f x %f)\n", m_vpWidth, m_vpHeight);

	// Start the clock
	StartCounter();

	// Try to get a shader path from the registry
	// This path will be used on startup if there is no shader name in the entry field
	ReadPathFromRegistry(m_ShaderPath, "Software\\Leading Edge\\FFGLshaderloader", "Filepath");

	return FF_SUCCESS;
}

ShaderLoader::~ShaderLoader()
{

}

FFResult ShaderLoader::DeInitGL()
{
	if(m_ShaderName[0] != NULL) {
		m_shader.UnbindShader();
		m_shader.FreeGLResources();
	}
	m_ShaderName[0] = 0; // signify no shader loaded
	
	// Save the shader file path to the registry if it successfully initialized
	if(bInitialized && m_ShaderPath[0])
		WritePathToRegistry(m_ShaderPath, "Software\\Leading Edge\\FFGLshaderloader", "Filepath");

	m_ShaderPath[0] = 0;
	m_UserShaderName[0] = 0;
	m_UserInput[0] = 0;
	
	if(m_fbo) m_extensions.glDeleteFramebuffersEXT(1, &m_fbo);
	if(m_glTexture0) glDeleteTextures(1, &m_glTexture0);
	if(m_glTexture1) glDeleteTextures(1, &m_glTexture1);
	if(m_glTexture2) glDeleteTextures(1, &m_glTexture2);
	if(m_glTexture3) glDeleteTextures(1, &m_glTexture3);
	m_glTexture0 = 0;
	m_glTexture1 = 0;
	m_glTexture2 = 0;
	m_glTexture3 = 0;
	m_fbo = 0;
	bInitialized = false;

	return FF_SUCCESS;
}

FFResult ShaderLoader::ProcessOpenGL(ProcessOpenGLStruct *pGL)
{
	FFGLTextureStruct Texture0;
	FFGLTextureStruct Texture1;
	FFGLTexCoords maxCoords;
	char filename[MAX_PATH];
	time_t datime;
	struct tm tmbuff;

	bStarted = true; // Let the parameters know it has started

	// Check for context loss
	HGLRC ctx = wglGetCurrentContext();
	if(!ctx) {
		return  FF_FAIL;
	}

	if(bInitialized) {

		// To the host this is an effect plugin, but it can be either a source or an effect
		// and will work without any input, so we still start up if even there is no input texture

		// Set the global viewport resolution.
		// Get the viewport dimensions from OpenGL for certainty
		// 14.01.15 - limited to Isadora
		// if(strstr(m_HostName, "Isadora") != 0) {
			float vpdim[4];
			glGetFloatv(GL_VIEWPORT, vpdim);
			printf("Viewport = %dx%d (%dx%d)\n", (int)m_vpWidth, (int)m_vpHeight, (int)vpdim[2], (int)vpdim[3]);
			m_vpWidth  = vpdim[2];
			m_vpHeight = vpdim[3];
		// }

		/*
		// LJ DEBUG
		if(m_inputTextureLocation >= 0) {
			if(m_inputTextureLocation1 >= 0)
				SetMinInputs(2);
			else
				SetMinInputs(1);
		}
		else
			SetMinInputs(0);
		*/

		// Is there is texture needed by the shader ?
		if(m_inputTextureLocation >= 0 || m_inputTextureLocation1 >= 0) {

			// Is there a texture available ?
			if(m_inputTextureLocation >= 0 && pGL->numInputTextures > 0 && pGL->inputTextures[0] != NULL) {

				Texture0 = *(pGL->inputTextures[0]);
				maxCoords = GetMaxGLTexCoords(Texture0);

				// Delete the local texture if the incoming size is different
				if((int)m_channelResolution[0][0] != Texture0.Width || (int)m_channelResolution[0][1] != Texture0.Height) {
					if(m_glTexture0 > 0) glDeleteTextures(1, &m_glTexture0);
				}

				// Set the resolution of the first texture size
				m_channelResolution[0][0] = (float)Texture0.Width;
				m_channelResolution[0][1] = (float)Texture0.Height;

				// For a power of two texture, the size will be different to the hardware size.
				// The shader will not compensate for this, so we have to create another texture
				// the same size as the resolution we set to the shader. Also the shader needs 
				// textures created with wrapping REPEAT rather than CLAMP to edge
				// So we create such a texture if the size changes and use it for every frame.
				CreateRectangleTexture(Texture0, maxCoords, m_glTexture0, GL_TEXTURE0, m_fbo, pGL->HostFBO);

				// Now we have a local texture of the right size and type
				// Filled with the data from the incoming Freeframe texture
			}

			// Repeat if there is a second incoming texture and the shader needs it
			if(m_inputTextureLocation1 >= 0 && pGL->numInputTextures > 1 && pGL->inputTextures[1] != NULL) {

				Texture1 = *(pGL->inputTextures[1]);
				maxCoords = GetMaxGLTexCoords(Texture1);

				if((int)m_channelResolution[1][0] != Texture1.Width || (int)m_channelResolution[1][1] != Texture1.Height) {
					if(m_glTexture1 > 0) glDeleteTextures(1, &m_glTexture1);
				}

				// Set the channel resolution of the second texture size
				m_channelResolution[1][0] = (float)Texture1.Width;
				m_channelResolution[1][1] = (float)Texture1.Height;

				CreateRectangleTexture(Texture1, maxCoords, m_glTexture1, GL_TEXTURE1, m_fbo, pGL->HostFBO);
			}

			/*
			// TODO - textures 3 and 4
			if(m_inputTextureLocation2 >= 0 && pGL->numInputTextures > 2 && pGL->inputTextures[2] != NULL) {
				Texture2 = *(pGL->inputTextures[2]);
				maxCoords = GetMaxGLTexCoords(Texture2);
				if((int)m_channelResolution[2][0] != Texture2.Width || (int)m_channelResolution[2][1] != Texture2.Height) {
					if(m_glTexture2 > 0) glDeleteTextures(1, &m_glTexture2);
				}
				// Set the channel resolution of the second texture size
				m_channelResolution[2][0] = (float)Texture2.Width;
				m_channelResolution[2][1] = (float)Texture2.Height;
				CreateRectangleTexture(Texture2, maxCoords, m_glTexture2, GL_TEXTURE1, m_fbo, pGL->HostFBO);
			}

			if(m_inputTextureLocation3 >= 0 && pGL->numInputTextures > 3 && pGL->inputTextures[3] != NULL) {
				Texture3 = *(pGL->inputTextures[3]);
				maxCoords = GetMaxGLTexCoords(Texture2);
				if((int)m_channelResolution[3][0] != Texture3.Width || (int)m_channelResolution[3][1] != Texture3.Height) {
					if(m_glTexture3 > 0) glDeleteTextures(1, &m_glTexture3);
				}
				// Set the channel resolution of the second texture size
				m_channelResolution[3][0] = (float)Texture3.Width;
				m_channelResolution[3][1] = (float)Texture3.Height;
				CreateRectangleTexture(Texture3, maxCoords, m_glTexture3, GL_TEXTURE1, m_fbo, pGL->HostFBO);
			}
			*/

		} // endif shader uses a texture

		// Calculate elapsed time
		lastTime = elapsedTime;
		elapsedTime = GetCounter()/1000.0; // In seconds - higher resolution than timeGetTime()
		m_time = m_time + (float)(elapsedTime-lastTime)*m_UserSpeed*2.0f; // increment scaled by user input 0.0 - 2.0

		// Just pass elapsed time for individual channel times
		m_channelTime[0] = m_time;
		m_channelTime[1] = m_time;
		m_channelTime[2] = m_time;
		m_channelTime[3] = m_time;

		// Calculate date vars
		time(&datime);
		localtime_s(&tmbuff, &datime);
		m_dateYear = (float)tmbuff.tm_year;
		m_dateMonth = (float)tmbuff.tm_mon+1;
		m_dateDay = (float)tmbuff.tm_mday;
		m_dateTime = (float)(tmbuff.tm_hour*3600 + tmbuff.tm_min*60 + tmbuff.tm_sec);

		// activate our shader
		m_shader.BindShader();

		//
		// Assign values and set the uniforms to the shader
		//

		//
		// Common
		//

		// First input texture
		// The shader will use the first texture bound to GL texture unit 0
		if(m_inputTextureLocation >= 0 && Texture0.Handle > 0) {
			m_extensions.glUniform1iARB(m_inputTextureLocation, 0);
		}

		// Second input texture
		// The shader will use the texture bound to GL texture unit 1
		if(m_inputTextureLocation1 >= 0 && Texture1.Handle > 0)
			m_extensions.glUniform1iARB(m_inputTextureLocation1, 1);

		/*
		// 4 channels
		if(m_inputTextureLocation2 >= 0 && Texture2.Handle > 0)
			m_extensions.glUniform1iARB(m_inputTextureLocation2, 2);

		if(m_inputTextureLocation3 >= 0 && Texture3.Handle > 0)
			m_extensions.glUniform1iARB(m_inputTextureLocation3, 3);
		*/

		// Elapsed time
		if(m_timeLocation >= 0) 
			m_extensions.glUniform1fARB(m_timeLocation, m_time);
	
		//
		// GLSL sandbox
		//
		// resolution (viewport size)
		if(m_screenLocation >= 0) 
			m_extensions.glUniform2fARB(m_screenLocation, m_vpWidth, m_vpHeight); 

		// mouse - Mouse position
		if(m_mouseLocation >= 0) { // Vec2 - normalized
			m_mouseX = m_UserMouseX;
			m_mouseY = m_UserMouseY;
			m_extensions.glUniform2fARB(m_mouseLocation, m_mouseX, m_mouseY); 
		}

		// surfaceSize - Mouse left drag position - in pixel coordinates
		if(m_surfaceSizeLocation >= 0) {
			m_mouseLeftX = m_UserMouseLeftX*m_vpWidth;
			m_mouseLeftY = m_UserMouseLeftY*m_vpHeight;
			m_extensions.glUniform2fARB(m_surfaceSizeLocation, m_mouseLeftX, m_mouseLeftY);
		}

		//
		// Shadertoy

		// iMouse
		// xy contain the current pixel coords (if LMB is down);
		// zw contain the click pixel.
		// Modified here equivalent to mouse unclicked or left button dragged
		// The mouse is not being simulated, they are just inputs that can be used within the shader.
		if(m_mouseLocationVec4 >= 0) {
			// Convert from 0-1 to pixel coordinates for ShaderToy
			// Here we use the resolution rather than the screen
			m_mouseX     = m_UserMouseX*m_vpWidth;
			m_mouseY     = m_UserMouseY*m_vpHeight;
			m_mouseLeftX = m_UserMouseLeftX*m_vpWidth;
			m_mouseLeftY = m_UserMouseLeftY*m_vpHeight;
			m_extensions.glUniform4fARB(m_mouseLocationVec4, m_mouseX, m_mouseY, m_mouseLeftX, m_mouseLeftY); 
		}

		// iResolution - viewport resolution
		if(m_resolutionLocation >= 0) // Vec3
			m_extensions.glUniform3fARB(m_resolutionLocation, m_vpWidth, m_vpHeight, 1.0); 

		// Channel resolutions are linked to the actual texture resolutions - the size is set in ProcessOpenGL
		// Global resolution is the viewport
		if(m_channelresolutionLocation >= 0) {
			// uniform vec3	iChannelResolution[4]
			// 4 channels Vec3. Float array is 4 rows, 3 cols
			// TODO - 4 channels - 2 & 3 are unused so will not have a texture anyway
			m_channelResolution[2][0] = m_vpWidth;
			m_channelResolution[2][1] = m_vpHeight;
			m_channelResolution[2][2] = 1.0;
			m_channelResolution[3][0] = m_vpWidth;
			m_channelResolution[3][1] = m_vpHeight;
			m_channelResolution[3][2] = 1.0;
			m_extensions.glUniform3fvARB(m_channelresolutionLocation, 4, (GLfloat *)m_channelResolution);
		}

		// iDate - vec4
		if(m_dateLocation >= 0) 
			m_extensions.glUniform4fARB(m_dateLocation, m_dateYear, m_dateMonth, m_dateDay, m_dateTime);

		// Channel elapsed time - vec4
		if(m_channeltimeLocation >= 0)
			m_extensions.glUniform1fvARB(m_channeltimeLocation, 4, m_channelTime);

		// Extras
		// Input colour is linked to the user controls Red, Green, Blue, Alpha
		if(m_inputColourLocation >= 0)
			m_extensions.glUniform4fARB(m_inputColourLocation, m_UserRed, m_UserGreen, m_UserBlue, m_UserAlpha);

		// Bind a texture if the shader needs one
		if(m_inputTextureLocation >= 0 && Texture0.Handle > 0) {
			m_extensions.glActiveTexture(GL_TEXTURE0);
			// For a power of two texture we will have created a local texture
			if(m_glTexture0 > 0)
				glBindTexture(GL_TEXTURE_2D, m_glTexture0);
			else
				glBindTexture(GL_TEXTURE_2D, Texture0.Handle);
		}

		// If there is a second texture, bind it to texture unit 1
		if(m_inputTextureLocation1 >= 0 && Texture1.Handle > 0) {
			m_extensions.glActiveTexture(GL_TEXTURE1);
			if(m_glTexture1 > 0)
				glBindTexture(GL_TEXTURE_2D, m_glTexture1);
			else
				glBindTexture(GL_TEXTURE_2D, Texture1.Handle);
		}

		/*
		// Texture units 2 and 3
		if(m_inputTextureLocation2 >= 0 && Texture2.Handle > 0) {
			m_extensions.glActiveTexture(GL_TEXTURE2);
			if(m_glTexture2 > 0)
				glBindTexture(GL_TEXTURE_2D, m_glTexture2);
			else
				glBindTexture(GL_TEXTURE_2D, Texture2.Handle);
		}

		if(m_inputTextureLocation3 >= 0 && Texture3.Handle > 0) {
			m_extensions.glActiveTexture(GL_TEXTURE3);
			if(m_glTexture3 > 0)
				glBindTexture(GL_TEXTURE_2D, m_glTexture3);
			else
				glBindTexture(GL_TEXTURE_2D, Texture3.Handle);
		}
		*/
		// Do the draw for the shader to work
		glEnable(GL_TEXTURE_2D);
		glBegin(GL_QUADS);
		glTexCoord2f(0.0, 0.0);	
		glVertex2f(-1.0, -1.0);
		glTexCoord2f(0.0, 1.0);	
		glVertex2f(-1.0,  1.0);
		glTexCoord2f(1.0, 1.0);	
		glVertex2f( 1.0,  1.0);
		glTexCoord2f(1.0, 0.0);	
		glVertex2f( 1.0, -1.0);
		glEnd();
		glDisable(GL_TEXTURE_2D);

		/*
		// unbind input texture 3
		if(m_inputTextureLocation3 >= 0 && Texture3.Handle > 0) {
			m_extensions.glActiveTexture(GL_TEXTURE3);
			glBindTexture(GL_TEXTURE_2D, 0);
		}

		// unbind input texture 2
		if(m_inputTextureLocation2 >= 0 && Texture2.Handle > 0) {
			m_extensions.glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_2D, 0);
		}
		*/

		// unbind input texture 1
		if(m_inputTextureLocation1 >= 0 && Texture1.Handle > 0) {
			m_extensions.glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, 0);
		}

		// unbind input texture 0
		m_extensions.glActiveTexture(GL_TEXTURE0); // default
		if(m_inputTextureLocation >= 0 && Texture0.Handle > 0)
			glBindTexture(GL_TEXTURE_2D, 0);

		// unbind the shader
		m_shader.UnbindShader();

	} // endif bInitialized

	// Check to see if the user has selected another shader
	if(CheckSpoutPanel()) {
		
		// Split out the file name
		_splitpath_s(m_ShaderPath, NULL, NULL, NULL, NULL, filename, MAX_PATH, NULL, 0);

		// Is it different to the current shader name ?
		if(m_ShaderName[0] == 0 || strcmp(m_ShaderName, filename) != 0) {
			if(LoadShaderFile(m_ShaderPath)) {
				// Set the new shader name
				strcpy_s(m_ShaderName, 256, filename);
				bInitialized = true;
			}
			else {
				// Same name
				bInitialized = false;
			}
		}
	}

	return FF_SUCCESS;
}

char * ShaderLoader::GetParameterDisplay(DWORD dwIndex) {

	memset(m_DisplayValue, 0, 15);
	
	switch (dwIndex) {

		case FFPARAM_SPEED:
			sprintf_s(m_DisplayValue, 16, "%d", (int)(m_UserSpeed*100.0));
			return m_DisplayValue;
	
		case FFPARAM_MOUSEX:
			sprintf_s(m_DisplayValue, 16, "%d", (int)(m_UserMouseX*m_vpWidth));
			return m_DisplayValue;

		case FFPARAM_MOUSEY:
			sprintf_s(m_DisplayValue, 16, "%d", (int)(m_UserMouseY*m_vpHeight));
			return m_DisplayValue;

		case FFPARAM_MOUSELEFTX:
			sprintf_s(m_DisplayValue, 16, "%d", (int)(m_UserMouseLeftX*m_vpWidth));
			return m_DisplayValue;

		case FFPARAM_MOUSELEFTY:
			sprintf_s(m_DisplayValue, 16, "%d", (int)(m_UserMouseLeftY*m_vpHeight));
			return m_DisplayValue;

		case FFPARAM_RED:
			sprintf_s(m_DisplayValue, 16, "%d", (int)(m_UserRed*256.0));
			return m_DisplayValue;

		case FFPARAM_GREEN:
			sprintf_s(m_DisplayValue, 16, "%d", (int)(m_UserGreen*256.0));
			return m_DisplayValue;

		case FFPARAM_BLUE:
			sprintf_s(m_DisplayValue, 16, "%d", (int)(m_UserBlue*256.0));
			return m_DisplayValue;

		case FFPARAM_ALPHA:
			sprintf_s(m_DisplayValue, 16, "%d", (int)(m_UserAlpha*256.0));
			return m_DisplayValue;

		default:
			return m_DisplayValue;
	}
	return NULL;

}

DWORD ShaderLoader::GetInputStatus(DWORD dwIndex)
{
	DWORD dwRet = FF_INPUT_NOTINUSE;

	switch (dwIndex) {

		case 0 :
			if(m_inputTextureLocation >= 0)
				dwRet = FF_INPUT_INUSE;
			break;

		case 1 :
			if(m_inputTextureLocation1 >= 0)
				dwRet = FF_INPUT_INUSE;
			break;

		/* TODO - 4 channels
		case 2 :
			if(m_inputTextureLocation2 >= 0)
				dwRet = FF_INPUT_INUSE;
			break;

		case 3 :
			if(m_inputTextureLocation3 >= 0)
				dwRet = FF_INPUT_INUSE;
			break;
		*/

		default :
			break;

	}

	return dwRet;

}



FFResult ShaderLoader::GetParameter(DWORD dwIndex)
{
	FFResult dwRet;

	switch (dwIndex) {

		case FFPARAM_SPEED:
			*((float *)(unsigned)&dwRet) = m_UserSpeed;
			return dwRet;
	
		case FFPARAM_MOUSEX:
			*((float *)(unsigned)&dwRet) = m_UserMouseX;
			return dwRet;

		case FFPARAM_MOUSEY:
			*((float *)(unsigned)&dwRet) = m_UserMouseY;
			return dwRet;

		case FFPARAM_MOUSELEFTX:
			*((float *)(unsigned)&dwRet) = m_UserMouseLeftX;
			return dwRet;

		case FFPARAM_MOUSELEFTY:
			*((float *)(unsigned)&dwRet) = m_UserMouseLeftY;
			return dwRet;

		case FFPARAM_RED:
			*((float *)(unsigned)&dwRet) = m_UserRed;
			return dwRet;

		case FFPARAM_GREEN:
			*((float *)(unsigned)&dwRet) = m_UserGreen;
			return dwRet;

		case FFPARAM_BLUE:
			*((float *)(unsigned)&dwRet) = m_UserBlue;
			return dwRet;

		case FFPARAM_ALPHA:
			*((float *)(unsigned)&dwRet) = m_UserAlpha;
			return dwRet;

		default:
			return FF_FAIL;

	}
}

//
// bStarted is set as soon as ProcessOpenGL is called
//
FFResult ShaderLoader::SetParameter(const SetParameterStruct* pParam)
{
	char filename[MAX_PATH];
	char filepath[MAX_PATH];
	bool bUserEntry = false;
	int firstLength, secondLength;

	if (pParam != NULL) {
		
		switch (pParam->ParameterNumber) {

			case FFPARAM_FILENAME:
				if(pParam->NewParameterValue.UIntValue && strlen((char*)pParam->NewParameterValue.PointerValue) > 0) {

					// Is it a new input ?
					if(strcmp(m_UserInput, (char*)pParam->NewParameterValue.PointerValue) != 0) {

						// Copy to global input
						strcpy_s(m_UserInput, MAX_PATH, (char*)pParam->NewParameterValue.PointerValue);

						// Remove leading and trailing quotes
						PathUnquoteSpacesA(m_UserInput); 
					
						// Copy to name and path strings for path checks
						strcpy_s(filepath, MAX_PATH, m_UserInput); // could be just a name
						strcpy_s(filename, MAX_PATH, m_UserInput); // could be a full path
						firstLength = strlen(filename);

						// Could be a full path or just a name so strip out the name
						PathStripPathA(filename); // Removes the path portion of a fully qualified path and file
						secondLength = strlen(filename);

						if(firstLength != secondLength) { 
							// path has been stripped and we now have a filename
							// if there is no extension, add one
							// If there is already a file name extension present, no extension will be added.
							PathAddExtension(filename, ".txt");
							PathAddExtension(filepath, ".txt");
						}
						else { 	// Just a name was entered 
							// if there is no extension, add one
							PathAddExtension(filename, ".txt");
							PathAddExtension(filepath, ".txt");
							// Add the dll path assuming the shader is in the same folder
							AddModulePath(filename, filepath);
						}

						// Now we have filename and filepath so set the user entries
						strcpy_s(m_UserShaderPath, MAX_PATH, filepath);
						strcpy_s(m_UserShaderName, MAX_PATH, filename);

						// On load, try to load a shader from the path entered
						// This is a one-off event so will not be done again
						if(!bInitialized && m_UserShaderPath[0] > 0 && !bStarted) {
							strcpy_s(m_ShaderPath, MAX_PATH, m_UserShaderPath); // set global path
							bInitialized = LoadShaderFile(m_ShaderPath); // m_ShaderName is now set by LoadShaderFile
						}
					}
				}
				else { 
					// Nothing entered in the name field
					m_UserShaderPath[0] = 0; // important for FFPARAM_SELECT below
					m_UserShaderName[0] = 0;
					// Try to load a shader from the global path obtained from the registry on load
					// This is a one-off event so will not be done again
					if(!bInitialized && m_ShaderPath[0] && !bStarted) {
						if(LoadShaderFile(m_ShaderPath)) {
							bInitialized = true;
							// m_ShaderName is now set by LoadShaderFile
						}
					}
				}
				break;

			// Update user entered name
			case FFPARAM_UPDATE :
				if (pParam->NewParameterValue.UIntValue && bStarted) { 
					// Is there any name entered ?
					if(!m_UserShaderPath[0]) {
						SelectSpoutPanel("No file name entered");
					}
					else {
						// Is it different to the current shader path ?
						if(strcmp(m_ShaderPath, m_UserShaderPath) != 0) {
							// Yes so load the shader file
							strcpy_s(m_ShaderPath, MAX_PATH, m_UserShaderPath);
							bInitialized = LoadShaderFile(m_ShaderPath); // m_ShaderName is now set by LoadShaderFile
						}
					}
				}
				break;


			// SpoutPanel shader file selection
			// This is a button, so refer to bStarted flag
			// or else it pops up when the plugin loads
			case FFPARAM_SELECT :
				if (pParam->NewParameterValue.UIntValue && bStarted) {
					if(m_UserShaderPath[0]) {
						SelectSpoutPanel("Shader Name entered\nClear the entry first");
					}
					else {
						SelectSpoutPanel("/FILEOPEN"); // Open the common file dialog
					}
				}
				break;

			// Activate an editor - based on the file association for a text file.
			case FFPARAM_EDIT :
				if (pParam->NewParameterValue.UIntValue && bStarted) {
					if(m_ShaderPath[0]) {
						OpenEditor(m_ShaderPath);
					}
				}
				break;

			// Reload an edited shader
			case FFPARAM_RELOAD :
				if (pParam->NewParameterValue.UIntValue && bStarted) { 
					if(m_ShaderPath[0]) {
						bInitialized = LoadShaderFile(m_ShaderPath);
					}
				}
				break;
		
			case FFPARAM_SPEED:
				m_UserSpeed = *((float *)(unsigned)&(pParam->NewParameterValue.UIntValue));
				break;

			case FFPARAM_MOUSEX:
				m_UserMouseX = *((float *)(unsigned)&(pParam->NewParameterValue.UIntValue));
				break;

			case FFPARAM_MOUSEY:
				m_UserMouseY = *((float *)(unsigned)&(pParam->NewParameterValue.UIntValue));
				break;

			case FFPARAM_MOUSELEFTX:
				m_UserMouseLeftX = *((float *)(unsigned)&(pParam->NewParameterValue.UIntValue));
				break;

			case FFPARAM_MOUSELEFTY:
				m_UserMouseLeftY = *((float *)(unsigned)&(pParam->NewParameterValue.UIntValue));
				break;

			case FFPARAM_RED:
				m_UserRed = *((float *)(unsigned)&(pParam->NewParameterValue.UIntValue));
				break;

			case FFPARAM_GREEN:
				m_UserGreen = *((float *)(unsigned)&(pParam->NewParameterValue.UIntValue));
				break;

			case FFPARAM_BLUE:
				m_UserBlue = *((float *)(unsigned)&(pParam->NewParameterValue.UIntValue));
				break;

			case FFPARAM_ALPHA:
				m_UserAlpha = *((float *)(unsigned)&(pParam->NewParameterValue.UIntValue));
				break;

			default:
				return FF_FAIL;
		}

		return FF_SUCCESS;
	
	}

	return FF_FAIL;
}


bool ShaderLoader::LoadShaderFile(const char *ShaderPath)
{
	std::string shaderString;
	std::string stoyUniforms;

	printf("LoadShaderFile(%s)\n", ShaderPath);

	// First check to see if the file exists
    if(_access(ShaderPath, 0) == -1) { // Mode 0 - existence check
		// SelectSpoutPanel("Shader file not found");
		return false;
	}

	// Open the file
	std::ifstream sourceFile(ShaderPath);

	// Source file loaded OK ?
	if(sourceFile.is_open()) {
	    // Get the shader fragment file source as a single string
		shaderString.assign( ( std::istreambuf_iterator< char >( sourceFile ) ), std::istreambuf_iterator< char >() );
		sourceFile.close();

		// Is it a shader file ?
		// Look for the key string "fragColor" as well as "gl_FragColor" to allow for revised ShaderToy spec with "mainImage"
		if(!(strstr(shaderString.c_str(), "fragColor") == 0 || strstr(shaderString.c_str(), "gl_FragColor") == 0)) {
			SelectSpoutPanel("Not a shader file");
			return bInitialized; // no change to the current shader
		}

		//
		// Extra uniforms specific to ShaderLoader for buth GLSL Sandbox and ShaderToy
		// TODO - extend these
		static char *extraUniforms = { "uniform vec4 inputColour;\n" };
		// For GLSL Sandbox, the extra "inputColour" uniform has to be typed into the shader
		//
		// uniform vec4 inputColour
		//
		
		// Is it a GLSL Sandbox file?
		// look for "uniform float time;". If it does not exist it is a ShaderToy file
		// This is an exact string, so the shader has to have it.
		if(strstr(shaderString.c_str(), "uniform float time;") == 0) {
			//
			// ShaderToy file
			//
			// Shadertoy does not include uniform variables in the source file so add them here
			//
			// uniform vec3			iResolution;			// the rendering resolution (in pixels)
			// uniform float		iGlobalTime;			// current time (in seconds)
			// uniform vec4		 	iMouse;					// xy contain the current pixel coords (if LMB is down). zw contain the click pixel.
			// uniform vec4			iDate;					// (year, month, day, time in seconds)
			// uniform float		iChannelTime[4];		// channel playback time (in seconds)
			// uniform vec3			iChannelResolution[4];	// channel resolution (in pixels)
			// uniform sampler2D	iChannel0;				// sampler for input texture 0.
			// uniform sampler2D	iChannel1;				// sampler for input texture 1.
			// uniform sampler2D	iChannel2;				// sampler for input texture 2.
			// uniform sampler2D	iChannel3;				// sampler for input texture 3.
			static char *uniforms = { "uniform vec3 iResolution;\n"
									  "uniform float iGlobalTime;\n"
									  "uniform vec4 iMouse;\n"
									  "uniform vec4 iDate;\n"
									  "uniform float iChannelTime[4];\n"
									  "uniform vec3 iChannelResolution[4];\n"
									  "uniform sampler2D iChannel0;\n"
									  "uniform sampler2D iChannel1;\n"
									  "uniform sampler2D iChannel2;\n"
									  "uniform sampler2D iChannel3;\n" };
			
			stoyUniforms = uniforms;
			stoyUniforms += extraUniforms;
			stoyUniforms += shaderString; // add the rest of the shared content

			// It might be a revised ShaderToy file with "mainImage" instead of "main"
			if(strstr(shaderString.c_str(), "void mainImage") != 0) {
				//
				// If it is a revised spec ShaderToy file, add a fix at the end for GLSL compatibility
				//
				// Credit Eric Newman 
				// http://magicmusicvisuals.com/forums/viewtopic.php?f=2&t=196
				//
				static char *stoyMainFunction = { "void main(void) {\n"
												  "    mainImage(gl_FragColor, gl_FragCoord.xy);\n"
												  "}\n" };
				stoyUniforms += stoyMainFunction;
			}

			shaderString = stoyUniforms; // the final string

		}
	
		// initialize gl shader
	//	m_shader.SetExtensions(&m_extensions);
		if (!m_shader.Compile(vertexShaderCode, shaderString.c_str())) {
			SelectSpoutPanel("Shader compile error");
			return false;
		}
		else {
			// activate our shader
			bool success = false;
			if (m_shader.IsReady()) {
				if (m_shader.BindShader())
					success = true;
			}

			if (!success) {
				SelectSpoutPanel("Shader bind error");
				return false;
			}
			else {
				// Set uniform locations to -1 so that they are only used if necessary
				m_timeLocation				 = -1;
				m_channeltimeLocation		 = -1;
				m_mouseLocation				 = -1;
				m_mouseLocationVec4			 = -1;
				m_dateLocation				 = -1;
				m_resolutionLocation		 = -1;
				m_channelresolutionLocation  = -1;
				m_inputTextureLocation		 = -1;
				m_inputTextureLocation1		 = -1;
				m_inputTextureLocation2		 = -1;
				m_inputTextureLocation3		 = -1;
				m_screenLocation			 = -1;
				m_surfaceSizeLocation		 = -1;
				// m_surfacePositionLocation	= -1; // TODO
				// m_vertexPositionLocation    = -1; // TODO

				// Extras
				// Input colour is linked to the user controls Red, Green, Blue, Alpha
				m_inputColourLocation        = -1;


				// lookup the "location" of each uniform

				//
				// GLSL Sandbox
				//
				// Normalized mouse position. Components of this vector are always between 0.0 and 1.0.
				//	uniform vec2 mouse;
				// Screen (Viewport) resolution.
				//	uniform vec2 resolution;
				// Used for mouse left drag currently
				//	uniform vec2 surfaceSize;
				//  TODO uniform vec2 surfacePosition;

				// Input textures do not appear to be in the GLSL Sandbox spec
				// but are allowed for here

				// From source of index.html on GitHub
				if(m_inputTextureLocation < 0)
					m_inputTextureLocation = m_shader.FindUniform("texture");

				// Preferred names tex0 and tex1 which are commonly used
				if(m_inputTextureLocation < 0)
					m_inputTextureLocation = m_shader.FindUniform("tex0");

				if(m_inputTextureLocation1 < 0)
					m_inputTextureLocation1 = m_shader.FindUniform("tex1");

				// TODO tex2 and tex3

				// Backbuffer is not supported and is mapped to Texture unit 0
				// From source of index.html on GitHub
				// https://github.com/mrdoob/glsl-sandbox/blob/master/static/index.html
				if(m_inputTextureLocation < 0)
					m_inputTextureLocation = m_shader.FindUniform("backbuffer");

				// From several sources
				if(m_inputTextureLocation < 0)
					m_inputTextureLocation = m_shader.FindUniform("bbuff");

				// Time
				if(m_timeLocation < 0)
					m_timeLocation = m_shader.FindUniform("time");

				// Mouse move
				if(m_mouseLocation < 0)
					m_mouseLocation = m_shader.FindUniform("mouse");

				// Screen size
				if(m_screenLocation < 0) // Vec2
					m_screenLocation = m_shader.FindUniform("resolution"); 

				// Mouse left drag
				if(m_surfaceSizeLocation < 0)
					m_surfaceSizeLocation = m_shader.FindUniform("surfaceSize");
				
				/*
				// TODO
				// surfacePosAttrib is the attribute, surfacePosition is the varying var
				m_surfacePositionLocation = m_shader.FindAttribute("surfacePosAttrib"); 
				if(m_surfacePositionLocation < 0) printf("surfacePosition attribute not found\n");
				if(m_surfacePositionLocation >= 0) {
					// enable the attribute
					m_extensions.glEnableVertexAttribArrayARB(m_surfacePositionLocation);
				}
				m_vertexPositionLocation = m_shader.FindAttribute("position");
				if(m_vertexPositionLocation < 0) printf("vertexPosition attribute not found\n");
				if(m_vertexPositionLocation >= 0) {
					// enable the attribute
					m_extensions.glEnableVertexAttribArrayARB(m_vertexPositionLocation);
				}
				*/

				//
				// Shadertoy
				//

				
				//
				// Texture inputs iChannelx
				//
				if(m_inputTextureLocation < 0)
					m_inputTextureLocation = m_shader.FindUniform("iChannel0");
				
				if(m_inputTextureLocation1 < 0)
					m_inputTextureLocation1 = m_shader.FindUniform("iChannel1");

				if(m_inputTextureLocation2 < 0)
					m_inputTextureLocation2 = m_shader.FindUniform("iChannel2");

				if(m_inputTextureLocation3 < 0)
					m_inputTextureLocation3 = m_shader.FindUniform("iChannel3");

				// iResolution
				if(m_resolutionLocation < 0) // Vec3
					m_resolutionLocation = m_shader.FindUniform("iResolution");

				// iMouse
				if(m_mouseLocationVec4 < 0) // Shadertoy is Vec4
					m_mouseLocationVec4 = m_shader.FindUniform("iMouse");

				// iGlobalTime
				if(m_timeLocation < 0)
					m_timeLocation = m_shader.FindUniform("iGlobalTime");

				// iDate
				if(m_dateLocation < 0)
					m_dateLocation = m_shader.FindUniform("iDate");

				// iChannelTime
				if(m_channeltimeLocation < 0)
					m_channeltimeLocation = m_shader.FindUniform("iChannelTime[4]");
				if(m_channeltimeLocation < 0)
					m_channeltimeLocation = m_shader.FindUniform("iChannelTime[0]");
				if(m_channeltimeLocation < 0)
					m_channeltimeLocation = m_shader.FindUniform("iChannelTime[1]");
				if(m_channeltimeLocation < 0)
					m_channeltimeLocation = m_shader.FindUniform("iChannelTime[2]");
				if(m_channeltimeLocation < 0)
					m_channeltimeLocation = m_shader.FindUniform("iChannelTime[3]");

				// iChannelResolution
				if(m_channelresolutionLocation < 0) // Vec3 width, height, depth * 4
					m_channelresolutionLocation = m_shader.FindUniform("iChannelResolution[4]");
				if(m_channelresolutionLocation < 0)
					m_channelresolutionLocation = m_shader.FindUniform("iChannelResolution[0]");
				if(m_channelresolutionLocation < 0)
					m_channelresolutionLocation = m_shader.FindUniform("iChannelResolution[1]");
				if(m_channelresolutionLocation < 0)
					m_channelresolutionLocation = m_shader.FindUniform("iChannelResolution[2]");
				if(m_channelresolutionLocation < 0)
					m_channelresolutionLocation = m_shader.FindUniform("iChannelResolution[3]");

				// ShaderLoader : inputColour - linked to user input
				if(m_inputColourLocation < 0)
					m_inputColourLocation = m_shader.FindUniform("inputColour");

				m_shader.UnbindShader();

				// Delete the local texture because it might be a different size
				// Delete the local texture because it might be a different size
				if(m_glTexture0 > 0) glDeleteTextures(1, &m_glTexture0);
				if(m_glTexture1 > 0) glDeleteTextures(1, &m_glTexture1);
				if(m_glTexture2 > 0) glDeleteTextures(1, &m_glTexture2);
				if(m_glTexture3 > 0) glDeleteTextures(1, &m_glTexture3);
				m_glTexture0 = 0;
				m_glTexture1 = 0;
				m_glTexture2 = 0;
				m_glTexture3 = 0;

				// Set the global path to registry because all went well
				WritePathToRegistry(m_ShaderPath, "Software\\Leading Edge\\FFGLshaderloader", "Filepath");

				// Start the clock again to start from zero
				StartCounter();

				return true;

			} // bind shader OK
		} // compile shader OK
	} // file open OK
	// else {
		printf("File open error\n");
	// }

	return bInitialized; // no change to the current shader

}


void ShaderLoader::SetDefaults() {

	elapsedTime            = 0.0;
	startTime              = 0.0;
	lastTime               = 0.0;
	PCFreq                 = 0.0;
	CounterStart           = 0;

	m_mouseX               = 0.5;
	m_mouseY               = 0.5;
	m_mouseLeftX           = 0.5;
	m_mouseLeftY           = 0.5;

	m_UserMouseX           = 0.5;
	m_UserMouseY           = 0.5;
	m_UserMouseLeftX       = 0.5;
	m_UserMouseLeftY       = 0.5;

	m_time                 = 0.0;
	m_dateYear             = 0.0;
	m_dateMonth            = 0.0;
	m_dateDay              = 0.0;
	m_dateTime             = 0.0;

	m_channelTime[0]       = 0.0;
	m_channelTime[1]       = 0.0;
	m_channelTime[2]       = 0.0;
	m_channelTime[3]       = 0.0;

	// ShaderToy -  Vec3 - 4 channels 
	m_channelResolution[0][0] = 0.0; // 0 is width
	m_channelResolution[0][1] = 0.0; // 1 is height
	m_channelResolution[0][2] = 1.0; // 2 is depth

	m_channelResolution[1][0] = 0.0;
	m_channelResolution[1][1] = 0.0;
	m_channelResolution[1][2] = 1.0;

	m_channelResolution[2][0] = 0.0;
	m_channelResolution[2][1] = 0.0;
	m_channelResolution[2][2] = 1.0;

	m_channelResolution[3][0] = 0.0;
	m_channelResolution[3][1] = 0.0;
	m_channelResolution[4][2] = 1.0;

	m_UserSpeed               = 0.5;
	m_UserMouseX              = 0.5;
	m_UserMouseY              = 0.5;
	m_UserMouseLeftX          = 0.5;
	m_UserMouseLeftY          = 0.5;

	// OpenGL
	m_glTexture0              = 0;
	m_glTexture1              = 0;
	m_glTexture2              = 0;
	m_glTexture3              = 0;
	m_fbo                     = 0;

}

bool ShaderLoader::LoadShader(std::string shaderString) {
		
		std::string stoyUniforms;
		//
		// Extra uniforms specific to ShaderMaker for buth GLSL Sandbox and ShaderToy
		// For GLSL Sandbox, the extra "inputColour" uniform has to be typed into the shader
		//		uniform vec4 inputColour
		static char *extraUniforms = { "uniform vec4 inputColour;\n" };
		
		// Is it a GLSL Sandbox file?
		// look for "uniform float time;". If it does not exist it is a ShaderToy file
		// This is an exact string, so the shader has to have it.
		if(strstr(shaderString.c_str(), "uniform float time;") == 0) {
			//
			// ShaderToy file
			//
			// Shadertoy does not include uniform variables in the source file so add them here
			//
			// uniform vec3			iResolution;			// the rendering resolution (in pixels)
			// uniform float		iGlobalTime;			// current time (in seconds)
			// uniform vec4		 	iMouse;					// xy contain the current pixel coords (if LMB is down). zw contain the click pixel.
			// uniform vec4			iDate;					// (year, month, day, time in seconds)
			// uniform float		iChannelTime[4];		// channel playback time (in seconds)
			// uniform vec3			iChannelResolution[4];	// channel resolution (in pixels)
			// uniform sampler2D	iChannel0;				// sampler for input texture 0.
			// uniform sampler2D	iChannel1;				// sampler for input texture 1.
			// uniform sampler2D	iChannel2;				// sampler for input texture 2.
			// uniform sampler2D	iChannel3;				// sampler for input texture 3.
			static char *uniforms = { "uniform vec3 iResolution;\n"
									  "uniform float iGlobalTime;\n"
									  "uniform vec4 iMouse;\n"
									  "uniform vec4 iDate;\n"
									  "uniform float iChannelTime[4];\n"
									  "uniform vec3 iChannelResolution[4];\n"
									  "uniform sampler2D iChannel0;\n"
									  "uniform sampler2D iChannel1;\n"
									  "uniform sampler2D iChannel2;\n"
									  "uniform sampler2D iChannel3;\n" };
			
			stoyUniforms = uniforms;
			stoyUniforms += extraUniforms;
			stoyUniforms += shaderString; // add the rest of the shared content
			shaderString = stoyUniforms;
		}
	
		// initialize gl shader
	//	m_shader.SetExtensions(&m_extensions);
		if (!m_shader.Compile(vertexShaderCode, shaderString.c_str())) {
			// SelectSpoutPanel("Shader compile error");
			return false;
		}
		else {
			// activate our shader
			bool success = false;
			if (m_shader.IsReady()) {
				if (m_shader.BindShader())
					success = true;
			}

			if (!success) {
				// SelectSpoutPanel("Shader bind error");
				return false;
			}
			else {
				// Set uniform locations to -1 so that they are only used if necessary
				m_timeLocation				 = -1;
				m_channeltimeLocation		 = -1;
				m_mouseLocation				 = -1;
				m_mouseLocationVec4			 = -1;
				m_dateLocation				 = -1;
				m_resolutionLocation		 = -1;
				m_channelresolutionLocation  = -1;
				m_inputTextureLocation		 = -1;
				m_inputTextureLocation1		 = -1;
				m_inputTextureLocation2		 = -1;
				m_inputTextureLocation3		 = -1;
				m_screenLocation			 = -1;
				m_surfaceSizeLocation		 = -1;
				// m_surfacePositionLocation	= -1; // TODO
				// m_vertexPositionLocation    = -1; // TODO

				// Extras
				// Input colour is linked to the user controls Red, Green, Blue, Alpha
				m_inputColourLocation        = -1;


				// lookup the "location" of each uniform

				//
				// GLSL Sandbox
				//
				// Normalized mouse position. Components of this vector are always between 0.0 and 1.0.
				//	uniform vec2 mouse;
				// Screen (Viewport) resolution.
				//	uniform vec2 resolution;
				// Used for mouse left drag currently
				//	uniform vec2 surfaceSize;
				//  TODO uniform vec2 surfacePosition;

				// Input textures do not appear to be in the GLSL Sandbox spec
				// but are allowed for here

				// From source of index.html on GitHub
				if(m_inputTextureLocation < 0)
					m_inputTextureLocation = m_shader.FindUniform("texture");

				// Preferred names tex0 and tex1 which are commonly used
				if(m_inputTextureLocation < 0)
					m_inputTextureLocation = m_shader.FindUniform("tex0");

				if(m_inputTextureLocation1 < 0)
					m_inputTextureLocation1 = m_shader.FindUniform("tex1");

				// TODO tex2 and tex3 ?

				// Backbuffer is not supported and is mapped to Texture unit 0
				// From source of index.html on GitHub
				// https://github.com/mrdoob/glsl-sandbox/blob/master/static/index.html
				if(m_inputTextureLocation < 0)
					m_inputTextureLocation = m_shader.FindUniform("backbuffer");

				// From several sources
				if(m_inputTextureLocation < 0)
					m_inputTextureLocation = m_shader.FindUniform("bbuff");

				// Time
				if(m_timeLocation < 0)
					m_timeLocation = m_shader.FindUniform("time");

				// Mouse move
				if(m_mouseLocation < 0)
					m_mouseLocation = m_shader.FindUniform("mouse");

				// Screen size
				if(m_screenLocation < 0) // Vec2
					m_screenLocation = m_shader.FindUniform("resolution"); 

				// Mouse left drag
				if(m_surfaceSizeLocation < 0)
					m_surfaceSizeLocation = m_shader.FindUniform("surfaceSize");
				
				/*
				// TODO
				// surfacePosAttrib is the attribute, surfacePosition is the varying var
				m_surfacePositionLocation = m_shader.FindAttribute("surfacePosAttrib"); 
				if(m_surfacePositionLocation < 0) printf("surfacePosition attribute not found\n");
				if(m_surfacePositionLocation >= 0) {
					// enable the attribute
					m_extensions.glEnableVertexAttribArrayARB(m_surfacePositionLocation);
				}
				m_vertexPositionLocation = m_shader.FindAttribute("position");
				if(m_vertexPositionLocation < 0) printf("vertexPosition attribute not found\n");
				if(m_vertexPositionLocation >= 0) {
					// enable the attribute
					m_extensions.glEnableVertexAttribArrayARB(m_vertexPositionLocation);
				}
				*/

				//
				// Shadertoy
				//

				
				//
				// Texture inputs iChannelx
				//
				if(m_inputTextureLocation < 0)
					m_inputTextureLocation = m_shader.FindUniform("iChannel0");
				if(m_inputTextureLocation1 < 0)
					m_inputTextureLocation1 = m_shader.FindUniform("iChannel1");
				if(m_inputTextureLocation2 < 0)
					m_inputTextureLocation2 = m_shader.FindUniform("iChannel2");
				if(m_inputTextureLocation3 < 0)
					m_inputTextureLocation3 = m_shader.FindUniform("iChannel3");

				// iResolution
				if(m_resolutionLocation < 0) // Vec3
					m_resolutionLocation = m_shader.FindUniform("iResolution");

				// iMouse
				if(m_mouseLocationVec4 < 0) // Shadertoy is Vec4
					m_mouseLocationVec4 = m_shader.FindUniform("iMouse");

				// iGlobalTime
				if(m_timeLocation < 0)
					m_timeLocation = m_shader.FindUniform("iGlobalTime");

				// iDate
				if(m_dateLocation < 0)
					m_dateLocation = m_shader.FindUniform("iDate");

				// iChannelTime
				if(m_channeltimeLocation < 0)
					m_channeltimeLocation = m_shader.FindUniform("iChannelTime[4]");
				if(m_channeltimeLocation < 0)
					m_channeltimeLocation = m_shader.FindUniform("iChannelTime[0]");
				if(m_channeltimeLocation < 0)
					m_channeltimeLocation = m_shader.FindUniform("iChannelTime[1]");
				if(m_channeltimeLocation < 0)
					m_channeltimeLocation = m_shader.FindUniform("iChannelTime[2]");
				if(m_channeltimeLocation < 0)
					m_channeltimeLocation = m_shader.FindUniform("iChannelTime[3]");

				// iChannelResolution
				if(m_channelresolutionLocation < 0) // Vec3 width, height, depth * 4
					m_channelresolutionLocation = m_shader.FindUniform("iChannelResolution[4]");
				if(m_channelresolutionLocation < 0)
					m_channelresolutionLocation = m_shader.FindUniform("iChannelResolution[0]");
				if(m_channelresolutionLocation < 0)
					m_channelresolutionLocation = m_shader.FindUniform("iChannelResolution[1]");
				if(m_channelresolutionLocation < 0)
					m_channelresolutionLocation = m_shader.FindUniform("iChannelResolution[2]");
				if(m_channelresolutionLocation < 0)
					m_channelresolutionLocation = m_shader.FindUniform("iChannelResolution[3]");

				// inputColour - linked to user input
				if(m_inputColourLocation < 0)
					m_inputColourLocation = m_shader.FindUniform("inputColour");

				m_shader.UnbindShader();

				// Delete the local texture because it might be a different size
				if(m_glTexture0 > 0) glDeleteTextures(1, &m_glTexture0);
				if(m_glTexture1 > 0) glDeleteTextures(1, &m_glTexture1);
				if(m_glTexture2 > 0) glDeleteTextures(1, &m_glTexture2);
				if(m_glTexture3 > 0) glDeleteTextures(1, &m_glTexture3);
				m_glTexture0 = 0;
				m_glTexture1 = 0;
				m_glTexture2 = 0;
				m_glTexture3 = 0;

				// Start the clock again to start from zero
				StartCounter();

				printf("shader Loaded OK\n");

				return true;

			} // bind shader OK
		} // compile shader OK
		// =============================================

		printf("shader Load failed\n");

		return false;
}

bool ShaderLoader::WritePathToRegistry(const char *filepath, const char *subkey, const char *valuename)
{
	HKEY  hRegKey;
	LONG  regres;
	char  mySubKey[512];

	// The required key
	strcpy_s(mySubKey, 512, subkey);

	// Does the key already exist ?
	regres = RegOpenKeyExA(HKEY_CURRENT_USER, mySubKey, NULL, KEY_ALL_ACCESS, &hRegKey);
	if(regres != ERROR_SUCCESS) { 
		// Create a new key
		regres = RegCreateKeyExA(HKEY_CURRENT_USER, mySubKey, NULL, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS,NULL, &hRegKey, NULL);
	}

	if(regres == ERROR_SUCCESS && hRegKey != NULL) {
		// Write the path
		regres = RegSetValueEx(hRegKey, valuename, 0, REG_SZ, (BYTE*)filepath, ((DWORD)strlen(filepath) + 1)*sizeof(unsigned char));
		RegCloseKey(hRegKey);
    }

	if(regres == ERROR_SUCCESS)
		return true;
	else
		return false;

}


bool ShaderLoader::ReadPathFromRegistry(const char *filepath, const char *subkey, const char *valuename)
{
	HKEY  hRegKey;
	LONG  regres;
	DWORD  dwSize, dwKey;  

	dwSize = MAX_PATH;

	// Does the key exist
	regres = RegOpenKeyEx(HKEY_CURRENT_USER, subkey, NULL, KEY_READ, &hRegKey);
	if(regres == ERROR_SUCCESS) {
		// Read the key Filepath value
		regres = RegQueryValueEx(hRegKey, valuename, NULL, &dwKey, (BYTE*)filepath, &dwSize);
		RegCloseKey(hRegKey);
		if(regres == ERROR_SUCCESS)
			return true;
	}

	// Just quit if the key does not exist
	return false;

}



void ShaderLoader::StartCounter()
{
    LARGE_INTEGER li;

	// Find frequency
    QueryPerformanceFrequency(&li);
    PCFreq = double(li.QuadPart)/1000.0;

	// Second call needed
    QueryPerformanceCounter(&li);
    CounterStart = li.QuadPart;

}

double ShaderLoader::GetCounter()
{
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return double(li.QuadPart-CounterStart)/PCFreq;
}

//
//
// http://www.codeguru.com/cpp/w-p/dll/tips/article.php/c3635/Tip-Detecting-a-HMODULEHINSTANCE-Handle-Within-the-Module-Youre-Running-In.htm
//
HMODULE ShaderLoader::GetCurrentModule()
{
	return reinterpret_cast<HMODULE>(&__ImageBase);
}

bool ShaderLoader::AddModulePath(const char *username, char *userpath)
{
	char drive[16];
	char dir[MAX_PATH];
	char path[MAX_PATH];
	char filepath[MAX_PATH];
	char modulepath[MAX_PATH];
	char shaderpath[MAX_PATH];
	HMODULE hModule;

	// LoadShaderFile needs the full path, so find the module path
	hModule = GetCurrentModule();
	GetModuleFileNameA(hModule, path, MAX_PATH); // Full path of this dll including it's name

	// Split out the path itself and look for the shader path
	// e.g. - G:\Program Files\Common Files\Freeframe\Shaders
	// TODO - update to Path function
	_splitpath_s(path, drive, 16, dir, MAX_PATH, NULL, NULL, NULL, 0);
	sprintf_s(modulepath, MAX_PATH, "%s%s", drive, dir);

	// Look for the file first in the dll path
	sprintf_s(filepath, MAX_PATH, "%s\\%s", modulepath, username);
    if(_access(filepath, 0) != -1) { // file exists so use this path
		strcpy_s(userpath, MAX_PATH, filepath);
		return true;
	}

	// If it is not there, look for a shader subfolder
	sprintf_s(shaderpath, MAX_PATH, "%s\\Shaders\\", modulepath);

	// Does the shader subfolder exist ?
    if(_access(shaderpath, 0) == -1) { // Mode 0 - existence check
		// Use the dll path
		strcpy_s(shaderpath, MAX_PATH, modulepath);
	}

	// Append the shader file name to the final file path
	sprintf_s(userpath, MAX_PATH, "%s\\%s", shaderpath, username);


	return true;
}


//
// Included File Open argument for this application
// can also be a user message
//
bool ShaderLoader::SelectSpoutPanel(const char *message)
{
	HWND hWnd;
	HANDLE hMutex1;
	HMODULE module;
	char UserMessage[512];
	bool bRet = false;
	char path[MAX_PATH], drive[MAX_PATH], dir[MAX_PATH], fname[MAX_PATH];

	if(message != NULL) {
		strcpy_s(UserMessage, 512, message); 
	}
	else {
		UserMessage[0] = 0; // make sure SpoutPanel does not see an un-initialized string
	}

	// SpoutPanel.exe has to be in the same folder as the host executable
	// This rather complicated process avoids having to use a dialog within a dll
	// which causes problems with FreeFrameGL plugins and the host GUI can freeze.

	// First check whether the panel is already running
	// by trying to open the application mutex.
	hMutex1 = OpenMutexA(MUTEX_ALL_ACCESS, 0, "SpoutPanel");
	if (!hMutex1) { // No mutex, so not running, so can open it

		// Find the path for SpoutPanel from the registry if the Spout2 installer has been used
		bRet = false;//ReadPathFromRegistry(path, "Software\\Leading Edge\\SpoutPanel", "Installpath");
		if(!bRet) {
			// Otherwise find the path of the host program where SpoutPanel should have been copied
			module = GetModuleHandle(NULL);
			GetModuleFileNameA(module, path, MAX_PATH);
			// TODO - update to path function
			_splitpath_s(path, drive, MAX_PATH, dir, MAX_PATH, fname, MAX_PATH, NULL, 0);
			_makepath_s(path, MAX_PATH, drive, dir, "SpoutPanel", ".exe");
		}

		//
		// Use  ShellExecuteEx so we can test its return value later
		//
		memset(&ShExecInfo, 0, sizeof(ShExecInfo));
		ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
		ShExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
		ShExecInfo.hwnd = NULL;
		ShExecInfo.lpVerb = NULL;
		ShExecInfo.lpFile = (LPCSTR)path;
		ShExecInfo.lpParameters = UserMessage;
		ShExecInfo.lpDirectory = NULL;
		ShExecInfo.nShow = SW_SHOW;
		ShExecInfo.hInstApp = NULL;	
		ShellExecuteExA(&ShExecInfo);
		Sleep(125); // alow time for SpoutPanel to open 0.125s

		// Find the dialog window and bring it to the top
		hWnd = FindWindowA(NULL, (LPCSTR)"Open"); // Common dialog title

		// Otherwise it is a user message
		if(!IsWindow(hWnd))
			hWnd = FindWindowA(NULL, (LPCSTR)"Spout"); // Message dialog title

		if(IsWindow(hWnd))
			SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_ASYNCWINDOWPOS | SWP_NOSIZE | SWP_NOMOVE);

		// Returns straight away here but multiple instances of SpoutPanel
		// are prevented in it's WinMain procedure by the mutex.
		// An infinite wait here causes problems.
		// The flag "bSpoutPanelOpened" is set here to indicate that the user
		// has opened the panel to select a sender. This flag is local to 
		// this process so will not affect any other receiver instance
		// Then when the selection panel closes, the selected string is tested
		bSpoutPanelOpened = true;

		return true;
	}
	else {
		// We opened it so close it, otherwise it is never released
		CloseHandle(hMutex1);
	}

	return true;

} // end SelectSpoutPanel

//
// Open a file in an editor
// Use the filepath and depend on associations for an editior
//
bool ShaderLoader::OpenEditor(const char *filepath)
{
	HANDLE hMutex;
	HWND hWnd = NULL;
	char directory[MAX_PATH];

	// Is SpoutPanel open ?
	hMutex = OpenMutexA(MUTEX_ALL_ACCESS, 0, "SpoutPanel");
	if (hMutex) { 
		// Already running so don't open the editor on top of it
		CloseHandle(hMutex);
		return false;
	}

	strcpy_s(directory, MAX_PATH, filepath);
	PathRemoveFileSpecA(directory); // Get the directory of the file path
	printf("Directory = %s\n", directory);

	//
	// Use  ShellExecuteEx so we can test its return value later
	//
	memset(&ShExecInfo, 0, sizeof(ShExecInfo));
	ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
	ShExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS; // to return hProcess
	ShExecInfo.hwnd = NULL;
	ShExecInfo.lpVerb = NULL; // can be edit
	// Can lock in NotePad for text files but better to use file associations
	// to allow for other user preferred editors
	// ShExecInfo.lpFile = (LPCSTR)"notepad.exe";
	// ShExecInfo.lpParameters = filepath;
	ShExecInfo.lpFile = (LPCSTR)filepath;
	ShExecInfo.lpParameters = "";
	ShExecInfo.lpDirectory = directory; // working directory NULL;
	ShExecInfo.nShow = SW_SHOW;
	ShExecInfo.hInstApp = NULL;	
	ShellExecuteExA(&ShExecInfo);
	Sleep(250); // alow time to open


	return true;

} // end OpenEditor

//
// ========= USER SELECTION PANEL TEST =====
//
//	This is necessary because the exit code needs to be tested
//
bool ShaderLoader::CheckSpoutPanel()
{
		HANDLE hMutex;
		DWORD dwExitCode;
		bool bRet = false;

		// If SpoutPanel has been activated, test if the user has clicked OK
		if(bSpoutPanelOpened) { // User has activated spout panel
			hMutex = OpenMutexA(MUTEX_ALL_ACCESS, 0, "SpoutPanel");
			if (!hMutex) { // It has now closed
				bSpoutPanelOpened = false;
				// call GetExitCodeProcess() with the hProcess member of SHELLEXECUTEINFO
				// to get the exit code from SpoutPanel
				if(ShExecInfo.hProcess) {
					GetExitCodeProcess(ShExecInfo.hProcess, &dwExitCode);
					// Only act if exit code = 0 (OK)
					if(dwExitCode == 0) {
	 					// SpoutPanel has been activated and OK clicked.
						// Read the selected file path from the registry
						ReadPathFromRegistry(m_ShaderPath, "Software\\Leading Edge\\SpoutPanel", "Filepath");
						bRet = true; // will pass on next call to ProcessOpenGL
					} // endif SpoutPanel OK
					else {
						bRet = false;
					}
				} // got the exit code
			} // endif no mutex so SpoutPanel has closed
			CloseHandle(hMutex);
		}
		return bRet;
} // ========= END USER SELECTION PANEL =====

// NPOTS textures support only the GL_CLAMP, GL_CLAMP_TO_EDGE, and GL_CLAMP_TO_BORDER wrap modes ??
// Seems OK with this.

void ShaderLoader::CreateRectangleTexture(FFGLTextureStruct Texture, FFGLTexCoords maxCoords, GLuint &glTexture, GLenum texunit, GLuint &fbo, GLuint hostFbo)
{
	// First create an fbo and a texture the same size if they don't exist
	if(fbo == 0) {
		m_extensions.glGenFramebuffersEXT(1, &fbo); 
	}

	if(glTexture == 0) {
		glGenTextures(1, &glTexture);
		m_extensions.glActiveTexture(texunit);
		glBindTexture(GL_TEXTURE_2D, glTexture);
		glTexImage2D(GL_TEXTURE_2D, 0,  GL_RGBA, Texture.Width, Texture.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glBindTexture(GL_TEXTURE_2D, 0);
		m_extensions.glActiveTexture(GL_TEXTURE0);
	} // endif created a new texture
				
	// Render the incoming texture to the local one via the fbo
	m_extensions.glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
	m_extensions.glFramebufferTexture2DEXT(GL_READ_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, glTexture, 0);
	glBindTexture(GL_TEXTURE_2D, Texture.Handle);
				
	glEnable(GL_TEXTURE_2D);
	glBegin(GL_QUADS);
	//
	// Must refer to maxCoords here because the texture
	// could be smaller than the hardware size containing it
	//
	//lower left
	glTexCoord2f(0.0, 0.0);
	glVertex2f(-1.0, -1.0);
	//upper left
	glTexCoord2f(0.0, (float)maxCoords.t);
	glVertex2f(-1.0, 1.0);
	// upper right
	glTexCoord2f((float)maxCoords.s, (float)maxCoords.t);
	glVertex2f(1.0, 1.0);
	//lower right
	glTexCoord2f((float)maxCoords.s, 0.0);
	glVertex2f(1.0, -1.0);
	glEnd();
	glDisable(GL_TEXTURE_2D);

	// unbind the input texture
	glBindTexture(GL_TEXTURE_2D, 0);

	// unbind the fbo
	if(hostFbo)
		m_extensions.glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, hostFbo);
	else
		m_extensions.glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);

}

