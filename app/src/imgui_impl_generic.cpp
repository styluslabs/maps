// ImGui Platform Binding for: GLFW
// This needs to be used along with a Renderer (e.g. OpenGL3, Vulkan..)
// (Info: GLFW is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan graphics context creation, etc.)

#include "imgui.h"
#include "imgui_impl_generic.h"

// Data
static double g_Time = 0.0;
static bool g_MouseJustPressed[5] = {0};
static bool g_MouseBtnDown[5] = {0};
static bool g_KeyDown[512] = {0};
static bool g_KeyJustPressed[512] = {0};

static const char* ImGui_ImplGeneric_GetClipboardText(void* user_data)
{
    return "";  //return glfwGetClipboardString((GLFWwindow*)user_data);
}

static void ImGui_ImplGeneric_SetClipboardText(void* user_data, const char* text)
{
    //glfwSetClipboardString((GLFWwindow*)user_data, text);
}

// action > 0 for press; <= 0 for release
void ImGui_ImplGeneric_MouseButtonCallback(int button, int action, int /*mods*/)
{
    if (button >= 0 && button < IM_ARRAYSIZE(g_MouseJustPressed)) {
        if(action > 0)
            g_MouseJustPressed[button] = true;
        g_MouseBtnDown[button] = action > 0;
    }
}

void ImGui_ImplGeneric_ScrollCallback(double xoffset, double yoffset)
{
    ImGuiIO& io = ImGui::GetIO();
    io.MouseWheelH += (float)xoffset;
    io.MouseWheel += (float)yoffset;
}

void ImGui_ImplGeneric_KeyCallback(int key, int, int action, int mods)
{
    if(action > 0)
        g_KeyJustPressed[key] = true;
    g_KeyDown[key] = action > 0;
}

void ImGui_ImplGeneric_CharCallback(unsigned int c)
{
    ImGuiIO& io = ImGui::GetIO();
    if (c > 0 && c < 0x10000)
        io.AddInputCharacter((unsigned short)c);
}

bool ImGui_ImplGeneric_Init()
{
    g_Time = 0.0;

    // Setup back-end capabilities flags
    ImGuiIO& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;         // We can honor GetMouseCursor() values (optional)
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;          // We can honor io.WantSetMousePos requests (optional, rarely used)

    // Keyboard mapping. ImGui will use those indices to peek into the io.KeysDown[] array.
    io.KeyMap[ImGuiKey_Tab] = SDL_SCANCODE_TAB;
    io.KeyMap[ImGuiKey_LeftArrow] = SDL_SCANCODE_LEFT;
    io.KeyMap[ImGuiKey_RightArrow] = SDL_SCANCODE_RIGHT;
    io.KeyMap[ImGuiKey_UpArrow] = SDL_SCANCODE_UP;
    io.KeyMap[ImGuiKey_DownArrow] = SDL_SCANCODE_DOWN;
    io.KeyMap[ImGuiKey_PageUp] = SDL_SCANCODE_PAGEUP;
    io.KeyMap[ImGuiKey_PageDown] = SDL_SCANCODE_PAGEDOWN;
    io.KeyMap[ImGuiKey_Home] = SDL_SCANCODE_HOME;
    io.KeyMap[ImGuiKey_End] = SDL_SCANCODE_END;
    io.KeyMap[ImGuiKey_Insert] = SDL_SCANCODE_INSERT;
    io.KeyMap[ImGuiKey_Delete] = SDL_SCANCODE_DELETE;
    io.KeyMap[ImGuiKey_Backspace] = SDL_SCANCODE_BACKSPACE;
    io.KeyMap[ImGuiKey_Space] = SDL_SCANCODE_SPACE;
    io.KeyMap[ImGuiKey_Enter] = SDL_SCANCODE_RETURN;
    io.KeyMap[ImGuiKey_Escape] = SDL_SCANCODE_ESCAPE;
    io.KeyMap[ImGuiKey_A] = SDL_SCANCODE_A;
    io.KeyMap[ImGuiKey_C] = SDL_SCANCODE_C;
    io.KeyMap[ImGuiKey_V] = SDL_SCANCODE_V;
    io.KeyMap[ImGuiKey_X] = SDL_SCANCODE_X;
    io.KeyMap[ImGuiKey_Y] = SDL_SCANCODE_Y;
    io.KeyMap[ImGuiKey_Z] = SDL_SCANCODE_Z;

    io.SetClipboardTextFn = ImGui_ImplGeneric_SetClipboardText;
    io.GetClipboardTextFn = ImGui_ImplGeneric_GetClipboardText;
    io.ClipboardUserData = 0;  //g_Window;

    //g_MouseCursors[ImGuiMouseCursor_Arrow] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    //g_MouseCursors[ImGuiMouseCursor_TextInput] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    //g_MouseCursors[ImGuiMouseCursor_ResizeAll] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);   // FIXME: GLFW doesn't have this.
    //g_MouseCursors[ImGuiMouseCursor_ResizeNS] = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
    //g_MouseCursors[ImGuiMouseCursor_ResizeEW] = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
    //g_MouseCursors[ImGuiMouseCursor_ResizeNESW] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);  // FIXME: GLFW doesn't have this.
    //g_MouseCursors[ImGuiMouseCursor_ResizeNWSE] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);  // FIXME: GLFW doesn't have this.
    //g_MouseCursors[ImGuiMouseCursor_Hand] = glfwCreateStandardCursor(GLFW_HAND_CURSOR);

    return true;
}

