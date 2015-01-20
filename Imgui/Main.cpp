// ImGui - standalone example application for OpenGL 2, using fixed pipeline

#ifdef _MSC_VER
#pragma warning (disable: 4996)                        // 'This function or variable may be unsafe': strcpy, strdup, sprintf, vsnprintf, sscanf, fopen
#endif
#ifdef __clang__
#pragma clang diagnostic ignored "-Wunused-function"   // warning: unused function
#endif

#include "imgui.h"
#include <stdio.h>
#include <windows.h>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <time.h>
#include "buffer.h"

// Glfw/Glew
#define GLEW_STATIC
#include <gl/glew.h>
#include <gl/glfw3.h>
#define OFFSETOF(TYPE, ELEMENT) ((size_t)&(((TYPE *)0)->ELEMENT))

#pragma comment(lib,"opengl32.lib")
#pragma comment(lib,"glu32.lib")
#pragma comment(lib,"glew32.lib")
#pragma comment(lib,"glfw3dll.lib")

static GLFWwindow* window;
static bool mousePressed[2] = { false, false };
HANDLE Mutex;
HANDLE emptySemaphore;
HANDLE fullSemaphore;
FILE* fp;
int t = 0;
bool bContinue = true;
std::string output;
char ssProducer[300] = { '\0' };
char ssConsumer[300] = { '\0' };/*
std::stringstream ssProducer;
std::stringstream ssConsumer;*/

DWORD WINAPI producer(LPVOID);
DWORD WINAPI consumer(LPVOID);

// This is the main rendering function that you have to implement and provide to ImGui (via setting up 'RenderDrawListsFn' in the ImGuiIO structure)
// If text or lines are blurry when integrating ImGui in your engine:
// - in your Render function, try translating your projection matrix by (0.5f,0.5f) or (0.375f,0.375f)
static void ImImpl_RenderDrawLists(ImDrawList** const cmd_lists, int cmd_lists_count)
{
	if (cmd_lists_count == 0)
		return;

	// We are using the OpenGL fixed pipeline to make the example code simpler to read!
	// A probable faster way to render would be to collate all vertices from all cmd_lists into a single vertex buffer.
	// Setup render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled, vertex/texcoord/color pointers.
	glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TRANSFORM_BIT);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_SCISSOR_TEST);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glEnable(GL_TEXTURE_2D);

	// Setup orthographic projection matrix
	const float width = ImGui::GetIO().DisplaySize.x;
	const float height = ImGui::GetIO().DisplaySize.y;
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0.0f, width, height, 0.0f, -1.0f, +1.0f);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	// Render command lists
	for (int n = 0; n < cmd_lists_count; n++)
	{
		const ImDrawList* cmd_list = cmd_lists[n];
		const unsigned char* vtx_buffer = (const unsigned char*)&cmd_list->vtx_buffer.front();
		glVertexPointer(2, GL_FLOAT, sizeof(ImDrawVert), (void*)(vtx_buffer + OFFSETOF(ImDrawVert, pos)));
		glTexCoordPointer(2, GL_FLOAT, sizeof(ImDrawVert), (void*)(vtx_buffer + OFFSETOF(ImDrawVert, uv)));
		glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(ImDrawVert), (void*)(vtx_buffer + OFFSETOF(ImDrawVert, col)));

		int vtx_offset = 0;
		for (size_t cmd_i = 0; cmd_i < cmd_list->commands.size(); cmd_i++)
		{
			const ImDrawCmd* pcmd = &cmd_list->commands[cmd_i];
			glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->texture_id);
			glScissor((int)pcmd->clip_rect.x, (int)(height - pcmd->clip_rect.w), (int)(pcmd->clip_rect.z - pcmd->clip_rect.x), (int)(pcmd->clip_rect.w - pcmd->clip_rect.y));
			glDrawArrays(GL_TRIANGLES, vtx_offset, pcmd->vtx_count);
			vtx_offset += pcmd->vtx_count;
		}
	}

	// Restore modified state
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glPopAttrib();
}

// NB: ImGui already provide OS clipboard support for Windows so this isn't needed if you are using Windows only.
static const char* ImImpl_GetClipboardTextFn()
{
	return glfwGetClipboardString(window);
}

static void ImImpl_SetClipboardTextFn(const char* text)
{
	glfwSetClipboardString(window, text);
}

// GLFW callbacks to get events
static void glfw_error_callback(int error, const char* description)
{
	fputs(description, stderr);
}

