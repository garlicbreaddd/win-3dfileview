#include <windows.h>
#include <shobjidl.h>
#include <string>
#include <thread>
#include <mutex>
#include <vector>
#include <iostream>

#include <shlobj.h>
#include <shlwapi.h>
#include <exdisp.h>  // For IShellWindows
#include <shobjidl.h> // For IFolderView


// Add these before any other includes
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <exdisp.h>      // For IShellWindows
#include <shobjidl.h>    // For IFolderView and other shell interfaces
#include <ole2.h>        // For COM basics
#include <shellapi.h>    // For HDROP and DragQueryFileW

#ifdef _WIN32
// Force console subsystem on Windows
#pragma comment(linker, "/subsystem:console")
#endif

// OpenGL includes
#define GLEW_STATIC
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Assimp for model loading
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

// stb_image for texture loading
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Global variables
std::mutex g_mutex;
std::wstring g_currentFile;
bool g_fileSelected = false;
bool g_shouldClose = false;
GLFWwindow* g_window = nullptr;
bool g_windowActive = false;

// 3D model data
struct Mesh {
    GLuint VAO, VBO, EBO;
    unsigned int indexCount;
};

struct Model {
    std::vector<Mesh> meshes;
    glm::vec3 min, max;
    float scaleFactor;
};

// GUID definitions for shell interfaces
DEFINE_GUID(CLSID_ShellWindows, 0x9BA05972, 0xF6A8, 0x11CF, 0xA4, 0x42, 0x00, 0xA0, 0xC9, 0x0A, 0x8F, 0x39);
DEFINE_GUID(IID_IShellWindows, 0x85CB6900, 0x4D95, 0x11CF, 0x96, 0x0C, 0x00, 0x80, 0xC7, 0xF4, 0xEE, 0x85);
DEFINE_GUID(SID_STopLevelBrowser, 0x4C96BE40, 0x915C, 0x11CF, 0x99, 0xD3, 0x00, 0xAA, 0x00, 0x4A, 0xE8, 0x37);

// Shell constants
#define SWC_EXPLORER 0x00000001
#define SWFO_NEEDDISPATCH 0x00000001

Model g_model;
glm::mat4 g_projection;
glm::mat4 g_view;
glm::vec3 g_cameraPos = glm::vec3(0.0f, 0.0f, 3.0f);
glm::vec3 g_cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 g_cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
float g_yaw = -90.0f;
float g_pitch = 0.0f;
float g_lastX = 400, g_lastY = 300;
bool g_firstMouse = true;
bool g_leftMousePressed = false;
double g_lastTime = 0.0;

// Function declarations
bool LoadModel(const std::string& path);
void ProcessMesh(aiMesh* mesh, const aiScene* scene, std::vector<Mesh>& meshes);
void SetupPreviewWindow();
void RenderLoop();
void Cleanup();
void MouseCallback(GLFWwindow* window, double xpos, double ypos);
void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
std::wstring GetSelectedFilePath();
void CalculateBoundingBox(const aiScene* scene, glm::vec3& min, glm::vec3& max);

int main() {
    // Start the preview window in a separate thread
    std::thread renderThread([]() {
        while (!g_shouldClose) {
            std::wstring newFile;
            bool hasFile = false;
            
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                newFile = g_currentFile;
                hasFile = g_fileSelected;
            }

            if (hasFile && !g_windowActive) {
                try {
                    SetupPreviewWindow();
                    g_windowActive = true;
                    RenderLoop();
                } catch (const std::exception& e) {
                    std::cerr << "Error: " << e.what() << std::endl;
                }
            } else if (!hasFile && g_windowActive) {
                glfwSetWindowShouldClose(g_window, GLFW_TRUE);
                g_windowActive = false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    // Main thread monitors file selection
    while (!g_shouldClose) {
        std::wstring selectedFile = GetSelectedFilePath();
        
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_currentFile = selectedFile;
            g_fileSelected = !selectedFile.empty();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    renderThread.join();
    return 0;
}

void SetupPreviewWindow() {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    g_window = glfwCreateWindow(800, 600, "3D Preview", nullptr, nullptr);
    if (!g_window) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwMakeContextCurrent(g_window);
    glfwSetCursorPosCallback(g_window, MouseCallback);
    glfwSetMouseButtonCallback(g_window, MouseButtonCallback);

    if (glewInit() != GLEW_OK) {
        glfwTerminate();
        throw std::runtime_error("Failed to initialize GLEW");
    }

    glEnable(GL_DEPTH_TEST);

    // Load the model
    std::string narrowPath(g_currentFile.begin(), g_currentFile.end());
    if (!LoadModel(narrowPath)) {
        glfwTerminate();
        throw std::runtime_error("Failed to load model");
    }

    std::cout << "Loaded model with " << g_model.meshes.size() << " meshes" << std::endl;

    // Set up projection matrix
    int width, height;
    glfwGetFramebufferSize(g_window, &width, &height);
    g_projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 0.1f, 100.0f);
}

bool LoadModel(const std::string& path) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, 
        aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals);
    
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "Assimp error: " << importer.GetErrorString() << std::endl;
        return false;
    }

    // Calculate bounding box and scale factor
    glm::vec3 min, max;
    CalculateBoundingBox(scene, min, max);
    g_model.min = min;
    g_model.max = max;
    
    // Calculate scale factor to normalize the model
    glm::vec3 size = max - min;
    float maxSize = std::max(std::max(size.x, size.y), size.z);
    g_model.scaleFactor = 2.0f / maxSize;

    // Process all meshes
    g_model.meshes.clear();
    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        ProcessMesh(scene->mMeshes[i], scene, g_model.meshes);
    }

    return true;
}