void ImGui_ImplGeneric_Shutdown()
{
    //for (ImGuiMouseCursor cursor_n = 0; cursor_n < ImGuiMouseCursor_COUNT; cursor_n++)
    //{
    //    glfwDestroyCursor(g_MouseCursors[cursor_n]);
    //    g_MouseCursors[cursor_n] = NULL;
    //}
}

void ImGui_ImplGeneric_UpdateMousePos(double mouse_x, double mouse_y)
{
  // Update buttons
  ImGuiIO& io = ImGui::GetIO();
  io.MousePos = ImVec2((float)mouse_x, (float)mouse_y);
}

void ImGui_ImplGeneric_UpdateMouseCursor()
{
    /*ImGuiIO& io = ImGui::GetIO();
    if ((io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) || glfwGetInputMode(g_Window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
        return;

    ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();
    if (imgui_cursor == ImGuiMouseCursor_None || io.MouseDrawCursor)
    {
        // Hide OS mouse cursor if imgui is drawing it or if it wants no cursor
        glfwSetInputMode(g_Window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    }
    else
    {
        // Show OS mouse cursor
        // FIXME-PLATFORM: Unfocused windows seems to fail changing the mouse cursor with GLFW 3.2, but 3.3 works here.
        glfwSetCursor(g_Window, g_MouseCursors[imgui_cursor] ? g_MouseCursors[imgui_cursor] : g_MouseCursors[ImGuiMouseCursor_Arrow]);
        glfwSetInputMode(g_Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }*/
}

void ImGui_ImplGeneric_Resize(int w, int h, int display_w, int display_h)
{
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2((float)w, (float)h);
  io.DisplayFramebufferScale = ImVec2(w > 0 ? ((float)display_w / w) : 0, h > 0 ? ((float)display_h / h) : 0);
}

void ImGui_ImplGeneric_NewFrame(double current_time)
{
    ImGuiIO& io = ImGui::GetIO();
    IM_ASSERT(io.Fonts->IsBuilt());     // Font atlas needs to be built, call renderer _NewFrame() function e.g. ImGui_ImplOpenGL3_NewFrame()

    // Update mouse buttons
    for (int i = 0; i < IM_ARRAYSIZE(io.MouseDown); i++) {
        // If a mouse press event came, always pass it as "mouse held this frame", so we don't miss click-release events that are shorter than 1 frame.
        io.MouseDown[i] = g_MouseJustPressed[i] || g_MouseBtnDown[i];
        g_MouseJustPressed[i] = false;
    }

    for (int i = 0; i < IM_ARRAYSIZE(io.KeysDown); i++) {
        io.KeysDown[i] = g_KeyJustPressed[i] || g_KeyDown[i];
        g_KeyJustPressed[i] = false;
    }

    io.KeyCtrl = io.KeysDown[SDL_SCANCODE_LCTRL] || io.KeysDown[SDL_SCANCODE_RCTRL];
    io.KeyShift = io.KeysDown[SDL_SCANCODE_LSHIFT] || io.KeysDown[SDL_SCANCODE_RSHIFT];
    io.KeyAlt = io.KeysDown[SDL_SCANCODE_LALT] || io.KeysDown[SDL_SCANCODE_RALT];
    io.KeySuper = io.KeysDown[SDL_SCANCODE_LGUI] || io.KeysDown[SDL_SCANCODE_RGUI];

    // Setup time step
    io.DeltaTime = g_Time > 0.0 ? (float)(current_time - g_Time) : (float)(1.0f/60.0f);
    g_Time = current_time;

    // Gamepad navigation mapping [BETA]
    memset(io.NavInputs, 0, sizeof(io.NavInputs));

    //ImGui_ImplGeneric_UpdateMousePosAndButtons();
    //ImGui_ImplGeneric_UpdateMouseCursor();
}