static void glfw_mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
	if (action == GLFW_PRESS && button >= 0 && button < 2)
		mousePressed[button] = true;
}

static void glfw_scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
	ImGuiIO& io = ImGui::GetIO();
	io.MouseWheel += (float)yoffset; // Use fractional mouse wheel, 1.0 unit 5 lines.
}

static void glfw_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	ImGuiIO& io = ImGui::GetIO();
	if (action == GLFW_PRESS)
		io.KeysDown[key] = true;
	if (action == GLFW_RELEASE)
		io.KeysDown[key] = false;
	io.KeyCtrl = (mods & GLFW_MOD_CONTROL) != 0;
	io.KeyShift = (mods & GLFW_MOD_SHIFT) != 0;
}

static void glfw_char_callback(GLFWwindow* window, unsigned int c)
{
	if (c > 0 && c < 0x10000)
		ImGui::GetIO().AddInputCharacter((unsigned short)c);
}

void InitGL()
{
	glfwSetErrorCallback(glfw_error_callback);
	if (!glfwInit())
		exit(1);

	window = glfwCreateWindow(1280, 720, "Producer and Consumer", NULL, NULL);
	glfwMakeContextCurrent(window);
	glfwSetKeyCallback(window, glfw_key_callback);
	glfwSetMouseButtonCallback(window, glfw_mouse_button_callback);
	glfwSetScrollCallback(window, glfw_scroll_callback);
	glfwSetCharCallback(window, glfw_char_callback);

	glewInit();
}

void LoadFontsTexture()
{
	ImGuiIO& io = ImGui::GetIO();
	ImFont* my_font1 = io.Fonts->AddFontDefault();
	//ImFont* my_font2 = io.Fonts->AddFontFromFileTTF("extra_fonts/Karla-Regular.ttf", 15.0f);
	//ImFont* my_font3 = io.Fonts->AddFontFromFileTTF("extra_fonts/ProggyClean.ttf", 13.0f); my_font3->DisplayOffset.y += 1;
	//ImFont* my_font4 = io.Fonts->AddFontFromFileTTF("extra_fonts/ProggyTiny.ttf", 10.0f); my_font4->DisplayOffset.y += 1;
	//ImFont* my_font5 = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 20.0f, io.Fonts->GetGlyphRangesJapanese());

	unsigned char* pixels;
	int width, height;
	io.Fonts->GetTexDataAsAlpha8(&pixels, &width, &height);

	GLuint tex_id;
	glGenTextures(1, &tex_id);
	glBindTexture(GL_TEXTURE_2D, tex_id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, width, height, 0, GL_ALPHA, GL_UNSIGNED_BYTE, pixels);

	// Store our identifier
	io.Fonts->TexID = (void *)(intptr_t)tex_id;
}

void InitImGui()
{
	ImGuiIO& io = ImGui::GetIO();
	io.DeltaTime = 1.0f / 60.0f;                                    // Time elapsed since last frame, in seconds (in this sample app we'll override this every frame because our time step is variable)
	io.KeyMap[ImGuiKey_Tab] = GLFW_KEY_TAB;                       // Keyboard mapping. ImGui will use those indices to peek into the io.KeyDown[] array.
	io.KeyMap[ImGuiKey_LeftArrow] = GLFW_KEY_LEFT;
	io.KeyMap[ImGuiKey_RightArrow] = GLFW_KEY_RIGHT;
	io.KeyMap[ImGuiKey_UpArrow] = GLFW_KEY_UP;
	io.KeyMap[ImGuiKey_DownArrow] = GLFW_KEY_DOWN;
	io.KeyMap[ImGuiKey_Home] = GLFW_KEY_HOME;
	io.KeyMap[ImGuiKey_End] = GLFW_KEY_END;
	io.KeyMap[ImGuiKey_Delete] = GLFW_KEY_DELETE;
	io.KeyMap[ImGuiKey_Backspace] = GLFW_KEY_BACKSPACE;
	io.KeyMap[ImGuiKey_Enter] = GLFW_KEY_ENTER;
	io.KeyMap[ImGuiKey_Escape] = GLFW_KEY_ESCAPE;
	io.KeyMap[ImGuiKey_A] = GLFW_KEY_A;
	io.KeyMap[ImGuiKey_C] = GLFW_KEY_C;
	io.KeyMap[ImGuiKey_V] = GLFW_KEY_V;
	io.KeyMap[ImGuiKey_X] = GLFW_KEY_X;
	io.KeyMap[ImGuiKey_Y] = GLFW_KEY_Y;
	io.KeyMap[ImGuiKey_Z] = GLFW_KEY_Z;

	io.RenderDrawListsFn = ImImpl_RenderDrawLists;
	io.SetClipboardTextFn = ImImpl_SetClipboardTextFn;
	io.GetClipboardTextFn = ImImpl_GetClipboardTextFn;

	LoadFontsTexture();
}