void CalculateBoundingBox(const aiScene* scene, glm::vec3& min, glm::vec3& max) {
    min = glm::vec3(FLT_MAX);
    max = glm::vec3(-FLT_MAX);

    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        const aiMesh* mesh = scene->mMeshes[i];
        for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
            const aiVector3D& vertex = mesh->mVertices[j];
            
            min.x = std::min(min.x, vertex.x);
            min.y = std::min(min.y, vertex.y);
            min.z = std::min(min.z, vertex.z);
            
            max.x = std::max(max.x, vertex.x);
            max.y = std::max(max.y, vertex.y);
            max.z = std::max(max.z, vertex.z);
        }
    }
}

void ProcessMesh(aiMesh* mesh, const aiScene* scene, std::vector<Mesh>& meshes) {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
        // Position
        vertices.push_back(mesh->mVertices[i].x);
        vertices.push_back(mesh->mVertices[i].y);
        vertices.push_back(mesh->mVertices[i].z);
        
        // Normal
        if (mesh->HasNormals()) {
            vertices.push_back(mesh->mNormals[i].x);
            vertices.push_back(mesh->mNormals[i].y);
            vertices.push_back(mesh->mNormals[i].z);
        } else {
            vertices.push_back(0.0f);
            vertices.push_back(0.0f);
            vertices.push_back(0.0f);
        }
    }

    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        aiFace face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; j++) {
            indices.push_back(face.mIndices[j]);
        }
    }

    Mesh m;
    glGenVertexArrays(1, &m.VAO);
    glGenBuffers(1, &m.VBO);
    glGenBuffers(1, &m.EBO);

    glBindVertexArray(m.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m.VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    // Normal attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    m.indexCount = indices.size();
    meshes.push_back(m);
}

void RenderLoop() {
    // Simple shader source
    const char* vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aNormal;
        
        out vec3 Normal;
        out vec3 FragPos;
        
        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;
        
        void main() {
            FragPos = vec3(model * vec4(aPos, 1.0));
            Normal = mat3(transpose(inverse(model))) * aNormal;
            gl_Position = projection * view * vec4(FragPos, 1.0);
        }
    )";

    const char* fragmentShaderSource = R"(
        #version 330 core
        out vec4 FragColor;
        
        in vec3 Normal;
        in vec3 FragPos;
        
        uniform vec3 lightPos = vec3(2.0, 2.0, 2.0);
        uniform vec3 lightColor = vec3(1.0, 1.0, 1.0);
        uniform vec3 objectColor = vec3(0.7, 0.7, 0.7);
        
        void main() {
            // Ambient
            float ambientStrength = 0.1;
            vec3 ambient = ambientStrength * lightColor;
            
            // Diffuse
            vec3 norm = normalize(Normal);
            vec3 lightDir = normalize(lightPos - FragPos);
            float diff = max(dot(norm, lightDir), 0.0);
            vec3 diffuse = diff * lightColor;
            
            // Specular (simplified)
            vec3 viewDir = normalize(-FragPos);
            vec3 reflectDir = reflect(-lightDir, norm);
            float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
            vec3 specular = 0.5 * spec * lightColor;
            
            vec3 result = (ambient + diffuse + specular) * objectColor;
            FragColor = vec4(result, 1.0);
        }
    )";

    // Compile shaders
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Calculate model matrix to center and scale the model
    glm::vec3 center = (g_model.min + g_model.max) * 0.5f;
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, -center);
    model = glm::scale(model, glm::vec3(g_model.scaleFactor));

    // Camera position based on model size
    float radius = glm::length(g_model.max - g_model.min) * g_model.scaleFactor;
    g_cameraPos = glm::vec3(0.0f, 0.0f, radius * 2.0f);

    while (!glfwWindowShouldClose(g_window)) {
        double currentTime = glfwGetTime();
        float deltaTime = currentTime - g_lastTime;
        g_lastTime = currentTime;

        // Clear screen
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Update view matrix
        g_view = glm::lookAt(g_cameraPos, g_cameraPos + g_cameraFront, g_cameraUp);

        // Use shader
        glUseProgram(shaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(g_view));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(g_projection));

        // Draw model
        for (const auto& mesh : g_model.meshes) {
            glBindVertexArray(mesh.VAO);
            glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
        }

        glfwSwapBuffers(g_window);
        glfwPollEvents();

        // Check if we should close
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (!g_fileSelected) {
                glfwSetWindowShouldClose(g_window, GLFW_TRUE);
            }
        }
    }

    // Cleanup
    for (auto& mesh : g_model.meshes) {
        glDeleteVertexArrays(1, &mesh.VAO);
        glDeleteBuffers(1, &mesh.VBO);
        glDeleteBuffers(1, &mesh.EBO);
    }
    glDeleteProgram(shaderProgram);
    glfwTerminate();
    g_windowActive = false;
}