void UpdateImGui()
{
	ImGuiIO& io = ImGui::GetIO();

	// Setup resolution (every frame to accommodate for window resizing)
	int w, h;
	int display_w, display_h;
	glfwGetWindowSize(window, &w, &h);
	glfwGetFramebufferSize(window, &display_w, &display_h);
	io.DisplaySize = ImVec2((float)display_w, (float)display_h);                                   // Display size, in pixels. For clamping windows positions.

	// Setup time step
	static double time = 0.0f;
	const double current_time = glfwGetTime();
	io.DeltaTime = (float)(current_time - time);
	time = current_time;

	// Setup inputs
	// (we already got mouse wheel, keyboard keys & characters from glfw callbacks polled in glfwPollEvents())
	double mouse_x, mouse_y;
	glfwGetCursorPos(window, &mouse_x, &mouse_y);
	mouse_x *= (float)display_w / w;                                                               // Convert mouse coordinates to pixels
	mouse_y *= (float)display_h / h;
	io.MousePos = ImVec2((float)mouse_x, (float)mouse_y);                                          // Mouse position, in pixels (set to -1,-1 if no mouse / on another screen, etc.)
	io.MouseDown[0] = mousePressed[0] || glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) != 0;  // If a mouse press event came, always pass it as "mouse held this frame", so we don't miss click-release events that are shorter than 1 frame.
	io.MouseDown[1] = mousePressed[1] || glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) != 0;

	// Start the frame
	ImGui::NewFrame();
}

const std::string currentDateTime() {
	time_t     now = time(0);
	struct tm  tstruct;
	char       buf[80];
	//tstruct = *localtime(&now);
	localtime_s(&tstruct, &now);

	// Visit http://en.cppreference.com/w/cpp/chrono/c/strftime
	// for more information about date/time format
	strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);

	return buf;
}

int stringToInt(const char* argv)
{
	std::istringstream ss(argv);
	int Int;
	if (!(ss >> Int))
	{
		fprintf_s(fp, "argv: Invalid number %s\n", argv);
		return 1;
	}

	return Int;
}

//Returns the last Win32 error, in string format. Returns an empty string if there is no error.
std::string GetLastErrorAsString(DWORD errorMessageID)
{
	//Get the error message, if any.
	if (errorMessageID == 0)
		return "No error message has been recorded";

	LPSTR messageBuffer = nullptr;
	size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

	std::string message(messageBuffer, size);

	//Free the buffer.
	LocalFree(messageBuffer);

	return message;
}

// Application code
int main(int argc, char** argv)
{
	InitGL();
	InitImGui();
	fopen_s(&fp, "error.log", "a");

	//bool show_test_window = true;
	//bool show_another_window = true;
	bool start = false;
	bool isFinished = true;
	static int nProducer = 1;
	static int nConsumer = 1;
	static int sleepTime = 1000;
	int numProducer = 1;
	int numConsumer = 1;
	HANDLE producerThread[50];
	HANDLE consumerThread[50];

	while (!glfwWindowShouldClose(window))
	{
		ImGuiIO& io = ImGui::GetIO();
		mousePressed[0] = mousePressed[1] = false;
		glfwPollEvents();
		UpdateImGui();

		{
			ImGui::Begin("Options");
			ImGui::SliderInt("The number of producer threads", &nProducer, 1, 50);
			ImGui::SliderInt("The number of consumer threads", &nConsumer, 1, 50);
			ImGui::InputInt("Process Waiting Time(ms)", &sleepTime);
			if (ImGui::Button("Start")) start = true;
			if (ImGui::Button("Pause")) bContinue = false;
			//if (ImGui::Button("Restart"));
			ImGui::End();
		}

		if (start&&isFinished)
		{
			start = false;
			bContinue = true;
			isFinished = false;
			numProducer = nProducer;
			numConsumer = nConsumer;

			#pragma region Init the buffer
			Mutex = CreateMutex(
				NULL,	// Security Attribute: If NULL, it disallow any children of the process creating this mutex lock to inherit the handle of the lock.
				FALSE,	// lock's inital owner: If FALSE, it indicates that the thread creating the mutex is not the inital owner.
				NULL);	// Name of the Mutex
			if (Mutex == NULL)
			{
				fprintf_s(fp, "%s CreateMutex error: %s", currentDateTime().c_str(), GetLastErrorAsString(GetLastError()));
				exit(EXIT_FAILURE);
			}

			emptySemaphore = CreateSemaphore(
				NULL,           // default security attributes
				BUFFER_SIZE,	// initial count
				BUFFER_SIZE,	// maximum count
				NULL);          // unnamed semaphore
			if (emptySemaphore == NULL)
			{
				fprintf_s(fp, "%s CreateSemaphore error: %s", currentDateTime().c_str(), GetLastErrorAsString(GetLastError()));
				exit(EXIT_FAILURE);
			}

			fullSemaphore = CreateSemaphore(
				NULL,
				0,
				BUFFER_SIZE,
				NULL);
			if (fullSemaphore == NULL)
			{
				fprintf_s(fp, "%s CreateSemaphore error: %s", currentDateTime().c_str(), GetLastErrorAsString(GetLastError()));
				exit(EXIT_FAILURE);
			}
			#pragma endregion

			#pragma region Create producer thread
			DWORD ThreadID;
			//std::vector<HANDLE> producerThread(numProducer);
			for (int i = 0; i < numProducer; i++)
			{
				producerThread[i] = CreateThread(
					NULL,       // default security attributes
					0,          // default stack size
					(LPTHREAD_START_ROUTINE)producer,
					NULL,       // no thread function arguments
					0,          // default creation flags
					&ThreadID); // receive thread identifier

				if (producerThread[i] == NULL)
				{
					fprintf_s(fp, "%s CreateProducerThread error: %s", currentDateTime().c_str(), GetLastErrorAsString(GetLastError()));
					exit(EXIT_FAILURE);
				}
			}
			#pragma endregion

			#pragma region Create consumer threads
			/*std::vector<HANDLE> consumerThread(numConsumer);*/
			for (int i = 0; i < numConsumer; i++)
			{
				consumerThread[i] = CreateThread(
					NULL,       // default security attributes
					0,          // default stack size
					(LPTHREAD_START_ROUTINE)consumer,
					NULL,       // no thread function arguments
					0,          // default creation flags
					&ThreadID); // receive thread identifier

				if (consumerThread[i] == NULL)
				{
					fprintf_s(fp, "%s CreateConsumerThread error: %s", currentDateTime().c_str(), GetLastErrorAsString(GetLastError()));
					exit(EXIT_FAILURE);
				}
			}
			#pragma endregion
		}

		if (!bContinue && !isFinished)
		{
			isFinished = true;

			WaitForMultipleObjects(numProducer, &producerThread[0], TRUE, sleepTime);
			WaitForMultipleObjects(numConsumer, &consumerThread[0], TRUE, sleepTime);

			for (int i = 0; i < numProducer; i++)
				CloseHandle(producerThread[i]);
			for (int i = 0; i < numConsumer; i++)
				CloseHandle(consumerThread[i]);

			char *begin = &ssProducer[0];
			char *end = begin + sizeof(ssProducer);
			std::fill(begin, end, 0);

			begin = &ssConsumer[0];
			end = begin + sizeof(ssConsumer);
			std::fill(begin, end, 0);
		}

		{
			ImGui::Begin("BUFFER");
			ImGui::Text(list().c_str());
			ImGui::End();
		}

		{
			ImGui::Begin("Producer ID");
			ImGui::Text(ssProducer);
			ImGui::End();
		}

		{
			ImGui::Begin("Consumer ID");
			ImGui::Text(ssConsumer);
			ImGui::End();
		}

		{
			ImGui::Begin("Log");
			ImGui::Text(output.c_str());
			ImGui::End();
		}

		// Rendering
		glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
		//glClearColor(clear_col.x, clear_col.y, clear_col.z, clear_col.w);
		glClearColor(0.3f, 0.3f, 0.3f, 0);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui::Render();
		glfwSwapBuffers(window);
	}

	// Cleanup
	CloseHandle(Mutex);
	CloseHandle(emptySemaphore);
	CloseHandle(fullSemaphore);
	fclose(fp);

	ImGui::Shutdown();
	glfwTerminate();
	
	system("PAUSE");
	return 0;
}