void MouseCallback(GLFWwindow* window, double xpos, double ypos) {
    if (g_firstMouse) {
        g_lastX = xpos;
        g_lastY = ypos;
        g_firstMouse = false;
    }

    float xoffset = xpos - g_lastX;
    float yoffset = g_lastY - ypos; // Reversed since y-coordinates go from bottom to top
    g_lastX = xpos;
    g_lastY = ypos;

    if (g_leftMousePressed) {
        float sensitivity = 0.1f;
        xoffset *= sensitivity;
        yoffset *= sensitivity;

        g_yaw += xoffset;
        g_pitch += yoffset;

        // Constrain pitch to avoid screen flip
        if (g_pitch > 89.0f) g_pitch = 89.0f;
        if (g_pitch < -89.0f) g_pitch = -89.0f;

        glm::vec3 front;
        front.x = cos(glm::radians(g_yaw)) * cos(glm::radians(g_pitch));
        front.y = sin(glm::radians(g_pitch));
        front.z = sin(glm::radians(g_yaw)) * cos(glm::radians(g_pitch));
        g_cameraFront = glm::normalize(front);
    }
}

void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        g_leftMousePressed = (action == GLFW_PRESS);
        if (g_leftMousePressed) {
            g_firstMouse = true;
        }
    }
}

std::wstring GetSelectedFilePath() {
    std::wstring result;
    
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (SUCCEEDED(hr)) {
        IShellWindows* psw = NULL;
        hr = CoCreateInstance(CLSID_ShellWindows, NULL, CLSCTX_ALL, IID_IShellWindows, (void**)&psw);
        
        if (SUCCEEDED(hr) && psw) {
            IDispatch* pdisp = NULL;
            VARIANT vEmpty = {};
            HWND hwnd;
            hr = psw->FindWindowSW(&vEmpty, &vEmpty, SWC_EXPLORER, (long*)&hwnd, SWFO_NEEDDISPATCH, &pdisp);
            
            if (SUCCEEDED(hr) && pdisp) {
                IServiceProvider* psp = NULL;
                hr = pdisp->QueryInterface(IID_IServiceProvider, (void**)&psp);
                
                if (SUCCEEDED(hr) && psp) {
                    IShellBrowser* psb = NULL;
                    hr = psp->QueryService(SID_STopLevelBrowser, IID_IShellBrowser, (void**)&psb);
                    
                    if (SUCCEEDED(hr) && psb) {
                        IShellView* psv = NULL;
                        hr = psb->QueryActiveShellView(&psv);
                        
                        if (SUCCEEDED(hr) && psv) {
                            IFolderView* pfv = NULL;
                            hr = psv->QueryInterface(IID_IFolderView, (void**)&pfv);
                            
                            if (SUCCEEDED(hr) && pfv) {
                                IDataObject* pdo = NULL;
                                hr = pfv->Items(SVGIO_SELECTION, IID_IDataObject, (void**)&pdo);
                                
                                if (SUCCEEDED(hr) && pdo) {
                                    FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
                                    STGMEDIUM stg = { TYMED_HGLOBAL };
                                    
                                    hr = pdo->GetData(&fmt, &stg);
                                    if (SUCCEEDED(hr)) {
                                        HDROP hdrop = (HDROP)GlobalLock(stg.hGlobal);
                                        if (hdrop) {
                                            UINT filesCount = DragQueryFileW(hdrop, 0xFFFFFFFF, NULL, 0);
                                            if (filesCount == 1) {
                                                WCHAR path[MAX_PATH];
                                                if (DragQueryFileW(hdrop, 0, path, MAX_PATH)) {
                                                    result = path;
                                                }
                                            }
                                            GlobalUnlock(stg.hGlobal);
                                        }
                                        ReleaseStgMedium(&stg);
                                    }
                                    pdo->Release();
                                }
                                pfv->Release();
                            }
                            psv->Release();
                        }
                        psb->Release();
                    }
                    psp->Release();
                }
                pdisp->Release();
            }
            psw->Release();
        }
        CoUninitialize();
    }
    
    return result;
}