DWORD WINAPI producer(LPVOID Param)
{
	UNREFERENCED_PARAMETER(Param);
	DWORD ThreadId = GetCurrentThreadId();
	sprintf(ssProducer + strlen(ssProducer), "%lu\n", ThreadId);
	//ssProducer << ThreadId << " \n";
	do
	{
		srand(time(NULL) + (t++));
		Sleep(rand() % 10);

		//produce an item in next_produced
		buffer_item next_produced = rand();

		//wait(empty)

		//printf("------------Producer %lu Waiting emptySemaphore---------------\n\n", ThreadId);
		WaitForSingleObject(emptySemaphore, INFINITE);
		//wait(mutex)
		//printf("------------Producer %lu Waiting Mutex---------------\n\n", ThreadId);
		WaitForSingleObject(Mutex, INFINITE); // INFINITE indicates that it will wait an infinite amount of	time for the lock to become available.	

		std::stringstream ss;
		Sleep(500);
		system("cls");
		printf("------------Producer ID: %lu---------------\n", ThreadId);

		ss << "------------------Producer-------------------\n\n";
		//add next_produced to the buffer
		if (insert_item(next_produced))
			fprintf_s(fp, "Producer %lu\tThe buffer is full\n", ThreadId);
		else
		{
			printf("Producer %lu\tproduced %d\n", ThreadId, next_produced);
			ss << "Producer " << ThreadId << " produced " << next_produced << "\n\n";
		}

		printf("Buffer: ");
		ss << "Buffer: ";
		for (int i = 0; i < counter; i++)
		{
			printf("¡¯");
			ss << "*";
		}
		printf("\n");
		ss << "\n\n";

		//signal(mutex)
		if (ReleaseMutex(Mutex) == 0)
			fprintf_s(fp, "Producer %lu\tRelease Mutex error: %s", ThreadId, GetLastErrorAsString(GetLastError()).c_str());
		printf("---------------------------------------------\n\n", ThreadId);
		ss << "---------------------------------------------\n\n";
		output = ss.str();
		//signal(full)
		if (ReleaseSemaphore(fullSemaphore, 1, NULL) == 0)
			fprintf_s(fp, "Producer %lu\tRelease Semaphore error: %s", ThreadId, GetLastErrorAsString(GetLastError()).c_str());

		t++;
	} while (bContinue);

	return TRUE;
}

DWORD WINAPI consumer(LPVOID Param)
{
	UNREFERENCED_PARAMETER(Param);
	DWORD ThreadId = GetCurrentThreadId();
	sprintf(ssConsumer + strlen(ssConsumer), "%lu\n", ThreadId);
	//ssConsumer << ThreadId << " \n";
	do
	{
		//wait(full)
		WaitForSingleObject(fullSemaphore, INFINITE);
		//wait(mutex)
		WaitForSingleObject(Mutex, INFINITE);

		std::stringstream ss;
		Sleep(500);
		system("cls");
		printf("------------Consumer ID: %lu---------------\n", ThreadId);
		ss << "------------------Consumer-------------------\n\n";
		//remove an item from buffer to next_consumed
		buffer_item next_consumed;
		if (remove_item(&next_consumed))
			fprintf_s(fp, "Consumer %lu\tThe buffer is empty\n", ThreadId);
		else
		{
			printf("Consumer %lu\tconsumed %d\n", ThreadId, next_consumed);
			ss << "Consumer " << ThreadId << " consumed " << next_consumed << "\n\n";
		}
		//printf("There are %d items in buffer\n", counter);
		printf("Buffer: ");
		ss << "Buffer: ";
		for (int i = 0; i < counter; i++)
		{
			printf("¡¯");
			ss << "*";
		}
		printf("\n");
		ss << "\n\n";

		//signal(mutex)
		if (ReleaseMutex(Mutex) == 0)
			fprintf_s(fp, "Consumer %lu\tRelease Mutex error: %s", ThreadId, GetLastErrorAsString(GetLastError()).c_str());
		printf("---------------------------------------------\n\n", ThreadId);
		ss << "---------------------------------------------\n\n";
		output = ss.str();
		//signal(empty)
		if (ReleaseSemaphore(emptySemaphore, 1, NULL) == 0)
			fprintf_s(fp, "Consumer %lu\tRelease Semaphore error: %s", ThreadId, GetLastErrorAsString(GetLastError()).c_str());

		//...
		//consume the item in next_consumed
		//sleep(rand);
		srand(time(NULL) + (t++));
		Sleep(rand() % 10);
	} while (bContinue);

	return TRUE;
}
